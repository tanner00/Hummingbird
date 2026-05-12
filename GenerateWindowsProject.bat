@echo off
setlocal
cd /D "%~dp0"

"%~dp0ThirdParty\premake5.exe" vs2022 --fatal --file=Hummingbird.lua || exit /B 1

mklink /J "Build\Assets" "Assets\"
mklink /J "Build\Shaders" "Source\Shaders"

call "%~dp0RHI\GenerateWindowsProject.bat" || exit /B 1
