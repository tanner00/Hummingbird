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

	GltfCamera camera =
	{
		.Transform = Matrix::Identity,
		.FieldOfViewYRadians = Pi / 3.0f,
		.AspectRatio = 16.0f / 9.0f,
		.NearZ = 0.01f,
		.FarZ = 1000.0f,
	};

	if (rootObject.HasKey("cameras"_view))
	{
		const JsonArray& cameraArray = rootObject["cameras"_view].GetArray();
		VERIFY(cameraArray.GetLength() == 1, "Expected only one GLTF camera!");
		for (const JsonValue& cameraValue : cameraArray)
		{
			const JsonObject& cameraObject = cameraValue.GetObject();

			const bool perspective = cameraObject.HasKey("perspective"_view) && cameraObject["type"_view].GetString() == "perspective"_view;
			VERIFY(perspective, "Expected GLTF camera to be perspective!");

			const JsonObject& perspectiveCameraObject = cameraObject["perspective"_view].GetObject();

			camera.FieldOfViewYRadians = static_cast<float>(perspectiveCameraObject["yfov"_view].GetDecimal());

			if (perspectiveCameraObject.HasKey("aspectRatio"_view))
				camera.AspectRatio = static_cast<float>(perspectiveCameraObject["aspectRatio"_view].GetDecimal());

			camera.NearZ = static_cast<float>(perspectiveCameraObject["znear"_view].GetDecimal());

			if (perspectiveCameraObject.HasKey("zfar"_view))
				camera.FarZ = static_cast<float>(perspectiveCameraObject["zfar"_view].GetDecimal());
		}
	}

	usize cameraIndex = INDEX_NONE;

	const JsonArray& nodeArray = rootObject["nodes"_view].GetArray();
	Array<GltfNode> nodes(nodeArray.GetLength(), GltfAllocator);
	for (usize nodeIndex = 0; nodeIndex < nodeArray.GetLength(); ++nodeIndex)
	{
		const JsonObject& nodeObject = nodeArray[nodeIndex].GetObject();

		usize mesh = INDEX_NONE;
		Matrix transform = Matrix::Identity;
		Array<usize> childNodes(GltfAllocator);

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

			transform = Matrix::Translation(translation.X, translation.Y, translation.Z) * rotation.GetMatrix() * Matrix::Scale(scale.X, scale.Y, scale.Z);
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
			VERIFY(cameraIndex == INDEX_NONE, "Expected only one camera per GLTF scene!");
			cameraIndex = nodeIndex;
		}

		nodes.Emplace(mesh, transform, INDEX_NONE, Move(childNodes));
	}

	for (usize i = 0; i < nodes.GetLength(); ++i)
	{
		const GltfNode& node = nodes[i];

		for (const usize childNodeIndex : node.ChildNodes)
		{
			nodes[childNodeIndex].Parent = i;
		}
	}

	if (cameraIndex != INDEX_NONE)
		camera.Transform = InternalCalculateGltfGlobalTransform(nodes, cameraIndex);

	const JsonArray& bufferArray = rootObject["buffers"_view].GetArray();
	Array<GltfBuffer> buffers(bufferArray.GetLength(), GltfAllocator);
	for (const JsonValue& bufferValue : bufferArray)
	{
		const JsonObject& bufferObject = bufferValue.GetObject();

		const String& bufferPath = bufferObject["uri"_view].GetString();
		const String fullPath = ResolveFilePath(filePath, bufferPath);

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
			const String fullPath = ResolveFilePath(filePath, imagePath);

			images.Emplace(LoadDdsImage(fullPath));
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

		usize baseColorTexture = INDEX_NONE;
		Float4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };

		if (materialObject.HasKey("pbrMetallicRoughness"_view))
		{
			const JsonObject& pbrMetallicRoughnessObject = materialObject["pbrMetallicRoughness"_view].GetObject();

			if (pbrMetallicRoughnessObject.HasKey("baseColorTexture"_view))
			{
				const JsonObject& baseColorTextureObject = pbrMetallicRoughnessObject["baseColorTexture"_view].GetObject();
				baseColorTexture = static_cast<usize>(baseColorTextureObject["index"_view].GetDecimal());
			}

			if (materialObject.HasKey("baseColorFactor"_view))
			{
				const JsonArray& baseColorFactorArray = materialObject["baseColorFactor"_view].GetArray();
				baseColorFactor = ToFloat4(baseColorFactorArray);
			}
		}

		if (materialObject.HasKey("extensions"_view))
		{
			const JsonObject& extensionsObject = materialObject["extensions"_view].GetObject();

			if (extensionsObject.HasKey("KHR_materials_pbrSpecularGlossiness"_view))
			{
				const JsonObject& pbrSpecularGlossinessObject = extensionsObject["KHR_materials_pbrSpecularGlossiness"_view].GetObject();

				if (pbrSpecularGlossinessObject.HasKey("diffuseTexture"_view))
				{
					const JsonObject& diffuseTextureObject = pbrSpecularGlossinessObject["diffuseTexture"_view].GetObject();
					baseColorTexture = static_cast<usize>(diffuseTextureObject["index"_view].GetDecimal());
				}

				if (pbrSpecularGlossinessObject.HasKey("diffuseFactor"_view))
				{
					const JsonArray& diffuseFactorArray = pbrSpecularGlossinessObject["diffuseFactor"_view].GetArray();
					baseColorFactor = ToFloat4(diffuseFactorArray);
				}
			}
		}

		GltfAlphaMode alphaMode = GltfAlphaMode::Opaque;
		float alphaCutoff = 0.5f;

		if (materialObject.HasKey("alphaMode"_view))
		{
			const String& alphaModeString = materialObject["alphaMode"_view].GetString();

			if (alphaModeString == "OPAQUE"_view)
			{
				alphaMode = GltfAlphaMode::Opaque;
			} else if (alphaModeString == "MASK"_view)
			{
				alphaMode = GltfAlphaMode::Mask;
			} else if (alphaModeString == "BLEND"_view)
			{
				alphaMode = GltfAlphaMode::Blend;
			}
			else
			{
				VERIFY(false, "Unexpected GLTF alpha mode!");
			}
		}

		if (materialObject.HasKey("alphaCutoff"_view))
		{
			alphaCutoff = static_cast<float>(materialObject["alphaCutoff"_view].GetDecimal());
		}

		materials.Emplace(baseColorTexture, baseColorFactor, alphaMode, alphaCutoff);
	}

	const auto toFilter = [](usize filter, bool magnification)
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

	const auto toAddress = [](usize address)
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
		.Camera = camera,
		.DefaultCamera = cameraIndex == INDEX_NONE,
	};
}

void UnloadGltfScene(GltfScene* scene)
{
	for (GltfImage& image : scene->Images)
	{
		UnloadDdsImage(&image.Image);
	}
	for (GltfBuffer& buffer : scene->Buffers)
	{
		GltfAllocator->Deallocate(buffer.Data, buffer.Size);
	}
}

Matrix CalculateGltfGlobalTransform(const GltfScene& scene, usize nodeIndex)
{
	return InternalCalculateGltfGlobalTransform(scene.Nodes, nodeIndex);
}
