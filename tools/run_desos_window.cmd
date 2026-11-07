@echo off
setlocal
REM Start DesOS in a QEMU graphics window so you can SEE the OS screen.
set "QEMU=D:\Program Files\qemu\qemu-system-x86_64.exe"
set "ISO=E:\Mine\A_Desael3\DesOS\build\DesOS.iso"

if not exist "%ISO%" (
  echo ERROR: ISO not found: %ISO%
  echo Build it first:  wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"
  exit /b 1
)

echo Starting DesOS in a QEMU graphics window...
echo   - Limine menu counts down 3 seconds, then boots the kernel
echo   - Then a dark-blue screen shows "DesOS boot OK"
echo   - Close the window to quit. Ctrl+Alt+G releases the mouse.
echo.

"%QEMU%" -cdrom "%ISO%" -m 512M -no-reboot -display gtk
if errorlevel 1 (
  echo.
  echo gtk display failed, retrying with sdl...
  "%QEMU%" -cdrom "%ISO%" -m 512M -no-reboot -display sdl
)
endlocal
