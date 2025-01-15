@echo off
premake5 vs2022 --fatal --file=Premake5.lua
mklink /j Build\Assets Assets\
mklink /j Build\Shaders Source\Shaders
mkdir Build\Debug
mkdir Build\Profile
mkdir Build\Release
mklink /j Build\Debug\D3D12 RHI\ThirdParty\D3D12
mklink /j Build\Profile\D3D12 RHI\ThirdParty\D3D12
mklink /j Build\Release\D3D12 RHI\ThirdParty\D3D12
mklink /j Build\Debug\dxc RHI\ThirdParty\dxc
mklink /j Build\Profile\dxc RHI\ThirdParty\dxc
mklink /j Build\Release\dxc RHI\ThirdParty\dxc
