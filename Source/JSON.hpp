#pragma once

#include "Luft/Array.hpp"
#include "Luft/HashTable.hpp"
#include "Luft/String.hpp"

class JsonObject;
class JsonValue;

using JsonArray = Array<JsonValue>;

enum class JsonTag
{
	None,
	Object,
	Array,
	String,
	Decimal,
	Boolean,
	Null,
};

class JsonValue
{
public:
	JsonValue()
	{
		Platform::MemorySet(this, 0, sizeof(JsonValue));
	}

	explicit JsonValue(JsonTag null)
		: NullValue(nullptr)
		, Tag(JsonTag::Null)
	{
		CHECK(null == JsonTag::Null);
	}

	explicit JsonValue(JsonObject* owning)
		: ObjectValue(owning)
		, Tag(JsonTag::Object)
	{
	}

	explicit JsonValue(JsonArray&& array)
		: ArrayValue(Move(array))
		, Tag(JsonTag::Array)
	{
	}

	explicit JsonValue(String&& string)
		: StringValue(Move(string))
		, Tag(JsonTag::String)
	{
	}

	explicit JsonValue(double decimal)
		: DecimalValue(decimal)
		, Tag(JsonTag::Decimal)
	{
	}

	explicit JsonValue(bool boolean)
		: BooleanValue(boolean)
		, Tag(JsonTag::Boolean)
	{
	}

	~JsonValue();

	JsonValue(const JsonValue& copy);
	JsonValue& operator=(const JsonValue& copy);

	JsonValue(JsonValue&& move);
	JsonValue& operator=(JsonValue&& move);

	JsonTag GetTag() const
	{
		return Tag;
	}

	const JsonObject& GetObject() const
	{
		VERIFY(Tag == JsonTag::Object, "Unexpected JSON value type!");
		CHECK(ObjectValue);
		return *ObjectValue;
	}

	const JsonArray& GetArray() const
	{
		VERIFY(Tag == JsonTag::Array, "Unexpected JSON value type!");
		return ArrayValue;
	}

	const String& GetString() const
	{
		VERIFY(Tag == JsonTag::String, "Unexpected JSON value type!");
		return StringValue;
	}

	double GetDecimal() const
	{
		VERIFY(Tag == JsonTag::Decimal, "Unexpected JSON value type!");
		return DecimalValue;
	}

	bool GetBoolean() const
	{
		VERIFY(Tag == JsonTag::Boolean, "Unexpected JSON value type!");
		return BooleanValue;
	}

private:
	union
	{
		JsonObject* ObjectValue;
		JsonArray ArrayValue;
		String StringValue;
		double DecimalValue;
		bool BooleanValue;
		void* NullValue;
	};
	JsonTag Tag;
};

class JsonObject
{
public:
	JsonObject()
		: Objects(1)
	{
	}

	explicit JsonObject(HashTable<String, JsonValue>&& objects)
		: Objects(Move(objects))
	{
	}

	JsonValue& operator[](StringView key)
	{
		VERIFY(HasKey(key), "Key not present in JSON object!");
		return Objects[key];
	}

	const JsonValue& operator[](StringView key) const
	{
		VERIFY(HasKey(key), "Key not present in JSON object!");
		return Objects[key];
	}

	bool HasKey(StringView key) const
	{
		return Objects.Contains(key);
	}

private:
	HashTable<String, JsonValue> Objects;
};

JsonObject LoadJson(StringView filePath);
