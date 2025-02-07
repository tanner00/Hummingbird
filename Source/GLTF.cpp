#include "GLTF.hpp"
#include "JSON.hpp"

static Allocator* GltfAllocator = &GlobalAllocator::Get();

static String ResolveFilePath(StringView sceneFilePath, StringView filePath)
{
	static constexpr char pathSeparator = '/';

	const usize directoryLength = sceneFilePath.ReverseFind(pathSeparator);
	VERIFY(directoryLength != INDEX_NONE, "Invalid GLTF file path!");

	String fullPath = String { directoryLength + sizeof(pathSeparator) + filePath.GetLength(), GltfAllocator };
	fullPath.Append(sceneFilePath, directoryLength);
	fullPath.Append(pathSeparator);
	fullPath.Append(filePath);

	return fullPath;
}

static Matrix InternalCalculateGltfGlobalTransform(const Array<GltfNode>& nodes, usize nodeIndex)
{
	if (nodeIndex == INDEX_NONE)
	{
		return Matrix::Identity;
	}

	const GltfNode& node = nodes[nodeIndex];
	return InternalCalculateGltfGlobalTransform(nodes, node.Parent) * node.Transform;
}

static Float4 ToFloat4(const JsonArray& floatArray)
{
	VERIFY(floatArray.GetLength() == 3 || floatArray.GetLength() == 4, "Expected GLTF float array to have 3 or 4 components!");
	return Float4
	{
		static_cast<float>(floatArray[0].GetDecimal()),
		static_cast<float>(floatArray[1].GetDecimal()),
		static_cast<float>(floatArray[2].GetDecimal()),
		(floatArray.GetLength() == 4) ? static_cast<float>(floatArray[3].GetDecimal()) : 0.0f,
	};
}

