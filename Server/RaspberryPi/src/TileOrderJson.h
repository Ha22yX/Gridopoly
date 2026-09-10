#pragma once

#include "TileOrderTopology.h"
#include <charconv>
#include <cctype>

namespace gridopoly::pi {

// Strict scalar-only parser for the versioned physical-evidence object.
// Reject duplicate keys, malformed numbers, unknown fields and nested values.
inline bool parseTileOrderObject(const std::string& body, std::size_t& cursor,
                                 TileOrderReport& output) {
  TileOrderReport out;
  auto space = [&] { while (cursor < body.size() &&
      std::isspace(static_cast<unsigned char>(body[cursor]))) ++cursor; };
  auto quoted = [&](std::string& value) {
    if (cursor >= body.size() || body[cursor++] != '"') return false;
    const auto start = cursor;
    while (cursor < body.size() && body[cursor] != '"') {
      if (body[cursor] == '\\' || static_cast<unsigned char>(body[cursor]) < 32) return false;
      ++cursor;
    }
    if (cursor == body.size()) return false;
    value = body.substr(start, cursor++ - start); return true;
  };
  space();
  if (cursor == body.size() || body[cursor++] != '{') return false;
  std::map<std::string, std::string> fields;
  while (true) {
    space(); std::string key;
    if (!quoted(key) || fields.count(key)) return false;
    space(); if (cursor == body.size() || body[cursor++] != ':') return false;
    space(); if (cursor == body.size()) return false;
    const auto start = cursor;
    if (body[cursor] == '"') { std::string ignored; if (!quoted(ignored)) return false; }
    else while (cursor < body.size() && body[cursor] != ',' && body[cursor] != '}' &&
                !std::isspace(static_cast<unsigned char>(body[cursor]))) ++cursor;
    fields[key] = body.substr(start, cursor - start);
    if (fields.size() > 9) return false;
    space(); if (cursor == body.size()) return false;
    if (body[cursor++] == '}') break;
    if (body[cursor - 1] != ',') return false;
  }
  static const char* required[] = {"version", "bootId", "txSeq", "upstreamBootId",
      "upstreamSeq", "ageMs", "inputState", "valid"};
  if (fields.size() != 8) return false;
  for (const auto* key : required) if (!fields.count(key)) return false;
  auto number = [&](const char* key, std::uint32_t& value) {
    const auto& text = fields[key];
    if (text.empty() || (text.size() > 1 && text[0] == '0')) return false;
    const auto p = std::from_chars(text.data(), text.data() + text.size(), value);
    return p.ec == std::errc{} && p.ptr == text.data() + text.size();
  };
  auto string = [&](const char* key, std::string& value) {
    const auto& text = fields[key];
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
    value = text.substr(1, text.size() - 2); return true;
  };
  std::uint32_t tx = 0, upstream = 0;
  if (!number("version", out.version) || !number("txSeq", tx) || tx > 65535 ||
      !number("upstreamSeq", upstream) || upstream > 65535 || !number("ageMs", out.ageMs) ||
      out.ageMs > TileOrderTopology::kEvidenceLeaseMs || !string("bootId", out.bootId) ||
      !string("inputState", out.inputState)) return false;
  if (fields["valid"] != "true" && fields["valid"] != "false") return false;
  out.valid = fields["valid"] == "true";
  if (out.valid) { if (!string("upstreamBootId", out.upstreamBootId)) return false; }
  else if (fields["upstreamBootId"] != "null" || out.ageMs != 0) return false;
  out.txSeq = static_cast<std::uint16_t>(tx);
  out.upstreamSeq = static_cast<std::uint16_t>(upstream);
  space();
  if (cursor >= body.size() || (body[cursor] != ',' && body[cursor] != '}') ||
      !TileOrderTopology::validReport(out)) return false;
  output = std::move(out);
  return true;
}

}  // namespace gridopoly::pi
