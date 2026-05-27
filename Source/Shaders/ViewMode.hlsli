#include "Common.hlsli"
#include "Surface.hlsli"
#include "Types.hlsli"

bool32 CheckViewMode(ViewMode viewMode, Surface surface, uint32 triangleIndex, out float32x3 rgb)
{
	if (viewMode == ViewMode::Lit)
	{
		return false;
	}

	switch (viewMode)
	{
	case ViewMode::Unlit:
		rgb = surface.IsSpecularGlossiness ? surface.DiffuseRGB : surface.BaseColorRGB;
		break;
	case ViewMode::Geometry:
		rgb = ToRGB(Hash(triangleIndex));
		break;
	case ViewMode::Normal:
		rgb = SRGBToLinear(surface.ShadeNormalWS * 0.5f + 0.5f);
		break;
	default:
		break;
	}
	return true;
}
