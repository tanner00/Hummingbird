#include "Renderer.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "DrawText.hpp"
#include "GLTF.hpp"

namespace HLSL
{
#include "Shaders/Luminance.hlsli"
}

using namespace RHI;

static ::Allocator* RendererAllocator = &GlobalAllocator::Get();

static BasicTexture CreateBasicTexture(Device* device,
									   ResourceDimensions dimensions,
									   uint16 mipMapCount,
									   ResourceFormat format,
									   const void* data,
									   StringView name)
{
	CHECK(data);

	const Resource texture = device->Create(
	{
		.Type = ResourceType::Texture2D,
		.Format = format,
		.Flags = ResourceFlags::None,
		.InitialLayout = BarrierLayout::GraphicsQueueCommon,
		.Dimensions = dimensions,
		.MipMapCount = mipMapCount,
		.Name = name,
	});
	const TextureView view = device->Create(
	{
		.Resource = texture,
		.Type = ViewType::ShaderResource,
		.Format = texture.Format,
		.MipMapCount = mipMapCount,
	});
	device->Write(&texture, data);

	return BasicTexture { texture, view };
}

static BasicBuffer CreateBasicBuffer(Device* device,
									 usize size,
									 usize stride,
									 ResourceFlags flags,
									 ViewType type,
									 const void* data,
									 StringView name)
{
	const Resource buffer = device->Create(
	{
		.Type = ResourceType::Buffer,
		.Flags = flags,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size,
		.Name = name,
	});
	const BufferView view = device->Create(BufferViewDescription
	{
		.Type = type,
		.Buffer =
		{
			.Resource = buffer,
			.Size = size,
			.Stride = stride,
		},
	});
	if (data)
	{
		device->Write(&buffer, data);
	}

	return BasicBuffer { buffer, view };
}

Renderer::Renderer(const Platform::Window* window)
	: Device(window)
	, Graphics(Device.Create(GraphicsContextDescription {}))
	, SceneMeshes(RendererAllocator)
	, SceneNodes(RendererAllocator)
	, SceneMaterials(RendererAllocator)
	, SceneTwoChannelNormalMaps(false)
	, Deferred(true)
	, ViewMode(HLSL::ViewMode::Lit)
#if !RELEASE
	, AverageCpuTime(0.0)
	, AverageGpuTime(0.0)
#endif
{
	CreateScreenTextures(window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&Device);

	static constexpr uint8 white[] = { 0xFF, 0xFF, 0xFF, 0xFF };
	WhiteTexture = CreateBasicTexture(&Device, { 1, 1 }, 1, ResourceFormat::RGBA8UNorm, white, "White Texture"_view);

	static constexpr uint8 defaultNormal[] = { 0x7F, 0x7F, 0xFF, 0x00 };
	DefaultNormalMapTexture = CreateBasicTexture(&Device, { 1, 1 }, 1, ResourceFormat::RGBA8UNorm, defaultNormal, "Default Normal Map Texture"_view);

	AnisotropicWrapSampler = Device.Create(
	{
		.MinificationFilter = SamplerFilter::Anisotropic,
		.MagnificationFilter = SamplerFilter::Anisotropic,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});
	PointClampSampler = Device.Create(
	{
		.MinificationFilter = SamplerFilter::Point,
		.MagnificationFilter = SamplerFilter::Point,
		.HorizontalAddress = SamplerAddress::Clamp,
		.VerticalAddress = SamplerAddress::Clamp,
	});

	SceneLuminanceBuffer = CreateBasicBuffer(&Device,
											 HLSL::LuminanceHistogramBinsCount * sizeof(uint32) + sizeof(float),
											 0,
											 ResourceFlags::UnorderedAccess,
											 ViewType::UnorderedAccess,
											 nullptr,
											 "Scene Luminance Buffer"_view);

	CreatePipelines();
}

Renderer::~Renderer()
{
	UnloadScene();
	DestroyPipelines();

	Device.Destroy(&WhiteTexture.Resource);
	Device.Destroy(&WhiteTexture.View);

	Device.Destroy(&DefaultNormalMapTexture.Resource);
	Device.Destroy(&DefaultNormalMapTexture.View);

	Device.Destroy(&AnisotropicWrapSampler);
	Device.Destroy(&PointClampSampler);

	Device.Destroy(&SceneLuminanceBuffer.Resource);
	Device.Destroy(&SceneLuminanceBuffer.View);

	DrawText::Get().Shutdown(Device);

	DestroyScreenTextures();

	Device.Destroy(&Graphics);
}

