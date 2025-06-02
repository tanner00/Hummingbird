#include "DDS.hpp"

#include <dxgiformat.h>

#define DDS_FORMAT(a, b, c, d) (((uint32)(d) << 24) | ((uint32)(c) << 16) | ((uint32)(b) << 8) | (uint32)(a))

namespace DDS
{

static Allocator* Allocator = &GlobalAllocator::Get();

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

struct Header
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

struct ExtendedHeader
{
	DXGI_FORMAT DxgiFormat;
	uint32 ResourceDimension;
	uint32 MiscFlags1;
	uint32 ArraySize;
	uint32 MiscFlags2;
};

static constexpr char FormatSignature[] = "DDS ";
static constexpr usize BaseHeaderSize = sizeof(Header) + (sizeof(FormatSignature) - 1);
static constexpr usize ExtendedHeaderSize = sizeof(ExtendedHeader);

static RHI::ResourceFormat From(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_UNKNOWN:
		return RHI::ResourceFormat::None;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return RHI::ResourceFormat::RGBA8UNorm;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return RHI::ResourceFormat::RGBA8UNormSRGB;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return RHI::ResourceFormat::RGBA16Float;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return RHI::ResourceFormat::RGBA32Float;
	case DXGI_FORMAT_R32G32_UINT:
		return RHI::ResourceFormat::RG32UInt;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return RHI::ResourceFormat::Depth24Stencil8;
	case DXGI_FORMAT_D32_FLOAT:
		return RHI::ResourceFormat::Depth32;
	case DXGI_FORMAT_BC1_UNORM:
		return RHI::ResourceFormat::BC1UNorm;
	case DXGI_FORMAT_BC3_UNORM:
		return RHI::ResourceFormat::BC3UNorm;
	case DXGI_FORMAT_BC5_UNORM:
		return RHI::ResourceFormat::BC5UNorm;
	case DXGI_FORMAT_BC7_UNORM:
		return RHI::ResourceFormat::BC7UNorm;
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return RHI::ResourceFormat::BC7UNormSRGB;
	}
	CHECK(false);
	return RHI::ResourceFormat::None;
}

static int32 Advance(usize* offset, usize count)
{
	CHECK(offset);
	*offset += count;
	return 0;
}

static uint32 ParseUInt32(StringView view, usize* offset)
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

