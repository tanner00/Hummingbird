#pragma once

#include "RHI/RHI.hpp"

namespace HLSL
{
#include "Shaders/Types.hlsli"
}

struct RenderContext
{
	RHI::Device* Device;
	RHI::GraphicsContext Graphics;

	RHI::ViewHeap ResourceViewHeap;
	RHI::ViewHeap SamplerViewHeap;
	RHI::ViewHeap RenderTargetViewHeap;
	RHI::ViewHeap DepthStencilViewHeap;

	RHI::Sampler LinearClampSampler;
};

inline RenderContext RenderContext = {};

inline RHI::Device& GlobalDevice()
{
	CHECK(RenderContext.Device);
	return *RenderContext.Device;
}

inline RHI::GraphicsContext& GlobalGraphics()
{
	return RenderContext.Graphics;
}

inline RHI::ViewHeap& GlobalResourceViewHeap()
{
	return RenderContext.ResourceViewHeap;
}

inline RHI::ViewHeap& GlobalSamplerViewHeap()
{
	return RenderContext.SamplerViewHeap;
}

inline RHI::ViewHeap& GlobalRenderTargetViewHeap()
{
	return RenderContext.RenderTargetViewHeap;
}

inline RHI::ViewHeap& GlobalDepthStencilViewHeap()
{
	return RenderContext.DepthStencilViewHeap;
}

inline void CreateRenderContext(Platform::Window* window, bool validation)
{
	RenderContext.Device = GlobalAllocator::Get().Create<RHI::Device>(RHI::DeviceDescription
	{
		.Window = window,
		.Validation = validation,
	});

	RenderContext.ResourceViewHeap = GlobalDevice().Create(
	{
		.Type = RHI::ViewHeapType::Resource,
		.Count = 1 << 12,
		.ReservedCount = 1,
	});
	RenderContext.SamplerViewHeap = GlobalDevice().Create(
	{
		.Type = RHI::ViewHeapType::Sampler,
		.Count = HLSL::SamplerCount,
		.ReservedCount = HLSL::SamplerCount,
	});
	RenderContext.RenderTargetViewHeap = GlobalDevice().Create(
	{
		.Type = RHI::ViewHeapType::RenderTarget,
		.Count = 4,
	});
	RenderContext.DepthStencilViewHeap = GlobalDevice().Create(
	{
		.Type = RHI::ViewHeapType::DepthStencil,
		.Count = 1,
	});

	RenderContext.Graphics = GlobalDevice().Create(RHI::GraphicsContextDescription {});

	RenderContext.LinearClampSampler = GlobalDevice().Create(
	{
		.MinificationFilter = RHI::SamplerFilter::Linear,
		.MagnificationFilter = RHI::SamplerFilter::Linear,
		.HorizontalAddress = RHI::SamplerAddress::Clamp,
		.VerticalAddress = RHI::SamplerAddress::Clamp,
		.ViewHeap = RenderContext.SamplerViewHeap,
		.ReservedIndex = HLSL::LinearClampSamplerIndex,
	});
}

inline void DestroyRenderContext()
{
	GlobalDevice().Destroy(&RenderContext.Graphics);
	GlobalDevice().Destroy(&RenderContext.LinearClampSampler);

	GlobalDevice().Destroy(&RenderContext.ResourceViewHeap);
	GlobalDevice().Destroy(&RenderContext.SamplerViewHeap);
	GlobalDevice().Destroy(&RenderContext.RenderTargetViewHeap);
	GlobalDevice().Destroy(&RenderContext.DepthStencilViewHeap);

	GlobalAllocator::Get().Destroy(RenderContext.Device);
}