void Renderer::Update(const CameraController& cameraController)
{
#if !RELEASE
	const double startCpuTime = Platform::GetTime();
#endif

#if !RELEASE
	if (IsKeyPressedOnce(Key::L))
	{
		ViewMode = HLSL::ViewMode::Lit;
	}
	if (IsKeyPressedOnce(Key::U))
	{
		ViewMode = HLSL::ViewMode::Unlit;
	}
	if (IsKeyPressedOnce(Key::G))
	{
		ViewMode = HLSL::ViewMode::Geometry;
	}
	if (IsKeyPressedOnce(Key::N))
	{
		ViewMode = HLSL::ViewMode::Normal;
	}

	if (IsKeyPressedOnce(Key::F))
	{
		Deferred = !Deferred;
	}

	if (IsKeyPressedOnce(Key::R))
	{
		Device.WaitForIdle();
		DestroyPipelines();
		CreatePipelines();
	}
#endif

	Graphics.Begin();

	Graphics.SetViewport(HDRRenderTarget.Resource.Dimensions.Width, HDRRenderTarget.Resource.Dimensions.Height);

	const Matrix view = cameraController.GetTransform().GetInverse();
	const Matrix projection = Matrix::Perspective
	(
		cameraController.GetFieldOfViewYRadians(),
		cameraController.GetAspectRatio(),
		cameraController.GetNearZ(),
		cameraController.GetFarZ()
	);
	const Vector viewPosition = cameraController.GetPosition();
	const HLSL::Scene sceneData =
	{
		.VertexBufferIndex = Device.Get(SceneVertexBuffer.View),
		.PrimitiveBufferIndex = Device.Get(ScenePrimitiveBuffer.View),
		.NodeBufferIndex = Device.Get(SceneNodeBuffer.View),
		.MaterialBufferIndex = Device.Get(SceneMaterialBuffer.View),
		.DrawCallBufferIndex = Device.Get(SceneDrawCallBuffer.View),
		.DirectionalLightBufferIndex = Device.Get(SceneDirectionalLightBuffer.View),
		.PointLightsBufferIndex = ScenePointLightsBuffer.View.IsValid() ? Device.Get(ScenePointLightsBuffer.View) : 0,
		.AccelerationStructureIndex = Device.Get(SceneAccelerationStructure),
		.ViewProjection = projection * view,
		.ViewPosition = { .X = viewPosition.X, .Y = viewPosition.Y, .Z = viewPosition.Z },
		.TwoChannelNormalMaps = SceneTwoChannelNormalMaps,
		.PointLightsCount = static_cast<uint32>(ScenePointLightsBuffer.View.Buffer.Size / sizeof(HLSL::PointLight)),
	};
	Device.Write(&SceneBuffers[Device.GetFrameIndex()].Resource, &sceneData);

	Graphics.ClearDepthStencil(DepthTexture.View);

	if (Deferred)
	{
		Graphics.ClearRenderTarget(VisibilityBufferRenderTarget.RenderTargetView, { .R = 0, .G = 0 });

		Graphics.SetRenderTarget(VisibilityBufferRenderTarget.RenderTargetView, DepthTexture.View);
		UpdateScene(VisibilityBufferPipeline, VisibilityBufferPipeline);

		Graphics.TextureBarrier
		(
			{ BarrierStage::DepthStencil, BarrierStage::ComputeShading },
			{ BarrierAccess::DepthStencilWrite, BarrierAccess::ShaderResource },
			{ BarrierLayout::DepthStencilWrite, BarrierLayout::GraphicsQueueShaderResource },
			DepthTexture.Resource
		);
		Graphics.TextureBarrier
		(
			{ BarrierStage::RenderTarget, BarrierStage::ComputeShading },
			{ BarrierAccess::RenderTarget, BarrierAccess::ShaderResource },
			{ BarrierLayout::RenderTarget, BarrierLayout::GraphicsQueueShaderResource },
			VisibilityBufferRenderTarget.Resource
		);

		const HLSL::DeferredRootConstants rootConstants =
		{
			.HDRTextureIndex = Device.Get(HDRRenderTarget.UnorderedAccessView),
			.AnisotropicWrapSamplerIndex = Device.Get(AnisotropicWrapSampler),
			.VisibilityBufferTextureIndex = Device.Get(VisibilityBufferRenderTarget.ShaderResourceView),
			.ViewMode = ViewMode,
		};

		Graphics.SetPipeline(DeferredPipeline);
		Graphics.SetRootConstants(&rootConstants);
		Graphics.SetConstantBuffer("Scene"_view, SceneBuffers[Device.GetFrameIndex()].Resource);
		Graphics.Dispatch((HDRRenderTarget.Resource.Dimensions.Width + 15) / 16, (HDRRenderTarget.Resource.Dimensions.Height + 15) / 16, 1);

		Graphics.TextureBarrier
		(
			{ BarrierStage::PixelShading, BarrierStage::None },
			{ BarrierAccess::ShaderResource, BarrierAccess::NoAccess },
			{ BarrierLayout::GraphicsQueueShaderResource, BarrierLayout::DepthStencilWrite },
			DepthTexture.Resource
		);
		Graphics.TextureBarrier
		(
			{ BarrierStage::PixelShading, BarrierStage::None },
			{ BarrierAccess::ShaderResource, BarrierAccess::NoAccess },
			{ BarrierLayout::GraphicsQueueShaderResource, BarrierLayout::RenderTarget },
			VisibilityBufferRenderTarget.Resource
		);
	}
	else
	{
		Graphics.TextureBarrier
		(
			{ BarrierStage::None, BarrierStage::RenderTarget },
			{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
			{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
			HDRRenderTarget.Resource
		);

		Graphics.ClearRenderTarget(HDRRenderTarget.RenderTargetView, { .R = 0.0f, .G = 0.0f, .B = 0.0f, .A = 0.0f });

		Graphics.SetRenderTarget(DepthTexture.View);
		UpdateScene(DepthPrePassPipeline, GraphicsPipeline::Invalid());

		Graphics.SetRenderTarget(HDRRenderTarget.RenderTargetView, DepthTexture.View);
		UpdateScene(ForwardOpaquePipeline, ForwardBlendPipeline);

		Graphics.TextureBarrier
		(
			{ BarrierStage::RenderTarget, BarrierStage::ComputeShading },
			{ BarrierAccess::RenderTarget, BarrierAccess::ShaderResource },
			{ BarrierLayout::RenderTarget, BarrierLayout::GraphicsQueueShaderResource },
			HDRRenderTarget.Resource
		);
	}

	Graphics.BufferBarrier
	(
		{ BarrierStage::None, BarrierStage::ComputeShading },
		{ BarrierAccess::NoAccess, BarrierAccess::UnorderedAccess },
		SceneLuminanceBuffer.Resource
	);

	const HLSL::LuminanceHistogramRootConstants luminanceHistogramRootConstants =
	{
		.HDRTextureIndex = Device.Get(HDRRenderTarget.ShaderResourceView),
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
	};

	Graphics.SetPipeline(LuminanceHistogramPipeline);
	Graphics.SetRootConstants(&luminanceHistogramRootConstants);
	Graphics.Dispatch((HDRRenderTarget.Resource.Dimensions.Width + 15) / 16, (HDRRenderTarget.Resource.Dimensions.Height + 15) / 16, 1);

	Graphics.BufferBarrier
	(
		{ BarrierStage::ComputeShading, BarrierStage::ComputeShading },
		{ BarrierAccess::UnorderedAccess, BarrierAccess::UnorderedAccess },
		SceneLuminanceBuffer.Resource
	);

	const HLSL::LuminanceAverageRootConstants luminanceAverageRootConstants =
	{
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
		.PixelCount = HDRRenderTarget.Resource.Dimensions.Width * HDRRenderTarget.Resource.Dimensions.Height,
	};

	Graphics.SetPipeline(LuminanceAveragePipeline);
	Graphics.SetRootConstants(&luminanceAverageRootConstants);
	Graphics.Dispatch(HLSL::LuminanceHistogramBinsCount, 1, 1);

	const BasicTexture& swapChainTexture = SwapChainTextures[Device.GetFrameIndex()];

	Graphics.BufferBarrier
	(
		{ BarrierStage::ComputeShading, BarrierStage::PixelShading },
		{ BarrierAccess::UnorderedAccess, BarrierAccess::UnorderedAccess },
		SceneLuminanceBuffer.Resource
	);

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		swapChainTexture.Resource
	);

	Graphics.SetRenderTarget(swapChainTexture.View);
	Graphics.SetViewport(swapChainTexture.Resource.Dimensions.Width, swapChainTexture.Resource.Dimensions.Height);

	const HLSL::ToneMapRootConstants toneMapRootConstants =
	{
		.HDRTextureIndex = Device.Get(HDRRenderTarget.ShaderResourceView),
		.AnisotropicWrapSamplerIndex = Device.Get(AnisotropicWrapSampler),
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
		.DebugViewMode = ViewMode != HLSL::ViewMode::Lit,
	};

	Graphics.SetPipeline(ToneMapPipeline);
	Graphics.SetRootConstants(&toneMapRootConstants);
	Graphics.Draw(3);

#if !RELEASE
	UpdateFrameTimes(startCpuTime);
#endif

	DrawText::Get().Submit(&Graphics, &Device, swapChainTexture.Resource.Dimensions.Width, swapChainTexture.Resource.Dimensions.Height);

	Graphics.TextureBarrier
	(
		{ BarrierStage::RenderTarget, BarrierStage::None },
		{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
		{ BarrierLayout::RenderTarget, BarrierLayout::Present },
		swapChainTexture.Resource
	);

	Graphics.End();

	Device.Submit(Graphics);
	Device.Present();
}

void Renderer::UpdateScene(const GraphicsPipeline& opaquePipeline, const GraphicsPipeline& blendPipeline)
{
	usize drawCallIndex = 0;
	for (usize nodeIndex = 0; nodeIndex < SceneNodes.GetLength(); ++nodeIndex)
	{
		const Node& node = SceneNodes[nodeIndex];
		const Mesh& mesh = SceneMeshes[node.MeshIndex];

		const Matrix normalTransform = node.Transform.GetInverse().GetTranspose();

		for (const Primitive& primitive : mesh.Primitives)
		{
			const HLSL::SceneRootConstants rootConstants =
			{
				.AnisotropicWrapSamplerIndex = Device.Get(AnisotropicWrapSampler),
				.DrawCallIndex = static_cast<uint32>(drawCallIndex),
				.PrimitiveIndex = static_cast<uint32>(primitive.GlobalIndex),
				.NodeIndex = static_cast<uint32>(nodeIndex),
				.ViewMode = ViewMode,
				.NormalTransform = normalTransform,
			};

			const bool translucent = (primitive.MaterialIndex != INDEX_NONE) ? SceneMaterials[primitive.MaterialIndex].Translucent : false;
			const bool opaqueOnly = translucent && !blendPipeline.IsValid();
			if (opaqueOnly)
			{
				continue;
			}

			if (translucent)
			{
				Graphics.SetPipeline(blendPipeline);
			}
			else
			{
				Graphics.SetPipeline(opaquePipeline);
			}

			Graphics.SetRootConstants(&rootConstants);

			Graphics.SetConstantBuffer("Scene"_view, SceneBuffers[Device.GetFrameIndex()].Resource);

			Graphics.SetVertexBuffer(0,
									 SubBuffer
									 {
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.PositionSize,
										 .Stride = primitive.PositionStride,
										 .Offset = primitive.PositionOffset,
									 });
			Graphics.SetVertexBuffer(1,
									 SubBuffer
									 {
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.TextureCoordinateSize,
										 .Stride = primitive.TextureCoordinateStride,
										 .Offset = primitive.TextureCoordinateOffset,
									 });
			Graphics.SetVertexBuffer(2,
									 SubBuffer
									 {
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.NormalSize,
										 .Stride = primitive.NormalStride,
										 .Offset = primitive.NormalOffset,
									 });
			Graphics.SetIndexBuffer(SubBuffer
									{
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.IndexSize,
										 .Stride = primitive.IndexStride,
										 .Offset = primitive.IndexOffset,
									});

			Graphics.DrawIndexed(primitive.IndexSize / primitive.IndexStride);

			++drawCallIndex;
		}
	}
}

#if !RELEASE
void Renderer::UpdateFrameTimes(double startCpuTime)
{
	const double cpuTime = Platform::GetTime() - startCpuTime;
	const double gpuTime = Graphics.GetMostRecentGpuTime();

	AverageCpuTime = AverageCpuTime * 0.95 + cpuTime * 0.05;
	AverageGpuTime = AverageGpuTime * 0.95 + gpuTime * 0.05;

	char cpuTimeText[16] = {};
	Platform::StringPrint("CPU: %.2fms", cpuTimeText, sizeof(cpuTimeText), AverageCpuTime * 1000.0);
	DrawText::Get().Draw(StringView { cpuTimeText, Platform::StringLength(cpuTimeText) },
						 { .X = 0.0f, .Y = 0.0f },
						 Float3 { .R = 1.0f, .G = 1.0f, .B = 1.0f },
						 32.0f);

	char gpuTimeText[16] = {};
	Platform::StringPrint("GPU: %.2fms", gpuTimeText, sizeof(gpuTimeText), AverageGpuTime * 1000.0);
	DrawText::Get().Draw(StringView { gpuTimeText, Platform::StringLength(gpuTimeText) },
						 { .X = 0.0f, .Y = 32.0f },
						 Float3 { .R = 1.0f, .G = 1.0f, .B = 1.0f },
						 32.0f);
}
#endif

void Renderer::Resize(uint32 width, uint32 height)
{
	Device.WaitForIdle();

	DestroyScreenTextures();
	Device.ReleaseAllDeletes();

	Device.ResizeSwapChain(width, height);
	CreateScreenTextures(width, height);

	Device.WaitForIdle();
}

void Renderer::LoadScene(const GLTF::Scene& scene)
{
	UnloadScene();

	SceneTwoChannelNormalMaps = scene.TwoChannelNormalMaps;

	VERIFY(scene.Buffers.GetLength() == 1, "GLTF file contains multiple buffers!");
	const GLTF::Buffer& vertexBuffer = scene.Buffers[0];

	usize globalPrimitiveIndex = 0;
	for (const GLTF::Mesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetLength(), RendererAllocator);

		for (const GLTF::Primitive& primitive : mesh.Primitives)
		{
			const GLTF::AccessorView positionView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Position]);
			const GLTF::AccessorView textureCoordinateView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::TexCoord0]);
			const GLTF::AccessorView normalView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Normal]);
			const GLTF::AccessorView indexView = GLTF::GetAccessorView(scene, primitive.Indices);

			primitives.Add(Primitive
			{
				.GlobalIndex = globalPrimitiveIndex,
				.PositionOffset = positionView.Offset,
				.PositionStride = positionView.Stride,
				.PositionSize = positionView.Size,
				.TextureCoordinateOffset = textureCoordinateView.Offset,
				.TextureCoordinateStride = textureCoordinateView.Stride,
				.TextureCoordinateSize = textureCoordinateView.Size,
				.NormalOffset = normalView.Offset,
				.NormalStride = normalView.Stride,
				.NormalSize = normalView.Size,
				.IndexOffset = indexView.Offset,
				.IndexStride = indexView.Stride,
				.IndexSize = indexView.Size,
				.MaterialIndex = primitive.Material,
				.AccelerationStructureResource = {},
			});
			++globalPrimitiveIndex;
		}

		SceneMeshes.Add(Mesh
		{
			.Primitives = Move(primitives),
		});
	}

	GLTF::Buffer finalVertexBuffer = vertexBuffer;
	SceneVertexBuffer = CreateBasicBuffer(&Device,
										  finalVertexBuffer.Size,
										  0,
										  ResourceFlags::None,
										  ViewType::ShaderResource,
										  finalVertexBuffer.Data,
										  "Scene Vertex Buffer"_view);

	Array<HLSL::Primitive> primitiveData(RendererAllocator);
	for (const Mesh& mesh : SceneMeshes)
	{
		for (const Primitive& primitive : mesh.Primitives)
		{
			primitiveData.Add(HLSL::Primitive
			{
				.PositionOffset = static_cast<uint32>(primitive.PositionOffset),
				.PositionStride = static_cast<uint32>(primitive.PositionStride),
				.TextureCoordinateOffset = static_cast<uint32>(primitive.TextureCoordinateOffset),
				.TextureCoordinateStride = static_cast<uint32>(primitive.TextureCoordinateStride),
				.NormalOffset = static_cast<uint32>(primitive.NormalOffset),
				.NormalStride = static_cast<uint32>(primitive.NormalStride),
				.IndexOffset = static_cast<uint32>(primitive.IndexOffset),
				.IndexStride = static_cast<uint32>(primitive.IndexStride),
				.MaterialIndex = static_cast<uint32>(primitive.MaterialIndex),
			});
		}
	}
	ScenePrimitiveBuffer = CreateBasicBuffer(&Device,
											 primitiveData.GetDataSize(),
											 primitiveData.GetElementSize(),
											 ResourceFlags::None,
											 ViewType::ShaderResource,
											 primitiveData.GetData(),
											 "Scene Primitive Buffer"_view);

	Array<Resource> transientResources(RendererAllocator);

	Graphics.Begin();

	for (Mesh& mesh : SceneMeshes)
	{
		for (Primitive& primitive : mesh.Primitives)
		{
			const AccelerationStructureGeometry geometry = AccelerationStructureGeometry
			{
				.VertexBuffer = SubBuffer
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.PositionSize,
					.Stride = primitive.PositionStride,
					.Offset = primitive.PositionOffset,
				},
				.IndexBuffer = SubBuffer
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.IndexSize,
					.Stride = primitive.IndexStride,
					.Offset = primitive.IndexOffset,
				},
				.Translucent = scene.Materials[primitive.MaterialIndex].AlphaMode != GLTF::AlphaMode::Opaque,
			};
			const AccelerationStructureSize size = Device.GetAccelerationStructureSize(geometry);

			Resource scratchResource = Device.Create(
			{
				.Type = ResourceType::Buffer,
				.Format = ResourceFormat::None,
				.Flags = ResourceFlags::UnorderedAccess,
				.InitialLayout = BarrierLayout::Undefined,
				.Size = size.ScratchSize,
				.Name = "Scratch Primitive Acceleration Structure"_view,
			});
			transientResources.Add(scratchResource);
			const Resource resultResource = Device.Create(
			{
				.Type = ResourceType::Buffer,
				.Format = ResourceFormat::None,
				.Flags = ResourceFlags::AccelerationStructure,
				.InitialLayout = BarrierLayout::Undefined,
				.Size = size.ResultSize,
				.Name = "Primitive Acceleration Structure"_view,
			});
			Graphics.BuildAccelerationStructure(geometry, scratchResource, resultResource);

			primitive.AccelerationStructureResource = resultResource;
		}
	}

	Graphics.GlobalBarrier
	(
		{ BarrierStage::BuildAccelerationStructure, BarrierStage::BuildAccelerationStructure },
		{ BarrierAccess::AccelerationStructureWrite, BarrierAccess::AccelerationStructureRead }
	);

	Array<AccelerationStructureInstance> instances(RendererAllocator);
	Array<HLSL::DrawCall> drawCallData(RendererAllocator);
	for (usize nodeIndex = 0; nodeIndex < scene.Nodes.GetLength(); ++nodeIndex)
	{
		const GLTF::Node& node = scene.Nodes[nodeIndex];
		if (node.Mesh == INDEX_NONE)
		{
			continue;
		}

		const Matrix transform = GLTF::CalculateGlobalTransform(scene, nodeIndex);

		const Mesh& mesh = SceneMeshes[node.Mesh];
		for (const Primitive& primitive : mesh.Primitives)
		{
			instances.Add(AccelerationStructureInstance
			{
				.ID = static_cast<uint32>(primitive.GlobalIndex),
				.Transform = transform,
				.AccelerationStructureResource = primitive.AccelerationStructureResource,
			});

			drawCallData.Add(HLSL::DrawCall
			{
				.NodeIndex = static_cast<uint32>(SceneNodes.GetLength()),
				.PrimitiveIndex = static_cast<uint32>(primitive.GlobalIndex),
			});
		}

		SceneNodes.Add(Node
		{
			.Transform = transform,
			.MeshIndex = node.Mesh,
		});
	}
	SceneDrawCallBuffer = CreateBasicBuffer(&Device,
											drawCallData.GetDataSize(),
											drawCallData.GetElementSize(),
											ResourceFlags::None,
											ViewType::ShaderResource,
											drawCallData.GetData(),
											"Scene Draw Call Buffer"_view);

	const Resource instancesResource = Device.Create(
	{
		.Type = ResourceType::AccelerationStructureInstances,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::Upload,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = instances.GetLength() * Device.GetAccelerationStructureInstanceSize(),
		.Name = "Scene Acceleration Structure Instances"_view,
	});
	transientResources.Add(instancesResource);
	Device.Write(&instancesResource, instances.GetData());

	const Buffer instancesBuffer = Buffer
	{
		.Resource = instancesResource,
		.Size = instancesResource.Size,
		.Stride = Device.GetAccelerationStructureInstanceSize(),
	};
	const AccelerationStructureSize size = Device.GetAccelerationStructureSize(instancesBuffer);

	const Resource scratchResource = Device.Create(
	{
		.Type = ResourceType::Buffer,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::UnorderedAccess,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size.ScratchSize,
		.Name = "Scratch Scene Acceleration Structure"_view,
	});
	transientResources.Add(scratchResource);
	SceneAccelerationStructureResource = Device.Create(
	{
		.Type = ResourceType::Buffer,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::AccelerationStructure,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size.ResultSize,
		.Name = "Scene Acceleration Structure"_view,
	});
	Graphics.BuildAccelerationStructure(instancesBuffer, scratchResource, SceneAccelerationStructureResource);

	SceneAccelerationStructure = Device.Create(AccelerationStructureDescription
	{
		.AccelerationStructureResource = SceneAccelerationStructureResource,
	});

	Graphics.End();
	Device.Submit(Graphics);
	Device.WaitForIdle();

	for (Resource& resource : transientResources)
	{
		Device.Destroy(&resource);
	}

	Array<HLSL::Node> nodeData(SceneNodes.GetLength(), RendererAllocator);
	for (const Node& node : SceneNodes)
	{
		nodeData.Add(HLSL::Node
		{
			.Transform = node.Transform,
			.NormalTransform = node.Transform.GetInverse().GetTranspose(),
		});
	}
	SceneNodeBuffer = CreateBasicBuffer(&Device,
										nodeData.GetDataSize(),
										nodeData.GetElementSize(),
										ResourceFlags::None,
										ViewType::ShaderResource,
										nodeData.GetData(),
										"Scene Node Buffer"_view);

	for (const GLTF::Material& gltfMaterial : scene.Materials)
	{
		const auto convertTexture = [this](const GLTF::Scene& scene, usize textureIndex, StringView textureName) -> BasicTexture
		{
			if (textureIndex == INDEX_NONE)
			{
				return BasicTexture { Resource::Invalid(), TextureView::Invalid() };
			}

			const GLTF::Texture& gltfTexture = scene.Textures[textureIndex];
			const GLTF::Image& gltfImage = scene.Images[gltfTexture.Image];

			DDS::Image image = DDS::LoadImage(gltfImage.Path.AsView());
			const BasicTexture texture = CreateBasicTexture(&Device,
															{ image.Width, image.Height },
															image.MipMapCount,
															image.Format,
															image.Data,
															textureName);
			DDS::UnloadImage(&image);

			return texture;
		};

		Material material =
		{
			.NormalMapTexture = convertTexture(scene, gltfMaterial.NormalMapTexture, "Scene Normal Map Texture"_view),
			.IsSpecularGlossiness = gltfMaterial.IsSpecularGlossiness,
			.Translucent = gltfMaterial.AlphaMode != GLTF::AlphaMode::Opaque,
			.AlphaCutoff = gltfMaterial.AlphaCutoff,
		};
		if (gltfMaterial.IsSpecularGlossiness)
		{
			material.SpecularGlossiness.DiffuseTexture = convertTexture(scene,
																		gltfMaterial.SpecularGlossiness.DiffuseTexture,
																		"Scene Diffuse Texture"_view);
			material.SpecularGlossiness.DiffuseFactor = gltfMaterial.SpecularGlossiness.DiffuseFactor;

			material.SpecularGlossiness.SpecularGlossinessTexture = convertTexture(scene,
																				   gltfMaterial.SpecularGlossiness.SpecularGlossinessTexture,
																				   "Scene Specular Glossiness Texture"_view);
			material.SpecularGlossiness.SpecularFactor = gltfMaterial.SpecularGlossiness.SpecularFactor;
			material.SpecularGlossiness.GlossinessFactor = gltfMaterial.SpecularGlossiness.GlossinessFactor;
		}
		else
		{
			material.MetallicRoughness.BaseColorTexture = convertTexture(scene,
																		 gltfMaterial.MetallicRoughness.BaseColorTexture,
																		 "Scene Base Color Texture"_view);
			material.MetallicRoughness.BaseColorFactor = gltfMaterial.MetallicRoughness.BaseColorFactor;

			material.MetallicRoughness.MetallicRoughnessTexture = convertTexture(scene,
																				 gltfMaterial.MetallicRoughness.MetallicRoughnessTexture,
																				 "Scene Metallic Roughness Texture"_view);
			material.MetallicRoughness.MetallicFactor = gltfMaterial.MetallicRoughness.MetallicFactor;
			material.MetallicRoughness.RoughnessFactor = gltfMaterial.MetallicRoughness.RoughnessFactor;
		}
		SceneMaterials.Add(material);
	}

	Array<HLSL::Material> materialData(SceneMaterials.GetLength(), RendererAllocator);
	for (const Material& material : SceneMaterials)
	{
		BasicTexture baseColorOrDiffuseTexture = WhiteTexture;
		if (material.IsSpecularGlossiness && material.SpecularGlossiness.DiffuseTexture.Resource.IsValid())
		{
			baseColorOrDiffuseTexture = material.SpecularGlossiness.DiffuseTexture;
		}
		else if (!material.IsSpecularGlossiness && material.MetallicRoughness.BaseColorTexture.Resource.IsValid())
		{
			baseColorOrDiffuseTexture = material.MetallicRoughness.BaseColorTexture;
		}

		BasicTexture metallicRoughnessOrSpecularGlossinessTexture = WhiteTexture;
		if (material.IsSpecularGlossiness && material.SpecularGlossiness.SpecularGlossinessTexture.Resource.IsValid())
		{
			metallicRoughnessOrSpecularGlossinessTexture = material.SpecularGlossiness.SpecularGlossinessTexture;
		}
		else if (!material.IsSpecularGlossiness && material.MetallicRoughness.MetallicRoughnessTexture.Resource.IsValid())
		{
			metallicRoughnessOrSpecularGlossinessTexture = material.MetallicRoughness.MetallicRoughnessTexture;
		}

		materialData.Add(HLSL::Material
		{
			.BaseColorOrDiffuseTextureIndex = Device.Get(baseColorOrDiffuseTexture.View),
			.NormalMapTextureIndex = Device.Get(material.NormalMapTexture.View.IsValid() ? material.NormalMapTexture.View : DefaultNormalMapTexture.View),
			.MetallicRoughnessOrSpecularGlossinessTextureIndex = Device.Get(metallicRoughnessOrSpecularGlossinessTexture.View),
			.BaseColorOrDiffuseFactor = material.IsSpecularGlossiness
									  ? material.SpecularGlossiness.DiffuseFactor
									  : material.MetallicRoughness.BaseColorFactor,
			.MetallicOrSpecularFactor = material.IsSpecularGlossiness
									  ? material.SpecularGlossiness.SpecularFactor
									  : Float3 { .R = material.MetallicRoughness.MetallicFactor, .G = 0.0f, .B = 0.0f },
			.RoughnessOrGlossinessFactor = material.IsSpecularGlossiness
										 ? material.SpecularGlossiness.GlossinessFactor
										 : material.MetallicRoughness.RoughnessFactor,
			.IsSpecularGlossiness = material.IsSpecularGlossiness,
			.AlphaCutoff = material.AlphaCutoff,
		});
	}
	SceneMaterialBuffer = CreateBasicBuffer(&Device,
											materialData.GetDataSize(),
											materialData.GetElementSize(),
											ResourceFlags::None,
											ViewType::ShaderResource,
											materialData.GetData(),
											"Scene Material Buffer"_view);

	bool hasDirectionalLight = false;
	HLSL::DirectionalLight directionalLight;
	Array<HLSL::PointLight> pointLights(RendererAllocator);

	for (const GLTF::Light& light : scene.Lights)
	{
		Vector translation;
		Quaternion orientation;
		DecomposeTransform(light.Transform, &translation, &orientation, nullptr);

		const Vector direction = -orientation.Rotate(GLTF::DefaultDirection);

		if (light.Type == GLTF::LightType::Directional)
		{
			CHECK(!hasDirectionalLight);
			hasDirectionalLight = true;

			directionalLight = HLSL::DirectionalLight
			{
				.Color = light.Color,
				.IntensityLux = light.Intensity,
				.Direction = { .X = direction.X, .Y = direction.Y, .Z = direction.Z },
			};
		}
		else if (light.Type == GLTF::LightType::Point)
		{
			pointLights.Add(HLSL::PointLight
			{
				.Color = light.Color,
				.IntensityCandela = light.Intensity,
				.Position = { .X = translation.X, .Y = translation.Y, .Z = translation.Z },
			});
		}
	}
	if (!hasDirectionalLight)
	{
		directionalLight = HLSL::DirectionalLight
		{
			.Color = { .R = 1.0f, .G = 1.0f, .B = 1.0f },
			.IntensityLux = 1.0f,
			.Direction = { .X = +0.0f, .Y = +1.0f, .Z = +0.0f },
		};
	}

	SceneDirectionalLightBuffer = CreateBasicBuffer(&Device,
													sizeof(directionalLight),
													0,
													ResourceFlags::None,
													ViewType::ConstantBuffer,
													&directionalLight,
													"Scene Directional Light Buffer"_view);
	if (!pointLights.IsEmpty())
	{
		ScenePointLightsBuffer = CreateBasicBuffer(&Device,
												   pointLights.GetDataSize(),
												   pointLights.GetElementSize(),
												   ResourceFlags::None,
												   ViewType::ShaderResource,
												   pointLights.GetData(),
												   "Scene Point Lights Buffer"_view);
	}

	for (BasicBuffer& sceneBuffer : SceneBuffers)
	{
		sceneBuffer = CreateBasicBuffer(&Device,
										sizeof(HLSL::Scene),
										0,
										ResourceFlags::Upload,
										ViewType::ConstantBuffer,
										nullptr,
										"Scene Buffer"_view);
	}
}

