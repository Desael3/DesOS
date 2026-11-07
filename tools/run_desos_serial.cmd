@echo off
setlocal
REM Watch DesOS boot logs right in this cmd window (no graphics window).
set "QEMU=D:\Program Files\qemu\qemu-system-x86_64.exe"
set "ISO=E:\Mine\A_Desael3\DesOS\build\DesOS.iso"

if not exist "%ISO%" (
  echo ERROR: ISO not found: %ISO%
  exit /b 1
)

echo Serial console COM1 -^> this window. This window is also the keyboard input,
echo so you can type commands after the B-stage self-tests finish.
echo Expect: Limine boot log, then DesOS boot OK, then B.1-B.11 self-tests.
echo Wait for the DesOS prompt, then type commands.
echo Try: help / mem / ticks / threads / version / run
echo QEMU never exits by itself; press Ctrl+C when done.
echo.

"%QEMU%" -cdrom "%ISO%" -m 512M -no-reboot -nographic
endlocal
