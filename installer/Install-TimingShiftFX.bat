@echo off
REM ---------------------------------------------------------------------------
REM  Manual installer for Timing Shift FX (alternative to the Setup.exe).
REM  Double-click this file. It asks for administrator rights and copies
REM  TimingShiftFX.ofx.bundle into C:\Program Files\Common Files\OFX\Plugins
REM  Keep this .bat next to the TimingShiftFX.ofx.bundle folder.
REM ---------------------------------------------------------------------------
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "SRC=%~dp0TimingShiftFX.ofx.bundle"
set "DST=%CommonProgramFiles%\OFX\Plugins\TimingShiftFX.ofx.bundle"
if not exist "%SRC%\Contents\Win64\TimingShiftFX.ofx" (
  echo ERROR: "%SRC%" not found. Put this file next to the TimingShiftFX.ofx.bundle folder.
  pause
  exit /b 1
)
tasklist /FI "IMAGENAME eq Resolve.exe" | find /I "Resolve.exe" >nul && (
  echo Please close DaVinci Resolve first, then press a key.
  pause
)
if exist "%DST%" rmdir /S /Q "%DST%"
xcopy "%SRC%" "%DST%\" /E /I /Y >nul
if %errorlevel% neq 0 (
  echo ERROR: copying failed.
  pause
  exit /b 1
)
echo.
echo Installed to: %DST%
echo Start DaVinci Resolve: Effects ^> OpenFX ^> Filters ^> Timing Shift FX
pause
