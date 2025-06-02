#include "GLTF.hpp"
#include "JSON.hpp"

namespace GLTF
{

static Allocator* Allocator = &GlobalAllocator::Get();

static String ResolveFilePath(StringView sceneFilePath, StringView filePath)
{
	static constexpr char pathSeparator = '/';

	const usize directoryLength = sceneFilePath.ReverseFind(pathSeparator);
	VERIFY(directoryLength != INDEX_NONE, "Invalid GLTF file path!");

	String fullPath = String { directoryLength + sizeof(pathSeparator) + filePath.GetLength(), Allocator };
	fullPath.Append(sceneFilePath, directoryLength);
	fullPath.Append(pathSeparator);
	fullPath.Append(filePath);

	return fullPath;
}

static Matrix InternalCalculateGlobalTransform(const Array<Node>& nodes, usize nodeIndex)
{
	if (nodeIndex == INDEX_NONE)
	{
		return Matrix::Identity;
	}

	const Node& node = nodes[nodeIndex];
	return InternalCalculateGlobalTransform(nodes, node.Parent) * node.Transform;
}

static Float4 ToFloat4(const JSON::Array& floatArray)
{
	VERIFY(floatArray.GetLength() == 3 || floatArray.GetLength() == 4, "Expected GLTF float array to have 3 or 4 components!");
	return Float4
	{
		.X = static_cast<float>(floatArray[0].GetDecimal()),
		.Y = static_cast<float>(floatArray[1].GetDecimal()),
		.Z = static_cast<float>(floatArray[2].GetDecimal()),
		.W = (floatArray.GetLength() == 4) ? static_cast<float>(floatArray[3].GetDecimal()) : 0.0f,
	};
}

Scene LoadScene(StringView filePath)
{
	const JSON::Object rootObject = JSON::Load(filePath);

	const JSON::Array& sceneArray = rootObject["scenes"_view].GetArray();
	VERIFY(sceneArray.GetLength() == 1, "GLTF file contains multiple scenes!");

	const JSON::Object& sceneObject = sceneArray[0].GetObject();

	const JSON::Array& sceneNodeArray = sceneObject["nodes"_view].GetArray();
	Array<usize> sceneNodes(sceneNodeArray.GetLength(), Allocator);
	for (const JSON::Value& nodeValue : sceneNodeArray)
	{
		sceneNodes.Emplace(static_cast<usize>(nodeValue.GetDecimal()));
	}

	Array<Camera> cameraTemplates(Allocator);
	if (rootObject.HasKey("cameras"_view))
	{
		const JSON::Array& cameraArray = rootObject["cameras"_view].GetArray();
		cameraTemplates.Reserve(cameraArray.GetLength());
		for (const JSON::Value& cameraValue : cameraArray)
		{
			const JSON::Object& cameraObject = cameraValue.GetObject();

			const bool perspective = cameraObject.HasKey("perspective"_view) && cameraObject["type"_view].GetString() == "perspective"_view;
			VERIFY(perspective, "Expected GLTF camera to be perspective!");

			const JSON::Object& perspectiveCameraObject = cameraObject["perspective"_view].GetObject();

			const float fieldOfViewYRadians = static_cast<float>(perspectiveCameraObject["yfov"_view].GetDecimal());

			const float aspectRatio = perspectiveCameraObject.HasKey("aspectRatio"_view)
									? static_cast<float>(perspectiveCameraObject["aspectRatio"_view].GetDecimal())
									: (16.0f / 9.0f);

			const float nearZ = static_cast<float>(perspectiveCameraObject["znear"_view].GetDecimal());

			const float farZ = perspectiveCameraObject.HasKey("zfar"_view)
							 ? static_cast<float>(perspectiveCameraObject["zfar"_view].GetDecimal())
							 : 1000.0f;

			cameraTemplates.Add(Camera
			{
				.FieldOfViewYRadians = fieldOfViewYRadians,
				.AspectRatio = aspectRatio,
				.NearZ = nearZ,
				.FarZ = farZ,
			});
		}
	}

	Array<Light> lightTemplates;
	if (rootObject.HasKey("extensions"_view))
	{
		const JSON::Object& extensionsObject = rootObject["extensions"_view].GetObject();
		if (extensionsObject.HasKey("KHR_lights_punctual"_view))
		{
			const JSON::Object& lightsPunctualObject = extensionsObject["KHR_lights_punctual"_view].GetObject();
			const JSON::Array& lightsArray = lightsPunctualObject["lights"_view].GetArray();

			for (const JSON::Value& light : lightsArray)
			{
				const JSON::Object& lightObject = light.GetObject();

				const float intensity = lightObject.HasKey("intensity"_view)
									  ? static_cast<float>(lightObject["intensity"_view].GetDecimal())
									  : 1.0f;

				const Float4 color = lightObject.HasKey("color"_view)
								   ? ToFloat4(lightObject["color"_view].GetArray())
								   : Float4 { .R = 1.0f, .G = 1.0f, .B = 1.0f, .A = 1.0f };

				LightType type = LightType::Directional;

				const String& typeString = lightObject["type"_view].GetString();
				if (typeString == "directional"_view)
				{
					type = LightType::Directional;
				}
				else if (typeString == "point"_view)
				{
					type = LightType::Point;
				}
				else
				{
					CHECK(false);
				}

				lightTemplates.Add(Light
				{
					.Intensity = intensity,
					.Color = { .R = color.R, .G = color.G, .B = color.B },
					.Type = type,
				});
			}
		}
	}

	Array<usize> cameraIndices(Allocator);
	Array<usize> lightIndices(Allocator);

	const JSON::Array& nodeArray = rootObject["nodes"_view].GetArray();
	Array<Node> nodes(nodeArray.GetLength(), Allocator);
	for (usize nodeIndex = 0; nodeIndex < nodeArray.GetLength(); ++nodeIndex)
	{
		Matrix transform = Matrix::Identity;
		Array<usize> childNodes(Allocator);
		usize mesh = INDEX_NONE;
		usize camera = INDEX_NONE;
		usize light = INDEX_NONE;

		const JSON::Object& nodeObject = nodeArray[nodeIndex].GetObject();

		const bool hasTranslation = nodeObject.HasKey("translation"_view);
		const bool hasRotation = nodeObject.HasKey("rotation"_view);
		const bool hasScale = nodeObject.HasKey("scale"_view);
		if (hasTranslation || hasRotation || hasScale)
		{
			VERIFY(!nodeObject.HasKey("matrix"_view), "Invalid GLTF node property combination!");

			Vector translation = Vector::Zero;
			if (hasTranslation)
			{
				const JSON::Array& translationArray = nodeObject["translation"_view].GetArray();
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
				const JSON::Array& rotationArray = nodeObject["rotation"_view].GetArray();
				VERIFY(rotationArray.GetLength() == 4, "Invalid GLTF rotation!");

				rotation = Quaternion
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
				const JSON::Array& scaleArray = nodeObject["scale"_view].GetArray();
				VERIFY(scaleArray.GetLength() == 3, "Invalid GLTF scale!");

				scale = Vector
				{
					static_cast<float>(scaleArray[0].GetDecimal()),
					static_cast<float>(scaleArray[1].GetDecimal()),
					static_cast<float>(scaleArray[2].GetDecimal()),
				};
			}

			transform = Matrix::Translation(translation.X, translation.Y, translation.Z)
					  * rotation.ToMatrix()
					  * Matrix::Scale(scale.X, scale.Y, scale.Z);
		}
		if (nodeObject.HasKey("matrix"_view))
		{
			VERIFY(!nodeObject.HasKey("translation"_view) &&
				   !nodeObject.HasKey("rotation"_view) &&
				   !nodeObject.HasKey("scale"_view), "Invalid GLTF node property combination!");

			const JSON::Array& matrixArray = nodeObject["matrix"_view].GetArray();
			VERIFY(matrixArray.GetLength() == 16, "Invalid GLTF matrix!");

			float* element = &transform.M00;
			for (const JSON::Value& elementValue : matrixArray)
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
			const JSON::Array& childrenArray = nodeObject["children"_view].GetArray();
			childNodes.Reserve(childrenArray.GetLength());
			for (const JSON::Value& childValue : childrenArray)
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
			const JSON::Object& extensionsObject = nodeObject["extensions"_view].GetObject();
			if (extensionsObject.HasKey("KHR_lights_punctual"_view))
			{
				lightIndices.Add(nodeIndex);

				const JSON::Object& khrLightsPunctualObject = extensionsObject["KHR_lights_punctual"_view].GetObject();
				light = static_cast<usize>(khrLightsPunctualObject["light"_view].GetDecimal());
			}
		}

		nodes.Emplace(transform, INDEX_NONE, Move(childNodes), mesh, camera, light);
	}

	for (usize i = 0; i < nodes.GetLength(); ++i)
	{
		const Node& node = nodes[i];

		for (const usize childNodeIndex : node.ChildNodes)
		{
			nodes[childNodeIndex].Parent = i;
		}
	}

	Array<Camera> cameras(cameraIndices.GetLength(), Allocator);
	for (usize cameraIndex : cameraIndices)
	{
		const Node& node = nodes[cameraIndex];

		Camera placedCamera = cameraTemplates[node.Camera];
		placedCamera.Transform = InternalCalculateGlobalTransform(nodes, cameraIndex);
		cameras.Add(placedCamera);
	}

	Array<Light> lights(Allocator);
	for (usize lightIndex : lightIndices)
	{
		const Node& node = nodes[lightIndex];

		Light placedLight = lightTemplates[node.Light];
		placedLight.Transform = InternalCalculateGlobalTransform(nodes, lightIndex);
		lights.Add(placedLight);
	}

	const JSON::Array& bufferArray = rootObject["buffers"_view].GetArray();
	Array<Buffer> buffers(bufferArray.GetLength(), Allocator);
	for (const JSON::Value& bufferValue : bufferArray)
	{
		const JSON::Object& bufferObject = bufferValue.GetObject();

		const String& bufferPath = bufferObject["uri"_view].GetString();
		const String fullPath = ResolveFilePath(filePath, bufferPath.AsView());

		const usize bufferSize = static_cast<usize>(bufferObject["byteLength"_view].GetDecimal());

		usize fileSize;
		uint8* bufferData = Platform::ReadEntireFile(fullPath.GetData(), fullPath.GetLength(), &fileSize, *Allocator);
		VERIFY(bufferSize == fileSize, "Failed to read GLTF buffer!");

		buffers.Emplace(bufferData, bufferSize);
	}

	const JSON::Array& bufferViewArray = rootObject["bufferViews"_view].GetArray();
	Array<BufferView> bufferViews(bufferViewArray.GetLength(), Allocator);
	for (const JSON::Value& bufferViewValue : bufferViewArray)
	{
		const JSON::Object& bufferViewObject = bufferViewValue.GetObject();

		const usize buffer = static_cast<usize>(bufferViewObject["buffer"_view].GetDecimal());
		const usize size = static_cast<usize>(bufferViewObject["byteLength"_view].GetDecimal());

		usize offset = 0;
		if (bufferViewObject.HasKey("byteOffset"_view))
		{
			offset = static_cast<usize>(bufferViewObject["byteOffset"_view].GetDecimal());
		}

		TargetType targetType;
		if (bufferViewObject.HasKey("target"_view))
		{
			const usize targetTypeNumber = static_cast<usize>(bufferViewObject["target"_view].GetDecimal());
			if (targetTypeNumber == 34962)
			{
				targetType = TargetType::ArrayBuffer;
			}
			else if (targetTypeNumber == 34963)
			{
				targetType = TargetType::ElementArrayBuffer;
			}
			else
			{
				VERIFY(false, "Unexpected GLTF target type!");
			}
		}
		else
			targetType = TargetType::ArrayBuffer;

		bufferViews.Emplace(buffer, size, offset, targetType);
	}

	const JSON::Array& meshArray = rootObject["meshes"_view].GetArray();
	Array<Mesh> meshes(meshArray.GetLength(), Allocator);
	for (const JSON::Value& meshValue : meshArray)
	{
		const JSON::Object& meshObject = meshValue.GetObject();

		const JSON::Array& primitiveArray = meshObject["primitives"_view].GetArray();
		Array<Primitive> primitives(primitiveArray.GetLength());
		for (const JSON::Value& primitiveValue : primitiveArray)
		{
			const JSON::Object& primitiveObject = primitiveValue.GetObject();

			if (primitiveObject.HasKey("mode"_view))
			{
				VERIFY(static_cast<usize>(primitiveObject["mode"_view].GetDecimal()) == 4, "Unexpected GLTF primitive type!");
			}

			const usize indices = static_cast<usize>(primitiveObject["indices"_view].GetDecimal());
			const usize material = static_cast<usize>(primitiveObject["material"_view].GetDecimal());

			const JSON::Object& attributesObject = primitiveObject["attributes"_view].GetObject();
			HashTable<AttributeType, usize> attributes { 4, Allocator };

			if (attributesObject.HasKey("POSITION"_view))
			{
				attributes.Add(AttributeType::Position, static_cast<usize>(attributesObject["POSITION"_view].GetDecimal()));
			}
			if (attributesObject.HasKey("NORMAL"_view))
			{
				attributes.Add(AttributeType::Normal, static_cast<usize>(attributesObject["NORMAL"_view].GetDecimal()));
			}
			if (attributesObject.HasKey("TANGENT"_view))
			{
				attributes.Add(AttributeType::Tangent, static_cast<usize>(attributesObject["TANGENT"_view].GetDecimal()));
			}
			if (attributesObject.HasKey("TEXCOORD_0"_view))
			{
				attributes.Add(AttributeType::TexCoord0, static_cast<usize>(attributesObject["TEXCOORD_0"_view].GetDecimal()));
			}

			primitives.Emplace(Move(attributes), indices, material);
		}

		meshes.Emplace(Move(primitives));
	}

	Array<Image> images(Allocator);
	if (rootObject.HasKey("images"_view))
	{
		const JSON::Array& imageArray = rootObject["images"_view].GetArray();
		images.Reserve(imageArray.GetLength());
		for (const JSON::Value& imageValue : imageArray)
		{
			const JSON::Object& imageObject = imageValue.GetObject();

			const String& imagePath = imageObject["uri"_view].GetString();
			const String fullPath = ResolveFilePath(filePath, imagePath.AsView());

			images.Emplace(fullPath);
		}
	}

	Array<Texture> textures(Allocator);
	if (rootObject.HasKey("textures"_view))
	{
		const JSON::Array& textureArray = rootObject["textures"_view].GetArray();
		textures.Reserve(textureArray.GetLength());
		for (const JSON::Value& textureValue : textureArray)
		{
			const JSON::Object& textureObject = textureValue.GetObject();

			const usize image = static_cast<usize>(textureObject["source"_view].GetDecimal());

			const usize sampler = textureObject.HasKey("sampler"_view)
								? static_cast<usize>(textureObject["sampler"_view].GetDecimal())
								: INDEX_NONE;

			textures.Emplace(image, sampler);
		}
	}

	const JSON::Array& materialArray = rootObject["materials"_view].GetArray();
	Array<Material> materials(materialArray.GetLength(), Allocator);
	for (const JSON::Value& materialValue : materialArray)
	{
		Material material =
		{
			.NormalMapTexture = INDEX_NONE,
			.AlphaMode = AlphaMode::Opaque,
			.AlphaCutoff = 0.5f,
		};

		const JSON::Object& materialObject = materialValue.GetObject();

		if (materialObject.HasKey("pbrMetallicRoughness"_view))
		{
			material.IsSpecularGlossiness = false;

			material.MetallicRoughness.BaseColorTexture = INDEX_NONE;
			material.MetallicRoughness.BaseColorFactor = { .R = 1.0f, .G = 1.0f, .B = 1.0f, .A = 1.0f };

			material.MetallicRoughness.MetallicRoughnessTexture = INDEX_NONE;
			material.MetallicRoughness.MetallicFactor = 1.0f;
			material.MetallicRoughness.RoughnessFactor = 1.0f;

			const JSON::Object& pbrMetallicRoughnessObject = materialObject["pbrMetallicRoughness"_view].GetObject();

			if (pbrMetallicRoughnessObject.HasKey("baseColorTexture"_view))
			{
				const JSON::Object& baseColorTextureObject = pbrMetallicRoughnessObject["baseColorTexture"_view].GetObject();
				material.MetallicRoughness.BaseColorTexture = static_cast<usize>(baseColorTextureObject["index"_view].GetDecimal());
			}
			if (pbrMetallicRoughnessObject.HasKey("baseColorFactor"_view))
			{
				const JSON::Array& baseColorFactorArray = pbrMetallicRoughnessObject["baseColorFactor"_view].GetArray();
				material.MetallicRoughness.BaseColorFactor = ToFloat4(baseColorFactorArray);
			}

			if (pbrMetallicRoughnessObject.HasKey("metallicRoughnessTexture"_view))
			{
				const JSON::Object& metallicRoughnessTextureObject = pbrMetallicRoughnessObject["metallicRoughnessTexture"_view].GetObject();
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
			const JSON::Object& extensionsObject = materialObject["extensions"_view].GetObject();

			if (extensionsObject.HasKey("KHR_materials_pbrSpecularGlossiness"_view))
			{
				const JSON::Object& pbrSpecularGlossinessObject = extensionsObject["KHR_materials_pbrSpecularGlossiness"_view].GetObject();

				material.IsSpecularGlossiness = true;

				material.SpecularGlossiness.DiffuseTexture = INDEX_NONE;
				material.SpecularGlossiness.DiffuseFactor = { .R = 1.0f, .G = 1.0f, .B = 1.0f, .A = 1.0f };

				material.SpecularGlossiness.SpecularGlossinessTexture = INDEX_NONE;
				material.SpecularGlossiness.SpecularFactor = { .R = 1.0f, .G = 1.0f, .B = 1.0f };
				material.SpecularGlossiness.GlossinessFactor = 1.0f;

				if (pbrSpecularGlossinessObject.HasKey("diffuseTexture"_view))
				{
					const JSON::Object& diffuseTextureObject = pbrSpecularGlossinessObject["diffuseTexture"_view].GetObject();
					material.MetallicRoughness.BaseColorTexture = static_cast<usize>(diffuseTextureObject["index"_view].GetDecimal());
				}
				if (pbrSpecularGlossinessObject.HasKey("diffuseFactor"_view))
				{
					const JSON::Array& diffuseFactorArray = pbrSpecularGlossinessObject["diffuseFactor"_view].GetArray();
					material.MetallicRoughness.BaseColorFactor = ToFloat4(diffuseFactorArray);
				}

				if (pbrSpecularGlossinessObject.HasKey("metallicRoughnessTexture"_view))
				{
					const JSON::Object& specularGlossinessTextureObject = pbrSpecularGlossinessObject["specularGlossinessTexture"_view].GetObject();
					material.SpecularGlossiness.SpecularGlossinessTexture = static_cast<usize>(specularGlossinessTextureObject["index"_view].GetDecimal());
				}
				if (pbrSpecularGlossinessObject.HasKey("specularFactor"_view))
				{
					const JSON::Array& specularFactorArray = pbrSpecularGlossinessObject["specularFactor"_view].GetArray();
					const Float4 specularFactor = ToFloat4(specularFactorArray);
					material.SpecularGlossiness.SpecularFactor = { .R = specularFactor.R, .G = specularFactor.G, .B = specularFactor.B };
				}
				if (pbrSpecularGlossinessObject.HasKey("roughnessFactor"_view))
				{
					material.MetallicRoughness.RoughnessFactor = static_cast<float>(pbrSpecularGlossinessObject["roughnessFactor"_view].GetDecimal());
				}
			}
		}

		if (materialObject.HasKey("normalTexture"_view))
		{
			const JSON::Object& normalTextureObject = materialObject["normalTexture"_view].GetObject();
			material.NormalMapTexture = static_cast<usize>(normalTextureObject["index"_view].GetDecimal());
		}

		if (materialObject.HasKey("alphaMode"_view))
		{
			const String& alphaModeString = materialObject["alphaMode"_view].GetString();

			if (alphaModeString == "OPAQUE"_view)
			{
				material.AlphaMode = AlphaMode::Opaque;
			}
			else if (alphaModeString == "MASK"_view)
			{
				material.AlphaMode = AlphaMode::Mask;
			}
			else if (alphaModeString == "BLEND"_view)
			{
				material.AlphaMode = AlphaMode::Blend;
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

	const auto toFilter = [](usize filter, bool magnification) -> Filter
	{
		if (magnification)
		{
			CHECK(filter == 9728 || filter == 9729);
		}

		switch (filter)
		{
		case 9728:
			return Filter::Nearest;
		case 9729:
			return Filter::Linear;
		case 9984:
			return Filter::NearestMipMapNearest;
		case 9985:
			return Filter::LinearMipMapNearest;
		case 9986:
			return Filter::NearestMipMapLinear;
		case 9987:
			return Filter::LinearMipMapLinear;
		default:
			CHECK(false);
		}
		return Filter::Nearest;
	};

	const auto toAddress = [](usize address) -> Address
	{
		switch (address)
		{
		case 10497:
			return Address::Repeat;
		case 33071:
			return Address::ClampToEdge;
		case 33648:
			return Address::MirroredRepeat;
		default:
			CHECK(false);
		}
		return Address::Repeat;
	};

	Array<Sampler> samplers(Allocator);
	if (rootObject.HasKey("samplers"_view))
	{
		const JSON::Array& samplerArray = rootObject["samplers"_view].GetArray();
		samplers.Reserve(samplerArray.GetLength());
		for (const JSON::Value& samplerValue : samplerArray)
		{
			const JSON::Object& samplerObject = samplerValue.GetObject();

			const Filter minification = samplerObject.HasKey("minFilter"_view)
									  ? toFilter(static_cast<usize>(samplerObject["minFilter"_view].GetDecimal()), false)
									  : Filter::Linear;
			const Filter magnification = samplerObject.HasKey("magFilter"_view)
									   ? toFilter(static_cast<usize>(samplerObject["magFilter"_view].GetDecimal()), true)
									   : Filter::Linear;

			const Address horizontal = samplerObject.HasKey("wrapS"_view)
									 ? toAddress(static_cast<usize>(samplerObject["wrapS"_view].GetDecimal()))
									 : Address::Repeat;
			const Address vertical = samplerObject.HasKey("wrapT"_view)
								   ? toAddress(static_cast<usize>(samplerObject["wrapT"_view].GetDecimal()))
								   : Address::Repeat;

			samplers.Emplace(minification, magnification, horizontal, vertical);
		}
	}

	const JSON::Array& accessorArray = rootObject["accessors"_view].GetArray();
	Array<Accessor> accessors(accessorArray.GetLength(), Allocator);
	for (const JSON::Value& accessorValue : accessorArray)
	{
		const JSON::Object& accessorObject = accessorValue.GetObject();

		const usize bufferView = static_cast<usize>(accessorObject["bufferView"_view].GetDecimal());
		const usize count = static_cast<usize>(accessorObject["count"_view].GetDecimal());
		const String& accessorTypeString = accessorObject["type"_view].GetString();
		const usize componentTypeNumber = static_cast<usize>(accessorObject["componentType"_view].GetDecimal());

		usize offset = 0;
		if (accessorObject.HasKey("byteOffset"_view))
		{
			offset = static_cast<usize>(accessorObject["byteOffset"_view].GetDecimal());
		}

		ComponentType componentType;
		if (componentTypeNumber == 5120)
		{
			componentType = ComponentType::Int8;
		}
		else if (componentTypeNumber == 5121)
		{
			componentType = ComponentType::UInt8;
		}
		else if (componentTypeNumber == 5122)
		{
			componentType = ComponentType::UInt16;
		}
		else if (componentTypeNumber == 5123)
		{
			componentType = ComponentType::Int16;
		}
		else if (componentTypeNumber == 5125)
		{
			componentType = ComponentType::UInt32;
		}
		else if (componentTypeNumber == 5126)
		{
			componentType = ComponentType::Float32;
		}
		else
		{
			VERIFY(false, "Unexpected GLTF component type!");
		}

		AccessorType accessorType;
		if (accessorTypeString == "SCALAR"_view)
		{
			accessorType = AccessorType::Scalar;
		}
		else if (accessorTypeString == "VEC2"_view)
		{
			accessorType = AccessorType::Vector2;
		}
		else if (accessorTypeString == "VEC3"_view)
		{
			accessorType = AccessorType::Vector3;
		}
		else if (accessorTypeString == "VEC4"_view)
		{
			accessorType = AccessorType::Vector4;
		}
		else if (accessorTypeString == "MAT2"_view)
		{
			accessorType = AccessorType::Matrix2;
		}
		else if (accessorTypeString == "MAT3"_view)
		{
			accessorType = AccessorType::Matrix3;
		}
		else if (accessorTypeString == "MAT4"_view)
		{
			accessorType = AccessorType::Matrix4;
		}
		else
		{
			VERIFY(false, "Unexpected GLTF accessor type!");
		}

		accessors.Emplace(bufferView, count, offset, componentType, accessorType);
	}

	bool twoChannelNormalMaps = false;
	if (rootObject.HasKey("extras"_view))
	{
		const JSON::Object& extrasObject = rootObject["extras"_view].GetObject();

		if (extrasObject.HasKey("twoChannelNormalMaps"_view))
		{
			twoChannelNormalMaps = extrasObject["twoChannelNormalMaps"_view].GetBoolean();
		}
	}

	return Scene
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
		.Lights = Move(lights),
		.TwoChannelNormalMaps = twoChannelNormalMaps,
	};
}

void UnloadScene(Scene* scene)
{
	for (Buffer& buffer : scene->Buffers)
	{
		Allocator->Deallocate(buffer.Data, buffer.Size);
	}
}

Matrix CalculateGlobalTransform(const Scene& scene, usize nodeIndex)
{
	return InternalCalculateGlobalTransform(scene.Nodes, nodeIndex);
}

}
