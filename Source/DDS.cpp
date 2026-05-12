#include "DDS.hpp"

#include "Luft/Platform.hpp"

#include <dxgiformat.h>

#define DDS_FORMAT(a, b, c, d) (((uint32)(d) << 24) | ((uint32)(c) << 16) | ((uint32)(b) << 8) | (uint32)(a))

namespace DDS
{

static Allocator* Allocator = &GlobalAllocator::Get();

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

struct Header
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

enum class ResourceDimension : uint32
{
	Unknown = 0,
	Buffer = 1,
	Texture1D = 2,
	Texture2D = 3,
	Texture3D = 4,
};

struct ExtendedHeader
{
	DXGI_FORMAT DxgiFormat;
	ResourceDimension ResourceDimension;
	uint32 MiscFlags1;
	uint32 ArraySize;
	uint32 MiscFlags2;
};

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
	default:
		CHECK(false);
	}
	return RHI::ResourceFormat::None;
}

Image LoadImage(StringView filePath)
{
	static constexpr const char* InvalidMessage = "Invalid DDS file!";
	static constexpr const char* UnexpectedMessage = "Unexpected DDS file!";

	usize fileSize;
	uint8* fileData = Platform::ReadEntireFile(filePath, &fileSize, Allocator);

	usize offset = 0;

	VERIFY(offset + 4 <= fileSize, InvalidMessage);
	VERIFY(StringView(reinterpret_cast<char*>(fileData), 4) == "DDS "_view, "Unexpected image file format!");
	offset += 4;

	VERIFY(offset + sizeof(Header) <= fileSize, InvalidMessage);
	Header header;
	Platform::MemoryCopy(&header, fileData + offset, sizeof(Header));
	offset += sizeof(Header);

	static constexpr uint32 headerCapsFlag = 0x1;
	static constexpr uint32 headerHeightFlag = 0x2;
	static constexpr uint32 headerWidthFlag = 0x4;
	static constexpr uint32 headerPixelFormatFlag = 0x1000;

	static constexpr uint32 pixelFormatCompressedOrCustomFlag = 0x4;

	static constexpr uint32 capsTextureFlag = 0x1000;

	VERIFY(header.Size == sizeof(Header), InvalidMessage);
	VERIFY(header.Flags & headerCapsFlag, InvalidMessage);
	VERIFY(header.Flags & headerHeightFlag, InvalidMessage);
	VERIFY(header.Flags & headerWidthFlag, InvalidMessage);
	VERIFY(header.Flags & headerPixelFormatFlag, InvalidMessage);

	VERIFY(header.Caps[0] & capsTextureFlag, InvalidMessage);

	VERIFY(header.Format.Size == 32, InvalidMessage);
	VERIFY(header.Format.Flags & pixelFormatCompressedOrCustomFlag, UnexpectedMessage);

	RHI::ResourceFormat format = RHI::ResourceFormat::None;

	switch (header.Format.CompressedOrCustomFormat)
	{
	case DDS_FORMAT('D', 'X', '1', '0'):
	{
		VERIFY(offset + sizeof(ExtendedHeader) <= fileSize, InvalidMessage);
		ExtendedHeader extendedHeader;
		Platform::MemoryCopy(&extendedHeader, fileData + offset, sizeof(ExtendedHeader));
		offset += sizeof(ExtendedHeader);

		VERIFY(extendedHeader.ResourceDimension == ResourceDimension::Texture2D, UnexpectedMessage);
		VERIFY(extendedHeader.ArraySize == 1, UnexpectedMessage);

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
		VERIFY(false, UnexpectedMessage);
	}

	return Image
	{
		.Data = fileData + offset,
		.DataSize = fileSize - offset,
		.HeaderSize = offset,
		.Format = format,
		.Width = header.Width,
		.Height = header.Height,
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
