@echo off
rem Installation de Racecab Frontend (questions posees a l ecran)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause
