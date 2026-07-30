#include "Renderer.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "GLTF.hpp"
#include "RenderContext.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "UI.hpp"

namespace HLSL
{
#include "Shaders/Luminance.hlsli"
}

using namespace RHI;

static ::Allocator* RendererAllocator = &GlobalAllocator::Get();

static ReadTexture CreateReadTexture(ResourceUploader::Lifetime lifetime,
									 ResourceDimensions dimensions,
									 uint16 mipMapCount,
									 ResourceFormat format,
									 const void* data,
									 StringView debugName)
{
	const Resource texture = ResourceUploader::Upload(lifetime, data,
	{
		.Type = ResourceType::Texture2D,
		.Format = format,
		.Flags = ResourceFlags::None,
		.InitialLayout = BarrierLayout::GraphicsQueueCommon,
		.Dimensions = dimensions,
		.MipMapCount = mipMapCount,
		.DebugName = debugName,
	});
	const TextureView view = GlobalDevice().Create(
	{
		.Type = ViewType::ShaderResource,
		.Resource = texture,
		.ViewHeap = GlobalResourceViewHeap(),
	});

	return ReadTexture { texture, view };
}

static ReadBuffer CreateReadBuffer(ResourceUploader::Lifetime lifetime,
								   usize size,
								   usize stride,
								   ResourceFlags flags,
								   ViewType type,
								   const void* data,
								   StringView debugName)
{
	const Resource buffer = ResourceUploader::Upload(lifetime, data,
	{
		.Type = ResourceType::Buffer,
		.Flags = flags,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size,
		.DebugName = debugName,
	});
	const BufferView view = GlobalDevice().Create(
	{
		.Type = type,
		.Buffer = Buffer
		{
			.Resource = buffer,
			.Size = size,
			.Stride = stride,
		},
		.ViewHeap = GlobalResourceViewHeap(),
	});

	return ReadBuffer { buffer, view };
}

static void DestroyReadTexture(ReadTexture* texture)
{
	GlobalDevice().Destroy(&texture->Resource);
	GlobalDevice().Destroy(&texture->View);
}

static void DestroyReadBuffer(ReadBuffer* buffer)
{
	GlobalDevice().Destroy(&buffer->Resource);
	GlobalDevice().Destroy(&buffer->View);
}

Renderer::Renderer(Platform::Window* window, bool validation)
	: FrameCount(0)
	, ViewMode(HLSL::ViewMode::Lit)
	, TemporalAntiAliasing({ .Enabled = true, .DiscardPreviousFrame = true, .PreviousWorldToClip = Matrix::Identity })
	, PathTrace(false)
	, AverageTimeCPU(0.0)
	, AverageTimeGPU(0.0)
	, SceneMeshes(RendererAllocator)
	, SceneNodes(RendererAllocator)
	, SceneMaterials(RendererAllocator)
	, SceneTwoChannelNormalMaps(false)
{
	CreateRenderContext(window, validation);

	ResourceUploader::Init();
	UI::Init();

	static constexpr uint8 white[] = { 0xff, 0xff, 0xff, 0xff };
	WhiteTexture = CreateReadTexture(ResourceUploader::Lifetime::Persistent,
									 { 1, 1 },
									 1,
									 ResourceFormat::RGBA8UNorm,
									 white,
									 "White Texture"_view);

	static constexpr uint8 defaultNormal[] = { 0x7f, 0x7f, 0xff, 0x00 };
	DefaultNormalMapTexture = CreateReadTexture(ResourceUploader::Lifetime::Persistent,
												{ 1, 1 },
												1,
												ResourceFormat::RGBA8UNorm,
												defaultNormal,
												"Default Normal Map Texture"_view);

	AnisotropicWrapSampler = GlobalDevice().Create(
	{
		.MinificationFilter = SamplerFilter::Anisotropic,
		.MagnificationFilter = SamplerFilter::Anisotropic,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
		.ViewHeap = GlobalSamplerViewHeap(),
		.ReservedIndex = HLSL::AnisotropicWrapSamplerIndex,
	});

	for (Resource& sceneBufferResource : SceneBufferResources)
	{
		sceneBufferResource = GlobalDevice().Create(
		{
			.Type = ResourceType::Buffer,
			.Flags = ResourceFlags::Upload,
			.InitialLayout = BarrierLayout::Undefined,
			.Size = sizeof(HLSL::Scene),
			.DebugName = "Scene Buffer"_view,
		});
	}

	SceneLuminanceBufferResource = GlobalDevice().Create(
	{
		.Type = ResourceType::Buffer,
		.Flags = ResourceFlags::UnorderedAccess,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = HLSL::LuminanceHistogramBinsCount * sizeof(uint32) + sizeof(float32),
		.DebugName = "Scene Luminance Buffer"_view,
	});
	SceneLuminanceBufferView = GlobalDevice().Create(
	{
		.Type = ViewType::UnorderedAccess,
		.Buffer = Buffer
		{
			.Resource = SceneLuminanceBufferResource,
			.Size = SceneLuminanceBufferResource.Size,
			.Stride = 0,
		},
		.ViewHeap = GlobalResourceViewHeap(),
	});

	CreatePipelines();
}

Renderer::~Renderer()
{
	GlobalDevice().WaitForIdle();

	UnloadScene();

	ResourceUploader::Shutdown();
	UI::Shutdown();

	DestroyReadTexture(&WhiteTexture);
	DestroyReadTexture(&DefaultNormalMapTexture);

	GlobalDevice().Destroy(&AnisotropicWrapSampler);

	for (Resource& sceneBufferResource : SceneBufferResources)
	{
		GlobalDevice().Destroy(&sceneBufferResource);
	}

	GlobalDevice().Destroy(&SceneLuminanceBufferResource);
	GlobalDevice().Destroy(&SceneLuminanceBufferView);

	DestroyPipelines();

	DestroySwapChainTextures();
	DestroyViewportTextures();

	DestroyRenderContext();
}