void Renderer::UnloadScene()
{
	for (BasicBuffer& sceneBuffer : SceneBuffers)
	{
		Device.Destroy(&sceneBuffer.Resource);
		Device.Destroy(&sceneBuffer.View);
	}
	Device.Destroy(&SceneVertexBuffer.Resource);
	Device.Destroy(&SceneVertexBuffer.View);
	Device.Destroy(&ScenePrimitiveBuffer.Resource);
	Device.Destroy(&ScenePrimitiveBuffer.View);
	Device.Destroy(&SceneNodeBuffer.Resource);
	Device.Destroy(&SceneNodeBuffer.View);
	Device.Destroy(&SceneDrawCallBuffer.Resource);
	Device.Destroy(&SceneDrawCallBuffer.View);
	Device.Destroy(&SceneMaterialBuffer.Resource);
	Device.Destroy(&SceneMaterialBuffer.View);
	Device.Destroy(&SceneDirectionalLightBuffer.Resource);
	Device.Destroy(&SceneDirectionalLightBuffer.View);
	Device.Destroy(&ScenePointLightsBuffer.Resource);
	Device.Destroy(&ScenePointLightsBuffer.View);

	for (Mesh& mesh : SceneMeshes)
	{
		for (Primitive& primitive : mesh.Primitives)
		{
			Device.Destroy(&primitive.AccelerationStructureResource);
		}
	}
	Device.Destroy(&SceneAccelerationStructureResource);
	Device.Destroy(&SceneAccelerationStructure);

	for (Material& material : SceneMaterials)
	{
		Device.Destroy(&material.NormalMapTexture.Resource);
		Device.Destroy(&material.NormalMapTexture.View);

		if (material.IsSpecularGlossiness)
		{
			Device.Destroy(&material.SpecularGlossiness.DiffuseTexture.Resource);
			Device.Destroy(&material.SpecularGlossiness.DiffuseTexture.View);
			Device.Destroy(&material.SpecularGlossiness.SpecularGlossinessTexture.Resource);
			Device.Destroy(&material.SpecularGlossiness.SpecularGlossinessTexture.View);
		}
		else
		{
			Device.Destroy(&material.MetallicRoughness.BaseColorTexture.Resource);
			Device.Destroy(&material.MetallicRoughness.BaseColorTexture.View);
			Device.Destroy(&material.MetallicRoughness.MetallicRoughnessTexture.Resource);
			Device.Destroy(&material.MetallicRoughness.MetallicRoughnessTexture.View);
		}
	}

	SceneMeshes.Clear();
	SceneMaterials.Clear();
	SceneNodes.Clear();
}

