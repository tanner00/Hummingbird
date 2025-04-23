#pragma once

#include "RHI/Resource.hpp"

#include "Luft/Base.hpp"
#include "Luft/String.hpp"

struct DdsImage
{
	uint8* Data;
	usize DataSize;

	RHI::ResourceFormat Format;

	uint32 Width;
	uint32 Height;

	uint16 MipMapCount;
};

DdsImage LoadDdsImage(StringView filePath);
void UnloadDdsImage(DdsImage* image);