void Renderer::Update(const CameraController& cameraController, float32 timeDelta, [[maybe_unused]] float64 frameStartTimeCPU)
{
	GlobalGraphics().Begin();

	GlobalGraphics().SetViewHeaps(GlobalResourceViewHeap(), GlobalSamplerViewHeap());

	if (FinalTexture.Resource.IsValid())
	{
		UpdateViewport(cameraController);
	}

	const Resource& swapChainTextureResource = SwapChainTextureResources[GlobalDevice().GetFrameIndex()];
	const TextureView& swapChainTextureView = SwapChainTextureViews[GlobalDevice().GetFrameIndex()];

	const ResourceDimensions swapChainDimensions = swapChainTextureResource.Dimensions;

	GlobalGraphics().TextureBarrier({ BarrierStage::None, BarrierStage::RenderTarget },
									{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
									{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
									swapChainTextureResource);

	RenderGraph::AddGraphicsPass("UI"_view,
								 { FinalTexture.ShaderResourceView },
								 {},
								 [timeDelta, swapChainTextureView, swapChainDimensions]
	{
		GlobalGraphics().SetViewport({ swapChainDimensions.Width, swapChainDimensions.Height });
		GlobalGraphics().ClearRenderTarget(swapChainTextureView);
		GlobalGraphics().SetRenderTarget(swapChainTextureView);

		UI::Submit(swapChainDimensions.Width, swapChainDimensions.Height, timeDelta);
	});

	RenderGraph::Execute();

	GlobalGraphics().TextureBarrier({ BarrierStage::RenderTarget, BarrierStage::None },
									{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
									{ BarrierLayout::RenderTarget, BarrierLayout::Present },
									swapChainTextureResource);

	GlobalGraphics().End();

	ResourceUploader::Flush();

	UpdateFrameTimes(frameStartTimeCPU);

	GlobalDevice().Submit(GlobalGraphics());
	GlobalDevice().Present();

	++FrameCount;
}

void Renderer::UpdateViewport(const CameraController& cameraController)
{
	float32x2 currentJitterNDC = { 0.0f, 0.0f };
	if (ShouldAntiAlias())
	{
		static constexpr float32x2 halton23Sequence[] =
		{
			{ 0.500000f, 0.333333f },
			{ 0.250000f, 0.666667f },
			{ 0.750000f, 0.111111f },
			{ 0.125000f, 0.444444f },
			{ 0.625000f, 0.777778f },
			{ 0.375000f, 0.222222f },
			{ 0.875000f, 0.555556f },
			{ 0.062500f, 0.888889f },
			{ 0.562500f, 0.037037f },
			{ 0.312500f, 0.370370f },
			{ 0.812500f, 0.703704f },
			{ 0.187500f, 0.148148f },
			{ 0.687500f, 0.481481f },
			{ 0.437500f, 0.814815f },
			{ 0.937500f, 0.259259f },
			{ 0.031250f, 0.592593f },
		};

		const float32x2 currentHalton = halton23Sequence[FrameCount % ARRAY_COUNT(halton23Sequence)];

		currentJitterNDC = float32x2
		{
			(currentHalton.X - 0.5f) * (2.0f / static_cast<float32>(FinalTexture.Resource.Dimensions.Width)),
			(currentHalton.Y - 0.5f) * (2.0f / static_cast<float32>(FinalTexture.Resource.Dimensions.Height)),
		};
	}

	const Matrix worldToView = cameraController.GetViewToWorld().GetInverse();
	const Matrix viewToClip = Matrix::ReverseDepth() * Matrix::Perspective(cameraController.GetFieldOfViewYRadians(),
																		   cameraController.GetAspectRatio(),
																		   cameraController.GetNear(),
																		   cameraController.GetFar());
	const Matrix worldToClip = viewToClip * worldToView;

	const HLSL::Scene sceneData =
	{
		.VertexBufferIndex = GlobalDevice().Get(SceneVertexBuffer.View),
		.PrimitiveBufferIndex = GlobalDevice().Get(ScenePrimitiveBuffer.View),
		.NodeBufferIndex = GlobalDevice().Get(SceneNodeBuffer.View),
		.MaterialBufferIndex = GlobalDevice().Get(SceneMaterialBuffer.View),
		.DrawCallBufferIndex = GlobalDevice().Get(SceneDrawCallBuffer.View),
		.DirectionalLightBufferIndex = GlobalDevice().Get(SceneDirectionalLightBuffer.View),
		.PointLightsBufferIndex = ScenePointLightsBuffer.View.IsValid() ? GlobalDevice().Get(ScenePointLightsBuffer.View) : 0,
		.AccelerationStructureIndex = GlobalDevice().Get(SceneAccelerationStructure),
		.WorldToClip = worldToClip,
		.JitteredWorldToClip = Matrix::Translation(currentJitterNDC.X, currentJitterNDC.Y, 0.0f) * worldToClip,
		.ClipToWorld = worldToClip.GetInverse(),
		.ViewPositionWS = float32x3
		{
			cameraController.GetPosition().X,
			cameraController.GetPosition().Y,
			cameraController.GetPosition().Z,
		},
		.TwoChannelNormalMaps = SceneTwoChannelNormalMaps,
		.PointLightsCount = ScenePointLightsBuffer.View.IsValid() ? static_cast<uint32>(Count(ScenePointLightsBuffer.View.Buffer)) : 0,
	};
	GlobalDevice().Write(&SceneBufferResources[GlobalDevice().GetFrameIndex()], &sceneData);

	if (PathTrace)
	{
		UpdatePathTracing();
	}
	else
	{
		UpdateRasterization();
	}

	RenderGraph::AddComputePass("Luminance Histogram"_view,
								{ HDRTexture.ShaderResourceView },
								{ SceneLuminanceBufferView },
								[this]
	{
		const HLSL::LuminanceHistogramRootConstants rootConstants =
		{
			.LuminanceBufferIndex = GlobalDevice().Get(SceneLuminanceBufferView),
			.HDRTextureIndex = GlobalDevice().Get(HDRTexture.ShaderResourceView),
		};

		GlobalGraphics().SetPipeline(LuminanceHistogramPipeline);
		GlobalGraphics().SetRootConstants(&rootConstants);
		GlobalGraphics().Dispatch({ (FinalTexture.Resource.Dimensions.Width + 15) / 16, (FinalTexture.Resource.Dimensions.Height + 15) / 16, 1 });
	});

	RenderGraph::AddComputePass("Luminance Average"_view,
								{ SceneLuminanceBufferView },
								{ SceneLuminanceBufferView },
								[this]
	{
		const HLSL::LuminanceAverageRootConstants rootConstants =
		{
			.LuminanceBufferIndex = GlobalDevice().Get(SceneLuminanceBufferView),
			.PixelCount = FinalTexture.Resource.Dimensions.Width * FinalTexture.Resource.Dimensions.Height,
		};

		GlobalGraphics().SetPipeline(LuminanceAveragePipeline);
		GlobalGraphics().SetRootConstants(&rootConstants);
		GlobalGraphics().Dispatch({ HLSL::LuminanceHistogramBinsCount, 1, 1 });
	});

	const TextureView toneMapTextureView = ShouldAntiAlias() ? AccumulationTexture.ShaderResourceView : HDRTexture.ShaderResourceView;

	RenderGraph::AddGraphicsPass("Tone Map"_view,
								 { SceneLuminanceBufferView, toneMapTextureView },
								 { FinalTexture.RenderTargetView },
								 [this, toneMapTextureView]
	{
		GlobalGraphics().SetViewport({ FinalTexture.Resource.Dimensions.Width, FinalTexture.Resource.Dimensions.Height });
		GlobalGraphics().SetRenderTarget(FinalTexture.RenderTargetView);

		const HLSL::ToneMapRootConstants rootConstants =
		{
			.HDRTextureIndex = GlobalDevice().Get(toneMapTextureView),
			.LuminanceBufferIndex = GlobalDevice().Get(SceneLuminanceBufferView),
			.ViewMode = ViewMode != HLSL::ViewMode::Lit,
		};

		GlobalGraphics().SetPipeline(ToneMapPipeline);
		GlobalGraphics().SetRootConstants(&rootConstants);
		GlobalGraphics().Draw(3);
	});

	if (ShouldAntiAlias())
	{
		Swap(AccumulationTexture, PreviousAccumulationTexture);
		TemporalAntiAliasing.DiscardPreviousFrame = false;
		TemporalAntiAliasing.PreviousWorldToClip = worldToClip;
	}
}

void Renderer::UpdateRasterization()
{
	RenderGraph::AddGraphicsPass("Visibility"_view,
								 {},
								 { VisibilityTexture.RenderTargetView, DepthTextureView },
								 [this]
	{
		GlobalGraphics().SetViewport({ FinalTexture.Resource.Dimensions.Width, FinalTexture.Resource.Dimensions.Height });
		GlobalGraphics().ClearRenderTarget(VisibilityTexture.RenderTargetView);
		GlobalGraphics().ClearDepthStencil(DepthTextureView);
		GlobalGraphics().SetRenderTarget(VisibilityTexture.RenderTargetView, DepthTextureView);

		usize drawCallIndex = 0;
		for (usize nodeIndex = 0; nodeIndex < SceneNodes.GetCount(); ++nodeIndex)
		{
			const Node& node = SceneNodes[nodeIndex];
			const Mesh& mesh = SceneMeshes[node.MeshIndex];

			for (const Primitive& primitive : mesh.Primitives)
			{
				const HLSL::VisibilityRootConstants rootConstants =
				{
					.DrawCallIndex = static_cast<uint32>(drawCallIndex),
					.PrimitiveIndex = static_cast<uint32>(primitive.GlobalIndex),
					.NodeIndex = static_cast<uint32>(nodeIndex),
					.ViewMode = ViewMode,
				};

				++drawCallIndex;

				const Material& material = SceneMaterials[primitive.MaterialIndex];

				GlobalGraphics().SetPipeline(material.DoubleSided ? VisibilityDoubleSidedPipeline : VisibilityPipeline);
				GlobalGraphics().SetRootConstants(&rootConstants);
				GlobalGraphics().SetConstantBuffer("Scene"_view, SceneBufferResources[GlobalDevice().GetFrameIndex()]);

				GlobalGraphics().SetVertexBuffer(0,
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.PositionSize,
					.Stride = primitive.PositionStride,
					.Offset = primitive.PositionOffset,
				});
				GlobalGraphics().SetVertexBuffer(1,
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.TextureCoordinateSize,
					.Stride = primitive.TextureCoordinateStride,
					.Offset = primitive.TextureCoordinateOffset,
				});
				GlobalGraphics().SetVertexBuffer(2,
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.NormalSize,
					.Stride = primitive.NormalStride,
					.Offset = primitive.NormalOffset,
				});
				GlobalGraphics().SetIndexBuffer(
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = primitive.IndexSize,
					.Stride = primitive.IndexStride,
					.Offset = primitive.IndexOffset,
				});

				GlobalGraphics().DrawIndexed(primitive.IndexSize / primitive.IndexStride);
			}
		}
	});

	RenderGraph::AddComputePass("Deferred"_view,
								{ VisibilityTexture.ShaderResourceView },
								{ HDRTexture.UnorderedAccessView },
								[this]
	{
		const HLSL::DeferredRootConstants rootConstants =
		{
			.HDRTextureIndex = GlobalDevice().Get(HDRTexture.UnorderedAccessView),
			.VisibilityTextureIndex = GlobalDevice().Get(VisibilityTexture.ShaderResourceView),
			.ViewMode = ViewMode,
		};

		GlobalGraphics().SetPipeline(DeferredPipeline);
		GlobalGraphics().SetRootConstants(&rootConstants);
		GlobalGraphics().SetConstantBuffer("Scene"_view, SceneBufferResources[GlobalDevice().GetFrameIndex()]);
		GlobalGraphics().Dispatch({ (FinalTexture.Resource.Dimensions.Width + 7) / 8, (FinalTexture.Resource.Dimensions.Height + 7) / 8, 1 });
	});

	if (ShouldAntiAlias())
	{
		RenderGraph::AddComputePass("Temporal Anti-Alias"_view,
									{ HDRTexture.ShaderResourceView, PreviousAccumulationTexture.ShaderResourceView, VisibilityTexture.ShaderResourceView },
									{ AccumulationTexture.UnorderedAccessView },
									[
										this,
										accumulationTexture = AccumulationTexture,
										previousAccumulationTexture = PreviousAccumulationTexture,
										discardPreviousFrame = TemporalAntiAliasing.DiscardPreviousFrame,
										previousWorldToClip = TemporalAntiAliasing.PreviousWorldToClip
									]
		{
			const HLSL::TemporalAntiAliasRootConstants rootConstants =
			{
				.AccumulationTextureIndex = GlobalDevice().Get(accumulationTexture.UnorderedAccessView),
				.HDRTextureIndex = GlobalDevice().Get(HDRTexture.ShaderResourceView),
				.PreviousAccumulationTextureIndex = GlobalDevice().Get(previousAccumulationTexture.ShaderResourceView),
				.VisibilityTextureIndex = GlobalDevice().Get(VisibilityTexture.ShaderResourceView),
				.DiscardPreviousFrame = discardPreviousFrame,
				.PreviousWorldToClip = previousWorldToClip,
			};

			GlobalGraphics().SetPipeline(TemporalAntiAliasPipeline);
			GlobalGraphics().SetRootConstants(&rootConstants);
			GlobalGraphics().SetConstantBuffer("Scene"_view, SceneBufferResources[GlobalDevice().GetFrameIndex()]);
			GlobalGraphics().Dispatch({ (FinalTexture.Resource.Dimensions.Width + 7) / 8, (FinalTexture.Resource.Dimensions.Height + 7) / 8, 1 });
		});
	}
}

