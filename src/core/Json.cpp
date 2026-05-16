#include "core/Json.h"

#include <charconv>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace bandforge {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text)
        : text_(text)
    {
    }

    JsonValue parse()
    {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (!atEnd()) {
            fail("Unexpected trailing characters");
        }
        return value;
    }

private:
    JsonValue parseValue()
    {
        skipWhitespace();
        if (atEnd()) {
            fail("Expected a JSON value");
        }

        const char ch = peek();
        if (ch == '{') {
            return parseObject();
        }
        if (ch == '[') {
            return parseArray();
        }
        if (ch == '"') {
            return parseString();
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return parseNumber();
        }
        if (consumeLiteral("true")) {
            return JsonValue(true);
        }
        if (consumeLiteral("false")) {
            return JsonValue(false);
        }
        if (consumeLiteral("null")) {
            return JsonValue(nullptr);
        }

        fail("Unexpected character");
    }

    JsonValue parseObject()
    {
        expect('{');
        JsonValue::Object object;
        skipWhitespace();
        if (tryConsume('}')) {
            return object;
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("Expected object key string");
            }
            auto key = parseString().stringValue();
            skipWhitespace();
            expect(':');
            object.emplace(std::move(key), parseValue());
            skipWhitespace();

            if (tryConsume('}')) {
                break;
            }
            expect(',');
        }

        return object;
    }

    JsonValue parseArray()
    {
        expect('[');
        JsonValue::Array array;
        skipWhitespace();
        if (tryConsume(']')) {
            return array;
        }

        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (tryConsume(']')) {
                break;
            }
            expect(',');
        }

        return array;
    }

    JsonValue parseString()
    {
        expect('"');
        std::string result;
        while (!atEnd()) {
            const char ch = get();
            if (ch == '"') {
                return result;
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }

            if (atEnd()) {
                fail("Unterminated escape sequence");
            }

            const char escaped = get();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                appendUnicodeEscape(result);
                break;
            default:
                fail("Unsupported escape sequence");
            }
        }

        fail("Unterminated string");
    }

    JsonValue parseNumber()
    {
        const std::size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }

        consumeDigits();
        if (!atEnd() && peek() == '.') {
            ++position_;
            consumeDigits();
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++position_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                ++position_;
            }
            consumeDigits();
        }

        const auto token = text_.substr(start, position_ - start);
        double value = 0.0;
        const auto begin = token.data();
        const auto end = token.data() + token.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end) {
            fail("Invalid number");
        }
        return value;
    }

    static unsigned int parseHex4(std::string_view text, std::size_t pos)
    {
        unsigned int cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = text[pos + static_cast<std::size_t>(i)];
            cp <<= 4U;
            if (ch >= '0' && ch <= '9')      cp += static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') cp += static_cast<unsigned int>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') cp += static_cast<unsigned int>(ch - 'A' + 10);
            else return 0xFFFFFFFFU; // sentinel: invalid
        }
        return cp;
    }

    static void appendUtf8(std::string& result, unsigned int cp)
    {
        if (cp <= 0x7F) {
            result.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFF) {
            result.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 6)  & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            result.push_back('?'); // out-of-range replacement
        }
    }

    void appendUnicodeEscape(std::string& result)
    {
        if (position_ + 4 > text_.size())
            fail("Incomplete unicode escape");

        const unsigned int cp1 = parseHex4(text_, position_);
        if (cp1 == 0xFFFFFFFFU) fail("Invalid unicode escape");
        position_ += 4;

        // High surrogate — look for low surrogate pair \uDC00–\uDFFF
        if (cp1 >= 0xD800u && cp1 <= 0xDBFFu) {
            if (position_ + 6 <= text_.size()
                && text_[position_] == '\\'
                && text_[position_ + 1] == 'u') {
                const unsigned int cp2 = parseHex4(text_, position_ + 2);
                if (cp2 != 0xFFFFFFFFU && cp2 >= 0xDC00u && cp2 <= 0xDFFFu) {
                    position_ += 6;
                    const unsigned int full = 0x10000u + ((cp1 - 0xD800u) << 10) + (cp2 - 0xDC00u);
                    appendUtf8(result, full);
                    return;
                }
            }
            // Unpaired high surrogate — emit replacement
            result.push_back('?');
            return;
        }

        // Low surrogate without preceding high surrogate
        if (cp1 >= 0xDC00u && cp1 <= 0xDFFFu) {
            result.push_back('?');
            return;
        }

        appendUtf8(result, cp1);
    }

    void consumeDigits()
    {
        const std::size_t start = position_;
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++position_;
        }
        if (position_ == start) {
            fail("Expected digit");
        }
    }

    bool consumeLiteral(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) == literal) {
            position_ += literal.size();
            return true;
        }
        return false;
    }

    void skipWhitespace()
    {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++position_;
        }
    }

    void expect(char expected)
    {
        if (atEnd() || get() != expected) {
            fail(std::string("Expected '") + expected + "'");
        }
    }

    bool tryConsume(char expected)
    {
        if (!atEnd() && peek() == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] char peek() const
    {
        return text_[position_];
    }

    char get()
    {
        return text_[position_++];
    }

    [[nodiscard]] bool atEnd() const noexcept
    {
        return position_ >= text_.size();
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        std::ostringstream stream;
        stream << message << " at byte " << position_;
        throw JsonError(stream.str());
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

std::string escapeString(const std::string& value)
{
    std::ostringstream stream;
    stream << '"';
    for (const char ch : value) {
        switch (ch) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(static_cast<unsigned char>(ch))
                       << std::dec << std::setfill(' ');
            } else {
                stream << ch;
            }
            break;
        }
    }
    stream << '"';
    return stream.str();
}

