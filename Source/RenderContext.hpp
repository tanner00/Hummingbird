#pragma once

#include "RHI/RHI.hpp"

struct RenderContext
{
	RHI::Device* Device;
	RHI::GraphicsContext Graphics;
	RHI::Sampler LinearWrapSampler;
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

inline void CreateRenderContext(Platform::Window* window)
{
	RenderContext.Device = GlobalAllocator::Get().Create<RHI::Device>(RHI::DeviceDescription
	{
		.Window = window,
	});

	RenderContext.Graphics = GlobalDevice().Create(RHI::GraphicsContextDescription {});
	RenderContext.LinearWrapSampler = GlobalDevice().Create(
	{
		.MinificationFilter = RHI::SamplerFilter::Linear,
		.MagnificationFilter = RHI::SamplerFilter::Linear,
		.HorizontalAddress = RHI::SamplerAddress::Clamp,
		.VerticalAddress = RHI::SamplerAddress::Clamp,
	});
}

inline void DestroyRenderContext()
{
	GlobalDevice().Destroy(&RenderContext.Graphics);
	GlobalDevice().Destroy(&RenderContext.LinearWrapSampler);

	GlobalAllocator::Get().Destroy(RenderContext.Device);
}
