#pragma once

#include "Base.hlsli"

static const float32 Pi = 3.14159265358979323846f;
static const float32 Infinity = 1.#INF;

template<typename T>
T ToSafeDenominator(T x, T epsilon = 1e-6)
{
	return x + (x >= 0.0 ? epsilon : -epsilon);
}
