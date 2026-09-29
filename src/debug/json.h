#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace directcraft::debug {

class Json {
public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(double value);
    Json(int value);
    Json(const char* value);
    Json(std::string value);
    Json(Array value);
    Json(Object value);

    static Json object();
    static Json array();
    static Json parse(const std::string& text);

    Type type() const { return value_.index() == 0 ? Type::Null : static_cast<Type>(value_.index()); }
    bool isNull() const;
    bool isObject() const;
    bool isArray() const;
    bool isString() const;
    bool isNumber() const;
    bool isBoolean() const;
    bool boolean(bool fallback = false) const;
    double number(double fallback = 0.0) const;
    int integer(int fallback = 0) const;
    std::string string(const std::string& fallback = {}) const;
    const Json* find(const std::string& key) const;
    Json* find(const std::string& key);
    const Json& at(const std::string& key) const;
    Json& operator[](const std::string& key);
    const Array& values() const;
    Array& values();
    std::string dump() const;

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage value_;
};

} // namespace directcraft::debug