void Renderer::UpdatePathTracing()
{
	RenderGraph::AddComputePass("Path Trace"_view,
								{},
								{ HDRTexture.UnorderedAccessView },
								[this]
	{
		const HLSL::PathTraceRootConstants rootConstants =
		{
			.HDRTextureIndex = GlobalDevice().Get(HDRTexture.UnorderedAccessView),
		};

		GlobalGraphics().SetPipeline(PathTracePipeline);
		GlobalGraphics().SetRootConstants(&rootConstants);
		GlobalGraphics().SetConstantBuffer("Scene"_view, SceneBufferResources[GlobalDevice().GetFrameIndex()]);
		GlobalGraphics().Dispatch({ (FinalTexture.Resource.Dimensions.Width + 15) / 16, (FinalTexture.Resource.Dimensions.Height + 15) / 16, 1 });
	});
}

void Renderer::UpdateFrameTimes(float64 frameStartTimeCPU)
{
	const float64 timeCPU = Platform::GetTime() - frameStartTimeCPU;
	const float64 timeGPU = GlobalGraphics().GetMostRecentTimeGPU();

	AverageTimeCPU = AverageTimeCPU * 0.95 + timeCPU * 0.05;
	AverageTimeGPU = AverageTimeGPU * 0.95 + timeGPU * 0.05;
}

