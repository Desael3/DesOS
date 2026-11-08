// kbd.c —— C.1 PS/2 键盘（set 1）：修饰键 / 扩展前缀 / 字符映射
//
// B.11 的链路保持不变：IRQ1 -> kbd_on_irq1() -> kbd_inject_scancode()
// -> shell_input_push()。自检也注入到同一个入口。
// C.1a：左右 Shift/Ctrl/Alt、Caps/Num/Scroll 锁定键、Shift 上档符号、
//       CapsLock 大小写、Ctrl+字母控制字符、0xE0 扩展前缀状态机。
// C.1b：0xE0 扩展键翻译成 KEY_* 控制码送进 Shell 行编辑。
// PS/2 鼠标（IRQ12）留 C.1c。

#include "kbd.h"
#include "shell.h"
#include "pic.h"
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline void ps2_wait_input(void) {
    uint64_t guard = 0;
    while ((inb(0x64) & 0x02) && guard < 100000) guard++;
}
static inline void ps2_wait_output(void) {
    uint64_t guard = 0;
    while (!(inb(0x64) & 0x01) && guard < 100000) guard++;
}

// 未按下 Shift 的主键区映射（B.11 原有表，保持不变）
static const char scancode_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

// Shift 上档：只列与未上档不同的键；字母大小写由 shift ^ caps 统一处理
static const char scancode_map_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
};

