#include "Renderer.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "DrawText.hpp"
#include "GLTF.hpp"

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
		.Format = TextureFormat::Rgba8SrgbUnorm,
		.MipMapCount = 1,
	});
	static constexpr usize white = 0xFFFFFFFF;
	Device.Write(WhiteTexture, &white);

	DefaultNormalMapTexture = Device.CreateTexture("Default Normal Map Texture"_view, BarrierLayout::GraphicsQueueCommon,
	{
		.Width = 1,
		.Height = 1,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Rgba8Unorm,
		.MipMapCount = 1,
	});
	static constexpr usize defaultNormal = 0x7F7FFF;
	Device.Write(DefaultNormalMapTexture, &defaultNormal);

	DefaultSampler = Device.CreateSampler(
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

	Device.DestroySampler(&DefaultSampler);
	Device.DestroyTexture(&DefaultNormalMapTexture);
	Device.DestroyTexture(&WhiteTexture);

	DrawText::Get().Shutdown();

	DestroyScreenTextures();
}

void Renderer::Update(const CameraController& cameraController)
{
#if !RELEASE
	const double startCpuTime = Platform::GetTime();
#endif

	static ViewMode viewMode = ViewMode::Lit;
#if !RELEASE
	if (IsKeyPressedOnce(Key::L))
	{
		viewMode = ViewMode::Lit;
	}
	if (IsKeyPressedOnce(Key::U))
	{
		viewMode = ViewMode::Unlit;
	}
	if (IsKeyPressedOnce(Key::G))
	{
		viewMode = ViewMode::Geometry;
	}
	if (IsKeyPressedOnce(Key::N))
	{
		viewMode = ViewMode::Normal;
	}

	if (IsKeyPressedOnce(Key::R))
	{
		Device.WaitForIdle();
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
	const Vector viewPosition = cameraController.GetPosition();
	const Hlsl::Scene sceneData =
	{
		.ViewProjection = projection * view,
		.ViewPosition = Float3 { viewPosition.X, viewPosition.Y, viewPosition.Z },
		.DefaultSamplerIndex = Device.Get(DefaultSampler),
		.NodeBufferIndex = Device.Get(SceneNodeBuffer),
		.MaterialBufferIndex = Device.Get(SceneMaterialBuffer),
		.DirectionalLightBufferIndex = Device.Get(SceneDirectionalLightBuffer),
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

	Graphics.SetPipeline(&SceneOpaquePipeline);

	Graphics.ClearRenderTarget(frameTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture);

	Graphics.SetRenderTarget(frameTexture, DepthTexture);

	for (usize i = 0; i < SceneNodes.GetLength(); ++i)
	{
		const Node& node = SceneNodes[i];
		const Mesh& mesh = SceneMeshes[node.MeshIndex];

		const Matrix normalTransform = node.Transform.GetInverse().GetTranspose();

		for (const Primitive& primitive : mesh.Primitives)
		{
			const Hlsl::SceneRootConstants rootConstants =
			{
				.NodeIndex = static_cast<uint32>(i),
				.MaterialIndex = static_cast<uint32>(primitive.MaterialIndex),
				.ViewMode = viewMode,
				.NormalTransform = normalTransform,
			};

			const bool requiresBlend = (primitive.MaterialIndex != INDEX_NONE) ? SceneMaterials[primitive.MaterialIndex].RequiresBlend : false;
			if (requiresBlend)
			{
				Graphics.SetPipeline(&SceneBlendPipeline);
			}
			else
			{
				Graphics.SetPipeline(&SceneOpaquePipeline);
			}

			Graphics.SetConstantBuffer("Scene"_view, SceneBuffer);

			Graphics.SetRootConstants(&rootConstants);

			Graphics.SetVertexBuffer(SceneVertexBuffer, 0, primitive.PositionOffset, primitive.PositionSize, primitive.PositionStride);
			Graphics.SetVertexBuffer(SceneVertexBuffer, 1, primitive.TextureCoordinateOffset, primitive.TextureCoordinateSize, primitive.TextureCoordinateStride);
			Graphics.SetVertexBuffer(SceneVertexBuffer, 2, primitive.NormalOffset, primitive.NormalSize, primitive.NormalStride);
			Graphics.SetVertexBuffer(SceneVertexBuffer, 3, primitive.TangentOffset, primitive.TangentSize, primitive.TangentStride);
			Graphics.SetIndexBuffer(SceneVertexBuffer, primitive.IndexOffset, primitive.IndexSize, primitive.IndexStride);

			Graphics.DrawIndexed(primitive.IndexSize / primitive.IndexStride);
		}
	}

#if !RELEASE
	UpdateFrameTimes(startCpuTime);
#endif

	DrawText::Get().Submit(&Graphics, frameTexture.GetWidth(), frameTexture.GetHeight());

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

void Renderer::LoadScene(const GltfScene& scene)
{
	UnloadScene();

	VERIFY(scene.Buffers.GetLength() == 1, "GLTF file contains multiple buffers!");
	const GltfBuffer& vertexBuffer = scene.Buffers[0];

	Array<Array<Float4>> computedTangents = Array<Array<Float4>> { RendererAllocator };
	usize computedTangentsCount = 0;

	for (const GltfMesh& mesh : scene.Meshes)
	{
		Array<Primitive> primitives(mesh.Primitives.GetLength(), RendererAllocator);

		for (const GltfPrimitive& primitive : mesh.Primitives)
		{
			const GltfAccessorView positionView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Position]);
			const GltfAccessorView textureCoordinateView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Texcoord0]);
			const GltfAccessorView normalView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Normal]);
			const GltfAccessorView indexView = GetGltfAccessorView(scene, primitive.Indices);

			GltfAccessorView tangentView;
			if (primitive.Attributes.Contains(GltfAttributeType::Tangent))
			{
				tangentView = GetGltfAccessorView(scene, primitive.Attributes[GltfAttributeType::Tangent]);
			}
			else
			{
				Platform::LogFormatted("LoadScene: Missing tangents from primitive!\n");

				const usize tangentsCount = normalView.Size / normalView.Stride;
				Array<Float4> primitiveTangents = Array<Float4> { tangentsCount, RendererAllocator };
				for (usize i = 0; i < tangentsCount; ++i)
				{
					static constexpr Float4 invalidTangent = { 0.0f, 0.0f, 0.0f, 0.0f };
					primitiveTangents.Add(invalidTangent);
				}

				tangentView = GltfAccessorView
				{
					.Offset = vertexBuffer.Size + computedTangentsCount * sizeof(Float4),
					.Stride = primitiveTangents.GetElementSize(),
					.Size = primitiveTangents.GetDataSize(),
				};
				computedTangentsCount += tangentsCount;

				computedTangents.Emplace(Move(primitiveTangents));
			}

			primitives.Add(Primitive
			{
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
		const auto convertTexture = [this](const GltfScene& scene, StringView textureName, usize textureIndex) -> Texture
		{
			if (textureIndex == INDEX_NONE)
				return Texture::Invalid();

			const GltfTexture& texture = scene.Textures[textureIndex];
			const GltfImage& image = scene.Images[texture.Image];

			DdsImage loadedImage = LoadDdsImage(image.Path.AsView());
			const Texture loadedTexture = Device.CreateTexture(textureName, BarrierLayout::GraphicsQueueCommon,
			{
				.Width = loadedImage.Width,
				.Height = loadedImage.Height,
				.Type = TextureType::Rectangle,
				.Format = loadedImage.Format,
				.MipMapCount = loadedImage.MipMapCount,
			});
			Device.Write(loadedTexture, loadedImage.Data);
			UnloadDdsImage(&loadedImage);

			return loadedTexture;
		};

		Material loadedMaterial =
		{
			.NormalMapTexture = convertTexture(scene, "Scene Normal Map Texture"_view, material.NormalMapTexture),
			.IsSpecularGlossiness = material.IsSpecularGlossiness,
			.RequiresBlend = material.AlphaMode == GltfAlphaMode::Blend,
			.AlphaCutoff = material.AlphaCutoff,
		};
		if (material.IsSpecularGlossiness)
		{
			loadedMaterial.SpecularGlossiness.DiffuseTexture = convertTexture(scene, "Scene Diffuse Texture"_view,
																			  material.SpecularGlossiness.DiffuseTexture);
			loadedMaterial.SpecularGlossiness.DiffuseFactor = material.SpecularGlossiness.DiffuseFactor;

			loadedMaterial.SpecularGlossiness.SpecularGlossinessTexture = convertTexture(scene, "Scene Specular Glossiness Texture"_view,
																						 material.SpecularGlossiness.SpecularGlossinessTexture);
			loadedMaterial.SpecularGlossiness.SpecularFactor = material.SpecularGlossiness.SpecularFactor;
			loadedMaterial.SpecularGlossiness.GlossinessFactor = material.SpecularGlossiness.GlossinessFactor;
		}
		else
		{
			loadedMaterial.MetallicRoughness.BaseColorTexture = convertTexture(scene, "Scene Base Color Texture"_view,
																			  material.MetallicRoughness.BaseColorTexture);
			loadedMaterial.MetallicRoughness.BaseColorFactor = material.MetallicRoughness.BaseColorFactor;

			loadedMaterial.MetallicRoughness.MetallicRoughnessTexture = convertTexture(scene, "Scene Metallic Roughness Texture"_view,
																					   material.MetallicRoughness.MetallicRoughnessTexture);
			loadedMaterial.MetallicRoughness.MetallicFactor = material.MetallicRoughness.MetallicFactor;
			loadedMaterial.MetallicRoughness.RoughnessFactor = material.MetallicRoughness.RoughnessFactor;
		}
		SceneMaterials.Add(loadedMaterial);
	}

	SceneBuffer = Device.CreateBuffer("Scene Buffer"_view,
	{
		.Type = BufferType::ConstantBuffer,
		.Usage = BufferUsage::Stream,
		.Size = sizeof(Hlsl::Scene),
	});

	GltfBuffer finalVertexBuffer = vertexBuffer;
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
	SceneVertexBuffer = Device.CreateBuffer("Scene Vertex Buffer"_view, finalVertexBuffer.Data,
	{
		.Type = BufferType::VertexBuffer,
		.Usage = BufferUsage::Static,
		.Size = finalVertexBuffer.Size,
	});
	if (!computedTangents.IsEmpty())
	{
		RendererAllocator->Deallocate(finalVertexBuffer.Data, finalVertexBuffer.Size);
	}

	Array<Hlsl::Node> nodeData(SceneNodes.GetLength(), RendererAllocator);
	for (const Node& node : SceneNodes)
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
	for (const Material& material : SceneMaterials)
	{
		Texture baseColorOrDiffuseTexture = WhiteTexture;
		if (material.IsSpecularGlossiness && material.SpecularGlossiness.DiffuseTexture.IsValid())
		{
			baseColorOrDiffuseTexture = material.SpecularGlossiness.DiffuseTexture;
		}
		else if (!material.IsSpecularGlossiness && material.MetallicRoughness.BaseColorTexture.IsValid())
		{
			baseColorOrDiffuseTexture = material.MetallicRoughness.BaseColorTexture;
		}

		Texture metallicRoughnessOrSpecularGlossinessTexture = WhiteTexture;
		if (material.IsSpecularGlossiness && material.SpecularGlossiness.SpecularGlossinessTexture.IsValid())
		{
			metallicRoughnessOrSpecularGlossinessTexture = material.SpecularGlossiness.SpecularGlossinessTexture;
		}
		else if (!material.IsSpecularGlossiness && material.MetallicRoughness.MetallicRoughnessTexture.IsValid())
		{
			metallicRoughnessOrSpecularGlossinessTexture = material.MetallicRoughness.MetallicRoughnessTexture;
		}

		materialData.Add(Hlsl::Material
		{
			.BaseColorOrDiffuseTextureIndex = Device.Get(baseColorOrDiffuseTexture),
			.BaseColorOrDiffuseFactor = material.IsSpecularGlossiness ?
											material.SpecularGlossiness.DiffuseFactor :
											material.MetallicRoughness.BaseColorFactor,
			.NormalMapTextureIndex = Device.Get(material.NormalMapTexture.IsValid() ? material.NormalMapTexture : DefaultNormalMapTexture),
			.MetallicRoughnessOrSpecularGlossinessTextureIndex = Device.Get(metallicRoughnessOrSpecularGlossinessTexture),
			.MetallicOrSpecularFactor = material.IsSpecularGlossiness ?
											material.SpecularGlossiness.SpecularFactor :
											Float3 { material.MetallicRoughness.MetallicFactor, 0.0f, 0.0f },
			.RoughnessOrGlossinessFactor = material.IsSpecularGlossiness ?
											material.SpecularGlossiness.GlossinessFactor :
											material.MetallicRoughness.RoughnessFactor,
			.IsSpecularGlossiness = material.IsSpecularGlossiness,
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

	Hlsl::DirectionalLight directionalLight;
	if (scene.DirectionalLights.IsEmpty())
	{
		directionalLight = Hlsl::DirectionalLight
		{
			.Direction = { +0.0f, +1.0f, +0.0f },
		};
	}
	else
	{
		CHECK(scene.DirectionalLights.GetLength() == 1);

		Quaternion lightOrientation;
		DecomposeTransform(scene.DirectionalLights[0].Transform, nullptr, &lightOrientation, nullptr);

		static const Vector defaultLightDirection = Vector { +0.0f, +0.0f, -1.0f };
		const Vector lightDirection = -lightOrientation.Rotate(defaultLightDirection);

		directionalLight = Hlsl::DirectionalLight
		{
			.Direction = { lightDirection.X, lightDirection.Y, lightDirection.Z },
		};
	}
	SceneDirectionalLightBuffer = Device.CreateBuffer("Scene Directional Light"_view, &directionalLight,
	{
		.Type = BufferType::ConstantBuffer,
		.Usage = BufferUsage::Static,
		.Size = sizeof(directionalLight),
	});
}

void Renderer::UnloadScene()
{
	Device.DestroyBuffer(&SceneBuffer);
	Device.DestroyBuffer(&SceneVertexBuffer);
	Device.DestroyBuffer(&SceneNodeBuffer);
	Device.DestroyBuffer(&SceneMaterialBuffer);
	Device.DestroyBuffer(&SceneDirectionalLightBuffer);

	for (Material& material : SceneMaterials)
	{
		Device.DestroyTexture(&material.NormalMapTexture);

		if (material.IsSpecularGlossiness)
		{
			Device.DestroyTexture(&material.SpecularGlossiness.DiffuseTexture);
			Device.DestroyTexture(&material.SpecularGlossiness.SpecularGlossinessTexture);
		}
		else
		{
			Device.DestroyTexture(&material.MetallicRoughness.BaseColorTexture);
			Device.DestroyTexture(&material.MetallicRoughness.MetallicRoughnessTexture);
		}
	}

	SceneMeshes.Clear();
	SceneMaterials.Clear();
	SceneNodes.Clear();
}

void Renderer::CreatePipelines()
{
	const auto createPipeline = [this](StringView shaderPath, bool alphaBlend) -> GraphicsPipeline
	{
		Shader vertex = Device.CreateShader(
		{
			.Stage = ShaderStage::Vertex,
			.FilePath = shaderPath,
		});
		Shader pixel = Device.CreateShader(
		{
			.Stage = ShaderStage::Pixel,
			.FilePath = shaderPath,
		});
		ShaderStages stages;
		stages.AddStage(vertex);
		stages.AddStage(pixel);

		const GraphicsPipeline pipeline = Device.CreatePipeline("Scene Pipeline"_view,
		{
			.Stages = Move(stages),
			.RenderTargetFormat = TextureFormat::Rgba8SrgbUnorm,
			.DepthFormat = TextureFormat::Depth32,
			.AlphaBlend = alphaBlend,
		});
		Device.DestroyShader(&vertex);
		Device.DestroyShader(&pixel);

		return pipeline;
	};

	SceneOpaquePipeline = createPipeline("Shaders/Scene.hlsl"_view, false);
	SceneBlendPipeline = createPipeline("Shaders/Scene.hlsl"_view, true);
}

void Renderer::DestroyPipelines()
{
	Device.DestroyPipeline(&SceneOpaquePipeline);
	Device.DestroyPipeline(&SceneBlendPipeline);
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
			.Format = TextureFormat::Rgba8SrgbUnorm,
			.MipMapCount = 1,
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
		.MipMapCount = 1,
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
