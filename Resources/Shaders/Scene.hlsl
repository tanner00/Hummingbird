struct VertexInput
{
	float3 Position : POSITION0;
};

struct PixelInput
{
	float4 Position : SV_POSITION;
};

struct RootConstants
{
	uint NodeIndex;
};
ConstantBuffer<RootConstants> RootConstants : register(b0);

struct Scene
{
	matrix ViewProjection;

	uint NodeBuffer;
};
ConstantBuffer<Scene> Scene : register(b1);

struct Node
{
	matrix Transform;
};

PixelInput VertexMain(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBuffer];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	PixelInput result;
	result.Position = mul(Scene.ViewProjection, mul(node.Transform, float4(input.Position, 1.0f)));
	return result;
}

uint Hash(uint v)
{
	v ^= 2747636419;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	return v;
}

float4 ToColor(uint v)
{
	return float4
	(
		float((v >>  0) & 0xFF) / 255.0f,
		float((v >>  8) & 0xFF) / 255.0f,
		float((v >> 16) & 0xFF) / 255.0f,
		1.0f
	);
}

float4 PixelMain(PixelInput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET
{
	return ToColor(Hash(primitiveID));
}
