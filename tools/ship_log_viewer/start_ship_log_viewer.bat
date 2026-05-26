@echo off
setlocal
cd /d "%~dp0"
set "VIEWER_SCRIPT=%~dp0start_ship_log_viewer.py"

where py >nul 2>nul
if %errorlevel%==0 (
  py -3 "%VIEWER_SCRIPT%"
  goto :eof
)

where python >nul 2>nul
if %errorlevel%==0 (
  python "%VIEWER_SCRIPT%"
  goto :eof
)

echo [viewer] Python launcher not found. Install Python or add py/python to PATH.
pause