void Renderer::ResizeSwapChain(uint32x2 dimensions)
{
	GlobalDevice().WaitForIdle();

	DestroySwapChainTextures();

	GlobalDevice().ResizeSwapChain(dimensions);
	CreateSwapChainTextures(dimensions);

	GlobalDevice().WaitForIdle();
}

void Renderer::ResizeViewport(uint32x2 dimensions)
{
	GlobalDevice().WaitForIdle();

	DestroyViewportTextures();
	CreateViewportTextures(dimensions);
}

void Renderer::LoadScene(const GLTF::Scene& scene)
{
	UnloadScene();

	SceneTwoChannelNormalMaps = scene.TwoChannelNormalMaps;

	VERIFY(scene.Buffers.GetCount() == 1, "GLTF file contains multiple buffers!");
	const GLTF::Buffer& vertexBuffer = scene.Buffers[0];

	GLTF::Buffer finalVertexBuffer = vertexBuffer;
	SceneVertexBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
										 finalVertexBuffer.Size,
										 0,
										 ResourceFlags::None,
										 ViewType::ShaderResource,
										 finalVertexBuffer.Data,
										 "Scene Vertex Buffer"_view);

	ResourceUploader::Flush();

	usize globalPrimitiveIndex = 0;
	for (const GLTF::Mesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetCount(), RendererAllocator);

		for (const GLTF::Primitive& primitive : mesh.Primitives)
		{
			const GLTF::AccessorView positionView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Position]);
			const GLTF::AccessorView textureCoordinateView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::TexCoord0]);
			const GLTF::AccessorView normalView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Normal]);
			const GLTF::AccessorView indexView = GLTF::GetAccessorView(scene, primitive.Indices);

			primitives.Add(Primitive
			{
				.GlobalIndex = globalPrimitiveIndex,
				.MaterialIndex = primitive.Material,
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
				.AccelerationStructureResource = {},
			});
			++globalPrimitiveIndex;
		}

		SceneMeshes.Add(Mesh
		{
			.Primitives = Move(primitives),
		});
	}

	Array<HLSL::Primitive> primitiveData(RendererAllocator);
	for (const Mesh& mesh : SceneMeshes)
	{
		for (const Primitive& primitive : mesh.Primitives)
		{
			primitiveData.Add(HLSL::Primitive
			{
				.MaterialIndex = static_cast<uint32>(primitive.MaterialIndex),
				.PositionOffset = static_cast<uint32>(primitive.PositionOffset),
				.PositionStride = static_cast<uint32>(primitive.PositionStride),
				.TextureCoordinateOffset = static_cast<uint32>(primitive.TextureCoordinateOffset),
				.TextureCoordinateStride = static_cast<uint32>(primitive.TextureCoordinateStride),
				.NormalOffset = static_cast<uint32>(primitive.NormalOffset),
				.NormalStride = static_cast<uint32>(primitive.NormalStride),
				.IndexOffset = static_cast<uint32>(primitive.IndexOffset),
				.IndexStride = static_cast<uint32>(primitive.IndexStride),
			});
		}
	}
	ScenePrimitiveBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
											primitiveData.GetDataSize(),
											primitiveData.GetElementSize(),
											ResourceFlags::None,
											ViewType::ShaderResource,
											primitiveData.GetData(),
											"Scene Primitive Buffer"_view);

	Array<Resource> transientResources(RendererAllocator);

	GlobalGraphics().Begin();

	for (Mesh& mesh : SceneMeshes)
	{
		for (Primitive& primitive : mesh.Primitives)
		{
			const GLTF::AlphaMode alphaMode = scene.Materials[primitive.MaterialIndex].AlphaMode;

			const RayTracingAccelerationStructureTriangleGeometry geometry =
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
				.Flags = alphaMode == GLTF::AlphaMode::Opaque ? RayTracingAccelerationStructureGeometryFlags::None
															  : RayTracingAccelerationStructureGeometryFlags::Translucent,
			};
			const RayTracingAccelerationStructureSize size = GlobalDevice().GetRayTracingAccelerationStructureSize(geometry);

			Resource scratchResource = GlobalDevice().Create(
			{
				.Type = ResourceType::Buffer,
				.Format = ResourceFormat::None,
				.Flags = ResourceFlags::UnorderedAccess,
				.InitialLayout = BarrierLayout::Undefined,
				.Size = size.ScratchSize,
				.DebugName = "Scratch Primitive Acceleration Structure"_view,
			});
			transientResources.Add(scratchResource);
			const Resource resultResource = GlobalDevice().Create(
			{
				.Type = ResourceType::Buffer,
				.Format = ResourceFormat::None,
				.Flags = ResourceFlags::RayTracingAccelerationStructure | ResourceFlags::UnorderedAccess,
				.InitialLayout = BarrierLayout::Undefined,
				.Size = size.ResultSize,
				.DebugName = "Primitive Acceleration Structure"_view,
			});
			GlobalGraphics().BuildRayTracingAccelerationStructure(geometry, scratchResource, resultResource);

			primitive.AccelerationStructureResource = resultResource;
		}
	}

	GlobalGraphics().GlobalBarrier({ BarrierStage::BuildRayTracingAccelerationStructure, BarrierStage::BuildRayTracingAccelerationStructure },
								   { BarrierAccess::RayTracingAccelerationStructureWrite, BarrierAccess::RayTracingAccelerationStructureRead });

	Array<RayTracingAccelerationStructureInstance> instances(RendererAllocator);
	Array<HLSL::DrawCall> drawCallData(RendererAllocator);
	for (usize nodeIndex = 0; nodeIndex < scene.Nodes.GetCount(); ++nodeIndex)
	{
		const GLTF::Node& node = scene.Nodes[nodeIndex];
		if (node.Mesh == INDEX_NONE)
		{
			continue;
		}

		const Matrix localToWorld = GLTF::CalculateLocalToWorld(scene, nodeIndex);

		const Mesh& mesh = SceneMeshes[node.Mesh];
		for (const Primitive& primitive : mesh.Primitives)
		{
			instances.Add(RayTracingAccelerationStructureInstance
			{
				.ID = static_cast<uint32>(primitive.GlobalIndex),
				.LocalToWorld = localToWorld,
				.Resource = primitive.AccelerationStructureResource,
			});

			drawCallData.Add(HLSL::DrawCall
			{
				.NodeIndex = static_cast<uint32>(SceneNodes.GetCount()),
				.PrimitiveIndex = static_cast<uint32>(primitive.GlobalIndex),
			});
		}

		SceneNodes.Add(Node
		{
			.LocalToWorld = localToWorld,
			.MeshIndex = node.Mesh,
		});
	}
	SceneDrawCallBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
										   drawCallData.GetDataSize(),
										   drawCallData.GetElementSize(),
										   ResourceFlags::None,
										   ViewType::ShaderResource,
										   drawCallData.GetData(),
										   "Scene Draw Call Buffer"_view);

	const Resource instancesResource = GlobalDevice().Create(
	{
		.Type = ResourceType::RayTracingAccelerationStructureInstances,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::Upload,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = instances.GetCount() * GlobalDevice().GetRayTracingAccelerationStructureInstanceSize(),
		.DebugName = "Scene Acceleration Structure Instances"_view,
	});
	transientResources.Add(instancesResource);
	GlobalDevice().Write(&instancesResource, instances.GetData());

	const Buffer instancesBuffer =
	{
		.Resource = instancesResource,
		.Size = instancesResource.Size,
		.Stride = GlobalDevice().GetRayTracingAccelerationStructureInstanceSize(),
	};
	const RayTracingAccelerationStructureSize size = GlobalDevice().GetRayTracingAccelerationStructureSize(instancesBuffer);

	const Resource scratchResource = GlobalDevice().Create(
	{
		.Type = ResourceType::Buffer,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::UnorderedAccess,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size.ScratchSize,
		.DebugName = "Scratch Scene Acceleration Structure"_view,
	});
	transientResources.Add(scratchResource);
	SceneAccelerationStructureResource = GlobalDevice().Create(
	{
		.Type = ResourceType::Buffer,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::RayTracingAccelerationStructure | ResourceFlags::UnorderedAccess,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = size.ResultSize,
		.DebugName = "Scene Acceleration Structure"_view,
	});
	GlobalGraphics().BuildRayTracingAccelerationStructure(instancesBuffer, scratchResource, SceneAccelerationStructureResource);

	SceneAccelerationStructure = GlobalDevice().Create(RayTracingAccelerationStructureDescription
	{
		.Resource = SceneAccelerationStructureResource,
		.ViewHeap = GlobalResourceViewHeap(),
	});

	GlobalGraphics().End();
	GlobalDevice().Submit(GlobalGraphics());
	GlobalDevice().WaitForIdle();

	for (Resource& resource : transientResources)
	{
		GlobalDevice().Destroy(&resource);
	}

	Array<HLSL::Node> nodeData(SceneNodes.GetCount(), RendererAllocator);
	for (const Node& node : SceneNodes)
	{
		nodeData.Add(HLSL::Node
		{
			.LocalToWorld = node.LocalToWorld,
			.NormalLocalToWorld = node.LocalToWorld.GetInverse().GetTranspose(),
		});
	}
	SceneNodeBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
									   nodeData.GetDataSize(),
									   nodeData.GetElementSize(),
									   ResourceFlags::None,
									   ViewType::ShaderResource,
									   nodeData.GetData(),
									   "Scene Node Buffer"_view);

	for (const GLTF::Material& gltfMaterial : scene.Materials)
	{
		const auto convertTexture = [](const GLTF::Scene& scene, usize textureIndex, StringView textureName) -> ReadTexture
		{
			if (textureIndex == INDEX_NONE)
			{
				return ReadTexture { Resource::Invalid(), TextureView::Invalid() };
			}

			const GLTF::Texture& gltfTexture = scene.Textures[textureIndex];
			const GLTF::Image& gltfImage = scene.Images[gltfTexture.Image];

			DDS::Image image = DDS::LoadImage(gltfImage.Path);
			const ReadTexture texture = CreateReadTexture(ResourceUploader::Lifetime::Scene,
														  { image.Width, image.Height },
														  image.MipMapCount,
														  image.Format,
														  image.Data,
														  textureName);
			DDS::UnloadImage(&image);

			return texture;
		};

		static bool blendWarningOnce = false;
		if (!blendWarningOnce && gltfMaterial.AlphaMode == GLTF::AlphaMode::Blend)
		{
			Platform::Log("Renderer::LoadScene: Blend materials are not supported!\n");
			blendWarningOnce = true;
		}

		Material material =
		{
			.IsSpecularGlossiness = gltfMaterial.IsSpecularGlossiness,
			.NormalMapTexture = convertTexture(scene, gltfMaterial.NormalMapTexture, "Scene Normal Map Texture"_view),
			.EmissiveTexture = convertTexture(scene, gltfMaterial.EmissiveTexture, "Scene Emissive Texture"_view),
			.EmissiveFactor = gltfMaterial.EmissiveFactor,
			.EmissiveStrength = gltfMaterial.EmissiveStrength,
			.Translucent = gltfMaterial.AlphaMode != GLTF::AlphaMode::Opaque,
			.AlphaCutoff = gltfMaterial.AlphaCutoff,
			.DoubleSided = gltfMaterial.DoubleSided,
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

	Array<HLSL::Material> materialData(SceneMaterials.GetCount(), RendererAllocator);
	for (const Material& material : SceneMaterials)
	{
		ReadTexture baseColorOrDiffuseTexture = WhiteTexture;
		if (material.IsSpecularGlossiness && material.SpecularGlossiness.DiffuseTexture.Resource.IsValid())
		{
			baseColorOrDiffuseTexture = material.SpecularGlossiness.DiffuseTexture;
		}
		else if (!material.IsSpecularGlossiness && material.MetallicRoughness.BaseColorTexture.Resource.IsValid())
		{
			baseColorOrDiffuseTexture = material.MetallicRoughness.BaseColorTexture;
		}

		ReadTexture metallicRoughnessOrSpecularGlossinessTexture = WhiteTexture;
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
			.BaseColorOrDiffuseTextureIndex = GlobalDevice().Get(baseColorOrDiffuseTexture.View),
			.MetallicRoughnessOrSpecularGlossinessTextureIndex = GlobalDevice().Get(metallicRoughnessOrSpecularGlossinessTexture.View),
			.BaseColorOrDiffuseFactor = material.IsSpecularGlossiness ? material.SpecularGlossiness.DiffuseFactor
																	  : material.MetallicRoughness.BaseColorFactor,
			.MetallicOrSpecularFactor = material.IsSpecularGlossiness ? material.SpecularGlossiness.SpecularFactor
																	  : float32x3 { material.MetallicRoughness.MetallicFactor, 0.0f, 0.0f },
			.RoughnessOrGlossinessFactor = material.IsSpecularGlossiness ? material.SpecularGlossiness.GlossinessFactor
																		 : material.MetallicRoughness.RoughnessFactor,
			.IsSpecularGlossiness = material.IsSpecularGlossiness,
			.NormalMapTextureIndex = GlobalDevice().Get(material.NormalMapTexture.View.IsValid() ? material.NormalMapTexture.View : DefaultNormalMapTexture.View),
			.EmissiveTextureIndex = GlobalDevice().Get(material.EmissiveTexture.View.IsValid() ? material.EmissiveTexture.View : WhiteTexture.View),
			.EmissiveFactor = material.EmissiveFactor,
			.EmissiveStrength = material.EmissiveStrength,
			.AlphaCutoff = material.AlphaCutoff,
			.DoubleSided = material.DoubleSided,
		});
	}
	SceneMaterialBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
										   materialData.GetDataSize(),
										   materialData.GetElementSize(),
										   ResourceFlags::None,
										   ViewType::ShaderResource,
										   materialData.GetData(),
										   "Scene Material Buffer"_view);

	bool hasDirectionalLight = false;
	HLSL::DirectionalLight directionalLight =
	{
		.RGB = { 1.0f, 1.0f, 1.0f },
		.IntensityLux = 1.0f,
		.DirectionWS = { 0.0f, 1.0f, 0.0f },
	};
	Array<HLSL::PointLight> pointLights(RendererAllocator);

	for (const GLTF::Light& light : scene.Lights)
	{
		Vector translationWS;
		Quaternion orientationWS;
		DecomposeTransform(light.LocalToWorld, &translationWS, &orientationWS, nullptr);

		const Vector directionWS = -orientationWS.Rotate(GLTF::DefaultDirectionLS);

		if (light.Type == GLTF::LightType::Directional)
		{
			CHECK(!hasDirectionalLight);
			hasDirectionalLight = true;

			directionalLight = HLSL::DirectionalLight
			{
				.RGB = light.RGB,
				.IntensityLux = light.Intensity,
				.DirectionWS = { directionWS.X, directionWS.Y, directionWS.Z },
			};
		}
		else if (light.Type == GLTF::LightType::Point)
		{
			pointLights.Add(HLSL::PointLight
			{
				.RGB = light.RGB,
				.IntensityCandela = light.Intensity,
				.PositionWS = { translationWS.X, translationWS.Y, translationWS.Z },
			});
		}
	}

	SceneDirectionalLightBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
												   sizeof(directionalLight),
												   0,
												   ResourceFlags::None,
												   ViewType::ConstantBuffer,
												   &directionalLight,
												   "Scene Directional Light Buffer"_view);
	if (!pointLights.IsEmpty())
	{
		ScenePointLightsBuffer = CreateReadBuffer(ResourceUploader::Lifetime::Scene,
												  pointLights.GetDataSize(),
												  pointLights.GetElementSize(),
												  ResourceFlags::None,
												  ViewType::ShaderResource,
												  pointLights.GetData(),
												  "Scene Point Lights Buffer"_view);
	}

	TemporalAntiAliasing.DiscardPreviousFrame = true;
}

