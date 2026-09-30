#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace km {

JValue JValue::Bool(bool v) {
    JValue j;
    j.type = Type::Bool;
    j.b = v;
    return j;
}

JValue JValue::Number(double v) {
    JValue j;
    j.type = Type::Number;
    j.n = v;
    return j;
}

JValue JValue::String(std::string v) {
    JValue j;
    j.type = Type::String;
    j.s = std::move(v);
    return j;
}

JValue JValue::Array() {
    JValue j;
    j.type = Type::Array;
    return j;
}

JValue JValue::Object() {
    JValue j;
    j.type = Type::Object;
    return j;
}

const JValue* JValue::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

JValue& JValue::set(std::string key, JValue v) {
    obj.emplace_back(std::move(key), std::move(v));
    return obj.back().second;
}

JValue& JValue::push(JValue v) {
    arr.push_back(std::move(v));
    return arr.back();
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    bool parse(JValue& out, std::string& err) {
        if (t_.size() >= 3 && static_cast<unsigned char>(t_[0]) == 0xEF &&
            static_cast<unsigned char>(t_[1]) == 0xBB && static_cast<unsigned char>(t_[2]) == 0xBF)
            p_ = 3;
        ws();
        if (!value(out, 0)) {
            err = err_.empty() ? "invalid JSON" : err_;
            err += " at offset " + std::to_string(p_);
            return false;
        }
        ws();
        if (p_ != t_.size()) {
            err = "unexpected trailing data at offset " + std::to_string(p_);
            return false;
        }
        return true;
    }

private:
    std::string_view t_;
    size_t p_ = 0;
    std::string err_;

    bool fail(const char* m) {
        if (err_.empty()) err_ = m;
        return false;
    }
    void ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_;
    }
    bool lit(std::string_view w) {
        if (t_.substr(p_, w.size()) != w) return fail("invalid literal");
        p_ += w.size();
        return true;
    }

    bool value(JValue& v, int depth) {
        if (depth > 64) return fail("nesting too deep");
        if (p_ >= t_.size()) return fail("unexpected end of input");
        char c = t_[p_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v.type = JValue::Type::String;
            return string(v.s);
        }
        if (c == 't') { v = JValue::Bool(true); return lit("true"); }
        if (c == 'f') { v = JValue::Bool(false); return lit("false"); }
        if (c == 'n') { v = JValue{}; return lit("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) return number(v);
        return fail("unexpected character");
    }

    bool object(JValue& v, int depth) {
        v = JValue::Object();
        ++p_;
        ws();
        if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
        while (true) {
            ws();
            if (p_ >= t_.size() || t_[p_] != '"') return fail("expected object key");
            std::string key;
            if (!string(key)) return false;
            ws();
            if (p_ >= t_.size() || t_[p_] != ':') return fail("expected ':'");
            ++p_;
            ws();
            JValue child;
            if (!value(child, depth + 1)) return false;
            v.obj.emplace_back(std::move(key), std::move(child));
            ws();
            if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool array(JValue& v, int depth) {
        v = JValue::Array();
        ++p_;
        ws();
        if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
        while (true) {
            ws();
            JValue child;
            if (!value(child, depth + 1)) return false;
            v.arr.push_back(std::move(child));
            ws();
            if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool hex4(unsigned& out) {
        if (p_ + 4 > t_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[p_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("invalid \\u escape");
        }
        return true;
    }

    static void appendUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++p_;  // opening quote
        out.clear();
        while (true) {
            if (p_ >= t_.size()) return fail("unterminated string");
            char c = t_[p_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= t_.size()) return fail("unterminated escape");
            char e = t_[p_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        unsigned lo;
                        if (p_ + 2 > t_.size() || t_[p_] != '\\' || t_[p_ + 1] != 'u')
                            return fail("unpaired surrogate");
                        p_ += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("invalid surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("unpaired surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape");
            }
        }
    }

    bool number(JValue& v) {
        size_t start = p_;
        if (t_[p_] == '-') ++p_;
        auto digits = [&] {
            size_t s = p_;
            while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') ++p_;
            return p_ > s;
        };
        if (!digits()) return fail("invalid number");
        if (p_ < t_.size() && t_[p_] == '.') {
            ++p_;
            if (!digits()) return fail("invalid number");
        }
        if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
            ++p_;
            if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) ++p_;
            if (!digits()) return fail("invalid number");
        }
        std::string tmp(t_.substr(start, p_ - start));
        v = JValue::Number(std::strtod(tmp.c_str(), nullptr));
        return true;
    }
};

void WriteString(std::string& o, const std::string& s) {
    o += '"';
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    o += '"';
}

void Write(std::string& o, const JValue& v, int indent) {
    auto nl = [&](int level) {
        o += '\n';
        o.append(static_cast<size_t>(level) * 2, ' ');
    };
    switch (v.type) {
        case JValue::Type::Null: o += "null"; break;
        case JValue::Type::Bool: o += v.b ? "true" : "false"; break;
        case JValue::Type::Number: {
            char buf[32];
            if (std::floor(v.n) == v.n && std::fabs(v.n) < 1e15)
                snprintf(buf, sizeof buf, "%.0f", v.n);
            else
                snprintf(buf, sizeof buf, "%.17g", v.n);
            o += buf;
            break;
        }
        case JValue::Type::String: WriteString(o, v.s); break;
        case JValue::Type::Array:
            if (v.arr.empty()) { o += "[]"; break; }
            o += '[';
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) o += ',';
                nl(indent + 1);
                Write(o, v.arr[i], indent + 1);
            }
            nl(indent);
            o += ']';
            break;
        case JValue::Type::Object:
            if (v.obj.empty()) { o += "{}"; break; }
            o += '{';
            for (size_t i = 0; i < v.obj.size(); ++i) {
                if (i) o += ',';
                nl(indent + 1);
                WriteString(o, v.obj[i].first);
                o += ": ";
                Write(o, v.obj[i].second, indent + 1);
            }
            nl(indent);
            o += '}';
            break;
    }
}

}  // namespace

bool ParseJson(std::string_view text, JValue& out, std::string& error) {
    Parser p(text);
    out = JValue{};
    return p.parse(out, error);
}

std::string WriteJson(const JValue& v) {
    std::string o;
    Write(o, v, 0);
    o += '\n';
    return o;
}

}  // namespace km
