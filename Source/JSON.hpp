#pragma once

#include "Luft/Array.hpp"
#include "Luft/HashTable.hpp"
#include "Luft/String.hpp"

namespace JSON
{

class Object;
class Value;

using Array = Array<Value>;

enum class Tag : uint8
{
	None,
	Object,
	Array,
	String,
	Decimal,
	Boolean,
	Null,
};

class Value
{
public:
	Value()
	{
		Platform::MemorySet(this, 0, sizeof(Value));
	}

	explicit Value(Tag null)
		: NullValue(nullptr)
		, Tag(Tag::Null)
	{
		CHECK(null == Tag::Null);
	}

	explicit Value(Object* owning)
		: ObjectValue(owning)
		, Tag(Tag::Object)
	{
	}

	explicit Value(Array&& array)
		: ArrayValue(Move(array))
		, Tag(Tag::Array)
	{
	}

	explicit Value(String&& string)
		: StringValue(Move(string))
		, Tag(Tag::String)
	{
	}

	explicit Value(double decimal)
		: DecimalValue(decimal)
		, Tag(Tag::Decimal)
	{
	}

	explicit Value(bool boolean)
		: BooleanValue(boolean)
		, Tag(Tag::Boolean)
	{
	}

	~Value();

	Value(const Value& copy);
	Value& operator=(const Value& copy);

	Value(Value&& move) noexcept;
	Value& operator=(Value&& move) noexcept;

	Tag GetTag() const
	{
		return Tag;
	}

	const Object& GetObject() const
	{
		VERIFY(Tag == Tag::Object, "Unexpected JSON value type!");
		CHECK(ObjectValue);
		return *ObjectValue;
	}

	const Array& GetArray() const
	{
		VERIFY(Tag == Tag::Array, "Unexpected JSON value type!");
		return ArrayValue;
	}

	const String& GetString() const
	{
		VERIFY(Tag == Tag::String, "Unexpected JSON value type!");
		return StringValue;
	}

	double GetDecimal() const
	{
		VERIFY(Tag == Tag::Decimal, "Unexpected JSON value type!");
		return DecimalValue;
	}

	bool GetBoolean() const
	{
		VERIFY(Tag == Tag::Boolean, "Unexpected JSON value type!");
		return BooleanValue;
	}

private:
	union
	{
		Object* ObjectValue;
		Array ArrayValue;
		String StringValue;
		double DecimalValue;
		bool BooleanValue;
		void* NullValue;
	};
	Tag Tag;
};

class Object
{
public:
	Object()
		: Objects(1)
	{
	}

	explicit Object(HashTable<String, Value>&& objects)
		: Objects(Move(objects))
	{
	}

	Value& operator[](StringView key)
	{
		VERIFY(HasKey(key), "Key not present in JSON object!");
		return Objects[key];
	}

	const Value& operator[](StringView key) const
	{
		VERIFY(HasKey(key), "Key not present in JSON object!");
		return Objects[key];
	}

	bool HasKey(StringView key) const
	{
		return Objects.Contains(key);
	}

private:
	HashTable<String, Value> Objects;
};

Object Load(StringView filePath);

}
