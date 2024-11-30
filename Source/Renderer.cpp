#include "Renderer.hpp"
#include "DrawText.hpp"

static Allocator* RendererAllocator = &GlobalAllocator::Get();

Renderer::Renderer(const Platform::Window* window)
	: Device(window)
	, Graphics(Device.CreateGraphicsContext())
#if !RELEASE
	, AverageCpuTime(0.0)
	, AverageGpuTime(0.0)
#endif
{
	CreateScreenTextures(window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&Device);

	DefaultTexture = Device.CreateTexture("Default Texture"_view, BarrierLayout::GraphicsQueueCommon,
	{
		.Width = 1,
		.Height = 1,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Rgba8Srgb,
	});
	constexpr usize white = 0xFFFFFFFF;
	Device.Write(DefaultTexture, &white);

	DefaultSampler = Device.CreateSampler(
	{
		.MinificationFilter = SamplerFilter::Linear,
		.MagnificationFilter = SamplerFilter::Linear,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});

	LoadScene(0);

	const auto createPipeline = [this](bool alphaBlend)
	{
		Shader vertex = Device.CreateShader(
		{
			.Stage = ShaderStage::Vertex,
			.FilePath = "Resources/Shaders/Scene.hlsl"_view,
		});
		Shader pixel = Device.CreateShader(
		{
			.Stage = ShaderStage::Pixel,
			.FilePath = "Resources/Shaders/Scene.hlsl"_view,
		});
		ShaderStages stages;
		stages.AddStage(vertex);
		stages.AddStage(pixel);

		const GraphicsPipeline pipeline = Device.CreateGraphicsPipeline("Scene Pipeline"_view,
		{
			.Stages = Move(stages),
			.RenderTargetFormat = TextureFormat::Rgba8Srgb,
			.DepthFormat = TextureFormat::Depth32,
			.AlphaBlend = alphaBlend,
		});
		Device.DestroyShader(&vertex);
		Device.DestroyShader(&pixel);

		return pipeline;
	};
	SceneOpaquePipeline = createPipeline(false);
	SceneBlendPipeline = createPipeline(true);
}

Renderer::~Renderer()
{
	UnloadScene();
	Device.DestroyGraphicsPipeline(&SceneOpaquePipeline);
	Device.DestroyGraphicsPipeline(&SceneBlendPipeline);

	Device.DestroySampler(&DefaultSampler);
	Device.DestroyTexture(&DefaultTexture);

	DrawText::Get().Shutdown();

	DestroyScreenTextures();
}

