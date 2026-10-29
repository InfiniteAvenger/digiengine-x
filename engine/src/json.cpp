#include "vpet/json.h"
#include <cstdlib>

namespace vpet {

namespace {
const Json kNull;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse(Json& out, std::string& err) {
        if (!value(out, 0) || (skip_ws(), pos_ != s_.size())) {
            if (err_.empty()) err_ = "unexpected trailing data";
            err = "JSON error at line " + std::to_string(line()) + ": " + err_;
            return false;
        }
        return true;
    }

private:
    bool fail(const char* msg) {
        if (err_.empty()) err_ = msg;
        return false;
    }

    int line() const {
        int n = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i)
            if (s_[i] == '\n') ++n;
        return n;
    }

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool literal(const char* word) {
        size_t start = pos_;
        for (const char* p = word; *p; ++p, ++pos_)
            if (pos_ >= s_.size() || s_[pos_] != *p) {
                pos_ = start;
                return false;
            }
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > 32) return fail("nesting too deep");
        skip_ws();
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        char c = s_[pos_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type_ = Json::Type::String;
            return string(out.str_);
        }
        if (literal("true")) { out.type_ = Json::Type::Bool; out.bool_ = true; return true; }
        if (literal("false")) { out.type_ = Json::Type::Bool; out.bool_ = false; return true; }
        if (literal("null")) { out.type_ = Json::Type::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* begin = s_.c_str() + pos_;
            char* end = nullptr;
            out.num_ = std::strtod(begin, &end);
            if (end == begin) return fail("bad number");
            pos_ += size_t(end - begin);
            out.type_ = Json::Type::Number;
            return true;
        }
        return fail("unexpected character");
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) break;
            char e = s_[pos_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return fail("bad \\u escape");
                    unsigned cp = unsigned(std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16));
                    pos_ += 4;
                    out += cp < 128 ? char(cp) : '?';  // the pixel font is ASCII only
                    break;
                }
                default: out += e; break;  // \" \\ \/
            }
        }
        return fail("unterminated string");
    }

    bool array(Json& out, int depth) {
        out.type_ = Json::Type::Array;
        ++pos_;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            out.items_.emplace_back();
            if (!value(out.items_.back(), depth + 1)) return false;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool object(Json& out, int depth) {
        out.type_ = Json::Type::Object;
        ++pos_;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected a key string");
            out.keys_.emplace_back();
            if (!string(out.keys_.back())) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            out.items_.emplace_back();
            if (!value(out.items_.back(), depth + 1)) return false;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
    std::string err_;
};

bool Json::parse(const std::string& text, Json& out, std::string& err) {
    out = Json{};
    return JsonParser(text).parse(out, err);
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object)
        for (size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key) return items_[i];
    return kNull;
}

const Json& Json::operator[](size_t i) const {
    if ((type_ == Type::Array || type_ == Type::Object) && i < items_.size()) return items_[i];
    return kNull;
}

size_t Json::size() const { return (type_ == Type::Array || type_ == Type::Object) ? items_.size() : 0; }

}  // namespace vpet