void Renderer::UnloadScene()
{
	GlobalDevice().WaitForIdle();

	ResourceUploader::Reset();

	DestroyReadBuffer(&SceneVertexBuffer);
	DestroyReadBuffer(&ScenePrimitiveBuffer);
	DestroyReadBuffer(&SceneDrawCallBuffer);
	DestroyReadBuffer(&SceneNodeBuffer);
	DestroyReadBuffer(&SceneMaterialBuffer);
	DestroyReadBuffer(&SceneDirectionalLightBuffer);
	DestroyReadBuffer(&ScenePointLightsBuffer);

	for (Mesh& mesh : SceneMeshes)
	{
		for (Primitive& primitive : mesh.Primitives)
		{
			GlobalDevice().Destroy(&primitive.AccelerationStructureResource);
		}
	}
	GlobalDevice().Destroy(&SceneAccelerationStructureResource);
	GlobalDevice().Destroy(&SceneAccelerationStructure);

	for (Material& material : SceneMaterials)
	{
		if (material.IsSpecularGlossiness)
		{
			DestroyReadTexture(&material.SpecularGlossiness.DiffuseTexture);
			DestroyReadTexture(&material.SpecularGlossiness.SpecularGlossinessTexture);
		}
		else
		{
			DestroyReadTexture(&material.MetallicRoughness.BaseColorTexture);
			DestroyReadTexture(&material.MetallicRoughness.MetallicRoughnessTexture);
		}
		DestroyReadTexture(&material.NormalMapTexture);
		DestroyReadTexture(&material.EmissiveTexture);
	}

	SceneMeshes.Clear();
	SceneNodes.Clear();
	SceneMaterials.Clear();
}

