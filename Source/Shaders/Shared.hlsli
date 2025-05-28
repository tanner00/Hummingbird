#if SHARED_ON
#ifdef __cplusplus
#include "RHI/HLSL.hpp"
#include "Luft/Math.hpp"
#define uint uint32
#else
#define PAD(size)
#define Float2 float2
#define Float3 float3
#define Float4 float4
#define Matrix matrix
#define bool32 bool
#endif
#endif

#if SHARED_OFF
#undef PAD
#undef Float2
#undef Float3
#undef Float4
#undef Matrix
#undef bool32
#undef uint
#endif
