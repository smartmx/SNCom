// sncom/src/core/json.h —— 极简 JSON（对象/数组/字符串/数字/布尔/空）
// 目的：零第三方依赖，读写与 PyCom 的 json.dump(ensure_ascii=False, indent=1) 对齐。
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace sncom {

class JsonValue;
using JsonPtr = std::shared_ptr<JsonValue>;

enum class JsonType { Null, Bool, Number, String, Array, Object };

class JsonValue {
public:
    JsonType type = JsonType::Null;
    bool      b   = false;
    double    num = 0.0;
    std::wstring str;
    std::vector<JsonPtr> arr;                                    // Array
    std::vector<std::pair<std::wstring, JsonPtr>> obj;           // Object（保持插入序）

    static JsonPtr Null();
    static JsonPtr Bool(bool v);
    static JsonPtr Num(double v);
    static JsonPtr Str(const std::wstring& v);
    static JsonPtr Arr();
    static JsonPtr Obj();

    bool isObject() const { return type == JsonType::Object; }
    bool isArray()  const { return type == JsonType::Array; }
    bool isString() const { return type == JsonType::String; }
    bool isNumber() const { return type == JsonType::Number; }
    bool isBool()   const { return type == JsonType::Bool; }
    bool isNull()   const { return type == JsonType::Null; }

    const JsonPtr* Get(const wchar_t* key) const;   // 对象取值，无则 nullptr
    void Set(const wchar_t* key, JsonPtr v);        // 对象写入（存在则替换，保持顺序）
    void Push(JsonPtr v);                           // 数组追加

    // 带默认值的取值（类型不符 → 返回默认）
    int          I(const wchar_t* k, int def) const;
    double       D(const wchar_t* k, double def) const;
    bool         B(const wchar_t* k, bool def) const;
    std::wstring S(const wchar_t* k, const std::wstring& def) const;

    std::wstring Dump(int indent = 1) const;
    static JsonPtr Parse(const std::wstring& text, std::wstring* err = nullptr);
};

// UTF-8 文件读写（写入不带 BOM；非 ASCII 原样输出，与 ensure_ascii=False 一致）
bool ReadFileUtf8(const std::wstring& path, std::wstring& out);
bool WriteFileUtf8(const std::wstring& path, const std::wstring& text);

} // namespace sncom
