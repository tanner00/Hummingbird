#include "Renderer.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "DrawText.hpp"
#include "GLTF.hpp"

#include "Shaders/Luminance.hlsli"

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
		device->Write(&buffer, data);

	return BasicBuffer { buffer, view };
}

Renderer::Renderer(const Platform::Window* window)
	: Device(window)
	, Graphics(Device.Create(GraphicsContextDescription {}))
	, ViewMode(ViewMode::Lit)
#if !RELEASE
	, AverageCpuTime(0.0)
	, AverageGpuTime(0.0)
#endif
{
	CreateScreenTextures(window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&Device);

	static constexpr uint8 white[] = { 0xFF, 0xFF, 0xFF, 0xFF };
	WhiteTexture = CreateBasicTexture(&Device, { 1, 1 }, 1, ResourceFormat::Rgba8Unorm, white, "White Texture"_view);

	static constexpr uint8 defaultNormal[] = { 0x7F, 0x7F, 0xFF, 0x00 };
	DefaultNormalMapTexture = CreateBasicTexture(&Device, { 1, 1 }, 1, ResourceFormat::Rgba8Unorm, defaultNormal, "Default Normal Map Texture"_view);

	SceneLuminanceBuffer = CreateBasicBuffer(&Device,
											 LuminanceHistogramBinsCount * sizeof(uint32) + sizeof(float),
											 0,
											 ResourceFlags::UnorderedAccess,
											 ViewType::UnorderedAccess,
											 nullptr,
											 "Scene Luminance Buffer"_view);

	DefaultSampler = Device.Create(
	{
		.MinificationFilter = SamplerFilter::Anisotropic,
		.MagnificationFilter = SamplerFilter::Anisotropic,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});

	CreatePipelines();
}

Renderer::~Renderer()
{
	UnloadScene();
	DestroyPipelines();

	Device.Destroy(&DefaultSampler);

	Device.Destroy(&SceneLuminanceBuffer.Resource);
	Device.Destroy(&SceneLuminanceBuffer.View);
	Device.Destroy(&WhiteTexture.Resource);
	Device.Destroy(&WhiteTexture.View);
	Device.Destroy(&DefaultNormalMapTexture.Resource);
	Device.Destroy(&DefaultNormalMapTexture.View);

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
		ViewMode = ViewMode::Lit;
	}
	if (IsKeyPressedOnce(Key::U))
	{
		ViewMode = ViewMode::Unlit;
	}
	if (IsKeyPressedOnce(Key::G))
	{
		ViewMode = ViewMode::Geometry;
	}
	if (IsKeyPressedOnce(Key::N))
	{
		ViewMode = ViewMode::Normal;
	}

	if (IsKeyPressedOnce(Key::R))
	{
		Device.WaitForIdle();
		DestroyPipelines();
		CreatePipelines();
	}