void Renderer::Update()
{
#if !RELEASE
	const double startCpuTime = Platform::GetTime();
#endif

	if (IsKeyPressedOnce(Key::One))
	{
		LoadScene(0);
	}
	else if (IsKeyPressedOnce(Key::Two))
	{
		LoadScene(1);
	}

	static uint32 geometryView = 0;
	if (IsKeyPressedOnce(Key::G))
	{
		geometryView = !geometryView;
	}

	const Matrix view = SceneCamera.Transform.GetInverse();
	const Matrix projection = Matrix::Perspective(SceneCamera.FieldOfViewYDegrees, SceneCamera.AspectRatio, SceneCamera.NearZ, SceneCamera.FarZ);
	const Hlsl::Scene instanceSceneData =
	{
		.ViewProjection = projection * view,
		.NodeBufferIndex = Device.Get(InstanceNodeBuffer),
	};
	Device.Write(InstanceSceneBuffer, &instanceSceneData);

	Graphics.Begin();

	const Texture& frameTexture = SwapChainTextures[Device.GetFrameIndex()];

	Graphics.SetViewport(frameTexture.GetWidth(), frameTexture.GetHeight());

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		frameTexture
	);

	Graphics.SetGraphicsPipeline(&SceneOpaquePipeline);

	Graphics.ClearRenderTarget(frameTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture);

	Graphics.SetRenderTarget(frameTexture, DepthTexture);

	const uint32 defaultTextureIndex = Device.Get(DefaultTexture);
	const uint32 defaultSamplerIndex = Device.Get(DefaultSampler);

	for (usize i = 0; i < SceneNodes.GetLength(); ++i)
	{
		const Node& node = SceneNodes[i];
		const Mesh& mesh = SceneMeshes[node.MeshIndex];

		for (const Primitive& primitive : mesh.Primitives)
		{
			Hlsl::SceneRootConstants rootConstants =
			{
				.NodeIndex = static_cast<uint32>(i),
				.SamplerIndex = defaultSamplerIndex,
				.AlphaCutoff = 0.0f,
				.BaseColorTextureIndex = defaultTextureIndex,
				.BaseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f },
				.GeometryView = geometryView,
			};

			bool requiresBlend = false;
			if (primitive.MaterialIndex != INDEX_NONE)
			{
				const Material& material = SceneMaterials[primitive.MaterialIndex];

				if (material.BaseColorTexture.IsValid())
				{
					rootConstants.BaseColorTextureIndex = Device.Get(material.BaseColorTexture);
				}
				rootConstants.BaseColorFactor = material.BaseColorFactor;

				if (material.Sampler.IsValid())
				{
					rootConstants.SamplerIndex = Device.Get(material.Sampler);
				}

				requiresBlend = material.RequiresBlend;
				rootConstants.AlphaCutoff = material.AlphaCutoff;
			}

			if (requiresBlend)
			{
				Graphics.SetGraphicsPipeline(&SceneBlendPipeline);
			}
			else
			{
				Graphics.SetGraphicsPipeline(&SceneOpaquePipeline);
			}

			Graphics.SetConstantBuffer("Scene"_view, InstanceSceneBuffer);

			Graphics.SetRootConstants("RootConstants"_view, &rootConstants);

			Graphics.SetVertexBuffer(SceneVertexBuffer, 0, primitive.PositionOffset, primitive.PositionSize, primitive.PositionStride);
			Graphics.SetVertexBuffer(SceneVertexBuffer, 1, primitive.TextureCoordinateOffset, primitive.TextureCoordinateSize, primitive.TextureCoordinateStride);
			Graphics.SetIndexBuffer(SceneVertexBuffer, primitive.IndexOffset, primitive.IndexSize, primitive.IndexStride);

			Graphics.DrawIndexed(primitive.IndexSize / primitive.IndexStride);
		}
	}

#if !RELEASE
	UpdateFrameTimes(startCpuTime);
