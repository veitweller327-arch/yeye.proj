@echo off
REM Removes Timing Shift FX from the OpenFX plugin folder (asks for admin rights).
net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "DST=%CommonProgramFiles%\OFX\Plugins\TimingShiftFX.ofx.bundle"
if exist "%DST%" (rmdir /S /Q "%DST%" & echo Removed %DST%) else (echo Not installed.)
pause