#endif

	Graphics.Begin();

	Graphics.SetViewport(HdrTexture.Dimensions.Width, HdrTexture.Dimensions.Height);

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		HdrTexture
	);

	Graphics.ClearRenderTarget(HdrTextureRenderTargetView, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture.View);

	{
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
			.ViewProjection = projection * view,
			.ViewPosition = Float3 { viewPosition.X, viewPosition.Y, viewPosition.Z },
			.DefaultSamplerIndex = Device.Get(DefaultSampler),
			.VertexBufferIndex = Device.Get(SceneVertexBuffer.View),
			.PrimitiveBufferIndex = Device.Get(ScenePrimitiveBuffer.View),
			.NodeBufferIndex = Device.Get(SceneNodeBuffer.View),
			.MaterialBufferIndex = Device.Get(SceneMaterialBuffer.View),
			.DirectionalLightBufferIndex = Device.Get(SceneDirectionalLightBuffer.View),
			.PointLightsBufferIndex = ScenePointLightsBuffer.View.IsValid() ? Device.Get(ScenePointLightsBuffer.View) : 0,
			.AccelerationStructureIndex = Device.Get(SceneAccelerationStructure),
			.PointLightsCount = static_cast<uint32>(ScenePointLightsBuffer.View.Buffer.Size / sizeof(HLSL::PointLight)),
		};
		Device.Write(&SceneBuffers[Device.GetFrameIndex()].Resource, &sceneData);

		Graphics.SetRenderTarget(DepthTexture.View);
		UpdateScene(SceneDepthPrePassPipeline, GraphicsPipeline::Invalid());

		Graphics.SetRenderTarget(HdrTextureRenderTargetView, DepthTexture.View);
		UpdateScene(SceneOpaquePipeline, SceneBlendPipeline);
	}

	Graphics.TextureBarrier
	(
		{ BarrierStage::RenderTarget, BarrierStage::ComputeShading },
		{ BarrierAccess::RenderTarget, BarrierAccess::ShaderResource },
		{ BarrierLayout::RenderTarget, BarrierLayout::ShaderResource },
		HdrTexture
	);
	Graphics.BufferBarrier
	(
		{ BarrierStage::None, BarrierStage::ComputeShading },
		{ BarrierAccess::NoAccess, BarrierAccess::UnorderedAccess },
		SceneLuminanceBuffer.Resource
	);

	const HLSL::LuminanceHistogramRootConstants luminanceHistogramRootConstants =
	{
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
		.HdrTextureIndex = Device.Get(HdrTextureShaderResourceView),
	};

	Graphics.SetPipeline(LuminanceHistogramPipeline);
	Graphics.SetRootConstants(&luminanceHistogramRootConstants);
	Graphics.Dispatch((HdrTexture.Dimensions.Width + 15) / 16, (HdrTexture.Dimensions.Height + 15) / 16, 1);

	Graphics.BufferBarrier
	(
		{ BarrierStage::ComputeShading, BarrierStage::ComputeShading },
		{ BarrierAccess::UnorderedAccess, BarrierAccess::UnorderedAccess },
		SceneLuminanceBuffer.Resource
	);

	const HLSL::LuminanceAverageRootConstants luminanceAverageRootConstants =
	{
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
		.PixelCount = HdrTexture.Dimensions.Width * HdrTexture.Dimensions.Height,
	};

	Graphics.SetPipeline(LuminanceAveragePipeline);
	Graphics.SetRootConstants(&luminanceAverageRootConstants);
	Graphics.Dispatch(LuminanceHistogramBinsCount, 1, 1);

	const Resource& swapChainTexture = SwapChainTextures[Device.GetFrameIndex()].Resource;
	const TextureView& swapChainTextureView = SwapChainTextures[Device.GetFrameIndex()].View;

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
		swapChainTexture
	);

	Graphics.SetRenderTarget(swapChainTextureView);
	Graphics.SetViewport(swapChainTexture.Dimensions.Width, swapChainTexture.Dimensions.Height);

	const HLSL::ToneMapRootConstants toneMapRootConstants =
	{
		.HdrTextureIndex = Device.Get(HdrTextureShaderResourceView),
		.DefaultSamplerIndex = Device.Get(DefaultSampler),
		.LuminanceBufferIndex = Device.Get(SceneLuminanceBuffer.View),
		.DebugViewMode = ViewMode != ViewMode::Lit,
	};

	Graphics.SetPipeline(ToneMapPipeline);
	Graphics.SetRootConstants(&toneMapRootConstants);
	Graphics.Draw(3);

#if !RELEASE
	UpdateFrameTimes(startCpuTime);
#endif

	DrawText::Get().Submit(&Graphics, &Device, swapChainTexture.Dimensions.Width, swapChainTexture.Dimensions.Height);

	Graphics.TextureBarrier
	(
		{ BarrierStage::RenderTarget, BarrierStage::None },
		{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
		{ BarrierLayout::RenderTarget, BarrierLayout::Present },
		swapChainTexture
	);

	Graphics.End();

	Device.Submit(Graphics);
	Device.Present();
}

