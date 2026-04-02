@echo off
setlocal
cd /D "%~dp0"

"%~dp0ThirdParty\premake5.exe" vs2022 --fatal --file=Hummingbird.lua || exit /B 1

mklink /J "Build\Assets" "Assets\"
mklink /J "Build\Shaders" "Source\Shaders"
mkdir "Build\Debug" 2>nul
mkdir "Build\Profile" 2>nul
mkdir "Build\Release" 2>nul
mklink /J "Build\Debug\D3D12" "RHI\ThirdParty\D3D12"
mklink /J "Build\Profile\D3D12" "RHI\ThirdParty\D3D12"
mklink /J "Build\Release\D3D12" "RHI\ThirdParty\D3D12"
mklink /J "Build\Debug\dxc" "RHI\ThirdParty\dxc"
mklink /J "Build\Profile\dxc" "RHI\ThirdParty\dxc"
mklink /J "Build\Release\dxc" "RHI\ThirdParty\dxc"