// 0xE0 扩展键 -> Shell 私有控制码（见 shell.h）
static const uint8_t ext_key_map[128] = {
    [0x47] = KEY_HOME,   [0x48] = KEY_UP,     [0x49] = KEY_PGUP,
    [0x4B] = KEY_LEFT,   [0x4D] = KEY_RIGHT,  [0x4F] = KEY_END,
    [0x50] = KEY_DOWN,   [0x51] = KEY_PGDN,
    [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
};

static volatile uint32_t mods;
static volatile int      e0_prefix;
static volatile int      e1_skip;
static volatile char     last_char;
static volatile uint16_t last_key;
static volatile uint32_t events;
static volatile uint32_t control_events;

static int is_modifier_code(uint8_t code) {
    switch (code) {
        case 0x2A: case 0x36: case 0x1D: case 0x38:
        case 0x3A: case 0x45: case 0x46:
            return 1;
        default:
            return 0;
    }
}

static void update_modifier(uint8_t code, int release, int extended) {
    // 0xE0 0x2A / 0xE0 0x36 是 Print Screen 序列里的伪 Shift，不能当修饰键
    if (extended && (code == 0x2A || code == 0x36)) return;

    uint32_t bit;
    switch (code) {
        case 0x2A: bit = KBD_MOD_LSHIFT;  break;
        case 0x36: bit = KBD_MOD_RSHIFT;  break;
        case 0x1D: bit = extended ? KBD_MOD_RCTRL : KBD_MOD_LCTRL; break;
        case 0x38: bit = extended ? KBD_MOD_RALT  : KBD_MOD_LALT;  break;
        case 0x3A: bit = KBD_MOD_CAPS;     break;
        case 0x45: bit = KBD_MOD_NUMLOCK; break;
        case 0x46: bit = KBD_MOD_SCROLL;  break;
        default: return;
    }
    // 锁定键只在按下时翻转，松开不重复翻转
    if (bit == KBD_MOD_CAPS || bit == KBD_MOD_NUMLOCK || bit == KBD_MOD_SCROLL) {
        if (!release) mods ^= bit;
        return;
    }
    if (release) mods &= ~bit;
    else         mods |= bit;
}

static char translate(uint8_t code, char base) {
    if (base == 0) return 0;
    int shift = (mods & KBD_MOD_SHIFT) != 0;
    int caps  = (mods & KBD_MOD_CAPS)  != 0;
    int ctrl  = (mods & KBD_MOD_CTRL)  != 0;
    int alt   = (mods & KBD_MOD_ALT)   != 0;

    // Ctrl/Alt 组合不向 Shell 插入普通字符；Ctrl+字母给标准控制字符
    if (ctrl || alt) {
        if (ctrl && !alt && base >= 'a' && base <= 'z')
            return (char)(base - 'a' + 1);
        return 0;
    }

    char c = base;
    if (shift) {
        char s = scancode_map_shift[code];
        if (s) c = s;
    }
    // 字母大小写 = shift XOR caps；符号/数字只看 Shift 上档表
    if (c >= 'a' && c <= 'z' && (shift ^ caps)) c = (char)(c - 32);
    return c;
}

void kbd_reset(void) {
    mods = 0;
    e0_prefix = 0;
    e1_skip = 0;
    last_char = 0;
    last_key = 0;
    events = 0;
    control_events = 0;
}

void kbd_init(void) {
    kbd_reset();
    // D.5：先排空 PS/2 输出缓冲。否则用户在 IRQ1 打开前按下的键（或控制器
    // 上电残留的 0xAA）会被下面 "read configuration byte" 的 inb(0x60)
    // 误读成 cfg，既丢掉按键又可能写出错误的控制器配置。
    for (int i = 0; i < 16 && (inb(0x64) & 0x01); i++) (void)inb(0x60);
    // 启用第一 PS/2 端口并允许其 IRQ1（APIC 模式下 8259 掩码无效，必须在此真正打开）。
    ps2_wait_input();
    outb(0x64, 0xAE);                 // enable first PS/2 port
    ps2_wait_input();
    outb(0x64, 0x20);                 // read configuration byte
    ps2_wait_output();
    uint8_t cfg = inb(0x60);
    cfg |= 0x01;                      // bit0：第一端口 IRQ1 使能
    cfg &= (uint8_t)~0x10;            // bit4：清键盘时钟禁用
    ps2_wait_input();
    outb(0x64, 0x60);                 // write configuration byte
    ps2_wait_input();
    outb(0x60, cfg);
    // 开启键盘扫描（Set 1 已为 BIOS 默认翻译状态），丢弃 ACK(0xFA)
    ps2_wait_input();
    outb(0x60, 0xF4);                 // Enable Scanning
    ps2_wait_output();
    (void)inb(0x60);
    pic_clear_mask(1);                // PIC 回退路径放开 IRQ1
}

// IRQ1 handler 与自检的软件注入共用同一条 scancode -> Shell 输入链路。
void kbd_inject_scancode(uint8_t sc) {
    if (e1_skip > 0) {          // Pause/Break 的 E1 多字节序列，整段忽略
        e1_skip--;
        e0_prefix = 0;
        return;
    }
    if (sc == 0xE1) {
        e1_skip = 2;
        e0_prefix = 0;
        return;
    }
    if (sc == 0xE0) {
        e0_prefix = 1;
        return;
    }

    int extended = e0_prefix;
    e0_prefix = 0;

    uint8_t code = sc & 0x7F;
    int release  = (sc & 0x80) != 0;

    last_key = extended ? (uint16_t)(0xE000u | code) : (uint16_t)code;

    if (is_modifier_code(code)) {
        update_modifier(code, release, extended);
        return;
    }
    if (release) return;         // 普通键断码不产生字符

    if (extended) {
        if (code == 0x1C) {      // 小键盘 Enter：仍按普通回车提交
            last_char = '\n';
            events++;
            shell_input_push('\r');
        } else if (code < 128 && ext_key_map[code] != 0) {
            // C.1b：方向键 / Home / End / Delete 等交给 Shell 行编辑
            control_events++;
            shell_input_push((char)ext_key_map[code]);
        }
        return;
    }

    char c = translate(code, scancode_map[code]);
    if (c == 0) return;
    last_char = c;
    events++;
    shell_input_push(c == '\n' ? '\r' : c);
}

void kbd_on_irq1(void) {
    uint8_t sc = inb(0x60);      // 读数据端口即确认中断
    kbd_inject_scancode(sc);
}

uint32_t kbd_modifiers(void)       { return mods; }
char     kbd_last_char(void)       { return last_char; }
uint16_t kbd_last_key(void)        { return last_key; }
uint32_t kbd_key_events(void)      { return events; }
uint32_t kbd_control_events(void)  { return control_events; }