void Renderer::UpdateScene(const GraphicsPipeline& opaquePipeline, const GraphicsPipeline& blendPipeline)
{
	for (usize nodeIndex = 0; nodeIndex < SceneNodes.GetLength(); ++nodeIndex)
	{
		const Node& node = SceneNodes[nodeIndex];
		const Mesh& mesh = SceneMeshes[node.MeshIndex];

		const Matrix normalTransform = node.Transform.GetInverse().GetTranspose();

		for (const Primitive& primitive : mesh.Primitives)
		{
			const HLSL::SceneRootConstants rootConstants =
			{
				.NodeIndex = static_cast<uint32>(nodeIndex),
				.MaterialIndex = static_cast<uint32>(primitive.MaterialIndex),
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

			Graphics.SetConstantBuffer("Scene"_view, SceneBuffers[Device.GetFrameIndex()].Resource);

			Graphics.SetRootConstants(&rootConstants);

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
			Graphics.SetVertexBuffer(3,
									 SubBuffer
									 {
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.TangentSize,
										 .Stride = primitive.TangentStride,
										 .Offset = primitive.TangentOffset,
									 });
			Graphics.SetIndexBuffer(SubBuffer
									{
										 .Resource = SceneVertexBuffer.Resource,
										 .Size = primitive.IndexSize,
										 .Stride = primitive.IndexStride,
										 .Offset = primitive.IndexOffset,
									});

			Graphics.DrawIndexed(primitive.IndexSize / primitive.IndexStride);
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
	DrawText::Get().Draw(StringView { cpuTimeText, Platform::StringLength(cpuTimeText) }, { 0.0f, 0.0f }, Float3 { 1.0f, 1.0f, 1.0f }, 32.0f);

	char gpuTimeText[16] = {};
	Platform::StringPrint("GPU: %.2fms", gpuTimeText, sizeof(gpuTimeText), AverageGpuTime * 1000.0);
	DrawText::Get().Draw(StringView { gpuTimeText, Platform::StringLength(gpuTimeText) }, { 0.0f, 32.0f }, Float3 { 1.0f, 1.0f, 1.0f }, 32.0f);
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

	VERIFY(scene.Buffers.GetLength() == 1, "GLTF file contains multiple buffers!");
	const GLTF::Buffer& vertexBuffer = scene.Buffers[0];

	Array<Array<Float4>> computedTangents(RendererAllocator);
	usize computedTangentsCount = 0;

	for (const GLTF::Mesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetLength(), RendererAllocator);

		for (const GLTF::Primitive& primitive : mesh.Primitives)
		{
			const GLTF::AccessorView positionView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Position]);
			const GLTF::AccessorView textureCoordinateView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Texcoord0]);
			const GLTF::AccessorView normalView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Normal]);
			const GLTF::AccessorView indexView = GLTF::GetAccessorView(scene, primitive.Indices);

			GLTF::AccessorView tangentView;
			if (primitive.Attributes.Contains(GLTF::AttributeType::Tangent))
			{
				tangentView = GLTF::GetAccessorView(scene, primitive.Attributes[GLTF::AttributeType::Tangent]);
			}
			else
			{
				Platform::LogFormatted("LoadScene: Missing tangents from primitive!\n");

				const usize tangentsCount = normalView.Size / normalView.Stride;
				Array<Float4> primitiveTangents(tangentsCount, RendererAllocator);
				for (usize i = 0; i < tangentsCount; ++i)
				{
					static constexpr Float4 invalidTangent = { 0.0f, 0.0f, 0.0f, 0.0f };
					primitiveTangents.Add(invalidTangent);
				}

				tangentView = GLTF::AccessorView
				{
					.Offset = vertexBuffer.Size + computedTangentsCount * sizeof(Float4),
					.Stride = primitiveTangents.GetElementSize(),
					.Size = primitiveTangents.GetDataSize(),
				};
				computedTangentsCount += tangentsCount;

				computedTangents.Add(Move(primitiveTangents));
			}

			primitives.Add(Primitive
			{
				.GlobalIndex = {},
				.PositionOffset = positionView.Offset,
				.PositionStride = positionView.Stride,
				.PositionSize = positionView.Size,
				.TextureCoordinateOffset = textureCoordinateView.Offset,
				.TextureCoordinateStride = textureCoordinateView.Stride,
				.TextureCoordinateSize = textureCoordinateView.Size,
				.NormalOffset = normalView.Offset,
				.NormalStride = normalView.Stride,
				.NormalSize = normalView.Size,
				.TangentOffset = tangentView.Offset,
				.TangentStride = tangentView.Stride,
				.TangentSize = tangentView.Size,
				.IndexOffset = indexView.Offset,
				.IndexStride = indexView.Stride,
				.IndexSize = indexView.Size,
				.MaterialIndex = primitive.Material,
				.AccelerationStructureResource = {},
			});
		}

		SceneMeshes.Add(Mesh
		{
			.Primitives = Move(primitives),
		});
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

	GLTF::Buffer finalVertexBuffer = vertexBuffer;
	if (!computedTangents.IsEmpty())
	{
		finalVertexBuffer.Size += computedTangentsCount * sizeof(Float4);
		finalVertexBuffer.Data = static_cast<uint8*>(RendererAllocator->Allocate(finalVertexBuffer.Size));
		Platform::MemoryCopy(finalVertexBuffer.Data, vertexBuffer.Data, vertexBuffer.Size);

		usize tangentsOffset = 0;
		for (const Array<Float4>& tangents : computedTangents)
		{
			Platform::MemoryCopy(finalVertexBuffer.Data + vertexBuffer.Size + tangentsOffset, tangents.GetData(), tangents.GetDataSize());
			tangentsOffset += tangents.GetDataSize();
		}
	}
	SceneVertexBuffer = CreateBasicBuffer(&Device,
										  finalVertexBuffer.Size,
										  0,
										  ResourceFlags::None,
										  ViewType::ShaderResource,
										  finalVertexBuffer.Data,
										  "Scene Vertex Buffer"_view);
	if (!computedTangents.IsEmpty())
	{
		RendererAllocator->Deallocate(finalVertexBuffer.Data, finalVertexBuffer.Size);
	}

	Array<HLSL::Primitive> primitiveData(RendererAllocator);
	for (const Mesh& mesh : SceneMeshes)
	{
		for (const Primitive& primitive : mesh.Primitives)
		{
			primitiveData.Add(HLSL::Primitive
			{
				.TextureCoordinateOffset = static_cast<uint32>(primitive.TextureCoordinateOffset),
				.TextureCoordinateStride = static_cast<uint32>(primitive.TextureCoordinateStride),
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

	usize globalPrimitiveIndex = 0;
	for (usize meshIndex = 0; meshIndex < scene.Meshes.GetLength(); ++meshIndex)
	{
		const GLTF::Mesh& gltfMesh = scene.Meshes[meshIndex];

		for (usize primitiveIndex = 0; primitiveIndex < gltfMesh.Primitives.GetLength(); ++primitiveIndex)
		{
			const GLTF::Primitive& gltfPrimitive = scene.Meshes[meshIndex].Primitives[primitiveIndex];

			const GLTF::AccessorView positionView = GLTF::GetAccessorView(scene, gltfPrimitive.Attributes[GLTF::AttributeType::Position]);
			const GLTF::AccessorView indexView = GLTF::GetAccessorView(scene, gltfPrimitive.Indices);

			const AccelerationStructureGeometry geometry = AccelerationStructureGeometry
			{
				.VertexBuffer = SubBuffer
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = positionView.Size,
					.Stride = positionView.Stride,
					.Offset = positionView.Offset,
				},
				.IndexBuffer = SubBuffer
				{
					.Resource = SceneVertexBuffer.Resource,
					.Size = indexView.Size,
					.Stride = indexView.Stride,
					.Offset = indexView.Offset,
				},
				.Translucent = scene.Materials[gltfPrimitive.Material].AlphaMode != GLTF::AlphaMode::Opaque,
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

			Primitive& primitive = SceneMeshes[meshIndex].Primitives[primitiveIndex];
			primitive.AccelerationStructureResource = resultResource;
			primitive.GlobalIndex = globalPrimitiveIndex;

			++globalPrimitiveIndex;
		}
	}

	Array<AccelerationStructureInstance> instances(RendererAllocator);
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
		}

		SceneNodes.Add(Node
		{
			.Transform = transform,
			.MeshIndex = node.Mesh,
		});
	}

	const Resource instancesResource = Device.Create(
	{
		.Type = ResourceType::AccelerationStructureInstances,
		.Format = ResourceFormat::None,
		.Flags = ResourceFlags::Upload,
		.InitialLayout = BarrierLayout::Undefined,
		.Size = instances.GetLength() * Device.GetAccelerationStructureInstanceSize(),
		.Name = "Scene Acceleration Structure Instances"_view,
	});
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

	transientResources.Add(scratchResource);
	transientResources.Add(instancesResource);
	for (Resource& resource : transientResources)
	{
		Device.Destroy(&resource);
	}

	for (const GLTF::Material& gltfMaterial : scene.Materials)
	{
		const auto convertTexture = [this](const GLTF::Scene& scene, usize textureIndex, StringView textureName) -> BasicTexture
		{
			if (textureIndex == INDEX_NONE)
				return BasicTexture { Resource::Invalid(), TextureView::Invalid() };

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

	Array<HLSL::Node> nodeData(SceneNodes.GetLength(), RendererAllocator);
	for (const Node& node : SceneNodes)
	{
		nodeData.Add(HLSL::Node
		{
			.Transform = node.Transform,
		});
	}
	SceneNodeBuffer = CreateBasicBuffer(&Device,
										SceneNodes.GetLength() * sizeof(HLSL::Node),
										sizeof(HLSL::Node),
										ResourceFlags::None,
										ViewType::ShaderResource,
										nodeData.GetData(),
										"Scene Node Buffer"_view);

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
			.BaseColorOrDiffuseFactor = material.IsSpecularGlossiness
									  ? material.SpecularGlossiness.DiffuseFactor
									  : material.MetallicRoughness.BaseColorFactor,
			.NormalMapTextureIndex = Device.Get(material.NormalMapTexture.View.IsValid() ? material.NormalMapTexture.View : DefaultNormalMapTexture.View),
			.MetallicRoughnessOrSpecularGlossinessTextureIndex = Device.Get(metallicRoughnessOrSpecularGlossinessTexture.View),
			.MetallicOrSpecularFactor = material.IsSpecularGlossiness
									  ? material.SpecularGlossiness.SpecularFactor
									  : Float3 { material.MetallicRoughness.MetallicFactor, 0.0f, 0.0f },
			.RoughnessOrGlossinessFactor = material.IsSpecularGlossiness
										 ? material.SpecularGlossiness.GlossinessFactor
										 : material.MetallicRoughness.RoughnessFactor,
			.IsSpecularGlossiness = material.IsSpecularGlossiness,
			.AlphaCutoff = material.AlphaCutoff,
		});
	}
	SceneMaterialBuffer = CreateBasicBuffer(&Device,
											SceneMaterials.GetLength() * sizeof(HLSL::Material),
											sizeof(HLSL::Material),
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

		static const Vector defaultLightDirection = Vector { +0.0f, +0.0f, -1.0f };
		const Vector direction = -orientation.Rotate(defaultLightDirection);

		if (light.Type == GLTF::LightType::Directional)
		{
			CHECK(!hasDirectionalLight);
			hasDirectionalLight = true;

			directionalLight = HLSL::DirectionalLight
			{
				.Color = light.Color,
				.IntensityLux = light.Intensity,
				.Direction = { direction.X, direction.Y, direction.Z },
			};
		}
		else if (light.Type == GLTF::LightType::Point)
		{
			pointLights.Add(HLSL::PointLight
			{
				.Color = light.Color,
				.IntensityCandela = light.Intensity,
				.Position = { translation.X, translation.Y, translation.Z },
			});
		}
	}
	if (!hasDirectionalLight)
	{
		directionalLight = HLSL::DirectionalLight
		{
			.Color = { 1.0f, 1.0f, 1.0f },
			.IntensityLux = 1.0f,
			.Direction = { +0.0f, +1.0f, +0.0f },
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
											   ResourceFormat format,
											   bool alphaBlend,
											   bool pixelShader,
											   bool depth) -> GraphicsPipeline
	{
		ShaderStages stages;

		Shader vertex = Device.Create(
		{
			.Stage = ShaderStage::Vertex,
			.FilePath = path,
		});
		stages.AddStage(vertex);

		Shader pixel;
		if (pixelShader)
		{
			pixel = Device.Create(
			{
				.Stage = ShaderStage::Pixel,
				.FilePath = path,
			});
			stages.AddStage(pixel);
		}

		const GraphicsPipeline pipeline = Device.Create(
		{
			.Stages = Move(stages),
			.RenderTargetFormat = format,
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
			.Stage = ShaderStage::Compute,
			.FilePath = path,
		});

		const ComputePipeline pipeline = Device.Create(
		{
			.Stage = Move(compute),
			.Name = name,
		});
		Device.Destroy(&compute);

		return pipeline;
	};

	SceneOpaquePipeline = createGraphicsPipeline("Scene Opaque Pipeline"_view,
												 "Shaders/Scene.hlsl"_view,
												 HdrFormat,
												 false,
												 true,
												 true);
	SceneBlendPipeline = createGraphicsPipeline("Scene Blend Pipeline"_view,
												"Shaders/Scene.hlsl"_view,
												HdrFormat,
												true,
												true,
												true);
	SceneDepthPrePassPipeline = createGraphicsPipeline("Scene Depth Pre-Pass Pipeline"_view,
													   "Shaders/Scene.hlsl"_view,
													   ResourceFormat::None,
													   false,
													   false,
													   true);

	ToneMapPipeline = createGraphicsPipeline("Tone Map Pipeline"_view,
											 "Shaders/ToneMap.hlsl"_view,
											 ResourceFormat::Rgba8SrgbUnorm,
											 false,
											 true,
											 false);

	LuminanceHistogramPipeline = createComputePipeline("Luminance Histogram Pipeline"_view, "Shaders/LuminanceHistogram.hlsl"_view);
	LuminanceAveragePipeline = createComputePipeline("Luminance Average Pipeline"_view, "Shaders/LuminanceAverage.hlsl"_view);
}

void Renderer::DestroyPipelines()
{
	Device.Destroy(&ToneMapPipeline);

	Device.Destroy(&LuminanceHistogramPipeline);
	Device.Destroy(&LuminanceAveragePipeline);

	Device.Destroy(&SceneOpaquePipeline);
	Device.Destroy(&SceneBlendPipeline);
	Device.Destroy(&SceneDepthPrePassPipeline);
}

void Renderer::CreateScreenTextures(uint32 width, uint32 height)
{
	for (usize i = 0; i < FramesInFlight; ++i)
	{
		SwapChainTextures[i].Resource = Device.Create(
		{
			.Type = ResourceType::Texture2D,
			.Format = ResourceFormat::Rgba8SrgbUnorm,
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

	HdrTexture = Device.Create(
	{
		.Type = ResourceType::Texture2D,
		.Format = HdrFormat,
		.Flags = ResourceFlags::RenderTarget,
		.InitialLayout = BarrierLayout::RenderTarget,
		.Dimensions = { width, height },
		.Name = "HDR Texture"_view,
	});
	HdrTextureRenderTargetView = Device.Create(
	{
		.Resource = HdrTexture,
		.Type = ViewType::RenderTarget,
		.Format = HdrTexture.Format,
	});
	HdrTextureShaderResourceView = Device.Create(
	{
		.Resource = HdrTexture,
		.Type = ViewType::ShaderResource,
		.Format = HdrTexture.Format,
	});
}

void Renderer::DestroyScreenTextures()
{
	Device.Destroy(&HdrTextureShaderResourceView);
	Device.Destroy(&HdrTextureRenderTargetView);
	Device.Destroy(&HdrTexture);
	Device.Destroy(&DepthTexture.View);
	Device.Destroy(&DepthTexture.Resource);
	for (BasicTexture& swapChainTexture : SwapChainTextures)
	{
		Device.Destroy(&swapChainTexture.Resource);
		Device.Destroy(&swapChainTexture.View);
	}
}
