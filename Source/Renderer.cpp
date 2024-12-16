#include "Renderer.hpp"
#include "CameraController.hpp"
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

	WhiteTexture = Device.CreateTexture("White Texture"_view, BarrierLayout::GraphicsQueueCommon,
	{
		.Width = 1,
		.Height = 1,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Rgba8Srgb,
	});
	static constexpr usize white = 0xFFFFFFFF;
	Device.Write(WhiteTexture, &white);

	DefaultSampler = Device.CreateSampler(
	{
		.MinificationFilter = SamplerFilter::Linear,
		.MagnificationFilter = SamplerFilter::Linear,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});

	CreatePipelines();
}

Renderer::~Renderer()
{
	UnloadScene();
	DestroyPipelines();

	Device.DestroySampler(&DefaultSampler);
	Device.DestroyTexture(&WhiteTexture);

	DrawText::Get().Shutdown();

	DestroyScreenTextures();
}

void Renderer::Update(const CameraController& cameraController)
{
#if !RELEASE
	const double startCpuTime = Platform::GetTime();
#endif

	static uint32 geometryView = 0;
#if !RELEASE
	if (IsKeyPressedOnce(Key::G))
	{
		geometryView = !geometryView;
	}

	if (IsKeyPressedOnce(Key::R))
	{
		DestroyPipelines();
		CreatePipelines();
	}
#endif

	const Matrix view = cameraController.GetTransform().GetInverse();
	const Matrix projection = Matrix::Perspective
	(
		cameraController.GetFieldOfViewYRadians(),
		cameraController.GetAspectRatio(),
		cameraController.GetNearZ(),
		cameraController.GetFarZ()
	);
	const Hlsl::Scene sceneData =
	{
		.ViewProjection = projection * view,
		.NodeBufferIndex = Device.Get(SceneNodeBuffer),
		.MaterialBufferIndex = Device.Get(SceneMaterialBuffer),
	};
	Device.Write(SceneBuffer, &sceneData);

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

	for (usize i = 0; i < SceneNodes.GetLength(); ++i)
	{
		const Node& node = SceneNodes[i];
		const Mesh& mesh = SceneMeshes[node.MeshIndex];

		for (const Primitive& primitive : mesh.Primitives)
		{
			const Hlsl::SceneRootConstants rootConstants =
			{
				.NodeIndex = static_cast<uint32>(i),
				.MaterialIndex = static_cast<uint32>(primitive.MaterialIndex),
				.GeometryView = geometryView,
			};

			const bool requiresBlend = (primitive.MaterialIndex != INDEX_NONE) ? SceneMaterials[primitive.MaterialIndex].RequiresBlend : false;
			if (requiresBlend)
			{
				Graphics.SetGraphicsPipeline(&SceneBlendPipeline);
			}
			else
			{
				Graphics.SetGraphicsPipeline(&SceneOpaquePipeline);
			}

			Graphics.SetConstantBuffer("Scene"_view, SceneBuffer);

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

void Renderer::LoadScene(const GltfScene& scene)
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

	UnloadScene();

	for (const GltfMesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetLength(), RendererAllocator);

		for (const GltfPrimitive& primitive : mesh.Primitives)
		{
			const GltfAccessorView positionView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Position]);
			const GltfAccessorView textureCoordinateView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Texcoord0]);
			const GltfAccessorView indexView = GetGltfAccessorView(scene, primitive.Indices);

			primitives.Add(Primitive
			{
				.PositionOffset = positionView.Offset,
				.PositionStride = positionView.Stride,
				.PositionSize = positionView.Size,
				.TextureCoordinateOffset = textureCoordinateView.Offset,
				.TextureCoordinateStride = textureCoordinateView.Stride,
				.TextureCoordinateSize = textureCoordinateView.Size,
				.IndexOffset = indexView.Offset,
				.IndexStride = indexView.Stride,
				.IndexSize = indexView.Size,
				.MaterialIndex = primitive.Material,
			});
		}

		SceneMeshes.Add(Mesh
		{
			.Primitives = Move(primitives),
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

	for (const GltfMaterial& material : scene.Materials)
	{
		usize baseColorSamplerIndex = INDEX_NONE;

		Texture primitiveBaseColorTexture = WhiteTexture;
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

			baseColorSamplerIndex = baseColorTexture.Sampler;
		}

		Sampler primitiveBaseColorSampler = DefaultSampler;
		if (baseColorSamplerIndex != INDEX_NONE)
		{
			const GltfSampler& sampler = scene.Samplers[baseColorSamplerIndex];

			primitiveBaseColorSampler = Device.CreateSampler(
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
			.BaseColorSampler = primitiveBaseColorSampler,
			.BaseColorFactor = material.BaseColorFactor,
			.RequiresBlend = material.AlphaMode == GltfAlphaMode::Blend,
			.AlphaCutoff = material.AlphaCutoff,
		});
	}

	SceneBuffer = Device.CreateBuffer("Scene Buffer"_view,
	{
		.Type = BufferType::ConstantBuffer,
		.Usage = BufferUsage::Stream,
		.Size = sizeof(Hlsl::Scene),
	});

	CHECK(scene.Buffers.GetLength() == 1);
	const GltfBuffer& vertexBufferData = scene.Buffers[0];
	SceneVertexBuffer = Device.CreateBuffer("Scene Vertex Buffer"_view, vertexBufferData.Data,
	{
		.Type = BufferType::VertexBuffer,
		.Usage = BufferUsage::Static,
		.Size = vertexBufferData.Size,
	});

	Array<Hlsl::Node> nodeData(SceneNodes.GetLength(), RendererAllocator);
	for (Node& node : SceneNodes)
	{
		nodeData.Add(Hlsl::Node
		{
			.Transform = node.Transform,
		});
	}
	SceneNodeBuffer = Device.CreateBuffer("Scene Node Buffer"_view, nodeData.GetData(),
	{
		.Type = BufferType::StructuredBuffer,
		.Usage = BufferUsage::Static,
		.Size = SceneNodes.GetLength() * sizeof(Hlsl::Node),
		.Stride = sizeof(Hlsl::Node),
	});

	Array<Hlsl::Material> materialData(SceneMaterials.GetLength(), RendererAllocator);
	for (Material& material : SceneMaterials)
	{
		materialData.Add(Hlsl::Material
		{
			.BaseColorTextureIndex = Device.Get(material.BaseColorTexture),
			.BaseColorSamplerIndex = Device.Get(material.BaseColorSampler),
			.BaseColorFactor = material.BaseColorFactor,
			.AlphaCutoff = material.AlphaCutoff,
		});
	}
	SceneMaterialBuffer = Device.CreateBuffer("Scene Material Buffer"_view, materialData.GetData(),
	{
		.Type = BufferType::StructuredBuffer,
		.Usage = BufferUsage::Static,
		.Size = SceneMaterials.GetLength() * sizeof(Hlsl::Material),
		.Stride = sizeof(Hlsl::Material),
	});
}

void Renderer::UnloadScene()
{
	Device.DestroyBuffer(&SceneBuffer);
	Device.DestroyBuffer(&SceneVertexBuffer);
	Device.DestroyBuffer(&SceneNodeBuffer);
	Device.DestroyBuffer(&SceneMaterialBuffer);

	for (Material& material : SceneMaterials)
	{
		if (material.BaseColorTexture != WhiteTexture)
		{
			Device.DestroyTexture(&material.BaseColorTexture);
		}
		if (material.BaseColorSampler != DefaultSampler)
		{
			Device.DestroySampler(&material.BaseColorSampler);
		}
	}

	SceneMeshes.Clear();
	SceneMaterials.Clear();
	SceneNodes.Clear();
}

void Renderer::CreatePipelines()
{
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

void Renderer::DestroyPipelines()
{
	Device.DestroyGraphicsPipeline(&SceneOpaquePipeline);
	Device.DestroyGraphicsPipeline(&SceneBlendPipeline);
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
