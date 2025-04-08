include "Common.lua"

workspace "Hummingbird"
	DefineConfigurations()
	DefinePlatforms()
	BuildPaths()

	startproject "Hummingbird"

filter { "files:**.hlsl" }
	flags { "ExcludeFromBuild" }
	filter {}

include "Luft/Luft.lua"
include "RHI/RHI.lua"

project "Hummingbird"
	kind "WindowedApp"

	includedirs { "Source", "Luft/Source", "RHI/Source", "RHI/ThirdParty" }
	links { "Luft", "RHI" }

	SetConfigurationSettings()
	UseWindowsSettings()

	files {
		"Source/**.cpp", "Source/**.hpp",
		"Source/**.hlsl", "Source/**.hlsli",
	}

	filter {}