void Renderer::CreatePipelines()
{
	const auto createGraphicsPipeline = [](StringView debugName,
										   StringView path,
										   bool pixelShader,
										   bool depth,
										   bool cull,
										   auto... formats) -> GraphicsPipeline
	{
		Shader vertex = GlobalDevice().Create(
		{
			.Stage = ShaderStage::Vertex,
			.FilePath = String(path, RendererAllocator),
		});

		Shader pixel;
		if (pixelShader)
		{
			pixel = GlobalDevice().Create(
			{
				.Stage = ShaderStage::Pixel,
				.FilePath = String(path, RendererAllocator),
			});
		}

		const GraphicsPipeline pipeline = GlobalDevice().Create(GraphicsPipelineDescription
		{
			.VertexStage = vertex,
			.PixelStage = pixel,
			.RenderTargetFormats = { formats... },
			.DepthStencilFormat = depth ? ResourceFormat::Depth32 : ResourceFormat::None,
			.AlphaBlend = false,
			.Cull = cull,
			.ReverseDepth = true,
			.DebugName = debugName,
		});

		GlobalDevice().Destroy(&vertex);
		if (pixel.IsValid())
		{
			GlobalDevice().Destroy(&pixel);
		}

		return pipeline;
	};
	const auto createComputePipeline = [](StringView debugName, StringView path) -> ComputePipeline
	{
		Shader compute = GlobalDevice().Create(
		{
			.Stage = ShaderStage::Compute,
			.FilePath = String(path, RendererAllocator),
		});

		const ComputePipeline pipeline = GlobalDevice().Create(
		{
			.Stage = compute,
			.DebugName = debugName,
		});
		GlobalDevice().Destroy(&compute);

		return pipeline;
	};

	VisibilityPipeline = createGraphicsPipeline("Visibility Buffer Pipeline"_view,
												"Shaders/Visibility.hlsl"_view,
												true,
												true,
												true,
												ResourceFormat::RG32UInt);
	VisibilityDoubleSidedPipeline = createGraphicsPipeline("Visibility Buffer Double-Sided Pipeline"_view,
														   "Shaders/Visibility.hlsl"_view,
														   true,
														   true,
														   false,
														   ResourceFormat::RG32UInt);
	DeferredPipeline = createComputePipeline("Deferred Pipeline"_view,
											 "Shaders/Deferred.hlsl"_view);

	TemporalAntiAliasPipeline = createComputePipeline("Temporal Anti-Alias Pipeline"_view,
													  "Shaders/TemporalAntiAlias.hlsl"_view);

	LuminanceHistogramPipeline = createComputePipeline("Luminance Histogram Pipeline"_view,
													   "Shaders/LuminanceHistogram.hlsl"_view);
	LuminanceAveragePipeline = createComputePipeline("Luminance Average Pipeline"_view,
													 "Shaders/LuminanceAverage.hlsl"_view);

	ToneMapPipeline = createGraphicsPipeline("Tone Map Pipeline"_view,
											 "Shaders/ToneMap.hlsl"_view,
											 true,
											 false,
											 false,
											 ResourceFormat::RGBA8UNormSRGB);

	PathTracePipeline = createComputePipeline("Path Trace Pipeline"_view,
											  "Shaders/PathTrace.hlsl"_view);

	UI::CreatePipeline();
}

