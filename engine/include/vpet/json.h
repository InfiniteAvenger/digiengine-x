#pragma once
#include <string>
#include <vector>

namespace vpet {

// Minimal read-only JSON DOM for pack files. Missing keys and wrong types
// return a null value or the supplied default instead of failing.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static bool parse(const std::string& text, Json& out, std::string& err);

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const;
    size_t size() const;  // element count for arrays and objects
    // Key of the i-th member of an object (empty for other types).
    std::string key(size_t i) const { return i < keys_.size() ? keys_[i] : std::string(); }

    double num_or(double def) const { return type_ == Type::Number ? num_ : def; }
    int int_or(int def) const { return type_ == Type::Number ? int(num_) : def; }
    bool bool_or(bool def) const { return type_ == Type::Bool ? bool_ : def; }
    std::string str_or(const std::string& def) const { return type_ == Type::String ? str_ : def; }

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> items_;      // array elements or object values
    std::vector<std::string> keys_;  // object keys, parallel to items_
};

}  // namespace vpet
