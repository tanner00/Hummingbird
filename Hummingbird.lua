include "Common.lua"

workspace "Hummingbird"
	DefineConfigurations()
	DefinePlatforms()
	BuildPaths()

	startproject "Hummingbird"

filter { "files:**.hlsl" }
	excludefrombuild { "On" }
	filter {}

include "Luft/Luft.lua"
include "RHI/RHI.lua"

project "Hummingbird"
	kind "WindowedApp"

	SetConfigurationSettings()
	UseWindowsSettings("RHI_D3D12=1")

	includedirs { "Source", "Luft/Source", "RHI/Source" }
	files {
		"Source/**.cpp", "Source/**.hpp",
		"Source/**.hlsl", "Source/**.hlsli",
		"Hummingbird.natvis"
	}

	links { "Luft", "RHI" }

	filter {}
