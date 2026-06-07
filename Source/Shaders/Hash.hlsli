#pragma once

#include "Base.hlsli"

uint32 Hash(uint32 x)
{
	x ^= 2747636419;
	x *= 2654435769;
	x ^= x >> 16;
	x *= 2654435769;
	x ^= x >> 16;
	x *= 2654435769;
	return x;
}
