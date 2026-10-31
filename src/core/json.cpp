#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wb {

Json Json::numberArray(const std::vector<double>& v) {
  Json a = array();
  a.items_.reserve(v.size());
  for (double d : v) a.items_.emplace_back(d);
  return a;
}

Json& Json::operator[](const std::string& key) {
  if (type_ == Type::Null) type_ = Type::Object;
  if (type_ != Type::Object) throw std::logic_error("Json: operator[] on non-object");
  for (std::size_t i = 0; i < keys_.size(); ++i)
    if (keys_[i] == key) return items_[i];
  keys_.push_back(key);
  items_.emplace_back();
  return items_.back();
}

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  for (std::size_t i = 0; i < keys_.size(); ++i)
    if (keys_[i] == key) return &items_[i];
  return nullptr;
}

Json& Json::push(Json v) {
  if (type_ == Type::Null) type_ = Type::Array;
  if (type_ != Type::Array) throw std::logic_error("Json: push on non-array");
  items_.push_back(std::move(v));
  return items_.back();
}

std::size_t Json::size() const {
  return (type_ == Type::Array || type_ == Type::Object) ? items_.size() : 0;
}

static void escapeTo(const std::string& s, std::string& out) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void Json::dumpTo(std::string& out) const {
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
      if (!std::isfinite(num_)) { out += "null"; break; }  // JSON has no NaN/Inf
      char buf[40];
      std::snprintf(buf, sizeof buf, "%.17g", num_);
      out += buf;
      break;
    }
    case Type::String: escapeTo(str_, out); break;
    case Type::Array:
      out.push_back('[');
      for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i) out.push_back(',');
        items_[i].dumpTo(out);
      }
      out.push_back(']');
      break;
    case Type::Object:
      out.push_back('{');
      for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i) out.push_back(',');
        escapeTo(keys_[i], out);
        out.push_back(':');
        items_[i].dumpTo(out);
      }
      out.push_back('}');
      break;
  }
}

std::string Json::dump() const {
  std::string out;
  dumpTo(out);
  return out;
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& t) : s_(t) {}

  Json parseDocument() {
    skipWs();
    Json v = parseValue(0);
    skipWs();
    if (pos_ != s_.size()) fail("unexpected trailing characters");
    return v;
  }

 private:
  [[noreturn]] void fail(const std::string& msg) const {
    throw JsonParseError("JSON error at offset " + std::to_string(pos_) + ": " + msg);
  }
  void skipWs() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
  }
  char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
  bool eat(char c) {
    if (peek() == c) { ++pos_; return true; }
    return false;
  }
  void expectWord(const char* w) {
    for (const char* p = w; *p; ++p) {
      if (peek() != *p) fail(std::string("expected '") + w + "'");
      ++pos_;
    }
  }

  Json parseValue(int depth) {
    if (depth > 64) fail("nesting too deep");
    skipWs();
    char c = peek();
    if (c == '{') return parseObject(depth);
    if (c == '[') return parseArray(depth);
    if (c == '"') return Json(parseString());
    if (c == 't') { expectWord("true"); return Json(true); }
    if (c == 'f') { expectWord("false"); return Json(false); }
    if (c == 'n') { expectWord("null"); return Json(); }
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
    fail("unexpected character");
  }

  Json parseNumber() {
    const char* begin = s_.c_str() + pos_;
    char* end = nullptr;
    double v = std::strtod(begin, &end);
    if (end == begin) fail("invalid number");
    pos_ += static_cast<std::size_t>(end - begin);
    if (!std::isfinite(v)) fail("number out of range");
    return Json(v);
  }

  std::string parseString() {
    if (!eat('"')) fail("expected string");
    std::string out;
    while (true) {
      if (pos_ >= s_.size()) fail("unterminated string");
      char c = s_[pos_++];
      if (c == '"') break;
      if (c == '\\') {
        if (pos_ >= s_.size()) fail("bad escape");
        char e = s_[pos_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > s_.size()) fail("bad \\u escape");
            unsigned cp = static_cast<unsigned>(std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16));
            pos_ += 4;
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: fail("bad escape");
        }
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  Json parseArray(int depth) {
    eat('[');
    Json a = Json::array();
    skipWs();
    if (eat(']')) return a;
    while (true) {
      a.push(parseValue(depth + 1));
      skipWs();
      if (eat(',')) continue;
      if (eat(']')) break;
      fail("expected ',' or ']'");
    }
    return a;
  }

  Json parseObject(int depth) {
    eat('{');
    Json o = Json::object();
    skipWs();
    if (eat('}')) return o;
    while (true) {
      skipWs();
      std::string key = parseString();
      skipWs();
      if (!eat(':')) fail("expected ':'");
      o[key] = parseValue(depth + 1);
      skipWs();
      if (eat(',')) continue;
      if (eat('}')) break;
      fail("expected ',' or '}'");
    }
    return o;
  }

  const std::string& s_;
  std::size_t pos_ = 0;
};

Json Json::parse(const std::string& text) {
  JsonParser p(text);
  return p.parseDocument();
}

}  // namespace wb
