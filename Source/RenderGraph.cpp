#include "RenderGraph.hpp"
#include "RenderContext.hpp"

using namespace RHI;

namespace RenderGraph
{

static ::Allocator* Allocator = &GlobalAllocator::Get();

enum class PassType : uint8
{
	Graphics,
	Compute,
};

struct Pass
{
	PassType Type;

	Array<View> Reads;
	Array<View> Writes;

	Function<void()> Function;
};

struct ResourceState
{
	Resource Resource;
	BarrierStage Stage;
	BarrierAccess Access;
	BarrierLayout Layout;
	bool Write;
};

static Array<Pass> Passes(Allocator);
static Array<ResourceState> ResourceStates(Allocator);

static ResourceState& GetResourceState(const Resource& resource)
{
	for (ResourceState& state : ResourceStates)
	{
		if (state.Resource.Backend == resource.Backend)
		{
			return state;
		}
	}

	ResourceStates.Add(ResourceState
	{
		.Resource = resource,
		.Stage = BarrierStage::None,
		.Access = BarrierAccess::NoAccess,
		.Layout = resource.InitialLayout,
		.Write = false,
	});
	return ResourceStates.Last();
}

static bool NeedsBarrier(BarrierAccess accessBefore,
						 BarrierAccess accessAfter,
						 BarrierLayout layoutBefore,
						 BarrierLayout layoutAfter,
						 bool writeBefore,
						 bool writeAfter)
{
	const bool layout = layoutBefore != layoutAfter;

	const bool synchronization = (accessBefore == BarrierAccess::UnorderedAccess || accessAfter == BarrierAccess::UnorderedAccess) &&
								 (writeBefore || writeAfter) &&
								 accessBefore != BarrierAccess::NoAccess && accessAfter != BarrierAccess::NoAccess;

	return layout || synchronization;
}

static void InsertBufferBarrier(const BufferView& bufferView, bool writeAfter, PassType passType)
{
	if (!bufferView.IsValid())
	{
		return;
	}

	const BarrierStage stageAfter = passType == PassType::Graphics ? BarrierStage::AllShading : BarrierStage::ComputeShading;
	BarrierAccess accessAfter = BarrierAccess::NoAccess;

	switch (bufferView.Type)
	{
	case ViewType::ConstantBuffer:
		accessAfter = BarrierAccess::ConstantBuffer;
		break;
	case ViewType::ShaderResource:
		accessAfter = BarrierAccess::ShaderResource;
		break;
	case ViewType::UnorderedAccess:
		accessAfter = BarrierAccess::UnorderedAccess;
		break;
	default:
		CHECK(false);
		break;
	}

	ResourceState& state = GetResourceState(bufferView.Buffer.Resource);

	if (NeedsBarrier(state.Access, accessAfter, BarrierLayout::Undefined, BarrierLayout::Undefined, state.Write, writeAfter))
	{
		GlobalGraphics().BufferBarrier({ state.Stage, stageAfter },
									   { state.Access, accessAfter },
									   bufferView.Buffer.Resource);
	}

	state.Stage = stageAfter;
	state.Access = accessAfter;
	state.Write = writeAfter;
}

static void InsertTextureBarrier(const TextureView& textureView, bool writeAfter, PassType passType)
{
	if (!textureView.IsValid())
	{
		return;
	}

	BarrierStage stageAfter = passType == PassType::Graphics ? BarrierStage::AllShading : BarrierStage::ComputeShading;
	BarrierAccess accessAfter = BarrierAccess::NoAccess;
	BarrierLayout layoutAfter = BarrierLayout::Undefined;

	switch (textureView.Type)
	{
	case ViewType::ShaderResource:
		accessAfter = BarrierAccess::ShaderResource;
		layoutAfter = BarrierLayout::GraphicsQueueShaderResource;
		break;
	case ViewType::UnorderedAccess:
		accessAfter = BarrierAccess::UnorderedAccess;
		layoutAfter = BarrierLayout::GraphicsQueueUnorderedAccess;
		break;
	case ViewType::RenderTarget:
		stageAfter = BarrierStage::RenderTarget;
		accessAfter = BarrierAccess::RenderTarget;
		layoutAfter = BarrierLayout::RenderTarget;
		break;
	case ViewType::DepthStencil:
		stageAfter = BarrierStage::DepthStencil;
		accessAfter = writeAfter ? BarrierAccess::DepthStencilWrite : BarrierAccess::DepthStencilRead;
		layoutAfter = writeAfter ? BarrierLayout::DepthStencilWrite : BarrierLayout::DepthStencilRead;
		break;
	default:
		CHECK(false);
		break;
	}

	ResourceState& state = GetResourceState(textureView.Resource);

	if (NeedsBarrier(state.Access, accessAfter, state.Layout, layoutAfter, state.Write, writeAfter))
	{
		GlobalGraphics().TextureBarrier({ state.Stage, stageAfter },
										{ state.Access, accessAfter },
										{ state.Layout, layoutAfter },
										textureView.Resource);
	}

	state.Stage = stageAfter;
	state.Access = accessAfter;
	state.Layout = layoutAfter;
	state.Write = writeAfter;
}

static void AddPass(PassType type, ArrayView<View> reads, ArrayView<View> writes, const Function<void()>& function)
{
	Passes.Add(Pass
	{
		.Type = type,
		.Reads = Array(reads, Allocator),
		.Writes = Array(writes, Allocator),
		.Function = function,
	});
}

void AddGraphicsPass(ArrayView<View> reads, ArrayView<View> writes, const Function<void()>& function)
{
	AddPass(PassType::Graphics, reads, writes, function);
}

void AddComputePass(ArrayView<View> reads, ArrayView<View> writes, const Function<void()>& function)
{
	AddPass(PassType::Compute, reads, writes, function);
}

void Execute()
{
	for (const Pass& pass : Passes)
	{
		for (const View& read : pass.Reads)
		{
			switch (read.Type)
			{
			case ViewResourceType::Buffer:
				InsertBufferBarrier(read.BufferView, false, pass.Type);
				break;
			case ViewResourceType::Texture:
				InsertTextureBarrier(read.TextureView, false, pass.Type);
				break;
			}
		}

		for (const View& write : pass.Writes)
		{
			switch (write.Type)
			{
			case ViewResourceType::Buffer:
				InsertBufferBarrier(write.BufferView, true, pass.Type);
				break;
			case ViewResourceType::Texture:
				InsertTextureBarrier(write.TextureView, true, pass.Type);
				break;
			}
		}

		pass.Function();
	}

	for (ResourceState& state : ResourceStates)
	{
		if (state.Resource.Type != ResourceType::Texture2D || state.Layout == state.Resource.InitialLayout)
		{
			continue;
		}

		CHECK(state.Resource.InitialLayout != BarrierLayout::Undefined);

		GlobalGraphics().TextureBarrier({ state.Stage, BarrierStage::None },
										{ state.Access, BarrierAccess::NoAccess },
										{ state.Layout, state.Resource.InitialLayout },
										state.Resource);
	}

	Passes.Clear();
	ResourceStates.Clear();
}

}
