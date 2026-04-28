#pragma once

#include "RHI/RHI.hpp"

#include "Luft/HashTable.hpp"

namespace HLSL
{
#include "Shaders/Types.hlsli"
}

struct Glyph
{
	float32x2 AtlasPosition;
	float32x2 AtlasSize;
	float32x2 PlanePosition;
	float32x2 PlaneSize;
	float Advance;
};

class DrawText : public NoCopy
{
public:
	void Init();
	void Shutdown();

	static DrawText& Get()
	{
		static DrawText instance;
		return instance;
	}

	void Draw(StringView text, float32x2 positionSS, float32x3 rgb, float scale);
	void Draw(StringView text, float32x2 positionSS, float32x4 rgba, float scale);

	void Submit(uint32 width, uint32 height);

private:
	DrawText()
		: Glyphs(64, &GlobalAllocator::Get())
		, Ascender(0.0f)
		, RootConstants()
		, CharacterIndex(0)
	{
	}

	HashTable<char, Glyph> Glyphs;

	float Ascender;

	HLSL::TextRootConstants RootConstants;

	usize CharacterIndex;
	Array<HLSL::Character> CharacterData;

	RHI::Resource FontTexture;
	RHI::TextureView FontTextureView;

	RHI::GraphicsPipeline Pipeline;

	RHI::Resource CharacterBuffers[RHI::FramesInFlight];
	RHI::BufferView CharacterBufferViews[RHI::FramesInFlight];
};
