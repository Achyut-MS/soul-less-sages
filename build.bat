@echo off
echo Building project...
mingw32-make %*
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Opening browser...
start http://localhost:8080

echo Starting server (Press Ctrl+C to stop)...
.\mdview.exe
