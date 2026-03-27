language "C++"
cppdialect "C++20"
warnings "Extra"
conformancemode "On"

flags { "MultiProcessorCompile", "FatalWarnings" }

exceptionhandling "Off"
rtti "Off"
staticruntime "On"
editandcontinue "Off"

function BuildPaths()
	location "Build"
	targetdir "Build/%{cfg.buildcfg}"
end

function DefinePlatforms()
	platforms { "Win64" }
end

function UseWindowsSettings(extra_define)
	filter "platforms:Win64"
		defines { "PLATFORM_WINDOWS=1", extra_define }
		system "Windows"
		toolset "Msc"
		architecture "x86_64"

	filter {}
end

function DefineConfigurations()
	configurations { "Debug", "Profile", "Release" }
end

function UseOptimizedSettings()
	flags { "LinkTimeOptimization", "NoBufferSecurityCheck" }
	omitframepointer "On"
	vectorextensions "AVX2"
end

function SetConfigurationSettings()
	filter "configurations:Debug"
		defines { "DEBUG=1", "PROFILE=0", "RELEASE=0" }
		optimize "Off"
		symbols "On"

	filter "configurations:Profile"
		defines { "DEBUG=0", "PROFILE=1", "RELEASE=0" }
		optimize "Full"
		symbols "On"

		UseOptimizedSettings()

	filter "configurations:Release"
		defines { "DEBUG=0", "PROFILE=0", "RELEASE=1" }
		optimize "Full"
		symbols "Off"

		UseOptimizedSettings()

	filter {}
end
