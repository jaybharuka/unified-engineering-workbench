// Minimal JSON value, parser and serializer (no external dependency).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace wb {

class JsonParseError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : type_(Type::Bool), bool_(b) {}
  Json(const char* s) : type_(Type::String), str_(s) {}
  Json(std::string s) : type_(Type::String), str_(std::move(s)) {}
  template <class T, typename std::enable_if<std::is_arithmetic<T>::value &&
                                                 !std::is_same<T, bool>::value,
                                             int>::type = 0>
  Json(T v) : type_(Type::Number), num_(static_cast<double>(v)) {}

  static Json array() { Json j; j.type_ = Type::Array; return j; }
  static Json object() { Json j; j.type_ = Type::Object; return j; }
  static Json numberArray(const std::vector<double>& v);

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  // Object access (insertion ordered). operator[] inserts a null if missing.
  Json& operator[](const std::string& key);
  const Json* find(const std::string& key) const;
  bool has(const std::string& key) const { return find(key) != nullptr; }

  // Array access.
  Json& push(Json v);
  std::size_t size() const;
  const Json& at(std::size_t i) const { return items_.at(i); }

  double asNumber(double fallback = 0.0) const { return isNumber() ? num_ : fallback; }
  bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
  const std::string& asString() const { return str_; }

  std::string dump() const;
  static Json parse(const std::string& text);  // throws JsonParseError

 private:
  void dumpTo(std::string& out) const;
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  std::vector<Json> items_;                          // array items or object values
  std::vector<std::string> keys_;                    // object keys (parallel to items_)
  friend class JsonParser;
};

}  // namespace wb
