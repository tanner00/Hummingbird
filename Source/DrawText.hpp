#pragma once

#include "Luft/Math.hpp"
#include "Luft/NoCopy.hpp"

#include "RHI/RHI.hpp"

struct Glyph
{
	Float2 AtlasPosition;
	Float2 AtlasSize;
	Float2 PlanePosition;
	Float2 PlaneSize;
	float Advance;
};

namespace Hlsl
{

struct TextRootConstants
{
	Matrix ViewProjection;
	Float2 UnitRange;

	uint32 CharacterBuffer;
	uint32 Texture;
	uint32 Sampler;
};

struct Character
{
	Float4 Color;

	Float2 ScreenPosition;

	Float2 AtlasPosition;
	Float2 AtlasSize;

	Float2 PlanePosition;
	Float2 PlaneSize;

	float Scale;
};

}

class DrawText : public NoCopy
{
public:
	DrawText();

	void Init(GpuDevice* device);
	void Shutdown();

	static DrawText& Get()
	{
		static DrawText instance;
		return instance;
	}

	void Draw(StringView text, Float2 position, Float3 rgb, float scale);
	void Draw(StringView text, Float2 position, Float4 rgba, float scale);

	void Submit(GraphicsContext* graphics, uint32 width, uint32 height);

private:
	HashTable<char, Glyph> Glyphs;

	float Ascender;

	Hlsl::TextRootConstants RootConstants;

	usize CharacterIndex;
	Array<Hlsl::Character> CharacterData;

	GraphicsPipeline Pipeline;

	Texture FontTexture;
	Sampler Sampler;

	Buffer CharacterBuffer;

	GpuDevice* Device;
};