void Renderer::CreatePipelines()
{
	const auto createGraphicsPipeline = [this](StringView name,
											   StringView path,
											   bool alphaBlend,
											   bool pixelShader,
											   bool depth,
											   auto... formats) -> GraphicsPipeline
	{
		ShaderStages stages;

		Shader vertex = Device.Create(
		{
			.FilePath = path,
			.Stage = ShaderStage::Vertex,
		});
		stages.AddStage(vertex);

		Shader pixel;
		if (pixelShader)
		{
			pixel = Device.Create(
			{
				.FilePath = path,
				.Stage = ShaderStage::Pixel,
			});
			stages.AddStage(pixel);
		}

		const GraphicsPipeline pipeline = Device.Create(
		{
			.Stages = Move(stages),
			.RenderTargetFormats = { formats... },
			.DepthStencilFormat = depth ? ResourceFormat::Depth32 : ResourceFormat::None,
			.AlphaBlend = alphaBlend,
			.Name = name,
		});
		Device.Destroy(&vertex);
		if (pixel.IsValid())
		{
			Device.Destroy(&pixel);
		}

		return pipeline;
	};
	const auto createComputePipeline = [this](StringView name, StringView path) -> ComputePipeline
	{
		Shader compute = Device.Create(
		{
			.FilePath = path,
			.Stage = ShaderStage::Compute,
		});

		const ComputePipeline pipeline = Device.Create(
		{
			.Stage = Move(compute),
			.Name = name,
		});
		Device.Destroy(&compute);

		return pipeline;
	};

	DepthPrePassPipeline = createGraphicsPipeline("Forward Depth Pre-Pass Pipeline"_view,
												  "Shaders/Forward.hlsl"_view,
												  false,
												  false,
												  true,
												  ResourceFormat::None);

	ForwardOpaquePipeline = createGraphicsPipeline("Forward Opaque Pipeline"_view,
												   "Shaders/Forward.hlsl"_view,
												   false,
												   true,
												   true,
												   HDRFormat);
	ForwardBlendPipeline = createGraphicsPipeline("Forward Blend Pipeline"_view,
												  "Shaders/Forward.hlsl"_view,
												  true,
												  true,
												  true,
												  HDRFormat);

	VisibilityBufferPipeline = createGraphicsPipeline("Visibility Buffer Pipeline"_view,
													  "Shaders/VisibilityBuffer.hlsl"_view,
													  false,
													  true,
													  true,
													  ResourceFormat::RG32UInt);
	DeferredPipeline = createComputePipeline("Deferred Pipeline"_view,
											 "Shaders/Deferred.hlsl"_view);

	ToneMapPipeline = createGraphicsPipeline("Tone Map Pipeline"_view,
											 "Shaders/ToneMap.hlsl"_view,
											 false,
											 true,
											 false,
											 ResourceFormat::RGBA8UNormSRGB);

	LuminanceHistogramPipeline = createComputePipeline("Luminance Histogram Pipeline"_view,
													   "Shaders/LuminanceHistogram.hlsl"_view);
	LuminanceAveragePipeline = createComputePipeline("Luminance Average Pipeline"_view,
													 "Shaders/LuminanceAverage.hlsl"_view);
}

