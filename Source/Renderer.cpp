#include "Renderer.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "DrawText.hpp"
#include "GLTF.hpp"

#include "Shaders/Luminance.hlsli"

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
	static constexpr uint8 white[] = { 0xFF, 0xFF, 0xFF, 0xFF };
	Device.Write(WhiteTexture, white);

	DefaultNormalMapTexture = Device.CreateTexture("Default Normal Map Texture"_view, BarrierLayout::GraphicsQueueCommon,
	{
		.Width = 1,
		.Height = 1,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Rgba8Unorm,
		.MipMapCount = 1,
	});
	static constexpr uint8 defaultNormal[] = { 0x7F, 0x7F, 0xFF, 0x00 };
	Device.Write(DefaultNormalMapTexture, defaultNormal);

	DefaultSampler = Device.CreateSampler(
	{
		.MinificationFilter = SamplerFilter::Anisotropic,
		.MagnificationFilter = SamplerFilter::Anisotropic,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});

	LuminanceBuffer = Device.CreateBuffer("Luminance Buffer"_view,
	{
		.Type = BufferType::StorageBuffer,
		.Usage = BufferUsage::Static,
		.Size = LuminanceHistogramBinsCount * sizeof(uint32) + sizeof(float),
	});

	CreatePipelines();
}

Renderer::~Renderer()
{
	UnloadScene();
	DestroyPipelines();

	Device.DestroyBuffer(&LuminanceBuffer);

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
		.PointLightsBufferIndex = ScenePointLightsBuffer.IsValid() ? Device.Get(ScenePointLightsBuffer) : 0,
		.PointLightsCount = static_cast<uint32>(ScenePointLightsBuffer.IsValid() ? ScenePointLightsBuffer.GetCount() : 0),
	};
	Device.Write(SceneBuffer, &sceneData);

	Graphics.Begin();

	Graphics.SetViewport(HdrTexture.GetWidth(), HdrTexture.GetHeight());

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		HdrTexture
	);

	Graphics.SetPipeline(&SceneOpaquePipeline);

	Graphics.ClearRenderTarget(HdrTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture);

	Graphics.SetRenderTarget(HdrTexture, DepthTexture);

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
		LuminanceBuffer
	);

	Hlsl::LuminanceHistogramRootConstants luminanceHistogramRootConstants =
	{
		.LuminanceBufferIndex = Device.Get(LuminanceBuffer),
		.HdrTextureIndex = Device.Get(HdrTexture, ViewType::ShaderResource),
	};

	Graphics.SetPipeline(&LuminanceHistogramPipeline);
	Graphics.SetRootConstants(&luminanceHistogramRootConstants);
	Graphics.Dispatch((HdrTexture.GetWidth() + 15) / 16, (HdrTexture.GetHeight() + 15) / 16, 1);

	Graphics.BufferBarrier
	(
		{ BarrierStage::ComputeShading, BarrierStage::ComputeShading },
		{ BarrierAccess::UnorderedAccess, BarrierAccess::UnorderedAccess },
		LuminanceBuffer
	);

	Hlsl::LuminanceAverageRootConstants luminanceAverageRootConstants =
	{
		.LuminanceBufferIndex = Device.Get(LuminanceBuffer),
		.PixelCount = HdrTexture.GetWidth() * HdrTexture.GetHeight(),
	};

	Graphics.SetPipeline(&LuminanceAveragePipeline);
	Graphics.SetRootConstants(&luminanceAverageRootConstants);
	Graphics.Dispatch(LuminanceHistogramBinsCount, 1, 1);

	const Texture& swapChainTexture = SwapChainTextures[Device.GetFrameIndex()];

	Graphics.BufferBarrier
	(
		{ BarrierStage::ComputeShading, BarrierStage::PixelShading },
		{ BarrierAccess::UnorderedAccess, BarrierAccess::UnorderedAccess },
		LuminanceBuffer
	);

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		swapChainTexture
	);

	Graphics.SetRenderTarget(swapChainTexture);
	Graphics.SetViewport(swapChainTexture.GetWidth(), swapChainTexture.GetHeight());

	Hlsl::ToneMapRootConstants toneMapRootConstants =
	{
		.HdrTextureIndex = Device.Get(HdrTexture, ViewType::ShaderResource),
		.DefaultSamplerIndex = Device.Get(DefaultSampler),
		.LuminanceBufferIndex = Device.Get(LuminanceBuffer),
		.DebugViewMode = viewMode != ViewMode::Lit,
	};

	Graphics.SetPipeline(&ToneMapPipeline);
	Graphics.SetRootConstants(&toneMapRootConstants);
	Graphics.Draw(3);