void stringifyInto(std::ostringstream& stream, const JsonValue& value, int indentSize, int depth)
{
    const auto writeIndent = [&]() {
        for (int i = 0; i < depth * indentSize; ++i) {
            stream << ' ';
        }
    };

    if (value.isNull()) {
        stream << "null";
    } else if (value.isBool()) {
        stream << (value.boolValue() ? "true" : "false");
    } else if (value.isNumber()) {
        stream << std::setprecision(std::numeric_limits<double>::digits10 + 1)
               << value.numberValue();
    } else if (value.isString()) {
        stream << escapeString(value.stringValue());
    } else if (value.isArray()) {
        const auto& array = value.array();
        stream << '[';
        if (!array.empty()) {
            stream << '\n';
            for (std::size_t i = 0; i < array.size(); ++i) {
                for (int j = 0; j < (depth + 1) * indentSize; ++j) {
                    stream << ' ';
                }
                stringifyInto(stream, array[i], indentSize, depth + 1);
                if (i + 1 != array.size()) {
                    stream << ',';
                }
                stream << '\n';
            }
            writeIndent();
        }
        stream << ']';
    } else {
        const auto& object = value.object();
        stream << '{';
        if (!object.empty()) {
            stream << '\n';
            std::size_t index = 0;
            for (const auto& [key, child] : object) {
                for (int j = 0; j < (depth + 1) * indentSize; ++j) {
                    stream << ' ';
                }
                stream << escapeString(key) << ": ";
                stringifyInto(stream, child, indentSize, depth + 1);
                if (++index != object.size()) {
                    stream << ',';
                }
                stream << '\n';
            }
            writeIndent();
        }
        stream << '}';
    }
}

} // namespace

JsonError::JsonError(const std::string& message)
    : std::runtime_error(message)
{
}

JsonValue::JsonValue()
    : value_(nullptr)
{
}

JsonValue::JsonValue(std::nullptr_t)
    : value_(nullptr)
{
}

JsonValue::JsonValue(bool value)
    : value_(value)
{
}

JsonValue::JsonValue(int value)
    : value_(static_cast<double>(value))
{
}

JsonValue::JsonValue(double value)
    : value_(value)
{
}

JsonValue::JsonValue(const char* value)
    : value_(std::string(value))
{
}

JsonValue::JsonValue(std::string value)
    : value_(std::move(value))
{
}

JsonValue::JsonValue(Array value)
    : value_(std::move(value))
{
}

JsonValue::JsonValue(Object value)
    : value_(std::move(value))
{
}

bool JsonValue::isNull() const noexcept
{
    return std::holds_alternative<std::nullptr_t>(value_);
}

bool JsonValue::isBool() const noexcept
{
    return std::holds_alternative<bool>(value_);
}

bool JsonValue::isNumber() const noexcept
{
    return std::holds_alternative<double>(value_);
}

bool JsonValue::isString() const noexcept
{
    return std::holds_alternative<std::string>(value_);
}

bool JsonValue::isArray() const noexcept
{
    return std::holds_alternative<Array>(value_);
}

bool JsonValue::isObject() const noexcept
{
    return std::holds_alternative<Object>(value_);
}

bool JsonValue::boolValue(bool fallback) const
{
    return isBool() ? std::get<bool>(value_) : fallback;
}

double JsonValue::numberValue(double fallback) const
{
    return isNumber() ? std::get<double>(value_) : fallback;
}

int JsonValue::intValue(int fallback) const
{
    return isNumber() ? static_cast<int>(std::get<double>(value_)) : fallback;
}

std::string JsonValue::stringValue(std::string fallback) const
{
    return isString() ? std::get<std::string>(value_) : std::move(fallback);
}

const JsonValue::Array& JsonValue::array() const
{
    if (!isArray()) {
        throw JsonError("JSON value is not an array");
    }
    return std::get<Array>(value_);
}

JsonValue::Array& JsonValue::array()
{
    if (!isArray()) {
        throw JsonError("JSON value is not an array");
    }
    return std::get<Array>(value_);
}

const JsonValue::Object& JsonValue::object() const
{
    if (!isObject()) {
        throw JsonError("JSON value is not an object");
    }
    return std::get<Object>(value_);
}

JsonValue::Object& JsonValue::object()
{
    if (!isObject()) {
        throw JsonError("JSON value is not an object");
    }
    return std::get<Object>(value_);
}

const JsonValue* JsonValue::find(const std::string& key) const
{
    if (!isObject()) {
        return nullptr;
    }
    const auto& objectValue = object();
    const auto found = objectValue.find(key);
    return found == objectValue.end() ? nullptr : &found->second;
}

JsonValue* JsonValue::find(const std::string& key)
{
    if (!isObject()) {
        return nullptr;
    }
    auto& objectValue = object();
    const auto found = objectValue.find(key);
    return found == objectValue.end() ? nullptr : &found->second;
}

std::string JsonValue::stringify(int indentSize) const
{
    std::ostringstream stream;
    stringifyInto(stream, *this, indentSize, 0);
    return stream.str();
}

JsonValue JsonValue::parse(std::string_view text)
{
    return Parser(text).parse();
}

} // namespace bandforge
