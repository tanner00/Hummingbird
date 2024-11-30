﻿#include "DDS.hpp"

#include <dxgiformat.h>

static Allocator* DdsAllocator = &GlobalAllocator::Get();

struct PixelFormat
{
	int32 Size;
	int32 Flags;
	int32 CompressedOrCustomFormat;
	int32 RgbBitCount;
	int32 RedBitMask;
	int32 GreenBitMask;
	int32 BlueBitMask;
	int32 AlphaBitMask;
};

struct DdsHeader
{
	int32 Size;
	int32 Flags;
	int32 Height;
	int32 Width;
	int32 PitchOrLinearSize;
	int32 Depth;
	int32 MipMapCount;
	int32 Reserved1[11];
	PixelFormat Format;
	int32 Caps[4];
	int32 Reserved2;
};

struct DdsExtendedHeader
{
	DXGI_FORMAT DxgiFormat;
	uint32 ResourceDimension;
	uint32 MiscFlags1;
	uint32 ArraySize;
	uint32 MiscFlags2;
};

static TextureFormat FromD3D12(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_UNKNOWN:
		return TextureFormat::None;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return TextureFormat::Rgba8;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return TextureFormat::Rgba8Srgb;
	case DXGI_FORMAT_BC7_UNORM:
		return TextureFormat::Bc7;
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return TextureFormat::Bc7Srgb;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return TextureFormat::Depth24Stencil8;
	case DXGI_FORMAT_D32_FLOAT:
		return TextureFormat::Depth32;
	}
	CHECK(false);
	return TextureFormat::None;
}

static int32 Advance(usize* offset, usize count)
{
	CHECK(offset);
	*offset += count;
	return 0;
}

static uint32 ParseUint32(StringView view, usize* offset)
{
	CHECK(offset);
	const usize currentOffset = *offset;
	VERIFY(currentOffset + sizeof(uint32) <= view.GetLength(), "Failed to parse integer!");

	const uint32 value = (view[currentOffset + 0] << 0)  |
						 (view[currentOffset + 1] << 8)  |
						 (view[currentOffset + 2] << 16) |
						 (view[currentOffset + 3] << 24);

	*offset += sizeof(uint32);
	return value;
}

static int32 ParseInt32(StringView view, usize* offset)
{
	CHECK(offset);
	const usize currentOffset = *offset;
	VERIFY(currentOffset + sizeof(uint32) <= view.GetLength(), "Failed to parse integer!");

	const int32 value = (view[currentOffset + 0] << 0)  |
						(view[currentOffset + 1] << 8)  |
						(view[currentOffset + 2] << 16) |
						(view[currentOffset + 3] << 24);

	*offset += sizeof(uint32);
	return value;
}