void Renderer::DestroyPipelines()
{
	GlobalDevice().Destroy(&VisibilityPipeline);
	GlobalDevice().Destroy(&VisibilityDoubleSidedPipeline);
	GlobalDevice().Destroy(&DeferredPipeline);
	GlobalDevice().Destroy(&TemporalAntiAliasPipeline);
	GlobalDevice().Destroy(&LuminanceHistogramPipeline);
	GlobalDevice().Destroy(&LuminanceAveragePipeline);
	GlobalDevice().Destroy(&ToneMapPipeline);
	GlobalDevice().Destroy(&PathTracePipeline);

	UI::DestroyPipeline();
}

void Renderer::RecreatePipelines()
{
	GlobalDevice().WaitForIdle();

	DestroyPipelines();
	CreatePipelines();
}

void Renderer::CreateSwapChainTextures(uint32x2 dimensions)
{
	for (usize frameIndex = 0; frameIndex < FramesInFlight; ++frameIndex)
	{
		SwapChainTextureResources[frameIndex] = GlobalDevice().Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = ResourceFormat::RGBA8UNormSRGB,
			.Flags = ResourceFlags::SwapChain | ResourceFlags::RenderTarget,
			.InitialLayout = BarrierLayout::RenderTarget,
			.Dimensions = { dimensions.X, dimensions.Y },
			.SwapChainIndex = static_cast<uint8>(frameIndex),
			.DebugName = "SwapChain Texture"_view,
		});
		SwapChainTextureViews[frameIndex] = GlobalDevice().Create(
		{
			.Type = ViewType::RenderTarget,
			.Resource = SwapChainTextureResources[frameIndex],
			.ViewHeap = GlobalRenderTargetViewHeap(),
		});
	}
}

