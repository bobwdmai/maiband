#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bandforge {

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& message);
};

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue();
    JsonValue(std::nullptr_t);
    JsonValue(bool value);
    JsonValue(int value);
    JsonValue(double value);
    JsonValue(const char* value);
    JsonValue(std::string value);
    JsonValue(Array value);
    JsonValue(Object value);

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;

    [[nodiscard]] bool boolValue(bool fallback = false) const;
    [[nodiscard]] double numberValue(double fallback = 0.0) const;
    [[nodiscard]] int intValue(int fallback = 0) const;
    [[nodiscard]] std::string stringValue(std::string fallback = {}) const;

    [[nodiscard]] const Array& array() const;
    [[nodiscard]] Array& array();
    [[nodiscard]] const Object& object() const;
    [[nodiscard]] Object& object();

    [[nodiscard]] const JsonValue* find(const std::string& key) const;
    [[nodiscard]] JsonValue* find(const std::string& key);

    [[nodiscard]] std::string stringify(int indentSize = 2) const;

    static JsonValue parse(std::string_view text);

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage value_;
};

} // namespace bandforge
