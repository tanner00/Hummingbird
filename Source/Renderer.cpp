#include "Renderer.hpp"
#include "DrawText.hpp"

Renderer::Renderer(const Platform::Window* window)
	: Device(window)
	, Graphics(Device.CreateGraphicsContext())
{
	CreateScreenTextures(window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&Device);

	LoadScene(0);

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

	ScenePipeline = Device.CreateGraphicsPipeline("Scene Pipeline"_view,
	{
		.Stages = Move(stages),
		.RenderTargetFormat = TextureFormat::Rgba8Srgb,
		.DepthFormat = TextureFormat::Depth32,
		.AlphaBlend = false,
	});

	Device.DestroyShader(&vertex);
	Device.DestroyShader(&pixel);
}

Renderer::~Renderer()
{
	Device.DestroyGraphicsPipeline(&ScenePipeline);

	Device.DestroyBuffer(&InstanceSceneBuffer);
	Device.DestroyBuffer(&InstanceNodeBuffer);

	Device.DestroyBuffer(&SceneVertexBuffer);

	DrawText::Get().Shutdown();

	DestroyScreenTextures();
}

void Renderer::Update()
{
	if (IsKeyPressedOnce(Key::One))
	{
		LoadScene(0);
	}
	else if (IsKeyPressedOnce(Key::Two))
	{
		LoadScene(1);
	}

	const Matrix view = SceneCamera.Transform.GetInverse();
	const Matrix projection = Matrix::Perspective(SceneCamera.FieldOfViewYDegrees, SceneCamera.AspectRatio, SceneCamera.NearZ, SceneCamera.FarZ);
	const Hlsl::Scene instanceSceneData =
	{
		.ViewProjection = projection * view,
		.NodeBuffer = Device.Get(InstanceNodeBuffer),
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

	Graphics.ClearRenderTarget(frameTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture);

	Graphics.SetRenderTarget(frameTexture, DepthTexture);

	Graphics.SetGraphicsPipeline(&ScenePipeline);

	Graphics.SetConstantBuffer("Scene"_view, InstanceSceneBuffer);

	for (usize i = 0; i < SceneNodes.GetLength(); ++i)
	{
		const Hlsl::SceneRootConstants rootConstants =
		{
			.NodeIndex = static_cast<uint32>(i),
		};
		Graphics.SetRootConstants("RootConstants"_view, &rootConstants);

		const Node& node = SceneNodes[i];
		const Mesh& mesh = SceneMeshes[node.Mesh];

		for (const Primitive& primitive : mesh.Primitives)
		{
			Graphics.SetVertexBuffer(SceneVertexBuffer, 0, primitive.PositionOffset, primitive.PositionSize, primitive.PositionStride);
			Graphics.SetIndexBuffer(SceneVertexBuffer, primitive.IndexOffset, primitive.IndexSize, primitive.IndexStride);

			Graphics.DrawIndexed(primitive.IndexSize / primitive.IndexStride);
		}
	}

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
	static const StringView scenes[] =
	{
		"Resources/GLTF/Sponza/Sponza.gltf"_view,
		"Resources/GLTF/Bistro/Bistro.gltf"_view,
	};
	CHECK(sceneIndex < ARRAY_COUNT(scenes));

	Device.DestroyBuffer(&InstanceSceneBuffer);
	Device.DestroyBuffer(&InstanceNodeBuffer);

	Device.DestroyBuffer(&SceneVertexBuffer);

	SceneMeshes.Clear();
	SceneNodes.Clear();

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
		Array<Primitive> primitives { mesh.Primitives.GetLength(), &GlobalAllocator::Get() };

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
				.IndexOffset = indexOffset,
				.IndexSize = indexSize,
				.IndexStride = indexStride,
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
			.Mesh = node.Mesh,
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

	Array<Hlsl::Node> nodeData(SceneNodes.GetLength(), &GlobalAllocator::Get());
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