DdsImage LoadDdsImage(StringView filePath)
{
	usize ddsFileSize;
	char* ddsFileData = reinterpret_cast<char*>(Platform::ReadEntireFile(filePath.GetData(), filePath.GetLength(), &ddsFileSize, *DdsAllocator));
	const StringView ddsFileView = { ddsFileData, ddsFileSize };

	VERIFY(ddsFileSize >= 4, "Invalid DDS file!");
	VERIFY(ddsFileView[0] == 'D', "Unexpected image file format!");
	VERIFY(ddsFileView[1] == 'D', "Unexpected image file format!");
	VERIFY(ddsFileView[2] == 'S', "Unexpected image file format!");
	VERIFY(ddsFileView[3] == ' ', "Unexpected image file format!");
	usize offset = 4;

	DdsHeader header =
	{
		.Size = ParseInt32(ddsFileView, &offset),
		.Flags = ParseInt32(ddsFileView, &offset),
		.Height = ParseInt32(ddsFileView, &offset),
		.Width = ParseInt32(ddsFileView, &offset),
		.PitchOrLinearSize = ParseInt32(ddsFileView, &offset),
		.Depth = ParseInt32(ddsFileView, &offset),
		.MipMapCount = ParseInt32(ddsFileView, &offset),
		.Reserved1 = { Advance(&offset, sizeof(DdsHeader::Reserved1)) },
		.Format =
		{
			.Size = ParseInt32(ddsFileView, &offset),
			.Flags = ParseInt32(ddsFileView, &offset),
			.CompressedOrCustomFormat = ParseInt32(ddsFileView, &offset),
			.RgbBitCount = ParseInt32(ddsFileView, &offset),
			.RedBitMask = ParseInt32(ddsFileView, &offset),
			.GreenBitMask = ParseInt32(ddsFileView, &offset),
			.BlueBitMask = ParseInt32(ddsFileView, &offset),
			.AlphaBitMask = ParseInt32(ddsFileView, &offset),
		},
		.Caps =
		{
			ParseInt32(ddsFileView, &offset),
			ParseInt32(ddsFileView, &offset),
			ParseInt32(ddsFileView, &offset),
			ParseInt32(ddsFileView, &offset),
		},
		.Reserved2 = Advance(&offset, sizeof(DdsHeader::Reserved2)),
	};
	if (header.Height < 0)
	{
		header.Height = -header.Height;
		Platform::Log("LoadDdsImage: Flipped-Y is currently unsupported!\n");
	}

	static constexpr uint32 headerCapsFlag = 0x1;
	static constexpr uint32 headerHeightFlag = 0x2;
	static constexpr uint32 headerWidthFlag = 0x4;
	static constexpr uint32 headerPixelFormatFlag = 0x1000;

	static constexpr uint32 pixelFormatCompressedOrCustomFlag = 0x4;

	static constexpr uint32 capsTextureFlag = 0x1000;

	static constexpr uint32 pixelFormatExtendedHeader = 808540228;

	VERIFY(header.Size == 124, "Invalid DDS file!");
	VERIFY(header.Flags & headerCapsFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerHeightFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerWidthFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerPixelFormatFlag, "Invalid DDS file!");

	VERIFY(header.Caps[0] & capsTextureFlag, "Invalid DDS file!");

	VERIFY(header.Format.Size == 32, "Invalid DDS file!");
	VERIFY(header.Format.Flags & pixelFormatCompressedOrCustomFlag, "Unexpected DDS file type!");
	VERIFY(header.Format.CompressedOrCustomFormat == pixelFormatExtendedHeader, "Unexpected DDS file type!");

	const DdsExtendedHeader extendedHeader =
	{
		.DxgiFormat = static_cast<DXGI_FORMAT>(ParseUint32(ddsFileView, &offset)),
		.ResourceDimension = ParseUint32(ddsFileView, &offset),
		.MiscFlags1 = ParseUint32(ddsFileView, &offset),
		.ArraySize = ParseUint32(ddsFileView, &offset),
		.MiscFlags2 = ParseUint32(ddsFileView, &offset),
	};

	static constexpr usize extendedHeaderRectangleTexture = 3;

	VERIFY(extendedHeader.ResourceDimension == extendedHeaderRectangleTexture, "Unexpected DDS file type!");
	VERIFY(extendedHeader.ArraySize == 1, "Unexpected DDS file type!");

	const usize imageDataSize = ddsFileSize - offset;
	uint8* imageData = static_cast<uint8*>(GlobalAllocator::Get().Allocate(imageDataSize));
	Platform::MemoryCopy(imageData, ddsFileData + offset, imageDataSize);

	GlobalAllocator::Get().Deallocate(ddsFileData, ddsFileSize);
	return DdsImage
	{
		.Data = imageData,
		.DataSize = imageDataSize,
		.Format = FromD3D12(extendedHeader.DxgiFormat),
		.Width = header.Width,
		.Height = header.Height,
	};
}

void UnloadDdsImage(DdsImage* image)
{
	GlobalAllocator::Get().Deallocate(image->Data, image->DataSize);
	image->Data = nullptr;
	image->DataSize = 0;
	image->Width = 0;
	image->Height = 0;
}