#if !RELEASE
	UpdateFrameTimes(startCpuTime);
#endif

	DrawText::Get().Submit(&Graphics, swapChainTexture.GetWidth(), swapChainTexture.GetHeight());

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
			.BaseColorOrDiffuseTextureIndex = Device.Get(baseColorOrDiffuseTexture,
														 ViewType::ShaderResource),
			.BaseColorOrDiffuseFactor = material.IsSpecularGlossiness ?
											material.SpecularGlossiness.DiffuseFactor :
											material.MetallicRoughness.BaseColorFactor,
			.NormalMapTextureIndex = Device.Get(material.NormalMapTexture.IsValid() ? material.NormalMapTexture : DefaultNormalMapTexture,
												ViewType::ShaderResource),
			.MetallicRoughnessOrSpecularGlossinessTextureIndex = Device.Get(metallicRoughnessOrSpecularGlossinessTexture,
												ViewType::ShaderResource),
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

	bool hasDirectionalLight = false;
	Hlsl::DirectionalLight directionalLight;
	Array<Hlsl::PointLight> pointLights(RendererAllocator);

	for (const GltfLight& light : scene.Lights)
	{
		Vector translation;
		Quaternion orientation;
		DecomposeTransform(light.Transform, &translation, &orientation, nullptr);

		static const Vector defaultLightDirection = Vector { +0.0f, +0.0f, -1.0f };
		const Vector direction = -orientation.Rotate(defaultLightDirection);

		if (light.Type == GltfLightType::Directional)
		{
			CHECK(!hasDirectionalLight);
			hasDirectionalLight = true;

			directionalLight = Hlsl::DirectionalLight
			{
				.Color = light.Color,
				.IntensityLux = light.Intensity,
				.Direction = { direction.X, direction.Y, direction.Z },
			};
		}
		else if (light.Type == GltfLightType::Point)
		{
			pointLights.Add(Hlsl::PointLight
			{
				.Color = light.Color,
				.IntensityCandela = light.Intensity,
				.Position = { translation.X, translation.Y, translation.Z },
			});
		}
	}
	if (!hasDirectionalLight)
	{
		directionalLight = Hlsl::DirectionalLight
		{
			.Color = { 1.0f, 1.0f, 1.0f },
			.IntensityLux = 1.0f,
			.Direction = { +0.0f, +1.0f, +0.0f },
		};
	}

	SceneDirectionalLightBuffer = Device.CreateBuffer("Scene Directional Light Buffer"_view, &directionalLight,
	{
		.Type = BufferType::ConstantBuffer,
		.Usage = BufferUsage::Static,
		.Size = sizeof(directionalLight),
	});
	if (!pointLights.IsEmpty())
	{
		ScenePointLightsBuffer = Device.CreateBuffer("Scene Point Lights Buffer"_view, pointLights.GetData(),
		{
			.Type = BufferType::StructuredBuffer,
			.Usage = BufferUsage::Static,
			.Size = pointLights.GetDataSize(),
			.Stride = pointLights.GetElementSize(),
		});
	}
}

