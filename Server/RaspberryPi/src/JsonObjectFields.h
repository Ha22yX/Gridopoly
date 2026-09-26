#pragma once
#include <cctype>
#include <map>
#include <set>
#include <string>

namespace gridopoly::pi {
// Strict bounded JSON syntax and top-level field positions. Protocol field
// readers must not accidentally accept a nested key or a valid scalar prefix.
inline bool jsonObjectFields(const std::string& text, std::map<std::string, std::size_t>& fields) {
  struct Parser {
    const std::string& s; std::size_t at{};
    std::map<std::string, std::size_t>& fields;
    void space() { while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at; }
    bool string(std::string* key = nullptr) {
      if (at >= s.size() || s[at++] != '"') return false;
      const auto start = at;
      while (at < s.size()) {
        const auto c = static_cast<unsigned char>(s[at++]);
        if (c == '"') { if (key) *key = s.substr(start, at - start - 1); return true; }
        if (c < 32) return false;
        if (c != '\\') continue;
        if (key || at == s.size()) return false;
        const auto escape = s[at++];
        if (escape == 'u') {
          for (unsigned i = 0; i < 4; ++i) if (at == s.size() ||
              !std::isxdigit(static_cast<unsigned char>(s[at++]))) return false;
        } else if (std::string("\"\\/bfnrt").find(escape) == std::string::npos) return false;
      }
      return false;
    }
    bool value(unsigned depth) {
      space(); if (depth > 8 || at == s.size()) return false;
      if (s[at] == '"') return string();
      if (s[at] == '{') return object(depth + 1);
      if (s[at] == '[') {
        ++at; space(); if (at < s.size() && s[at] == ']') { ++at; return true; }
        for (unsigned count = 0; count < 128; ++count) {
          if (!value(depth + 1)) return false;
          space(); if (at == s.size()) return false;
          if (s[at] == ']') { ++at; return true; }
          if (s[at++] != ',') return false;
        }
        return false;
      }
      for (const auto* literal : {"true", "false", "null"}) {
        const std::string word(literal);
        if (s.compare(at, word.size(), word) == 0) { at += word.size(); return true; }
      }
      if (s[at] == '-') ++at;
      if (at == s.size()) return false;
      if (s[at] == '0') ++at;
      else {
        const auto begin = at;
        while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
        if (begin == at) return false;
      }
      if (at < s.size() && s[at] == '.') {
        const auto begin = ++at;
        while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
        if (begin == at) return false;
      }
      if (at < s.size() && (s[at] == 'e' || s[at] == 'E')) {
        ++at; if (at < s.size() && (s[at] == '-' || s[at] == '+')) ++at;
        const auto begin = at;
        while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
        if (begin == at) return false;
      }
      return true;
    }
    bool object(unsigned depth) {
      if (at >= s.size() || s[at++] != '{') return false;
      space(); if (at < s.size() && s[at] == '}') { ++at; return true; }
      std::set<std::string> keys;
      for (unsigned count = 0; count < 128; ++count) {
        space(); std::string key;
        if (!string(&key) || !keys.insert(key).second) return false;
        space(); if (at == s.size() || s[at++] != ':') return false;
        space(); if (depth == 0) fields[key] = at;
        if (!value(depth)) return false;
        space(); if (at == s.size()) return false;
        if (s[at] == '}') { ++at; return true; }
        if (s[at++] != ',') return false;
      }
      return false;
    }
  };
  fields.clear(); if (text.size() > 16384) return false;
  Parser parser{text, 0, fields}; parser.space();
  if (!parser.object(0)) return false;
  parser.space(); return parser.at == text.size();
}
}  // namespace gridopoly::pi
