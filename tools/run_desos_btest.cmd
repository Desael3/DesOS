@echo off
rem run_desos_btest.cmd —— 可见窗口 + 日志双保险，一键跑全量自动验收（B/C/D 阶段）
rem 双击即可：先确认 build\DesOS.iso 已由 make_iso.sh 生成。
title DesOS Full QEMU Acceptance
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_qemu_btest.ps1"
echo.
echo Log: E:\Mine\A_Desael3\DesOS\build\b_selftest_qemu.log
pause
