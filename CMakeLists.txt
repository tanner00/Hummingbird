cmake_minimum_required(VERSION 3.26)

project(Hummingbird LANGUAGES CXX)

set(RHI_D3D12 ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

set(clang_option_prefix)
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
	set(clang_option_prefix "/clang:")

	set(CMAKE_VS_GLOBALS
		"UseMultiToolTask=true"
		"EnforceProcessCountAcrossBuilds=true"
	)
endif()

add_compile_options(
	"${clang_option_prefix}-Wall"
	"${clang_option_prefix}-Wextra"
	"${clang_option_prefix}-Werror"
	"${clang_option_prefix}-Wno-missing-designated-field-initializers"
	"${clang_option_prefix}-Wno-missing-field-initializers"
	"${clang_option_prefix}-fno-exceptions"
	"${clang_option_prefix}-fno-rtti"
)

add_subdirectory(Luft)
add_subdirectory(RHI)

add_executable(Hummingbird WIN32)

set(HummingbirdShaders
	Source/Shaders/Barycentrics.hlsli
	Source/Shaders/Base.hlsli
	Source/Shaders/Color.hlsli
	Source/Shaders/Deferred.hlsl
	Source/Shaders/Geometry.hlsli
	Source/Shaders/Hash.hlsli
	Source/Shaders/Luminance.hlsli
	Source/Shaders/LuminanceAverage.hlsl
	Source/Shaders/LuminanceHistogram.hlsl
	Source/Shaders/LuminanceHistogram.hlsli
	Source/Shaders/Math.hlsli
	Source/Shaders/PathTrace.hlsl
	Source/Shaders/PathTrace.hlsli
	Source/Shaders/PBR.hlsli
	Source/Shaders/Samplers.hlsli
	Source/Shaders/Shadow.hlsli
	Source/Shaders/Shared.hlsli
	Source/Shaders/Surface.hlsli
	Source/Shaders/TemporalAntiAlias.hlsl
	Source/Shaders/TemporalAntiAlias.hlsli
	Source/Shaders/ToneMap.hlsl
	Source/Shaders/ToneMap.hlsli
	Source/Shaders/Transform.hlsli
	Source/Shaders/Types.hlsli
	Source/Shaders/UI.hlsl
	Source/Shaders/UI.hlsli
	Source/Shaders/ViewMode.hlsli
	Source/Shaders/Visibility.hlsl
)

target_sources(Hummingbird
	PRIVATE
		Source/CameraController.cpp
		Source/DDS.cpp
		Source/Editor.cpp
		Source/GLTF.cpp
		Source/JSON.cpp
		Source/Renderer.cpp
		Source/RenderGraph.cpp
		Source/ResourceUploader.cpp
		Source/Start.cpp
		Source/UI.cpp
		Source/CameraController.hpp
		Source/DDS.hpp
		Source/Editor.hpp
		Source/GLTF.hpp
		Source/JSON.hpp
		Source/RenderContext.hpp
		Source/RenderGraph.hpp
		Source/RenderTypes.hpp
		Source/Renderer.hpp
		Source/ResourceUploader.hpp
		Source/UI.hpp
		Hummingbird.natvis
		${HummingbirdShaders}
)

set_source_files_properties(${HummingbirdShaders} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(Hummingbird
	PRIVATE
		Source
)

target_link_libraries(Hummingbird
	PRIVATE
		RHI
		Luft
)

set_target_properties(Hummingbird PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Hummingbird>"
)

rhi_add_runtime_dependencies(Hummingbird)

function(target_symlink_directories target)
	foreach(source_directory IN LISTS ARGN)
		cmake_path(GET source_directory FILENAME directory_name)

		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E create_symlink
				"${source_directory}"
				"$<TARGET_FILE_DIR:${target}>/${directory_name}"
			VERBATIM
		)
	endforeach()
endfunction()

target_symlink_directories(Hummingbird
	"${CMAKE_CURRENT_SOURCE_DIR}/Assets"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders"
)
