#pragma once

#include "RHI/Resource.hpp"

#include "Luft/Base.hpp"
#include "Luft/String.hpp"

namespace DDS
{

struct Image
{
	uint8* Data;
	usize DataSize;
	usize HeaderSize;

	RHI::ResourceFormat Format;

	uint32 Width;
	uint32 Height;

	uint16 MipMapCount;
};

Image LoadImage(StringView filePath);
void UnloadImage(Image* image);

}
