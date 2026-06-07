#if SHARED_ON
#ifdef __cplusplus
#include "RHI/HLSL.hpp"
#include "Luft/Math.hpp"
#else
#include "Base.hlsli"
#define PAD(size)
#define Matrix matrix
#endif
#endif

#if SHARED_OFF
#undef PAD
#undef Matrix
#endif
