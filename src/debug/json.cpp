#include "debug/json.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace directcraft::debug {
namespace {
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}
    Json parse() { skip(); Json result = value(); skip(); if (position_ != text_.size()) fail("trailing characters"); return result; }
private:
    const std::string& text_;
    std::size_t position_{0};
    [[noreturn]] void fail(const char* message) const { throw std::runtime_error(std::string("JSON parse error at ") + std::to_string(position_) + ": " + message); }
    void skip() { while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_; }
    bool consume(char expected) { skip(); if (position_ < text_.size() && text_[position_] == expected) { ++position_; return true; } return false; }
    Json value() {
        skip(); if (position_ >= text_.size()) fail("unexpected end");
        switch (text_[position_]) {
            case '{': return object(); case '[': return array(); case '"': return Json(string());
            case 't': literal("true"); return Json(true); case 'f': literal("false"); return Json(false); case 'n': literal("null"); return Json(nullptr);
            default: return Json(number());
        }
    }
    void literal(const char* literalText) { while (*literalText) { if (position_ >= text_.size() || text_[position_++] != *literalText++) fail("invalid literal"); } }
    std::string string() {
        if (!consume('"')) fail("expected string"); std::string result;
        while (position_ < text_.size()) { const char character = text_[position_++]; if (character == '"') return result; if (character != '\\') { result += character; continue; }
            if (position_ >= text_.size()) fail("invalid escape"); const char escaped = text_[position_++];
            switch (escaped) { case '"': result += '"'; break; case '\\': result += '\\'; break; case '/': result += '/'; break; case 'b': result += '\b'; break; case 'f': result += '\f'; break; case 'n': result += '\n'; break; case 'r': result += '\r'; break; case 't': result += '\t'; break; default: fail("unsupported escape"); }
        }
        fail("unterminated string");
    }
    double number() {
        skip(); const std::size_t start = position_; if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) ++position_;
        while (position_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[position_])) || text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E' || text_[position_] == '-' || text_[position_] == '+')) ++position_;
        try { return std::stod(text_.substr(start, position_ - start)); } catch (...) { fail("invalid number"); }
    }
    Json object() {
        Json result = Json::object(); consume('{'); skip(); if (consume('}')) return result;
        while (true) { skip(); const std::string key = string(); if (!consume(':')) fail("expected colon"); result[key] = value(); if (consume('}')) return result; if (!consume(',')) fail("expected comma"); }
    }
    Json array() {
        Json result = Json::array(); consume('['); skip(); if (consume(']')) return result;
        while (true) { result.values().push_back(value()); if (consume(']')) return result; if (!consume(',')) fail("expected comma"); }
    }
};

std::string escape(const std::string& value) { std::string result = "\""; for (const char character : value) { switch (character) { case '"': result += "\\\""; break; case '\\': result += "\\\\"; break; case '\n': result += "\\n"; break; case '\r': result += "\\r"; break; case '\t': result += "\\t"; break; default: result += character; } } return result + '"'; }
}

Json::Json() : value_(nullptr) {}
Json::Json(std::nullptr_t) : value_(nullptr) {}
Json::Json(bool value) : value_(value) {}
Json::Json(double value) : value_(value) {}
Json::Json(int value) : value_(static_cast<double>(value)) {}
Json::Json(const char* value) : value_(std::string(value)) {}
Json::Json(std::string value) : value_(std::move(value)) {}
Json::Json(Array value) : value_(std::move(value)) {}
Json::Json(Object value) : value_(std::move(value)) {}
Json Json::object() { return Json(Object{}); }
Json Json::array() { return Json(Array{}); }
Json Json::parse(const std::string& text) { return Parser(text).parse(); }
bool Json::isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
bool Json::isObject() const { return std::holds_alternative<Object>(value_); }
bool Json::isArray() const { return std::holds_alternative<Array>(value_); }
bool Json::isString() const { return std::holds_alternative<std::string>(value_); }
bool Json::isNumber() const { return std::holds_alternative<double>(value_); }
bool Json::isBoolean() const { return std::holds_alternative<bool>(value_); }
bool Json::boolean(bool fallback) const { return isBoolean() ? std::get<bool>(value_) : fallback; }
double Json::number(double fallback) const { return isNumber() ? std::get<double>(value_) : fallback; }
int Json::integer(int fallback) const { return isNumber() ? static_cast<int>(std::get<double>(value_)) : fallback; }
std::string Json::string(const std::string& fallback) const { return isString() ? std::get<std::string>(value_) : fallback; }
const Json* Json::find(const std::string& key) const { if (!isObject()) return nullptr; const auto& objectValue = std::get<Object>(value_); const auto iterator = objectValue.find(key); return iterator == objectValue.end() ? nullptr : &iterator->second; }
Json* Json::find(const std::string& key) { if (!isObject()) return nullptr; auto& objectValue = std::get<Object>(value_); const auto iterator = objectValue.find(key); return iterator == objectValue.end() ? nullptr : &iterator->second; }
const Json& Json::at(const std::string& key) const { const auto* found = find(key); if (!found) throw std::runtime_error("missing JSON field: " + key); return *found; }
Json& Json::operator[](const std::string& key) { if (!isObject()) value_ = Object{}; return std::get<Object>(value_)[key]; }
const Json::Array& Json::values() const { if (!isArray()) throw std::runtime_error("JSON value is not an array"); return std::get<Array>(value_); }
Json::Array& Json::values() { if (!isArray()) value_ = Array{}; return std::get<Array>(value_); }
std::string Json::dump() const {
    if (isNull()) return "null"; if (isBoolean()) return boolean() ? "true" : "false"; if (isNumber()) { std::ostringstream stream; stream << std::setprecision(15) << number(); return stream.str(); }
    if (isString()) return escape(string()); if (isArray()) { std::string result = "["; bool first = true; for (const auto& item : values()) { if (!first) result += ','; first = false; result += item.dump(); } return result + ']'; }
    std::string result = "{"; bool first = true; for (const auto& [key, item] : std::get<Object>(value_)) { if (!first) result += ','; first = false; result += escape(key) + ':' + item.dump(); } return result + '}';
}
} // namespace directcraft::debug
