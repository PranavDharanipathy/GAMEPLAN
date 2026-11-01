#include "gameplan/error.hpp"
#include "gameplan/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gp {

namespace {
const Json kNull;

struct Parser {
    const std::string& t;
    size_t i = 0;
    explicit Parser(const std::string& s) : t(s) {}
    [[noreturn]] void fail(const char* msg) {
        size_t line = 1;
        for (size_t k = 0; k < i && k < t.size(); ++k) if (t[k] == '\n') ++line;
        GP_THROW(std::runtime_error(std::string("JSON parse error: ") + msg + " at line " + std::to_string(line)));
    }
    void ws() {
        while (i < t.size()) {
            char c = t[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {  // tolerate // comments in hand-written configs
                while (i < t.size() && t[i] != '\n') ++i;
                continue;
            }
            break;
        }
    }
    Json value() {
        ws();
        if (i >= t.size()) fail("unexpected end");
        char c = t[i];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return Json(string());
        if (c == 't' && t.compare(i, 4, "true") == 0) { i += 4; return Json(true); }
        if (c == 'f' && t.compare(i, 5, "false") == 0) { i += 5; return Json(false); }
        if (c == 'n' && t.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        return number();
    }
    Json number() {
        size_t s = i;
        if (t[i] == '-' || t[i] == '+') ++i;
        while (i < t.size() && (isdigit((unsigned char)t[i]) || t[i] == '.' || t[i] == 'e' || t[i] == 'E' || t[i] == '-' || t[i] == '+')) ++i;
        if (s == i) fail("bad value");
        return Json(std::strtod(t.substr(s, i - s).c_str(), nullptr));
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    std::string string() {
        ++i;
        std::string o;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\') {
                if (i >= t.size()) fail("bad escape");
                char e = t[i++];
                switch (e) {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u': {
                        if (i + 4 > t.size()) fail("bad unicode");
                        unsigned cp = std::stoul(t.substr(i, 4), nullptr, 16); i += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                            unsigned lo = std::stoul(t.substr(i + 2, 4), nullptr, 16); i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(o, cp); break;
                    }
                    default: o += e;
                }
            } else o += c;
        }
        if (i >= t.size()) fail("unterminated string");
        ++i;
        return o;
    }
    Json array() {
        ++i;
        Json a = Json::array();
        ws();
        if (i < t.size() && t[i] == ']') { ++i; return a; }
        for (;;) {
            a.push(value());
            ws();
            if (i < t.size() && t[i] == ',') { ++i; ws(); if (i < t.size() && t[i] == ']') { ++i; return a; } continue; }
            if (i < t.size() && t[i] == ']') { ++i; return a; }
            fail("expected , or ]");
        }
    }
    Json object() {
        ++i;
        Json o = Json::object();
        ws();
        if (i < t.size() && t[i] == '}') { ++i; return o; }
        for (;;) {
            ws();
            if (i >= t.size() || t[i] != '"') fail("expected key");
            std::string k = string();
            ws();
            if (i >= t.size() || t[i] != ':') fail("expected :");
            ++i;
            o.set(k, value());
            ws();
            if (i < t.size() && t[i] == ',') { ++i; ws(); if (i < t.size() && t[i] == '}') { ++i; return o; } continue; }
            if (i < t.size() && t[i] == '}') { ++i; return o; }
            fail("expected , or }");
        }
    }
};

void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += char(c);
        }
    }
    out += '"';
}
}  // namespace

Json Json::parse(const std::string& text) {
    Parser p(text);
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing characters");
    return v;
}
Json Json::parseFile(const std::string& path) { return parse(readTextFile(path)); }

double Json::num() const {
    if (type_ == Type::Number) return n_;
    if (type_ == Type::Bool) return b_ ? 1.0 : 0.0;
    GP_THROW(std::runtime_error("JSON: not a number"));
}
bool Json::boolean() const {
    if (type_ != Type::Bool) GP_THROW(std::runtime_error("JSON: not a bool"));
    return b_;
}
const std::string& Json::str() const {
    if (type_ != Type::String) GP_THROW(std::runtime_error("JSON: not a string"));
    return s_;
}
size_t Json::size() const {
    if (type_ == Type::Array) return a_.size();
    if (type_ == Type::Object) return o_.size();
    return 0;
}
const Json& Json::operator[](size_t i) const {
    if (type_ != Type::Array || i >= a_.size()) GP_THROW(std::runtime_error("JSON: index out of range"));
    return a_[i];
}
Json& Json::operator[](size_t i) {
    if (type_ != Type::Array || i >= a_.size()) GP_THROW(std::runtime_error("JSON: index out of range"));
    return a_[i];
}
void Json::push(Json v) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) GP_THROW(std::runtime_error("JSON: push on non-array"));
    a_.push_back(std::move(v));
}
bool Json::has(const std::string& k) const {
    if (type_ != Type::Object) return false;
    for (auto& kv : o_) if (kv.first == k) return true;
    return false;
}
const Json& Json::operator[](const std::string& k) const {
    if (type_ != Type::Object) return kNull;
    for (auto& kv : o_) if (kv.first == k) return kv.second;
    return kNull;
}
Json& Json::set(const std::string& k, Json v) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (type_ != Type::Object) GP_THROW(std::runtime_error("JSON: set on non-object"));
    for (auto& kv : o_) if (kv.first == k) { kv.second = std::move(v); return kv.second; }
    o_.emplace_back(k, std::move(v));
    return o_.back().second;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append(size_t(d * indent), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(n_)) { out += "null"; break; }
            char b[32];
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%.0f", n_);
            else std::snprintf(b, sizeof b, "%.10g", n_);
            out += b;
            break;
        }
        case Type::String: escapeTo(out, s_); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < a_.size(); ++i) {
                if (i) out += ',';
                nl(depth + 1);
                a_[i].dumpTo(out, indent, depth + 1);
            }
            if (!a_.empty()) nl(depth);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < o_.size(); ++i) {
                if (i) out += ',';
                nl(depth + 1);
                escapeTo(out, o_[i].first);
                out += indent < 0 ? ":" : ": ";
                o_[i].second.dumpTo(out, indent, depth + 1);
            }
            if (!o_.empty()) nl(depth);
            out += '}';
            break;
    }
}
std::string Json::dump(int indent) const {
    std::string s;
    dumpTo(s, indent, 0);
    return s;
}

std::string readTextFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) GP_THROW(std::runtime_error("cannot open " + path));
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}
void writeTextFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) GP_THROW(std::runtime_error("cannot write " + path));
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}
std::string fnv1a64Hex(const std::string& data) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : data) { h ^= c; h *= 1099511628211ULL; }
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}

// Compact SHA-256 (FIPS 180-4) for content addressing of packages and records.
std::string sha256Hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string m = data;
    uint64_t bitlen = uint64_t(data.size()) * 8;
    m += char(0x80);
    while (m.size() % 64 != 56) m += char(0);
    for (int i = 7; i >= 0; --i) m += char((bitlen >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(uint8_t(m[off + 4 * i])) << 24) | (uint32_t(uint8_t(m[off + 4 * i + 1])) << 16) |
                   (uint32_t(uint8_t(m[off + 4 * i + 2])) << 8) | uint32_t(uint8_t(m[off + 4 * i + 3]));
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

}  // namespace gp
