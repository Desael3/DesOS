# run_qemu_btest.ps1 —— 用 Windows 版 QEMU 验收阶段 B（B.1–B.11，含 B.6b）
#
# 原理同 run_qemu_test.ps1：limine.conf 的 serial: yes + QEMU -nographic 把 COM1
# 接到 stdout，重定向到日志文件后匹配内核自检标记。
# 判定：B.1–B.11 各项 selftest（含 B.6b APIC）均 PASS、ring3 程序真输出、
#       Shell 进入交互循环，且总标记 B-STAGE: PASS（共 16 项检查）。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_qemu_btest.ps1
$ErrorActionPreference = "Stop"

$iso = "E:\Mine\A_Desael3\DesOS\build\DesOS.iso"
$qemu = "D:\Program Files\qemu\qemu-system-x86_64.exe"
$log = "E:\Mine\A_Desael3\DesOS\build\b_selftest_qemu.log"
$err = "E:\Mine\A_Desael3\DesOS\build\b_selftest_qemu.err"
$waitSeconds = 20   # limine.conf timeout: 3 + 引导 + 1 MiB 堆自检 + 抢占调度 worker 忙等

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
$clean = [regex]::Replace($text, "\x1b\[[0-9;?]*[A-Za-z]", "")

Write-Host "=== 串口输出（已去 ANSI）==="
Write-Host $clean
Write-Host "=== 日志已保存：$log ==="
if (Test-Path $err) {
    $e = (Get-Content $err -Raw)
    if ($e) { Write-Host "=== QEMU stderr ==="; Write-Host $e }
}

$checks = @(
    @{ Name = "M1 boot";        Pattern = "DesOS boot OK" },
    @{ Name = "B.1 PMM";        Pattern = "\[B\.1\] selftest: PASS" },
    @{ Name = "B.2 VMM";        Pattern = "\[B\.2\] selftest: PASS" },
    @{ Name = "B.3 heap";       Pattern = "\[B\.3\] selftest: PASS" },
    @{ Name = "B.4 GDT/TSS";    Pattern = "\[B\.4\] selftest: PASS" },
    @{ Name = "B.5 IDT/#PF";    Pattern = "\[B\.5\] selftest: PASS" },
    @{ Name = "B.6 PIC";        Pattern = "\[B\.6\] selftest: PASS" },
    @{ Name = "B.6b APIC";      Pattern = "\[B\.6b\] selftest: PASS" },
    @{ Name = "B.7 sched";      Pattern = "\[B\.7\] selftest: PASS" },
    @{ Name = "B.8 threads";    Pattern = "\[B\.8\] selftest: PASS" },
    @{ Name = "B.9 syscall";    Pattern = "\[B\.9\] selftest: PASS" },
    @{ Name = "B.10 ELF/ring3"; Pattern = "\[B\.10\] selftest: PASS" },
    @{ Name = "B.10 ring3 out"; Pattern = "Hello from DesOS userland \(ring3\)!" },
    @{ Name = "B.11 shell";     Pattern = "\[B\.11\] selftest: PASS" },
    @{ Name = "B.11 interactive"; Pattern = "\[B\.11\] interactive shell" },
    @{ Name = "B-STAGE total";  Pattern = "B-STAGE: PASS" }
)
$failed = 0
foreach ($c in $checks) {
    $hit = $clean -match $c.Pattern
    Write-Host ("{0,-18} {1}" -f $c.Name, $(if ($hit) { "PASS" } else { "FAIL" }))
    if (-not $hit) { $failed++ }
}
if ($clean -match "B-STAGE: FAIL|FATAL|PANIC|selftest: FAIL") { $failed++ }

if ($failed -eq 0) {
    Write-Host "RESULT: PASS - B.1-B.11 (incl. B.6b APIC) all self-tests passed"
    exit 0
} else {
    Write-Host "RESULT: FAIL - $failed check(s) failed"
    exit 1
}
