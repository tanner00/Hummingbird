#pragma once

#include "Luft/Base.hpp"
#include "Luft/String.hpp"

#include "RHI/Common.hpp"

struct DdsImage
{
	uint8* Data;
	usize DataSize;

	TextureFormat Format;

	uint32 Width;
	uint32 Height;
};

DdsImage LoadDdsImage(StringView filePath);
void DestroyDdsImage(DdsImage* image);