GltfScene LoadGltfScene(StringView filePath)
{
	const JsonObject rootObject = LoadJson(filePath);

	const JsonArray& sceneArray = rootObject["scenes"_view].GetArray();
	VERIFY(sceneArray.GetLength() == 1, "GLTF file contains multiple scenes!");

	const JsonObject& sceneObject = sceneArray[0].GetObject();

	const JsonArray& sceneNodeArray = sceneObject["nodes"_view].GetArray();
	Array<usize> sceneNodes(sceneNodeArray.GetLength(), GltfAllocator);
	for (const JsonValue& nodeValue : sceneNodeArray)
	{
		sceneNodes.Emplace(static_cast<usize>(nodeValue.GetDecimal()));
	}

	Array<GltfCamera> cameraTemplates(GltfAllocator);
	if (rootObject.HasKey("cameras"_view))
	{
		const JsonArray& cameraArray = rootObject["cameras"_view].GetArray();
		cameraTemplates.Reserve(cameraArray.GetLength());
		for (const JsonValue& cameraValue : cameraArray)
		{
			const JsonObject& cameraObject = cameraValue.GetObject();

			const bool perspective = cameraObject.HasKey("perspective"_view) && cameraObject["type"_view].GetString() == "perspective"_view;
			VERIFY(perspective, "Expected GLTF camera to be perspective!");

			const JsonObject& perspectiveCameraObject = cameraObject["perspective"_view].GetObject();

			const float fieldOfViewYRadians = static_cast<float>(perspectiveCameraObject["yfov"_view].GetDecimal());

			const float aspectRatio = perspectiveCameraObject.HasKey("aspectRatio"_view) ?
										static_cast<float>(perspectiveCameraObject["aspectRatio"_view].GetDecimal())
										: (16.0f / 9.0f);

			const float nearZ = static_cast<float>(perspectiveCameraObject["znear"_view].GetDecimal());

			const float farZ = perspectiveCameraObject.HasKey("zfar"_view) ?
									static_cast<float>(perspectiveCameraObject["zfar"_view].GetDecimal()) :
									1000.0f;

			cameraTemplates.Add(GltfCamera
			{
				.FieldOfViewYRadians = fieldOfViewYRadians,
				.AspectRatio = aspectRatio,
				.NearZ = nearZ,
				.FarZ = farZ,
			});
		}
	}

	Array<GltfDirectionalLight> directionalLightTemplates;
	if (rootObject.HasKey("extensions"_view))
	{
		const JsonObject& extensionsObject = rootObject["extensions"_view].GetObject();
		if (extensionsObject.HasKey("KHR_lights_punctual"_view))
		{
			const JsonObject& khrLightsPunctualObject = extensionsObject["KHR_lights_punctual"_view].GetObject();
			const JsonArray& lightsArray = khrLightsPunctualObject["lights"_view].GetArray();

			for (const JsonValue& light : lightsArray)
			{
				const JsonObject& lightObject = light.GetObject();

				VERIFY(lightObject["type"_view].GetString() == "directional"_view, "Unexpected GLTF light type!");

				const float intensityLux = lightObject.HasKey("intensity"_view) ?
											static_cast<float>(lightObject["intensity"_view].GetDecimal()) :
											1.0f;

				const Float4 color = lightObject.HasKey("color"_view) ?
										ToFloat4(lightObject["color"_view].GetArray()) :
										Float4 { 1.0f, 1.0f, 1.0f, 1.0f };

				directionalLightTemplates.Add(GltfDirectionalLight
				{
					.IntensityLux = intensityLux,
					.Color = Float3 { color.X, color.Y, color.Z },
				});
			}
		}
	}

	Array<usize> cameraIndices(GltfAllocator);
	Array<usize> lightIndices(GltfAllocator);

	const JsonArray& nodeArray = rootObject["nodes"_view].GetArray();
	Array<GltfNode> nodes(nodeArray.GetLength(), GltfAllocator);
	for (usize nodeIndex = 0; nodeIndex < nodeArray.GetLength(); ++nodeIndex)
	{
		const JsonObject& nodeObject = nodeArray[nodeIndex].GetObject();

		Matrix transform = Matrix::Identity;
		Array<usize> childNodes(GltfAllocator);
		usize mesh = INDEX_NONE;
		usize camera = INDEX_NONE;
		usize light = INDEX_NONE;

		const bool hasTranslation = nodeObject.HasKey("translation"_view);
		const bool hasRotation = nodeObject.HasKey("rotation"_view);
		const bool hasScale = nodeObject.HasKey("scale"_view);
		if (hasTranslation || hasRotation || hasScale)
		{
			VERIFY(!nodeObject.HasKey("matrix"_view), "Invalid GLTF node property combination!");

			Vector translation = Vector::Zero;
			if (hasTranslation)
			{
				const JsonArray& translationArray = nodeObject["translation"_view].GetArray();
				VERIFY(translationArray.GetLength() == 3, "Invalid GLTF translation!");

				translation =
				{
					static_cast<float>(translationArray[0].GetDecimal()),
					static_cast<float>(translationArray[1].GetDecimal()),
					static_cast<float>(translationArray[2].GetDecimal()),
				};
			}

			Quaternion rotation = Quaternion::Identity;
			if (hasRotation)
			{
				const JsonArray& rotationArray = nodeObject["rotation"_view].GetArray();
				VERIFY(rotationArray.GetLength() == 4, "Invalid GLTF rotation!");

				rotation =
				{
					static_cast<float>(rotationArray[0].GetDecimal()),
					static_cast<float>(rotationArray[1].GetDecimal()),
					static_cast<float>(rotationArray[2].GetDecimal()),
					static_cast<float>(rotationArray[3].GetDecimal()),
				};
			}

			Vector scale = Vector { 1.0f, 1.0f, 1.0f };
			if (hasScale)
			{
				const JsonArray& scaleArray = nodeObject["scale"_view].GetArray();
				VERIFY(scaleArray.GetLength() == 3, "Invalid GLTF scale!");

				scale =
				{
					static_cast<float>(scaleArray[0].GetDecimal()),
					static_cast<float>(scaleArray[1].GetDecimal()),
					static_cast<float>(scaleArray[2].GetDecimal()),
				};
			}

			transform = Matrix::Translation(translation.X, translation.Y, translation.Z) * rotation.ToMatrix() * Matrix::Scale(scale.X, scale.Y, scale.Z);
		}
		if (nodeObject.HasKey("matrix"_view))
		{
			VERIFY(!nodeObject.HasKey("translation"_view) &&
				   !nodeObject.HasKey("rotation"_view) &&
				   !nodeObject.HasKey("scale"_view), "Invalid GLTF node property combination!");

			const JsonArray& matrixArray = nodeObject["matrix"_view].GetArray();
			VERIFY(matrixArray.GetLength() == 16, "Invalid GLTF matrix!");

			float* element = &transform.M00;
			for (const JsonValue& elementValue : matrixArray)
			{
				*element = static_cast<float>(elementValue.GetDecimal());
				++element;
			}
		}
		if (nodeObject.HasKey("mesh"_view))
		{
			mesh = static_cast<usize>(nodeObject["mesh"_view].GetDecimal());
		}
		if (nodeObject.HasKey("children"_view))
		{
			const JsonArray& childrenArray = nodeObject["children"_view].GetArray();
			childNodes.Reserve(childrenArray.GetLength());
			for (const JsonValue& childValue : childrenArray)
			{
				childNodes.Emplace(static_cast<usize>(childValue.GetDecimal()));
			}
		}
		if (nodeObject.HasKey("camera"_view))
		{
			camera = static_cast<usize>(nodeObject["camera"_view].GetDecimal());
			cameraIndices.Add(nodeIndex);
		}
		if (nodeObject.HasKey("extensions"_view))
		{
			const JsonObject& extensionsObject = nodeObject["extensions"_view].GetObject();
			if (extensionsObject.HasKey("KHR_lights_punctual"_view))
			{
				const JsonObject& khrLightsPunctualObject = extensionsObject["KHR_lights_punctual"_view].GetObject();

				light = static_cast<usize>(khrLightsPunctualObject["light"_view].GetDecimal());
				lightIndices.Add(nodeIndex);
			}
		}

		nodes.Emplace(transform, INDEX_NONE, Move(childNodes), mesh, camera, light);
	}

	for (usize i = 0; i < nodes.GetLength(); ++i)
	{
		const GltfNode& node = nodes[i];

		for (const usize childNodeIndex : node.ChildNodes)
		{
			nodes[childNodeIndex].Parent = i;
		}
	}

	Array<GltfCamera> cameras(cameraIndices.GetLength(), GltfAllocator);
	for (usize cameraIndex : cameraIndices)
	{
		const GltfNode& node = nodes[cameraIndex];

		GltfCamera placedCamera = cameraTemplates[node.Camera];
		placedCamera.Transform = InternalCalculateGltfGlobalTransform(nodes, cameraIndex);
		cameras.Add(placedCamera);
	}

	Array<GltfDirectionalLight> directionalLights(GltfAllocator);
	for (usize lightIndex : lightIndices)
	{
		const GltfNode& node = nodes[lightIndex];

		GltfDirectionalLight placedDirectionalLight = directionalLightTemplates[node.Light];
		placedDirectionalLight.Transform = InternalCalculateGltfGlobalTransform(nodes, lightIndex);
		directionalLights.Add(placedDirectionalLight);
	}

	const JsonArray& bufferArray = rootObject["buffers"_view].GetArray();
	Array<GltfBuffer> buffers(bufferArray.GetLength(), GltfAllocator);
	for (const JsonValue& bufferValue : bufferArray)
	{
		const JsonObject& bufferObject = bufferValue.GetObject();

		const String& bufferPath = bufferObject["uri"_view].GetString();
		const String fullPath = ResolveFilePath(filePath, bufferPath.AsView());

		const usize bufferSize = static_cast<usize>(bufferObject["byteLength"_view].GetDecimal());

		usize fileSize;
		uint8* bufferData = Platform::ReadEntireFile(fullPath.GetData(), fullPath.GetLength(), &fileSize, *GltfAllocator);
		VERIFY(bufferSize == fileSize, "Failed to read GLTF buffer!");

		buffers.Emplace(bufferData, bufferSize);
	}

	const JsonArray& bufferViewArray = rootObject["bufferViews"_view].GetArray();
	Array<GltfBufferView> bufferViews(bufferViewArray.GetLength(), GltfAllocator);
	for (const JsonValue& bufferViewValue : bufferViewArray)
	{
		const JsonObject& bufferViewObject = bufferViewValue.GetObject();

		const usize buffer = static_cast<usize>(bufferViewObject["buffer"_view].GetDecimal());
		const usize size = static_cast<usize>(bufferViewObject["byteLength"_view].GetDecimal());

		usize offset = 0;
		if (bufferViewObject.HasKey("byteOffset"_view))
			offset = static_cast<usize>(bufferViewObject["byteOffset"_view].GetDecimal());

		GltfTargetType targetType;
		if (bufferViewObject.HasKey("target"_view))
		{
			const usize targetTypeNumber = static_cast<usize>(bufferViewObject["target"_view].GetDecimal());
			if (targetTypeNumber == 34962)
				targetType = GltfTargetType::ArrayBuffer;
			else if (targetTypeNumber == 34963)
				targetType = GltfTargetType::ElementArrayBuffer;
			else
				VERIFY(false, "Unexpected GLTF target type!");
		}
		else
			targetType = GltfTargetType::ArrayBuffer;

		bufferViews.Emplace(buffer, size, offset, targetType);
	}

	const JsonArray& meshArray = rootObject["meshes"_view].GetArray();
	Array<GltfMesh> meshes(meshArray.GetLength(), GltfAllocator);
	for (const JsonValue& meshValue : meshArray)
	{
		const JsonObject& meshObject = meshValue.GetObject();

		const JsonArray& primitiveArray = meshObject["primitives"_view].GetArray();
		Array<GltfPrimitive> primitives(primitiveArray.GetLength());
		for (const JsonValue& primitiveValue : primitiveArray)
		{
			const JsonObject& primitiveObject = primitiveValue.GetObject();

			if (primitiveObject.HasKey("mode"_view))
				VERIFY(static_cast<usize>(primitiveObject["mode"_view].GetDecimal()) == 4, "Unexpected GLTF primitive type!");

			const usize indices = static_cast<usize>(primitiveObject["indices"_view].GetDecimal());
			const usize material = static_cast<usize>(primitiveObject["material"_view].GetDecimal());

			const JsonObject& attributesObject = primitiveObject["attributes"_view].GetObject();
			HashTable<GltfAttributeType, usize> attributes { 4, GltfAllocator };

			if (attributesObject.HasKey("POSITION"_view))
				attributes.Add(GltfAttributeType::Position, static_cast<usize>(attributesObject["POSITION"_view].GetDecimal()));
			if (attributesObject.HasKey("NORMAL"_view))
				attributes.Add(GltfAttributeType::Normal, static_cast<usize>(attributesObject["NORMAL"_view].GetDecimal()));
			if (attributesObject.HasKey("TANGENT"_view))
				attributes.Add(GltfAttributeType::Tangent, static_cast<usize>(attributesObject["TANGENT"_view].GetDecimal()));
			if (attributesObject.HasKey("TEXCOORD_0"_view))
				attributes.Add(GltfAttributeType::Texcoord0, static_cast<usize>(attributesObject["TEXCOORD_0"_view].GetDecimal()));

			primitives.Emplace(Move(attributes), indices, material);
		}

		meshes.Emplace(Move(primitives));
	}

	Array<GltfImage> images(GltfAllocator);
	if (rootObject.HasKey("images"_view))
	{
		const JsonArray& imageArray = rootObject["images"_view].GetArray();
		images.Reserve(imageArray.GetLength());
		for (const JsonValue& imageValue : imageArray)
		{
			const JsonObject& imageObject = imageValue.GetObject();

			const String& imagePath = imageObject["uri"_view].GetString();
			const String fullPath = ResolveFilePath(filePath, imagePath.AsView());

			images.Emplace(fullPath);
		}
	}

	Array<GltfTexture> textures(GltfAllocator);
	if (rootObject.HasKey("textures"_view))
	{
		const JsonArray& textureArray = rootObject["textures"_view].GetArray();
		textures.Reserve(textureArray.GetLength());
		for (const JsonValue& textureValue : textureArray)
		{
			const JsonObject& textureObject = textureValue.GetObject();

			const usize image = static_cast<usize>(textureObject["source"_view].GetDecimal());

			const usize sampler = textureObject.HasKey("sampler"_view) ? static_cast<usize>(textureObject["sampler"_view].GetDecimal()) : INDEX_NONE;

			textures.Emplace(image, sampler);
		}
	}

	const JsonArray& materialArray = rootObject["materials"_view].GetArray();
	Array<GltfMaterial> materials(materialArray.GetLength(), GltfAllocator);
	for (const JsonValue& materialValue : materialArray)
	{
		const JsonObject& materialObject = materialValue.GetObject();

		GltfMaterial material =
		{
			.NormalMapTexture = INDEX_NONE,
			.AlphaMode = GltfAlphaMode::Opaque,
			.AlphaCutoff = 0.5f,
		};

		if (materialObject.HasKey("pbrMetallicRoughness"_view))
		{
			const JsonObject& pbrMetallicRoughnessObject = materialObject["pbrMetallicRoughness"_view].GetObject();

			material.IsSpecularGlossiness = false;

			material.MetallicRoughness.BaseColorTexture = INDEX_NONE;
			material.MetallicRoughness.BaseColorFactor = Float4 { 1.0f, 1.0f, 1.0f, 1.0f };

			material.MetallicRoughness.MetallicRoughnessTexture = INDEX_NONE;
			material.MetallicRoughness.MetallicFactor = 1.0f;
			material.MetallicRoughness.RoughnessFactor = 1.0f;

			if (pbrMetallicRoughnessObject.HasKey("baseColorTexture"_view))
			{
				const JsonObject& baseColorTextureObject = pbrMetallicRoughnessObject["baseColorTexture"_view].GetObject();
				material.MetallicRoughness.BaseColorTexture = static_cast<usize>(baseColorTextureObject["index"_view].GetDecimal());
			}
			if (pbrMetallicRoughnessObject.HasKey("baseColorFactor"_view))
			{
				const JsonArray& baseColorFactorArray = pbrMetallicRoughnessObject["baseColorFactor"_view].GetArray();
				material.MetallicRoughness.BaseColorFactor = ToFloat4(baseColorFactorArray);
			}

			if (pbrMetallicRoughnessObject.HasKey("metallicRoughnessTexture"_view))
			{
				const JsonObject& metallicRoughnessTextureObject = pbrMetallicRoughnessObject["metallicRoughnessTexture"_view].GetObject();
				material.MetallicRoughness.MetallicRoughnessTexture = static_cast<usize>(metallicRoughnessTextureObject["index"_view].GetDecimal());
			}
			if (pbrMetallicRoughnessObject.HasKey("metallicFactor"_view))
			{
				material.MetallicRoughness.MetallicFactor = static_cast<float>(pbrMetallicRoughnessObject["metallicFactor"_view].GetDecimal());
			}
			if (pbrMetallicRoughnessObject.HasKey("roughnessFactor"_view))
			{
				material.MetallicRoughness.RoughnessFactor = static_cast<float>(pbrMetallicRoughnessObject["roughnessFactor"_view].GetDecimal());
			}
		}

		if (materialObject.HasKey("extensions"_view))
		{
			const JsonObject& extensionsObject = materialObject["extensions"_view].GetObject();

			if (extensionsObject.HasKey("KHR_materials_pbrSpecularGlossiness"_view))
			{
				const JsonObject& pbrSpecularGlossinessObject = extensionsObject["KHR_materials_pbrSpecularGlossiness"_view].GetObject();

				material.IsSpecularGlossiness = true;

				material.SpecularGlossiness.DiffuseTexture = INDEX_NONE;
				material.SpecularGlossiness.DiffuseFactor = Float4 { 1.0f, 1.0f, 1.0f, 1.0f };

				material.SpecularGlossiness.SpecularGlossinessTexture = INDEX_NONE;
				material.SpecularGlossiness.SpecularFactor = Float3 { 1.0f, 1.0f, 1.0f };
				material.SpecularGlossiness.GlossinessFactor = 1.0f;

				if (pbrSpecularGlossinessObject.HasKey("diffuseTexture"_view))
				{
					const JsonObject& diffuseTextureObject = pbrSpecularGlossinessObject["diffuseTexture"_view].GetObject();
					material.MetallicRoughness.BaseColorTexture = static_cast<usize>(diffuseTextureObject["index"_view].GetDecimal());
				}
				if (pbrSpecularGlossinessObject.HasKey("diffuseFactor"_view))
				{
					const JsonArray& diffuseFactorArray = pbrSpecularGlossinessObject["diffuseFactor"_view].GetArray();
					material.MetallicRoughness.BaseColorFactor = ToFloat4(diffuseFactorArray);
				}

				if (pbrSpecularGlossinessObject.HasKey("metallicRoughnessTexture"_view))
				{
					const JsonObject& specularGlossinessTextureObject = pbrSpecularGlossinessObject["specularGlossinessTexture"_view].GetObject();
					material.SpecularGlossiness.SpecularGlossinessTexture = static_cast<usize>(specularGlossinessTextureObject["index"_view].GetDecimal());
				}
				if (pbrSpecularGlossinessObject.HasKey("specularFactor"_view))
				{
					const JsonArray& specularFactorArray = pbrSpecularGlossinessObject["specularFactor"_view].GetArray();
					const Float4 specularFactor = ToFloat4(specularFactorArray);
					material.SpecularGlossiness.SpecularFactor = Float3 { specularFactor.X, specularFactor.Y, specularFactor.Z };
				}
				if (pbrSpecularGlossinessObject.HasKey("roughnessFactor"_view))
				{
					material.MetallicRoughness.RoughnessFactor = static_cast<float>(pbrSpecularGlossinessObject["roughnessFactor"_view].GetDecimal());
				}
			}
		}

		if (materialObject.HasKey("normalTexture"_view))
		{
			const JsonObject& normalTextureObject = materialObject["normalTexture"_view].GetObject();
			material.NormalMapTexture = static_cast<usize>(normalTextureObject["index"_view].GetDecimal());
		}

		if (materialObject.HasKey("alphaMode"_view))
		{
			const String& alphaModeString = materialObject["alphaMode"_view].GetString();

			if (alphaModeString == "OPAQUE"_view)
			{
				material.AlphaMode = GltfAlphaMode::Opaque;
			}
			else if (alphaModeString == "MASK"_view)
			{
				material.AlphaMode = GltfAlphaMode::Mask;
			}
			else if (alphaModeString == "BLEND"_view)
			{
				material.AlphaMode = GltfAlphaMode::Blend;
			}
			else
			{
				VERIFY(false, "Unexpected GLTF alpha mode!");
			}
		}

		if (materialObject.HasKey("alphaCutoff"_view))
		{
			material.AlphaCutoff = static_cast<float>(materialObject["alphaCutoff"_view].GetDecimal());
		}

		materials.Add(material);
	}

	const auto toFilter = [](usize filter, bool magnification) -> GltfFilter
	{
		if (magnification)
			CHECK(filter == 9728 || filter == 9729);

		switch (filter)
		{
		case 9728:
			return GltfFilter::Nearest;
		case 9729:
			return GltfFilter::Linear;
		case 9984:
			return GltfFilter::NearestMipMapNearest;
		case 9985:
			return GltfFilter::LinearMipMapNearest;
		case 9986:
			return GltfFilter::NearestMipMapLinear;
		case 9987:
			return GltfFilter::LinearMipMapLinear;
		default:
			CHECK(false);
		}
		return GltfFilter::Nearest;
	};

	const auto toAddress = [](usize address) -> GltfAddress
	{
		switch (address)
		{
		case 10497:
			return GltfAddress::Repeat;
		case 33071:
			return GltfAddress::ClampToEdge;
		case 33648:
			return GltfAddress::MirroredRepeat;
		default:
			CHECK(false);
		}
		return GltfAddress::Repeat;
	};

	Array<GltfSampler> samplers(GltfAllocator);
	if (rootObject.HasKey("samplers"_view))
	{
		const JsonArray& samplerArray = rootObject["samplers"_view].GetArray();
		samplers.Reserve(samplerArray.GetLength());
		for (const JsonValue& samplerValue : samplerArray)
		{
			const JsonObject& samplerObject = samplerValue.GetObject();

			const GltfFilter minification = samplerObject.HasKey("minFilter"_view) ?
												toFilter(static_cast<usize>(samplerObject["minFilter"_view].GetDecimal()), false) :
												GltfFilter::Linear;
			const GltfFilter magnification = samplerObject.HasKey("magFilter"_view) ?
												toFilter(static_cast<usize>(samplerObject["magFilter"_view].GetDecimal()), true) :
												GltfFilter::Linear;

			const GltfAddress horizontal = samplerObject.HasKey("wrapS"_view) ?
												toAddress(static_cast<usize>(samplerObject["wrapS"_view].GetDecimal())) :
												GltfAddress::Repeat;
			const GltfAddress vertical = samplerObject.HasKey("wrapT"_view) ?
												toAddress(static_cast<usize>(samplerObject["wrapT"_view].GetDecimal())) :
												GltfAddress::Repeat;

			samplers.Emplace(minification, magnification, horizontal, vertical);
		}
	}

	const JsonArray& accessorArray = rootObject["accessors"_view].GetArray();
	Array<GltfAccessor> accessors(accessorArray.GetLength(), GltfAllocator);
	for (const JsonValue& accessorValue : accessorArray)
	{
		const JsonObject& accessorObject = accessorValue.GetObject();

		const usize bufferView = static_cast<usize>(accessorObject["bufferView"_view].GetDecimal());
		const usize count = static_cast<usize>(accessorObject["count"_view].GetDecimal());
		const String& accessorTypeString = accessorObject["type"_view].GetString();
		const usize componentTypeNumber = static_cast<usize>(accessorObject["componentType"_view].GetDecimal());

		usize offset = 0;
		if (accessorObject.HasKey("byteOffset"_view))
			offset = static_cast<usize>(accessorObject["byteOffset"_view].GetDecimal());

		GltfComponentType componentType;
		if (componentTypeNumber == 5120)
			componentType = GltfComponentType::Int8;
		else if (componentTypeNumber == 5121)
			componentType = GltfComponentType::Uint8;
		else if (componentTypeNumber == 5122)
			componentType = GltfComponentType::Uint16;
		else if (componentTypeNumber == 5123)
			componentType = GltfComponentType::Int16;
		else if (componentTypeNumber == 5125)
			componentType = GltfComponentType::Uint32;
		else if (componentTypeNumber == 5126)
			componentType = GltfComponentType::Float32;
		else
			VERIFY(false, "Unexpected GLTF component type!");

		GltfAccessorType accessorType;
		if (accessorTypeString == "SCALAR"_view)
			accessorType = GltfAccessorType::Scalar;
		else if (accessorTypeString == "VEC2"_view)
			accessorType = GltfAccessorType::Vector2;
		else if (accessorTypeString == "VEC3"_view)
			accessorType = GltfAccessorType::Vector3;
		else if (accessorTypeString == "VEC4"_view)
			accessorType = GltfAccessorType::Vector4;
		else if (accessorTypeString == "MAT2"_view)
			accessorType = GltfAccessorType::Matrix2;
		else if (accessorTypeString == "MAT3"_view)
			accessorType = GltfAccessorType::Matrix3;
		else if (accessorTypeString == "MAT4"_view)
			accessorType = GltfAccessorType::Matrix4;
		else
			VERIFY(false, "Unexpected GLTF accessor type!");

		accessors.Emplace(bufferView, count, offset, componentType, accessorType);
	}

	return GltfScene
	{
		.TopLevelNodes = Move(sceneNodes),
		.Nodes = Move(nodes),
		.Buffers = Move(buffers),
		.BufferViews = Move(bufferViews),
		.Meshes = Move(meshes),
		.Images = Move(images),
		.Textures = Move(textures),
		.Samplers = Move(samplers),
		.Materials = Move(materials),
		.Accessors = Move(accessors),
		.Cameras = Move(cameras),
		.DirectionalLights = Move(directionalLights),
	};
}

void UnloadGltfScene(GltfScene* scene)
{
	for (GltfBuffer& buffer : scene->Buffers)
	{
		GltfAllocator->Deallocate(buffer.Data, buffer.Size);
	}
}

Matrix CalculateGltfGlobalTransform(const GltfScene& scene, usize nodeIndex)
{
	return InternalCalculateGltfGlobalTransform(scene.Nodes, nodeIndex);
}
