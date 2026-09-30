#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace km {

// Minimal JSON document model; strings are UTF-8.
struct JValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JValue> arr;
    std::vector<std::pair<std::string, JValue>> obj;  // Keeps insertion order.

    static JValue Bool(bool v);
    static JValue Number(double v);
    static JValue String(std::string v);
    static JValue Array();
    static JValue Object();

    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const JValue* find(std::string_view key) const;
    JValue& set(std::string key, JValue v);
    JValue& push(JValue v);
};

bool ParseJson(std::string_view text, JValue& out, std::string& error);
std::string WriteJson(const JValue& v);  // Pretty-printed with two-space indent.

}  // namespace km
