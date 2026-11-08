# run_qemu_btest.ps1 - Windows QEMU full acceptance: stages B/C/D
#   (B.1-B.11 incl. B.6b APIC, C.1-C.7, D.1-D.5) + M1 boot
#
# Principle (same as run_qemu_test.ps1): limine.conf serial: yes + QEMU -nographic
# wires COM1 to stdout; we redirect to a log and match the kernel self-test markers.
# Pass criteria: 43 serial markers (43 $checks entries) all hit, no FAIL/PANIC,
# and the 3 generated CLI artifacts exist. Note: quiet boot (D.5) only mutes the
# framebuffer mirror - the COM1 serial log stays complete, so all self-test markers
# are still observable here.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_qemu_btest.ps1
$ErrorActionPreference = "Stop"

$iso = "E:\Mine\A_Desael3\DesOS\build\DesOS.iso"
$qemu = "D:\Program Files\qemu\qemu-system-x86_64.exe"
$log = "E:\Mine\A_Desael3\DesOS\build\b_selftest_qemu.log"
$err = "E:\Mine\A_Desael3\DesOS\build\b_selftest_qemu.err"
$diskimg = "E:\Mine\A_Desael3\DesOS\build\disk.img"
if (-not (Test-Path $diskimg)) {        # blank 16 MiB hard disk for C.4 acceptance
    $fs = [System.IO.File]::Create($diskimg); $fs.SetLength(16MB); $fs.Close()
}
$waitSeconds = 30   # limine timeout 3 + boot + 1 MiB heap self-test + preemptive worker spin

if (-not (Test-Path $iso)) { Write-Host "ERROR: ISO not found: $iso (run make_iso.sh first)"; exit 2 }
if (-not (Test-Path $qemu)) { Write-Host "ERROR: QEMU not found: $qemu"; exit 2 }
if (Test-Path $log) { Remove-Item $log }
if (Test-Path $err) { Remove-Item $err }

$p = Start-Process -FilePath $qemu -ArgumentList `
    "-m", "512M", "-nographic", "-no-reboot", "-boot", "d", `
    "-netdev", "user,id=n0", "-device", "e1000,netdev=n0", `
    "-device", "qemu-xhci,id=x0", "-device", "usb-kbd", "-device", "usb-mouse", `
    "-drive", "file=$diskimg,format=raw,if=ide,index=0,media=disk", "-cdrom", $iso `
    -PassThru -RedirectStandardOutput $log -RedirectStandardError $err

Start-Sleep -Seconds $waitSeconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 1

$raw = [System.IO.File]::ReadAllBytes($log)
$text = [System.Text.Encoding]::ASCII.GetString($raw)
$clean = [regex]::Replace($text, "\x1b\[[0-9;?]*[A-Za-z]", "")

Write-Host "=== serial output (ANSI stripped) ==="
Write-Host $clean
Write-Host "=== log saved: $log ==="
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
    @{ Name = "B.10 W^X";       Pattern = "\[B\.10\] W\^X: PASS" },
    @{ Name = "B.10 ring3 out"; Pattern = "Hello from DesOS userland \(ring3\)!" },
    @{ Name = "B.11 shell";     Pattern = "\[B\.11\] selftest: PASS" },
    @{ Name = "B.11 interactive"; Pattern = "\[B\.11\] interactive shell" },
    @{ Name = "B-STAGE total";  Pattern = "B-STAGE: PASS" },
    @{ Name = "C.3 PCI enum";   Pattern = "\[C\.3\] selftest: PASS" },
    @{ Name = "C.4 disk";      Pattern = "\[C\.4\] selftest: PASS" },
    @{ Name = "C.5 net";       Pattern = "\[C\.5\] selftest: PASS" },
    @{ Name = "C.6 usb";       Pattern = "\[C\.6\] selftest: PASS" },
    @{ Name = "C.7 acpi";       Pattern = "\[C\.7\] selftest: PASS" },
    @{ Name = "C-STAGE total";  Pattern = "C-STAGE: PASS" },
    @{ Name = "D.1 libc out";   Pattern = "libc demo: pid=" },
    @{ Name = "D.2 priv self";  Pattern = "\[D\.2\] priv selftest: PASS" },
    @{ Name = "D.2 user denied"; Pattern = "priv level=3 power-op: denied" },
    @{ Name = "D.2 sudo allowed"; Pattern = "priv level=2 power-op: allowed" },
    @{ Name = "D.3 service self"; Pattern = "\[D\.3\] service selftest: PASS" },
    @{ Name = "D.3 svc table";  Pattern = "fb running kernel" },
    @{ Name = "D.3 userland svc count"; Pattern = "services: 7\r?\n  fb running kernel" },
    @{ Name = "D.3b manifest";  Pattern = "\[D\.3\] userapp selftest: PASS" },
    @{ Name = "D.3b init run";  Pattern = "init: DesOS userspace started" },
    @{ Name = "D.3b init exit"; Pattern = "\[D\.3\] init: exited" },
    @{ Name = "D.3b shell app"; Pattern = "run: app=init privilege=system" },
    @{ Name = "D.4 help list";  Pattern = "DesOS CLI - command list" },
    @{ Name = "D.4 help detail"; Pattern = "usage: run \[app\] \[priv\]" },
    @{ Name = "D.4 alias";      Pattern = "stage D \(libc / privilege / init / CLI\)" },
    @{ Name = "D.4 whoami";     Pattern = "whoami: main" },
    @{ Name = "D.4 --help";     Pattern = "usage: ticks" },
    @{ Name = "D.5 quiet boot"; Pattern = "\[D\.5\] console: quiet boot" },
    @{ Name = "D.5 first key";  Pattern = "\[D\.5\] shell startup selftest: PASS" },
    @{ Name = "D.5 first prompt"; Pattern = "(?s)interactive shell: COM1 serial.*?DesOS> " },
    @{ Name = "D.5 verbose";    Pattern = "verbose: screen mirror off" }
)
$failed = 0
foreach ($c in $checks) {
    $hit = $clean -match $c.Pattern
    Write-Host ("{0,-18} {1}" -f $c.Name, $(if ($hit) { "PASS" } else { "FAIL" }))
    if (-not $hit) { $failed++ }
}
if ($clean -match "B-STAGE: FAIL|FATAL|PANIC|selftest: FAIL") { $failed++ }

# D.4：Python 生成器产物必须存在（构建链会在编译前自动生成）
$cliArtifacts = @("docs\cli-commands.md", "docs\cli-help.txt", "kernel\inc\cli_commands.h")
foreach ($a in $cliArtifacts) {
    if (-not (Test-Path (Join-Path $PSScriptRoot ("..\" + $a)))) {
        Write-Host ("D.4 artifact missing: " + $a)
        $failed++
    }
}

if ($failed -eq 0) {
    Write-Host "RESULT: PASS - B.1-B.11 + C.1-C.7 + D.1-D.5 all self-tests passed"
    exit 0
} else {
    Write-Host "RESULT: FAIL - $failed check(s) failed"
    exit 1
}
