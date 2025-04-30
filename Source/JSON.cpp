#include "JSON.hpp"

#include "Luft/Math.hpp"

namespace JSON
{

static Allocator* Allocator = &GlobalAllocator::Get();

static Array ParseArray(StringView buffer, usize* index);
static Object ParseObject(StringView buffer, usize* index);

Value::~Value()
{
	switch (Tag)
	{
	case Tag::Object:
		ObjectValue->~Object();
		Allocator->Deallocate(ObjectValue, sizeof(*ObjectValue));
		ObjectValue = nullptr;
		break;
	case Tag::Array:
		ArrayValue.~Array();
		break;
	case Tag::String:
		StringValue.~String();
		break;
	case Tag::Decimal:
		DecimalValue = 0.0;
		break;
	case Tag::Boolean:
		BooleanValue = false;
		break;
	case Tag::Null:
		NullValue = nullptr;
		break;
	case Tag::None:
		break;
	}
	Tag = Tag::None;
}

Value::Value(const Value& copy)
	: Tag(copy.Tag)
{
	switch (Tag)
	{
	case Tag::Object:
		ObjectValue = copy.ObjectValue;
		break;
	case Tag::Array:
		ArrayValue = copy.ArrayValue;
		break;
	case Tag::String:
		StringValue = copy.StringValue;
		break;
	case Tag::Decimal:
		DecimalValue = copy.DecimalValue;
		break;
	case Tag::Boolean:
		BooleanValue = copy.BooleanValue;
		break;
	case Tag::Null:
		NullValue = nullptr;
		break;
	case Tag::None:
		break;
	}
}

Value& Value::operator=(const Value& copy)
{
	if (&copy == this)
		return *this;

	Tag = copy.Tag;

	switch (Tag)
	{
	case Tag::Object:
		ObjectValue = copy.ObjectValue;
		break;
	case Tag::Array:
		ArrayValue = copy.ArrayValue;
		break;
	case Tag::String:
		StringValue = copy.StringValue;
		break;
	case Tag::Decimal:
		DecimalValue = copy.DecimalValue;
		break;
	case Tag::Boolean:
		BooleanValue = copy.BooleanValue;
		break;
	case Tag::Null:
		NullValue = nullptr;
		break;
	case Tag::None:
		break;
	}

	return *this;
}

Value::Value(Value&& move) noexcept
	: Tag(move.Tag)
{
	move.Tag = Tag::None;

	switch (Tag)
	{
	case Tag::Object:
		ObjectValue = move.ObjectValue;
		move.ObjectValue = nullptr;
		break;
	case Tag::Array:
		new (&ArrayValue, LuftNewMarker {}) Array { Move(move.ArrayValue) };
		break;
	case Tag::String:
		new (&StringValue, LuftNewMarker {}) String { Move(move.StringValue) };
		break;
	case Tag::Decimal:
		DecimalValue = move.DecimalValue;
		move.DecimalValue = 0.0;
		break;
	case Tag::Boolean:
		BooleanValue = move.BooleanValue;
		move.BooleanValue = false;
		break;
	case Tag::Null:
		NullValue = nullptr;
		move.NullValue = nullptr;
		break;
	case Tag::None:
		break;
	}
}

Value& Value::operator=(Value&& move) noexcept
{
	if (&move == this)
		return *this;

	this->~Value();

	Tag = move.Tag;
	move.Tag = Tag::None;

	switch (Tag)
	{
	case Tag::Object:
		ObjectValue = move.ObjectValue;
		move.ObjectValue = nullptr;
		break;
	case Tag::Array:
		ArrayValue = Move(move.ArrayValue);
		break;
	case Tag::String:
		StringValue = Move(move.StringValue);
		break;
	case Tag::Decimal:
		DecimalValue = move.DecimalValue;
		move.DecimalValue = 0.0;
		break;
	case Tag::Boolean:
		BooleanValue = move.BooleanValue;
		move.BooleanValue = false;
		break;
	case Tag::Null:
		NullValue = nullptr;
		move.NullValue = nullptr;
		break;
	case Tag::None:
		break;
	}

	return *this;
}

static bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

static bool IsSpace(char c)
{
	return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static void Advance(usize* index, usize count)
{
	CHECK(index);
	*index += count;
}

static bool IsInRange(StringView buffer, usize index, usize count = 0)
{
	return index + count < buffer.GetLength();
}

static void SkipWhitespace(StringView buffer, usize* index)
{
	CHECK(index);
	while (IsInRange(buffer, *index) && IsSpace(buffer[*index]))
	{
		*index += 1;
	}
}

static char PeekCharacter(StringView buffer, usize index)
{
	VERIFY(IsInRange(buffer, index), "Failed to read character!");
	return buffer[index];
}

static void ExpectCharacter(StringView buffer, usize* index, char c)
{
	CHECK(index);
	VERIFY(IsInRange(buffer, *index) && buffer[*index] == c, "Failed to parse expected character!");
	Advance(index, 1);
}

static void ExpectString(StringView buffer, usize* index, StringView expect)
{
	for (usize i = 0; i < expect.GetLength(); ++i)
	{
		ExpectCharacter(buffer, index, expect[i]);
	}
}

static uint64 ParseUint64(StringView buffer, usize* index)
{
	CHECK(index);
	usize offset = 0;
	uint64 factor = 1;
	while (IsInRange(buffer, *index + offset) && IsDigit(buffer[*index + offset]))
	{
		++offset;
		factor *= 10;
	}
	VERIFY(offset != 0, "Expected to read a number and failed!");

	uint64 result = 0;
	for (usize i = 0; i < offset; ++i)
	{
		factor /= 10;
		result += factor * (PeekCharacter(buffer, *index + i) - '0');
	}

	*index += offset;
	return result;
}

static int64 ParseInt64(StringView buffer, usize* index)
{
	CHECK(index);
	const bool isNegative = PeekCharacter(buffer, *index) == '-';
	const bool isSign = isNegative || PeekCharacter(buffer, *index) == '+';
	VERIFY(IsDigit(buffer[*index]) || isSign, "Expected to read a number and failed!");

	if (isSign)
	{
		Advance(index, 1);
	}
	const int64 value = static_cast<int64>(ParseUint64(buffer, index));
	return isNegative ? -value : value;
}

static double ParseDoubleNoExponent(StringView buffer, usize* index)
{
	CHECK(index);
	const bool isNegative = PeekCharacter(buffer, *index) == '-';
	const int64 whole = ParseInt64(buffer, index);

	uint64 fractional = 0;
	uint64 divisor = 1;
	if (PeekCharacter(buffer, *index) == '.')
	{
		Advance(index, 1);

		if (IsInRange(buffer, *index) && IsDigit(buffer[*index]))
		{
			const usize startingIndex = *index;
			fractional = ParseUint64(buffer, index);

			usize fractionalLength = *index - startingIndex;
			while (fractionalLength != 0)
			{
				divisor *= 10;
				--fractionalLength;
			}
		}
	}

	const float factor = isNegative ? -1.0f : 1.0f;
	return static_cast<double>(whole) + factor * (static_cast<double>(fractional) / static_cast<double>(divisor));
}

static void ParseEscapeSequence(StringView buffer, usize* index, String* result)
{
	const auto parseAnyOf = [](StringView buffer, usize* index, String* result, const char* any, usize anyLength) -> bool
	{
		CHECK(index);
		for (usize i = 0; i < anyLength; ++i)
		{
			if (PeekCharacter(buffer, *index) == any[i])
			{
				result->Append(any[i]);
				Advance(index, 1);
				return true;
			}
		}
		return false;
	};

	static constexpr char acceptableEscape[] = "\"\\/bfnrtu";
	static constexpr char acceptableHex[] = "0123456789abcdefABCDEF";

	ExpectCharacter(buffer, index, '\\');
	result->Append('\\');

	const bool matchedEscape = parseAnyOf(buffer, index, result, acceptableEscape, ARRAY_COUNT(acceptableEscape));
	VERIFY(matchedEscape, "Failed to parse escape sequence!");

	if (PeekCharacter(buffer, *index - 1) == 'u')
	{
		static constexpr usize codepointLength = 4;
		for (usize i = 0; i < codepointLength; ++i)
		{
			VERIFY(IsInRange(buffer, *index), "Failed to parse unicode codepoint!");
			const bool matchedHex = parseAnyOf(buffer, index, result, acceptableHex, ARRAY_COUNT(acceptableHex));
			VERIFY(matchedHex, "Failed to parse unicode codepoint!");
		}
	}
}

static String ParseString(StringView buffer, usize* index)
{
	CHECK(index);

	String result(16);

	ExpectCharacter(buffer, index, '"');
	while (PeekCharacter(buffer, *index) != '"')
	{
		const char c = buffer[*index];
		if (c == '\\')
		{
			ParseEscapeSequence(buffer, index, &result);
		}
		else
		{
			result.Append(c);
			Advance(index, 1);
		}
	}
	ExpectCharacter(buffer, index, '"');

	return result;
}

static double ParseNumber(StringView buffer, usize* index)
{
	const double mantissa = ParseDoubleNoExponent(buffer, index);
	int64 exponent = 0;
	if (PeekCharacter(buffer, *index) == 'e' || PeekCharacter(buffer, *index) == 'E')
	{
		Advance(index, 1);
		exponent = ParseInt64(buffer, index);
	}

	const double power = static_cast<double>(Power10(Absolute(exponent)));
	const double multiplier = (exponent >= 0) ? power : (1.0 / power);
	return mantissa * multiplier;
}

static Value ParseValue(StringView buffer, usize* index)
{
	CHECK(index);

	SkipWhitespace(buffer, index);

	Value value = {};

	const char leading = PeekCharacter(buffer, *index);
	if (leading == '"')
	{
		value = Value { ParseString(buffer, index) };
	}
	else if (IsDigit(leading) || leading == '-' || leading == '+')
	{
		value = Value { ParseNumber(buffer, index) };
	}
	else if (leading == '{')
	{
		Object* object = Allocator->Create<Object>(ParseObject(buffer, index));

		value = Value { object };
	}
	else if (leading == '[')
	{
		value = Value { ParseArray(buffer, index) };
	}
	else if (leading == 't')
	{
		ExpectString(buffer, index, "true"_view);

		value = Value { true };
	}
	else if (leading == 'f')
	{
		ExpectString(buffer, index, "false"_view);

		value = Value { false };
	}
	else if (leading == 'n')
	{
		ExpectString(buffer, index, "null"_view);

		value = Value { Tag::Null };
	}
	else
	{
		VERIFY(false, "Failed to parse JSON value!");
	}
	SkipWhitespace(buffer, index);

	return value;
}

static Array ParseArray(StringView buffer, usize* index)
{
	CHECK(index);

	ExpectCharacter(buffer, index, '[');
	SkipWhitespace(buffer, index);

	if (PeekCharacter(buffer, *index) == ']')
	{
		Advance(index, 1);
		return Array(Allocator);
	}

	Array array(Allocator);
	while (IsInRange(buffer, *index))
	{
		array.Add(ParseValue(buffer, index));

		if (PeekCharacter(buffer, *index) != ',')
		{
			break;
		}
		ExpectCharacter(buffer, index, ',');
	}
	ExpectCharacter(buffer, index, ']');

	return array;
}

static Object ParseObject(StringView buffer, usize* index)
{
	CHECK(index);

	SkipWhitespace(buffer, index);
	ExpectCharacter(buffer, index, '{');
	SkipWhitespace(buffer, index);

	if (PeekCharacter(buffer, *index) == '}')
	{
		Advance(index, 1);
		return Object {};
	}

	static constexpr usize objectBucketCount = 8;
	HashTable<String, Value> object(objectBucketCount, Allocator);

	while (IsInRange(buffer, *index))
	{
		String key = ParseString(buffer, index);
		SkipWhitespace(buffer, index);
		ExpectCharacter(buffer, index, ':');
		Value value = ParseValue(buffer, index);

		object.Add(Move(key), Move(value));

		if (PeekCharacter(buffer, *index) != ',')
		{
			break;
		}
		ExpectCharacter(buffer, index, ',');
		SkipWhitespace(buffer, index);
	}
	ExpectCharacter(buffer, index, '}');

	return Object { Move(object) };
}

Object Load(StringView filePath)
{
	usize fileSize;
	char* fileData = reinterpret_cast<char*>(Platform::ReadEntireFile(filePath.GetData(), filePath.GetLength(), &fileSize, *Allocator));
	const StringView fileView = { fileData, fileSize };

	usize index = 0;
	const Object object = ParseObject(fileView, &index);

	Allocator->Deallocate(fileData, fileSize);
	return object;
}

}