void Renderer::DestroySwapChainTextures()
{
	for (usize frameIndex = 0; frameIndex < FramesInFlight; ++frameIndex)
	{
		GlobalDevice().Destroy(&SwapChainTextureResources[frameIndex]);
		GlobalDevice().Destroy(&SwapChainTextureViews[frameIndex]);
	}
}

void Renderer::CreateViewportTextures(uint32x2 dimensions)
{
	const auto createRenderTargetTexture = [dimensions](ResourceFormat format, StringView debugName) -> RenderTargetTexture
	{
		const Resource resource = GlobalDevice().Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = format,
			.Flags = ResourceFlags::RenderTarget,
			.InitialLayout = BarrierLayout::RenderTarget,
			.Dimensions = { dimensions.X, dimensions.Y },
			.DebugName = debugName,
		});
		return RenderTargetTexture
		{
			.Resource = resource,
			.ShaderResourceView = GlobalDevice().Create(
			{
				.Type = ViewType::ShaderResource,
				.Resource = resource,
				.ViewHeap = GlobalResourceViewHeap(),
			}),
			.RenderTargetView = GlobalDevice().Create(
			{
				.Type = ViewType::RenderTarget,
				.Resource = resource,
				.ViewHeap = GlobalRenderTargetViewHeap(),
			}),
		};
	};
	const auto createWriteTexture = [dimensions](ResourceFormat format, StringView debugName) -> WriteTexture
	{
		const Resource resource = GlobalDevice().Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = format,
			.Flags = ResourceFlags::UnorderedAccess,
			.InitialLayout = BarrierLayout::GraphicsQueueUnorderedAccess,
			.Dimensions = { dimensions.X, dimensions.Y },
			.DebugName = debugName,
		});
		return WriteTexture
		{
			.Resource = resource,
			.ShaderResourceView = GlobalDevice().Create(
			{
				.Type = ViewType::ShaderResource,
				.Resource = resource,
				.ViewHeap = GlobalResourceViewHeap(),
			}),
			.UnorderedAccessView = GlobalDevice().Create(
			{
				.Type = ViewType::UnorderedAccess,
				.Resource = resource,
				.ViewHeap = GlobalResourceViewHeap(),
			}),
		};
	};

	DepthTextureResource = GlobalDevice().Create(
	{
		.Type = ResourceType::Texture2D,
		.Format = ResourceFormat::Depth32,
		.Flags = ResourceFlags::DepthStencil,
		.InitialLayout = BarrierLayout::DepthStencilWrite,
		.Dimensions = { dimensions.X, dimensions.Y },
		.ClearDepth = 0.0f,
		.DebugName = "Depth Texture"_view,
	});
	DepthTextureView = GlobalDevice().Create(
	{
		.Type = ViewType::DepthStencil,
		.Resource = DepthTextureResource,
		.ViewHeap = GlobalDepthStencilViewHeap(),
	});

	VisibilityTexture = createRenderTargetTexture(ResourceFormat::RG32UInt, "Visibility Texture"_view);

	HDRTexture = createWriteTexture(ResourceFormat::RGBA32Float, "HDR Texture"_view);
	AccumulationTexture = createWriteTexture(ResourceFormat::RGBA32Float, "Accumulation Texture"_view);
	PreviousAccumulationTexture = createWriteTexture(ResourceFormat::RGBA32Float, "Accumulation Texture"_view);

	FinalTexture = createRenderTargetTexture(ResourceFormat::RGBA8UNormSRGB, "Final Texture"_view);

	TemporalAntiAliasing.DiscardPreviousFrame = true;
}

void Renderer::DestroyViewportTextures()
{
	const auto destroyRenderTargetTexture = [](RenderTargetTexture* renderTargetTexture) -> void
	{
		GlobalDevice().Destroy(&renderTargetTexture->Resource);
		GlobalDevice().Destroy(&renderTargetTexture->ShaderResourceView);
		GlobalDevice().Destroy(&renderTargetTexture->RenderTargetView);
	};
	const auto destroyWriteTexture = [](WriteTexture* writeTexture) -> void
	{
		GlobalDevice().Destroy(&writeTexture->Resource);
		GlobalDevice().Destroy(&writeTexture->ShaderResourceView);
		GlobalDevice().Destroy(&writeTexture->UnorderedAccessView);
	};

	GlobalDevice().Destroy(&DepthTextureResource);
	GlobalDevice().Destroy(&DepthTextureView);

	destroyRenderTargetTexture(&VisibilityTexture);

	destroyWriteTexture(&HDRTexture);
	destroyWriteTexture(&AccumulationTexture);
	destroyWriteTexture(&PreviousAccumulationTexture);

	destroyRenderTargetTexture(&FinalTexture);
}