#endif

	DrawText::Get().Submit(Graphics, frameTexture.GetWidth(), frameTexture.GetHeight());

	Graphics.TextureBarrier
	(
		{ BarrierStage::RenderTarget, BarrierStage::None },
		{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
		{ BarrierLayout::RenderTarget, BarrierLayout::Present },
		frameTexture
	);

	Graphics.End();

	Device.Submit(Graphics);
	Device.Present();
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
	DrawText::Get().Draw(StringView { gpuTimeText, Platform::StringLength(gpuTimeText) }, { 0.0f, 24.0f }, Float3 { 1.0f, 1.0f, 1.0f }, 32.0f);
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

void Renderer::LoadScene(usize sceneIndex)
{
	const auto convertSamplerFilter = [](const GltfFilter& filter)
	{
		switch (filter)
		{
		case GltfFilter::Nearest:
			return SamplerFilter::Point;
		case GltfFilter::Linear:
			return SamplerFilter::Linear;
		case GltfFilter::NearestMipMapNearest:
		case GltfFilter::NearestMipMapLinear:
			Platform::Log("LoadScene: Using fallback filter!\n");
			return SamplerFilter::Point;
		case GltfFilter::LinearMipMapNearest:
		case GltfFilter::LinearMipMapLinear:
			Platform::Log("LoadScene: Using fallback filter!\n");
			return SamplerFilter::Linear;
		}
		CHECK(false);
		return SamplerFilter::Linear;
	};
	const auto convertSamplerAddress = [](const GltfAddress& address)
	{
		switch (address)
		{
		case GltfAddress::Repeat:
			return SamplerAddress::Wrap;
		case GltfAddress::ClampToEdge:
			return SamplerAddress::Clamp;
		case GltfAddress::MirroredRepeat:
			return SamplerAddress::Mirror;
		}
		CHECK(false);
		return SamplerAddress::Wrap;
	};

	static const StringView scenes[] =
	{
		"Resources/GLTF/Sponza/Sponza.gltf"_view,
		"Resources/GLTF/Bistro/Bistro.gltf"_view,
	};
	CHECK(sceneIndex < ARRAY_COUNT(scenes));

	const double start = Platform::GetTime();

	UnloadScene();

	GltfScene scene = LoadGltfScene(scenes[sceneIndex]);

	CHECK(scene.Buffers.GetLength() == 1);
	const GltfBuffer& sceneVertexBufferData = scene.Buffers[0];
	SceneVertexBuffer = Device.CreateBuffer("Scene Vertex Buffer"_view, sceneVertexBufferData.Data,
	{
		.Type = BufferType::VertexBuffer,
		.Usage = BufferUsage::Static,
		.Size = sceneVertexBufferData.Size,
	});

	for (const GltfMesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetLength(), RendererAllocator);

		for (const GltfPrimitive& primitive : mesh.Primitives)
		{
			const GltfAccessor positionAccessor = scene.Accessors[primitive.Attributes[GltfAttributeType::Position]];
			CHECK(positionAccessor.ComponentType == GltfComponentType::Float32 && positionAccessor.AccessorType == GltfAccessorType::Vector3);
			const GltfBufferView& positionBufferView = scene.BufferViews[positionAccessor.BufferView];
			CHECK(positionBufferView.Target == GltfTargetType::ArrayBuffer);

			const GltfBuffer& positionBuffer = scene.Buffers[positionBufferView.Buffer];
			const usize positionOffset = positionAccessor.Offset + positionBufferView.Offset;
			const usize positionStride = GetGltfElementSize(positionAccessor.AccessorType, positionAccessor.ComponentType);
			const usize positionSize = positionAccessor.Count * positionStride;
			CHECK(positionOffset + positionSize <= positionBuffer.Size);

			const GltfAccessor textureCoordinateAccessor = scene.Accessors[primitive.Attributes[GltfAttributeType::Texcoord0]];
			CHECK(textureCoordinateAccessor.ComponentType == GltfComponentType::Float32 &&
				  textureCoordinateAccessor.AccessorType == GltfAccessorType::Vector2);
			const GltfBufferView& textureCoordinateBufferView = scene.BufferViews[textureCoordinateAccessor.BufferView];
			CHECK(textureCoordinateBufferView.Target == GltfTargetType::ArrayBuffer);

			const GltfBuffer& textureCoordinateBuffer = scene.Buffers[textureCoordinateBufferView.Buffer];
			const usize textureCoordinateOffset = textureCoordinateAccessor.Offset + textureCoordinateBufferView.Offset;
			const usize textureCoordinateStride = GetGltfElementSize(textureCoordinateAccessor.AccessorType, textureCoordinateAccessor.ComponentType);
			const usize textureCoordinateSize = textureCoordinateAccessor.Count * textureCoordinateStride;
			CHECK(textureCoordinateOffset + textureCoordinateSize <= textureCoordinateBuffer.Size);

			const GltfAccessor indexAccessor = scene.Accessors[primitive.Indices];
			CHECK((indexAccessor.ComponentType == GltfComponentType::Uint16 || indexAccessor.ComponentType == GltfComponentType::Int16 ||
				   indexAccessor.ComponentType == GltfComponentType::Uint32) &&
				   indexAccessor.AccessorType == GltfAccessorType::Scalar);
			const GltfBufferView& indexBufferView = scene.BufferViews[indexAccessor.BufferView];
			CHECK(indexBufferView.Target == GltfTargetType::ElementArrayBuffer);

			const GltfBuffer& indexBuffer = scene.Buffers[indexBufferView.Buffer];
			const usize indexOffset = indexAccessor.Offset + indexBufferView.Offset;
			const usize indexStride = GetGltfElementSize(indexAccessor.AccessorType, indexAccessor.ComponentType);
			const usize indexSize = indexAccessor.Count * indexStride;
			CHECK(indexOffset + indexSize <= indexBuffer.Size);

			primitives.Add(Primitive
			{
				.PositionOffset = positionOffset,
				.PositionSize = positionSize,
				.PositionStride = positionStride,
				.TextureCoordinateOffset = textureCoordinateOffset,
				.TextureCoordinateSize = textureCoordinateSize,
				.TextureCoordinateStride = textureCoordinateStride,
				.IndexOffset = indexOffset,
				.IndexSize = indexSize,
				.IndexStride = indexStride,
				.MaterialIndex = primitive.Material,
			});
		}

		SceneMeshes.Add(Mesh
		{
			.Primitives = Move(primitives),
		});
	}

	for (const GltfMaterial& material : scene.Materials)
	{
		usize samplerIndex = INDEX_NONE;

		Texture primitiveBaseColorTexture;
		if (material.BaseColorTexture != INDEX_NONE)
		{
			const GltfTexture& baseColorTexture = scene.Textures[material.BaseColorTexture];
			const GltfImage& image = scene.Images[baseColorTexture.Image];

			primitiveBaseColorTexture = Device.CreateTexture("Scene Base Color Texture"_view, BarrierLayout::GraphicsQueueCommon,
			{
				.Width = image.Image.Width,
				.Height = image.Image.Height,
				.Type = TextureType::Rectangle,
				.Format = image.Image.Format,
			});
			Device.Write(primitiveBaseColorTexture, image.Image.Data);

			samplerIndex = baseColorTexture.Sampler;
		}

		Sampler primitiveSampler;
		if (samplerIndex != INDEX_NONE)
		{
			const GltfSampler& sampler = scene.Samplers[samplerIndex];

			primitiveSampler = Device.CreateSampler(
			{
				.MinificationFilter = convertSamplerFilter(sampler.MinificationFilter),
				.MagnificationFilter = convertSamplerFilter(sampler.MagnificationFilter),
				.HorizontalAddress = convertSamplerAddress(sampler.HorizontalAddress),
				.VerticalAddress = convertSamplerAddress(sampler.VerticalAddress),
			});
		}

		SceneMaterials.Add(Material
		{
			.BaseColorTexture = primitiveBaseColorTexture,
			.BaseColorFactor = material.BaseColorFactor,
			.Sampler = primitiveSampler,
			.RequiresBlend = material.AlphaMode == GltfAlphaMode::Blend,
			.AlphaCutoff = material.AlphaCutoff,
		});
	}

	for (usize i = 0; i < scene.Nodes.GetLength(); ++i)
	{
		const GltfNode& node = scene.Nodes[i];
		if (node.Mesh == INDEX_NONE)
		{
			continue;
		}

		SceneNodes.Add(Node
		{
			.Transform = CalculateGltfGlobalTransform(scene, i),
			.MeshIndex = node.Mesh,
		});
	}

	CHECK(!scene.DefaultCamera);
	SceneCamera = scene.Camera;

	UnloadGltfScene(&scene);

	InstanceSceneBuffer = Device.CreateBuffer("Instance Scene Buffer"_view,
	{
		.Type = BufferType::ConstantBuffer,
		.Usage = BufferUsage::Stream,
		.Size = sizeof(Hlsl::Scene),
	});

	Array<Hlsl::Node> nodeData(SceneNodes.GetLength(), RendererAllocator);
	for (Node& node : SceneNodes)
	{
		nodeData.Add(Hlsl::Node
		{
			.Transform = node.Transform,
		});
	}
	InstanceNodeBuffer = Device.CreateBuffer("Instance Node Buffer"_view, nodeData.GetData(),
	{
		.Type = BufferType::StructuredBuffer,
		.Usage = BufferUsage::Static,
		.Size = SceneNodes.GetLength() * sizeof(Hlsl::Node),
		.Stride = sizeof(Hlsl::Node),
	});

	const double end = Platform::GetTime();

	Platform::LogFormatted("Scene took %.2fs to load\n", end - start);
}

