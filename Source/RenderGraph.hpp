#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Function.hpp"
#include "Luft/String.hpp"

namespace RenderGraph
{

enum class ViewResourceType : uint8
{
	Buffer,
	Texture,
};

class View
{
public:
	View(const RHI::BufferView& bufferView)
		: BufferView(bufferView)
		, Type(ViewResourceType::Buffer)
	{
	}

	View(const RHI::TextureView& textureView)
		: TextureView(textureView)
		, Type(ViewResourceType::Texture)
	{
	}

	union
	{
		RHI::BufferView BufferView;
		RHI::TextureView TextureView;
	};
	ViewResourceType Type;
};

void AddGraphicsPass(StringView name, ArrayView<View> reads, ArrayView<View> writes, const Function<void()>& function);
void AddComputePass(StringView name, ArrayView<View> reads, ArrayView<View> writes, const Function<void()>& function);

void Execute();

float64 GetTimerMostRecentTimeGPU(usize slot);
usize GetTimerCount();
StringView GetTimerName(usize slot);

}
