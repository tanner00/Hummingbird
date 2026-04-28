#pragma once

#define SHARED_ON true
#include "Shared.hlsli"
#undef SHARED_ON

static const uint32 LuminanceHistogramBinsCount = 256;

static const float32 LuminanceLogMinimum = -20.0f; // Floor(Log2(0.000001f))
static const float32 LuminanceLogRange = 47.0f; // Ceiling(Log2(100000000.0f) - LuminanceLogMinimum)

#define SHARED_OFF true
#include "Shared.hlsli"
#undef SHARED_OFF