void Renderer::UnloadScene()
{
	Device.DestroyBuffer(&InstanceSceneBuffer);
	Device.DestroyBuffer(&InstanceNodeBuffer);

	for (Material& material : SceneMaterials)
	{
		Device.DestroyTexture(&material.BaseColorTexture);
		Device.DestroySampler(&material.Sampler);
	}

	Device.DestroyBuffer(&SceneVertexBuffer);

	SceneMeshes.Clear();
	SceneMaterials.Clear();
	SceneNodes.Clear();
}

void Renderer::CreateScreenTextures(uint32 width, uint32 height)
{
	for (usize i = 0; i < FramesInFlight; ++i)
	{
		SwapChainTextures[i] = Device.CreateTexture("SwapChain Render Target"_view, BarrierLayout::Undefined,
		{
			.Width = width,
			.Height = height,
			.Type = TextureType::Rectangle,
			.Format = TextureFormat::Rgba8Srgb,
			.RenderTarget = true,
		},
		Device.GetSwapChainResource(i));
	}
	DepthTexture = Device.CreateTexture("Depth Buffer"_view, BarrierLayout::DepthStencilWrite,
	{
		.Width = width,
		.Height = height,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Depth32,
	});
}

void Renderer::DestroyScreenTextures()
{
	for (Texture& texture : SwapChainTextures)
	{
		Device.DestroyTexture(&texture);
	}
	Device.DestroyTexture(&DepthTexture);
}
