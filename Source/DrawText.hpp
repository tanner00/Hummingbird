#pragma once

#include "RHI/RHI.hpp"

namespace HLSL
{
#include "Shaders/Types.hlsli"
}

struct Glyph
{
	Float2 AtlasPosition;
	Float2 AtlasSize;
	Float2 PlanePosition;
	Float2 PlaneSize;
	float Advance;
};

class DrawText : public NoCopy
{
public:
	DrawText();

	void Init(RHI::Device* device);
	void Shutdown(const RHI::Device& device);

	static DrawText& Get()
	{
		static DrawText instance;
		return instance;
	}

	void Draw(StringView text, Float2 position, Float3 rgb, float scale);
	void Draw(StringView text, Float2 position, Float4 rgba, float scale);

	void Submit(RHI::GraphicsContext* graphics, RHI::Device* device, uint32 width, uint32 height);

private:
	HashTable<char, Glyph> Glyphs;

	float Ascender;

	HLSL::TextRootConstants RootConstants;

	usize CharacterIndex;
	Array<HLSL::Character> CharacterData;

	RHI::GraphicsPipeline Pipeline;

	RHI::Resource FontTexture;
	RHI::TextureView FontTextureView;

	RHI::Sampler LinearWrapSampler;

	RHI::Resource CharacterBuffers[RHI::FramesInFlight];
	RHI::BufferView CharacterBufferViews[RHI::FramesInFlight];
};
