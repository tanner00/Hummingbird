#pragma once

#include "RHI/Texture.hpp"

#include "Luft/Base.hpp"
#include "Luft/String.hpp"

struct DdsImage
{
	uint8* Data;
	usize DataSize;

	TextureFormat Format;

	uint32 Width;
	uint32 Height;

	uint32 MipMapCount;
};

DdsImage LoadDdsImage(StringView filePath);
void UnloadDdsImage(DdsImage* image);
