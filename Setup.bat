@echo off
setlocal
cd /D "%~dp0"

if not exist "%~dp0ThirdParty" mkdir "%~dp0ThirdParty"

set "WORK=%TEMP%\premake-%RANDOM%.zip"
curl -fL -o "%WORK%" ^
  "https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip" ^
  || exit /B 1

tar -xf "%WORK%" -C "%~dp0ThirdParty" || exit /B 1
del "%WORK%" 2>nul

call "%~dp0RHI\Setup.bat" || exit /B 1
