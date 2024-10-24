#include "DDS.hpp"

struct PixelFormat
{
	uint32 Size;
	uint32 Flags;
	uint32 CompressedOrCustomFormat;
	uint32 RgbBitCount;
	uint32 RedBitMask;
	uint32 GreenBitMask;
	uint32 BlueBitMask;
	uint32 AlphaBitMask;
};

struct DdsHeader
{
	uint32 Size;
	uint32 Flags;
	uint32 Height;
	uint32 Width;
	uint32 PitchOrLinearSize;
	uint32 Depth;
	uint32 MipMapCount;
	uint32 Reserved1[11];
	PixelFormat Format;
	uint32 Caps[4];
	uint32 Reserved2;
};

struct DdsExtendedHeader
{
	DXGI_FORMAT DxgiFormat;
	uint32 ResourceDimension;
	uint32 MiscFlags1;
	uint32 ArraySize;
	uint32 MiscFlags2;
};

static uint32 Advance(usize* offset, usize count)
{
	CHECK(offset);
	*offset += count;
	return 0;
}

static uint32 ParseUint32(StringView view, usize* offset)
{
	CHECK(offset);
	const usize currentOffset = *offset;
	VERIFY(currentOffset + sizeof(uint32) <= view.Length, "Failed to parse integer!");

	const uint32 value = (view[currentOffset + 0] << 0)  |
						 (view[currentOffset + 1] << 8)  |
						 (view[currentOffset + 2] << 16) |
						 (view[currentOffset + 3] << 24);

	*offset += sizeof(uint32);
	return value;
}

DdsImage LoadDdsImage(StringView filePath)
{
	usize ddsFileSize;
	uint8* ddsFileData = Platform::ReadEntireFile(reinterpret_cast<const char*>(filePath.Buffer), filePath.Length, &ddsFileSize, GlobalAllocator::Get());
	const StringView ddsFileView = { ddsFileData, ddsFileSize };

	VERIFY(ddsFileSize >= 4, "Invalid DDS file!");
	VERIFY(ddsFileView[0] == u8'D', "Unexpected image file format!");
	VERIFY(ddsFileView[1] == u8'D', "Unexpected image file format!");
	VERIFY(ddsFileView[2] == u8'S', "Unexpected image file format!");
	VERIFY(ddsFileView[3] == u8' ', "Unexpected image file format!");
	usize offset = 4;

	const DdsHeader header =
	{
		.Size = ParseUint32(ddsFileView, &offset),
		.Flags = ParseUint32(ddsFileView, &offset),
		.Height = ParseUint32(ddsFileView, &offset),
		.Width = ParseUint32(ddsFileView, &offset),
		.PitchOrLinearSize = ParseUint32(ddsFileView, &offset),
		.Depth = ParseUint32(ddsFileView, &offset),
		.MipMapCount = ParseUint32(ddsFileView, &offset),
		.Reserved1 = { Advance(&offset, sizeof(DdsHeader::Reserved1)) },
		.Format =
		{
			.Size = ParseUint32(ddsFileView, &offset),
			.Flags = ParseUint32(ddsFileView, &offset),
			.CompressedOrCustomFormat = ParseUint32(ddsFileView, &offset),
			.RgbBitCount = ParseUint32(ddsFileView, &offset),
			.RedBitMask = ParseUint32(ddsFileView, &offset),
			.GreenBitMask = ParseUint32(ddsFileView, &offset),
			.BlueBitMask = ParseUint32(ddsFileView, &offset),
			.AlphaBitMask = ParseUint32(ddsFileView, &offset),
		},
		.Caps =
		{
			ParseUint32(ddsFileView, &offset),
			ParseUint32(ddsFileView, &offset),
			ParseUint32(ddsFileView, &offset),
			ParseUint32(ddsFileView, &offset)
		},
		.Reserved2 = Advance(&offset, sizeof(DdsHeader::Reserved2)),
	};

	static constexpr uint32 headerCapsFlag = 0x1;
	static constexpr uint32 headerHeightFlag = 0x2;
	static constexpr uint32 headerWidthFlag = 0x4;
	static constexpr uint32 headerPixelFormatFlag = 0x1000;

	static constexpr uint32 pixelFormatCompressedOrCustomFlag = 0x4;

	static constexpr uint32 capsTextureFlag = 0x1000;

	static constexpr uint32 pixelFormatExtendedHeader = 808540228;

	VERIFY(header.Size == 124, "Invalid DDS file!");
	VERIFY((header.Flags & headerCapsFlag), "Invalid DDS file!");
	VERIFY((header.Flags & headerHeightFlag), "Invalid DDS file!");
	VERIFY((header.Flags & headerWidthFlag), "Invalid DDS file!");
	VERIFY((header.Flags & headerPixelFormatFlag), "Invalid DDS file!");

	VERIFY((header.Caps[0] & capsTextureFlag), "Invalid DDS file!");

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

void DestroyDdsImage(DdsImage* image)
{
	GlobalAllocator::Get().Deallocate(image->Data, image->DataSize);
	image->Data = nullptr;
	image->DataSize = 0;
	image->Width = 0;
	image->Height = 0;
}
