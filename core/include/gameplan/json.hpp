// Minimal, dependency-free JSON value/parser/writer used for season packages,
// robot descriptions, match headers and reports. Deterministic output ordering
// (objects keep insertion order) so hashes of written files are reproducible.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gp {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(int v) : type_(Type::Number), n_(v) {}
    Json(long v) : type_(Type::Number), n_(double(v)) {}
    Json(long long v) : type_(Type::Number), n_(double(v)) {}
    Json(unsigned v) : type_(Type::Number), n_(v) {}
    Json(unsigned long v) : type_(Type::Number), n_(double(v)) {}
    Json(unsigned long long v) : type_(Type::Number), n_(double(v)) {}
    Json(double v) : type_(Type::Number), n_(v) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    static Json parse(const std::string& text);
    static Json parseFile(const std::string& path);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }
    bool isBool() const { return type_ == Type::Bool; }

    double num() const;
    double num(double def) const { return isNumber() ? n_ : def; }
    int integer() const { return int(num()); }
    bool boolean() const;
    bool boolean(bool def) const { return isBool() ? b_ : def; }
    const std::string& str() const;
    std::string str(const std::string& def) const { return isString() ? s_ : def; }

    // Array
    size_t size() const;
    const Json& operator[](size_t i) const;
    Json& operator[](size_t i);
    const Json& operator[](int i) const { return (*this)[size_t(i)]; }
    Json& operator[](int i) { return (*this)[size_t(i)]; }
    void push(Json v);
    const Array& items() const { return a_; }

    // Object
    bool has(const std::string& k) const;
    const Json& operator[](const std::string& k) const;  // returns Null json if missing
    const Json& operator[](const char* k) const { return (*this)[std::string(k)]; }
    Json& set(const std::string& k, Json v);
    const Object& members() const { return o_; }
    double get(const std::string& k, double def) const { return (*this)[k].num(def); }
    std::string get(const std::string& k, const char* def) const { return (*this)[k].str(def); }

    std::string dump(int indent = -1) const;
    void dumpTo(std::string& out, int indent, int depth) const;

private:
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    Array a_;
    Object o_;
};

std::string readTextFile(const std::string& path);
void writeTextFile(const std::string& path, const std::string& text);
std::string fnv1a64Hex(const std::string& data);
std::string sha256Hex(const std::string& data);

}  // namespace gp