Image LoadImage(StringView filePath)
{
	usize fileSize;
	char* fileData = reinterpret_cast<char*>(Platform::ReadEntireFile(filePath.GetData(), filePath.GetLength(), &fileSize, *Allocator));
	const StringView fileView = { fileData, fileSize };

	VERIFY(fileSize >= sizeof(FormatSignature) - 1, "Invalid DDS file!");
	for (usize i = 0; i < sizeof(FormatSignature) - 1; ++i)
	{
		VERIFY(fileView[i] == FormatSignature[i], "Unexpected image file format!");
	}
	usize offset = 4;

	Header header =
	{
		.Size = ParseInt32(fileView, &offset),
		.Flags = ParseInt32(fileView, &offset),
		.Height = ParseInt32(fileView, &offset),
		.Width = ParseInt32(fileView, &offset),
		.PitchOrLinearSize = ParseInt32(fileView, &offset),
		.Depth = ParseInt32(fileView, &offset),
		.MipMapCount = ParseInt32(fileView, &offset),
		.Reserved1 = { Advance(&offset, sizeof(Header::Reserved1)) },
		.Format =
		{
			.Size = ParseInt32(fileView, &offset),
			.Flags = ParseInt32(fileView, &offset),
			.CompressedOrCustomFormat = ParseInt32(fileView, &offset),
			.RgbBitCount = ParseInt32(fileView, &offset),
			.RedBitMask = ParseInt32(fileView, &offset),
			.GreenBitMask = ParseInt32(fileView, &offset),
			.BlueBitMask = ParseInt32(fileView, &offset),
			.AlphaBitMask = ParseInt32(fileView, &offset),
		},
		.Caps =
		{
			ParseInt32(fileView, &offset),
			ParseInt32(fileView, &offset),
			ParseInt32(fileView, &offset),
			ParseInt32(fileView, &offset),
		},
		.Reserved2 = Advance(&offset, sizeof(Header::Reserved2)),
	};
	if (header.Height < 0)
	{
		header.Height = -header.Height;
		Platform::Log("DDS::LoadImage: Flipped-Y is currently unsupported!\n");
	}

	static constexpr uint32 headerCapsFlag = 0x1;
	static constexpr uint32 headerHeightFlag = 0x2;
	static constexpr uint32 headerWidthFlag = 0x4;
	static constexpr uint32 headerPixelFormatFlag = 0x1000;

	static constexpr uint32 pixelFormatCompressedOrCustomFlag = 0x4;

	static constexpr uint32 capsTextureFlag = 0x1000;

	VERIFY(header.Size == 124, "Invalid DDS file!");
	VERIFY(header.Flags & headerCapsFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerHeightFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerWidthFlag, "Invalid DDS file!");
	VERIFY(header.Flags & headerPixelFormatFlag, "Invalid DDS file!");

	VERIFY(header.Caps[0] & capsTextureFlag, "Invalid DDS file!");

	VERIFY(header.Format.Size == 32, "Invalid DDS file!");
	VERIFY(header.Format.Flags & pixelFormatCompressedOrCustomFlag, "Unexpected DDS file type!");

	RHI::ResourceFormat format = RHI::ResourceFormat::None;
	usize headerSize = BaseHeaderSize;

	switch (header.Format.CompressedOrCustomFormat)
	{
	case DDS_FORMAT('D', 'X', '1', '0'):
	{
		headerSize += ExtendedHeaderSize;

		const ExtendedHeader extendedHeader =
		{
			.DxgiFormat = static_cast<DXGI_FORMAT>(ParseUInt32(fileView, &offset)),
			.ResourceDimension = ParseUInt32(fileView, &offset),
			.MiscFlags1 = ParseUInt32(fileView, &offset),
			.ArraySize = ParseUInt32(fileView, &offset),
			.MiscFlags2 = ParseUInt32(fileView, &offset),
		};

		static constexpr usize extendedHeaderRectangleTexture = 3;

		VERIFY(extendedHeader.ResourceDimension == extendedHeaderRectangleTexture, "Unexpected DDS file type!");
		VERIFY(extendedHeader.ArraySize == 1, "Unexpected DDS file type!");

		format = From(extendedHeader.DxgiFormat);
		break;
	}
	case DDS_FORMAT('D', 'X', 'T', '1'):
		format = From(DXGI_FORMAT_BC1_UNORM);
		break;
	case DDS_FORMAT('D', 'X', 'T', '5'):
		format = From(DXGI_FORMAT_BC3_UNORM);
		break;
	case DDS_FORMAT('A', 'T', 'I', '2'):
		format = From(DXGI_FORMAT_BC5_UNORM);
		break;
	default:
		VERIFY(false, "Unexpected DDS file type!");
	}

	uint8* imageData = reinterpret_cast<uint8*>(fileData + headerSize);
	const usize imageDataSize = fileSize - headerSize;

	return Image
	{
		.Data = imageData,
		.DataSize = imageDataSize,
		.HeaderSize = headerSize,
		.Format = format,
		.Width = static_cast<uint32>(header.Width),
		.Height = static_cast<uint32>(header.Height),
		.MipMapCount = static_cast<uint16>(header.MipMapCount),
	};
}

void UnloadImage(Image* image)
{
	Allocator->Deallocate(image->Data - image->HeaderSize, image->DataSize + image->HeaderSize);
	image->Data = nullptr;
	image->DataSize = 0;
	image->HeaderSize = 0;
	image->Width = 0;
	image->Height = 0;
	image->MipMapCount = 0;
}

}