void Renderer::DestroyPipelines()
{
	Device.Destroy(&DepthPrePassPipeline);

	Device.Destroy(&ForwardOpaquePipeline);
	Device.Destroy(&ForwardBlendPipeline);

	Device.Destroy(&VisibilityBufferPipeline);
	Device.Destroy(&DeferredPipeline);

	Device.Destroy(&ToneMapPipeline);

	Device.Destroy(&LuminanceHistogramPipeline);
	Device.Destroy(&LuminanceAveragePipeline);
}

void Renderer::CreateScreenTextures(uint32 width, uint32 height)
{
	const auto createRenderTarget = [&](ResourceFormat format, StringView textureName)
	{
		const Resource resource = Device.Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = format,
			.Flags = ResourceFlags::RenderTarget | ResourceFlags::UnorderedAccess,
			.InitialLayout = BarrierLayout::RenderTarget,
			.Dimensions = { width, height },
			.Name = textureName,
		});
		return RenderTarget
		{
			.Resource = resource,
			.RenderTargetView = Device.Create(
			{
				.Resource = resource,
				.Type = ViewType::RenderTarget,
				.Format = format,
			}),
			.ShaderResourceView = Device.Create(
			{
				.Resource = resource,
				.Type = ViewType::ShaderResource,
				.Format = format,
			}),
			.UnorderedAccessView = Device.Create(
			{
				.Resource = resource,
				.Type = ViewType::UnorderedAccess,
				.Format = format,
			}),
		};
	};

	for (usize i = 0; i < FramesInFlight; ++i)
	{
		SwapChainTextures[i].Resource = Device.Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = ResourceFormat::RGBA8UNormSRGB,
			.Flags = ResourceFlags::SwapChain | ResourceFlags::RenderTarget,
			.InitialLayout = BarrierLayout::Undefined,
			.Dimensions = { width, height },
			.SwapChainIndex = static_cast<uint8>(i),
			.Name = "SwapChain Texture"_view,
		});
		SwapChainTextures[i].View = Device.Create(
		{
			.Resource = SwapChainTextures[i].Resource,
			.Type = ViewType::RenderTarget,
			.Format = SwapChainTextures[i].Resource.Format,
		});
	}

	DepthTexture.Resource = Device.Create(
	{
		.Type = ResourceType::Texture2D,
		.Format = ResourceFormat::Depth32,
		.Flags = ResourceFlags::DepthStencil,
		.InitialLayout = BarrierLayout::DepthStencilWrite,
		.Dimensions = { width, height },
		.Name = "Depth Texture"_view,
	});
	DepthTexture.View = Device.Create(
	{
		.Resource = DepthTexture.Resource,
		.Type = ViewType::DepthStencil,
		.Format = DepthTexture.Resource.Format,
	});

	HDRRenderTarget = createRenderTarget(ResourceFormat::RGBA32Float, "HDR Texture"_view);
	VisibilityBufferRenderTarget = createRenderTarget(ResourceFormat::RG32UInt, "Visibility Buffer Texture"_view);
}

void Renderer::DestroyScreenTextures()
{
	const auto destroyRenderTarget = [this](RenderTarget* renderTarget)
	{
		Device.Destroy(&renderTarget->Resource);
		Device.Destroy(&renderTarget->RenderTargetView);
		Device.Destroy(&renderTarget->ShaderResourceView);
		Device.Destroy(&renderTarget->UnorderedAccessView);
	};

	for (BasicTexture& swapChainTexture : SwapChainTextures)
	{
		Device.Destroy(&swapChainTexture.Resource);
		Device.Destroy(&swapChainTexture.View);
	}
	Device.Destroy(&DepthTexture.View);
	Device.Destroy(&DepthTexture.Resource);

	destroyRenderTarget(&HDRRenderTarget);
	destroyRenderTarget(&VisibilityBufferRenderTarget);
}
