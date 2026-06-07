#pragma once

#include "Types.hlsli"

SamplerState GetLinearClampSampler()
{
	return SamplerDescriptorHeap[LinearClampSamplerIndex];
}

SamplerState GetAnisotropicWrapSampler()
{
	return SamplerDescriptorHeap[AnisotropicWrapSamplerIndex];
}
