# run_qemu_test.ps1 —— 用 Windows 版 QEMU 启动 DesOS.iso 并验收内核启动
#
# 原理：limine.conf 里 serial: yes，Limine 与内核都把 COM1(0x3F8) 当作输出。
#   QEMU 的 -nographic 把 COM1 接到自身 stdio，用 RedirectStandardOutput 落盘，
#   即可稳定拿到「Limine 引导日志 + 内核串口输出」，避免 TCP server 的时序竞态。
#
# 判定：串口出现 "DesOS boot OK" 视为成功（由 kernel_main 打印）。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_qemu_test.ps1
$ErrorActionPreference = "Stop"

$iso = "E:\Mine\A_Desael3\DesOS\build\DesOS.iso"
$qemu = "D:\Program Files\qemu\qemu-system-x86_64.exe"
$log = "E:\Mine\A_Desael3\DesOS\build\kernel_boot.log"
$err = "E:\Mine\A_Desael3\DesOS\build\qemu_boot.err"
$waitSeconds = 10   # 需覆盖 limine.conf 的 timeout: 3 + 引导/打印时间

if (-not (Test-Path $iso)) { Write-Host "ERROR: 找不到 $iso（请先运行 make_iso.sh）"; exit 2 }
if (-not (Test-Path $qemu)) { Write-Host "ERROR: 找不到 QEMU: $qemu"; exit 2 }
if (Test-Path $log) { Remove-Item $log }
if (Test-Path $err) { Remove-Item $err }

$p = Start-Process -FilePath $qemu -ArgumentList `
    "-cdrom", $iso, "-m", "512M", "-nographic", "-no-reboot" `
    -PassThru -RedirectStandardOutput $log -RedirectStandardError $err

Start-Sleep -Seconds $waitSeconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 1

$raw = [System.IO.File]::ReadAllBytes($log)
$text = [System.Text.Encoding]::ASCII.GetString($raw)
# 去掉 ANSI 转义序列，便于阅读与匹配
$clean = [regex]::Replace($text, "\x1b\[[0-9;?]*[A-Za-z]", "")

Write-Host "=== 串口输出（已去 ANSI）==="
Write-Host $clean
if (Test-Path $err) {
    $e = (Get-Content $err -Raw)
    if ($e) { Write-Host "=== QEMU stderr ==="; Write-Host $e }
}

if ($clean -match "DesOS boot OK") {
    Write-Host "RESULT: PASS —— 内核已由 Limine 引导并进入 kernel_main"
    exit 0
} else {
    Write-Host "RESULT: FAIL —— 未检测到 'DesOS boot OK'"
    if ($clean -match "PANIC|not valid|outside kernel image|Failed") {
        Write-Host "HINT: Limine 报了错，请查看上方串口输出定位"
    }
    exit 1
}