void Renderer::UnloadScene()
{
	Device.DestroyBuffer(&SceneBuffer);
	Device.DestroyBuffer(&SceneVertexBuffer);
	Device.DestroyBuffer(&SceneNodeBuffer);
	Device.DestroyBuffer(&SceneMaterialBuffer);
	Device.DestroyBuffer(&SceneDirectionalLightBuffer);
	Device.DestroyBuffer(&ScenePointLightsBuffer);

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
	const auto createGraphicsPipeline = [this](StringView name,
											   StringView path,
											   TextureFormat format,
											   bool alphaBlend,
											   bool depth) -> GraphicsPipeline
	{
		Shader vertex = Device.CreateShader(
		{
			.Stage = ShaderStage::Vertex,
			.FilePath = path,
		});
		Shader pixel = Device.CreateShader(
		{
			.Stage = ShaderStage::Pixel,
			.FilePath = path,
		});
		ShaderStages stages;
		stages.AddStage(vertex);
		stages.AddStage(pixel);

		const GraphicsPipeline pipeline = Device.CreatePipeline(name,
		{
			.Stages = Move(stages),
			.RenderTargetFormat = format,
			.DepthFormat = depth ? TextureFormat::Depth32 : TextureFormat::None,
			.AlphaBlend = alphaBlend,
		});
		Device.DestroyShader(&vertex);
		Device.DestroyShader(&pixel);

		return pipeline;
	};
	const auto createComputePipeline = [this](StringView name, StringView path) -> ComputePipeline
	{
		Shader compute = Device.CreateShader(
		{
			.Stage = ShaderStage::Compute,
			.FilePath = path,
		});

		const ComputePipeline pipeline = Device.CreatePipeline(name,
		{
			.Stage = Move(compute),
		});
		Device.DestroyShader(&compute);

		return pipeline;
	};

	SceneOpaquePipeline = createGraphicsPipeline("Scene Opaque Pipeline"_view,
												 "Shaders/Scene.hlsl"_view,
												 HdrFormat,
												 false,
												 true);
	SceneBlendPipeline = createGraphicsPipeline("Scene Blend Pipeline"_view,
												"Shaders/Scene.hlsl"_view,
												HdrFormat,
												true,
												true);

	ToneMapPipeline = createGraphicsPipeline("Tone Map Pipeline"_view,
											 "Shaders/ToneMap.hlsl"_view,
											 TextureFormat::Rgba8SrgbUnorm,
											 false,
											 false);

	LuminanceHistogramPipeline = createComputePipeline("Luminance Histogram Pipeline"_view, "Shaders/LuminanceHistogram.hlsl"_view);
	LuminanceAveragePipeline = createComputePipeline("Luminance Average Pipeline"_view, "Shaders/LuminanceAverage.hlsl"_view);
}

void Renderer::DestroyPipelines()
{
	Device.DestroyPipeline(&ToneMapPipeline);

	Device.DestroyPipeline(&LuminanceHistogramPipeline);
	Device.DestroyPipeline(&LuminanceAveragePipeline);

	Device.DestroyPipeline(&SceneOpaquePipeline);
	Device.DestroyPipeline(&SceneBlendPipeline);
}

void Renderer::CreateScreenTextures(uint32 width, uint32 height)
{
	for (usize i = 0; i < FramesInFlight; ++i)
	{
		SwapChainTextures[i] = Device.CreateTexture("SwapChain Render Target Texture"_view, BarrierLayout::Undefined,
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
	DepthTexture = Device.CreateTexture("Depth Texture"_view, BarrierLayout::DepthStencilWrite,
	{
		.Width = width,
		.Height = height,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Depth32,
		.MipMapCount = 1,
	});

	HdrTexture = Device.CreateTexture("HDR Texture"_view, BarrierLayout::RenderTarget,
	{
		.Width = width,
		.Height = height,
		.Type = TextureType::Rectangle,
		.Format = HdrFormat,
		.MipMapCount = 1,
		.RenderTarget = true,
		.Storage = true,
	});
}

void Renderer::DestroyScreenTextures()
{
	for (Texture& texture : SwapChainTextures)
	{
		Device.DestroyTexture(&texture);
	}
	Device.DestroyTexture(&DepthTexture);

	Device.DestroyTexture(&HdrTexture);
}
