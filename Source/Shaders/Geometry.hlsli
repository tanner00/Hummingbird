#pragma once

#include "Types.hlsli"

void LoadTriangleIndices(ByteAddressBuffer vertexBuffer, Primitive primitive, uint32 triangleOffset, out uint32 indices[3])
{
	if (primitive.IndexStride == 2)
	{
		indices[0] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 0);
		indices[1] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 1);
		indices[2] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 2);
	}
	else if (primitive.IndexStride == 4)
	{
		indices[0] = vertexBuffer.Load<uint32>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 0);
		indices[1] = vertexBuffer.Load<uint32>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 1);
		indices[2] = vertexBuffer.Load<uint32>(primitive.IndexOffset + triangleOffset + primitive.IndexStride * 2);
	}
}

void LoadTrianglePositions(ByteAddressBuffer vertexBuffer, Primitive primitive, uint32 indices[3], out float32x3 positionsLS[3])
{
	positionsLS[0] = vertexBuffer.Load<float32x3>(primitive.PositionOffset + indices[0] * primitive.PositionStride);
	positionsLS[1] = vertexBuffer.Load<float32x3>(primitive.PositionOffset + indices[1] * primitive.PositionStride);
	positionsLS[2] = vertexBuffer.Load<float32x3>(primitive.PositionOffset + indices[2] * primitive.PositionStride);
}

void LoadTriangleTextureCoordinates(ByteAddressBuffer vertexBuffer, Primitive primitive, uint32 indices[3], out float32x2 textureCoordinates[3])
{
	textureCoordinates[0] = vertexBuffer.Load<float32x2>(primitive.TextureCoordinateOffset + indices[0] * primitive.TextureCoordinateStride);
	textureCoordinates[1] = vertexBuffer.Load<float32x2>(primitive.TextureCoordinateOffset + indices[1] * primitive.TextureCoordinateStride);
	textureCoordinates[2] = vertexBuffer.Load<float32x2>(primitive.TextureCoordinateOffset + indices[2] * primitive.TextureCoordinateStride);
}

void LoadTriangleNormals(ByteAddressBuffer vertexBuffer, Primitive primitive, uint32 indices[3], out float32x3 normalsLS[3])
{
	normalsLS[0] = vertexBuffer.Load<float32x3>(primitive.NormalOffset + indices[0] * primitive.NormalStride);
	normalsLS[1] = vertexBuffer.Load<float32x3>(primitive.NormalOffset + indices[1] * primitive.NormalStride);
	normalsLS[2] = vertexBuffer.Load<float32x3>(primitive.NormalOffset + indices[2] * primitive.NormalStride);
}
