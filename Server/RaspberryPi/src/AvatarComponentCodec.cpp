#include "AvatarComponentCodec.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <charconv>
#include <string>

#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::pi {
namespace {

constexpr std::array<std::array<std::uint8_t, 3>, 20> kHairPalette{{
    {104, 116, 124}, {176, 83, 43}, {40, 48, 58}, {80, 55, 47},
    {142, 90, 60}, {209, 164, 79}, {216, 204, 176}, {45, 132, 138},
    {112, 92, 82}, {139, 54, 42}, {104, 42, 44}, {200, 151, 78},
    {213, 139, 92}, {166, 178, 188}, {235, 238, 234}, {117, 37, 63},
    {194, 85, 119}, {111, 78, 160}, {55, 93, 168}, {48, 125, 91},
}};

constexpr std::array<std::array<std::uint8_t, 3>, 8> kSkinPalette{{
    {239, 202, 173}, {232, 181, 139}, {224, 158, 100}, {202, 141, 82},
    {173, 128, 83}, {161, 96, 62}, {137, 83, 56}, {91, 53, 42},
}};

std::uint16_t get16(const std::uint8_t* input) {
  return static_cast<std::uint16_t>(input[0]) |
      (static_cast<std::uint16_t>(input[1]) << 8);
}

std::uint32_t get32(const std::uint8_t* input) {
  return static_cast<std::uint32_t>(input[0]) |
      (static_cast<std::uint32_t>(input[1]) << 8) |
      (static_cast<std::uint32_t>(input[2]) << 16) |
      (static_cast<std::uint32_t>(input[3]) << 24);
}

std::uint32_t roundHalfUp(std::uint64_t numerator, std::uint32_t denominator) {
  return static_cast<std::uint32_t>((numerator + denominator / 2u) / denominator);
}

std::uint8_t tinted(std::uint8_t target, std::uint32_t numerator,
                    std::uint32_t denominator) {
  return static_cast<std::uint8_t>(std::min<std::uint32_t>(
      255u, roundHalfUp(static_cast<std::uint64_t>(target) * numerator, denominator)));
}

bool validHeaderIdentity(const AvatarComponentHeader& header,
                         AvatarComponentKind expectedKind,
                         std::uint8_t expectedPresetId) {
  if (header.schema != 1 || header.kind != expectedKind ||
      header.presetId != expectedPresetId || expectedPresetId < 1 || expectedPresetId > 10 ||
      header.encoding != 1 || header.canvasWidth != kAvatarComponentCanvasWidth ||
      header.canvasHeight != kAvatarComponentCanvasHeight || header.width == 0 ||
      header.height == 0) return false;
  const auto right = static_cast<std::uint32_t>(header.x) + header.width;
  const auto bottom = static_cast<std::uint32_t>(header.y) + header.height;
  const auto pixels = static_cast<std::uint64_t>(header.width) * header.height;
  return right <= header.canvasWidth && bottom <= header.canvasHeight &&
      pixels <= std::numeric_limits<std::uint32_t>::max() / 4u &&
      header.decodedBytes == pixels * 4u && header.encodedBytes != 0;
}

}  // namespace

bool decodeAvatarComponent(const std::vector<std::uint8_t>& bytes,
                           AvatarComponentKind expectedKind,
                           std::uint8_t expectedPresetId,
                           AvatarComponent& output) {
  output = AvatarComponent{};
  if (bytes.size() < kAvatarComponentHeaderSize ||
      std::memcmp(bytes.data(), "GAVC", 4) != 0) return false;
  AvatarComponentHeader header{};
  header.schema = bytes[4];
  header.kind = static_cast<AvatarComponentKind>(bytes[5]);
  header.presetId = bytes[6];
  header.encoding = bytes[7];
  header.canvasWidth = get16(bytes.data() + 8);
  header.canvasHeight = get16(bytes.data() + 10);
  header.x = get16(bytes.data() + 12);
  header.y = get16(bytes.data() + 14);
  header.width = get16(bytes.data() + 16);
  header.height = get16(bytes.data() + 18);
  header.decodedBytes = get32(bytes.data() + 20);
  header.encodedBytes = get32(bytes.data() + 24);
  header.crc32 = get32(bytes.data() + 28);
  if (!validHeaderIdentity(header, expectedKind, expectedPresetId) ||
      static_cast<std::uint64_t>(kAvatarComponentHeaderSize) + header.encodedBytes !=
          bytes.size()) return false;

  std::vector<std::uint8_t> rgba(header.decodedBytes);
  std::size_t inputOffset = kAvatarComponentHeaderSize;
  std::size_t outputOffset = 0;
  while (inputOffset < bytes.size() && outputOffset < rgba.size()) {
    if (inputOffset + 2 > bytes.size()) return false;
    const auto token = get16(bytes.data() + inputOffset);
    inputOffset += 2;
    const auto count = static_cast<std::size_t>(token & 0x7FFFu);
    if (count == 0 || count > (rgba.size() - outputOffset) / 4u) return false;
    if ((token & 0x8000u) != 0) {
      if (inputOffset + 4 > bytes.size()) return false;
      for (std::size_t index = 0; index < count; ++index) {
        std::memcpy(rgba.data() + outputOffset, bytes.data() + inputOffset, 4);
        outputOffset += 4;
      }
      inputOffset += 4;
    } else {
      const auto copyBytes = count * 4u;
      if (inputOffset + copyBytes > bytes.size()) return false;
      std::memcpy(rgba.data() + outputOffset, bytes.data() + inputOffset, copyBytes);
      inputOffset += copyBytes;
      outputOffset += copyBytes;
    }
  }
  if (inputOffset != bytes.size() || outputOffset != rgba.size() ||
      gridopoly::protocol::crc32(rgba.data(), rgba.size()) != header.crc32) return false;
  output.header = header;
  output.rgba = std::move(rgba);
  return true;
}

std::filesystem::path avatarComponentPath(const std::filesystem::path& root,
                                          AvatarComponentKind kind,
                                          std::uint8_t presetId) {
  if (presetId < 1 || presetId > 10) return {};
  const char* directory = nullptr;
  char prefix = 0;
  switch (kind) {
    case AvatarComponentKind::Face: directory = "face"; prefix = 'f'; break;
    case AvatarComponentKind::Outfit: directory = "outfit"; prefix = 'o'; break;
    case AvatarComponentKind::Hair: directory = "hair"; prefix = 'h'; break;
    default: return {};
  }
  std::string filenameText(1, prefix);
  filenameText.append(std::to_string(static_cast<unsigned>(presetId)));
  filenameText.append(".gavc");
  const std::filesystem::path filename{filenameText};
  return root / directory / filename;
}

bool loadAvatarComponent(const std::filesystem::path& root,
                         AvatarComponentKind kind,
                         std::uint8_t presetId,
                         AvatarComponent& output,
                         std::vector<std::uint8_t>* encodedFile) {
  const auto path = avatarComponentPath(root, kind, presetId);
  if (path.empty()) return false;
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
  if (input.bad() || !decodeAvatarComponent(bytes, kind, presetId, output)) return false;
  if (encodedFile != nullptr) *encodedFile = std::move(bytes);
  return true;
}

bool parseAvatarComponentRelative(std::string_view value,
                                  AvatarComponentKind& kind,
                                  std::uint8_t& presetId) {
  const auto slash = value.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash != value.rfind('/') ||
      slash + 1 >= value.size()) return false;
  const auto directory = value.substr(0, slash);
  char prefix = 0;
  if (directory == "hair") {
    kind = AvatarComponentKind::Hair;
    prefix = 'h';
  } else if (directory == "face") {
    kind = AvatarComponentKind::Face;
    prefix = 'f';
  } else if (directory == "outfit") {
    kind = AvatarComponentKind::Outfit;
    prefix = 'o';
  } else {
    return false;
  }
  const auto file = value.substr(slash + 1);
  constexpr std::string_view suffix = ".gavc";
  if (file.size() <= 1 + suffix.size() || file.front() != prefix ||
      file.substr(file.size() - suffix.size()) != suffix) return false;
  const auto digits = file.substr(1, file.size() - 1 - suffix.size());
  if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) return false;
  unsigned preset = 0;
  const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), preset);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
      preset < 1 || preset > 10) return false;
  presetId = static_cast<std::uint8_t>(preset);
  return true;
}

std::array<std::uint8_t, 3> hairPaletteRgb(std::uint8_t id) {
  return id >= 1 && id <= kHairPalette.size() ? kHairPalette[id - 1]
                                               : std::array<std::uint8_t, 3>{};
}

std::array<std::uint8_t, 3> skinPaletteRgb(std::uint8_t id) {
  return id >= 1 && id <= kSkinPalette.size() ? kSkinPalette[id - 1]
                                               : std::array<std::uint8_t, 3>{};
}

void tintHairPixel(std::uint8_t* rgba, const std::array<std::uint8_t, 3>& target) {
  if (rgba == nullptr || rgba[3] == 0) return;
  const auto weight = 54u * rgba[0] + 183u * rgba[1] + 19u * rgba[2];
  if (weight < 6528u) return;
  const auto numerator = 7u * 65280u + 21u * weight;
  constexpr std::uint32_t denominator = 20u * 65280u;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    rgba[channel] = tinted(target[channel], numerator, denominator);
  }
}

void tintSkinPixel(std::uint8_t* rgba, const std::array<std::uint8_t, 3>& target) {
  if (rgba == nullptr || rgba[3] == 0) return;
  const auto weight = 54u * rgba[0] + 183u * rgba[1] + 19u * rgba[2];
  const auto high = std::max({rgba[0], rgba[1], rgba[2]});
  const auto low = std::min({rgba[0], rgba[1], rgba[2]});
  if (weight < 23552u || 5u * (high - low) < high ||
      25u * rgba[0] < 26u * rgba[1] || 25u * rgba[1] < 23u * rgba[2]) return;
  std::uint32_t numerator = weight;
  std::uint32_t denominator = 45568u;
  if (100u * weight < 42u * denominator) {
    numerator = 42u;
    denominator = 100u;
  } else if (100u * weight > 135u * denominator) {
    numerator = 135u;
    denominator = 100u;
  }
  for (std::size_t channel = 0; channel < 3; ++channel) {
    rgba[channel] = tinted(target[channel], numerator, denominator);
  }
}

void blendAvatarPixel(std::uint8_t* destination, const std::uint8_t* source) {
  if (destination == nullptr || source == nullptr) return;
  const auto sourceAlpha = static_cast<std::uint32_t>(source[3]);
  if (sourceAlpha == 0) return;
  if (sourceAlpha == 255) {
    std::memcpy(destination, source, 4);
    return;
  }
  const auto destinationAlpha = static_cast<std::uint32_t>(destination[3]);
  const auto inverse = 255u - sourceAlpha;
  const auto outputAlpha = sourceAlpha + roundHalfUp(destinationAlpha * inverse, 255u);
  if (outputAlpha == 0) {
    std::memset(destination, 0, 4);
    return;
  }
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const auto premultiplied = static_cast<std::uint64_t>(source[channel]) * sourceAlpha +
        roundHalfUp(static_cast<std::uint64_t>(destination[channel]) * destinationAlpha * inverse,
                    255u);
    destination[channel] = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(255u, roundHalfUp(premultiplied, outputAlpha)));
  }
  destination[3] = static_cast<std::uint8_t>(outputAlpha);
}

bool placeAvatarComponent(const AvatarComponent& component,
                          const std::array<std::uint8_t, 3>& tint,
                          std::vector<std::uint8_t>& canvasRgba) {
  const auto& header = component.header;
  if (header.canvasWidth != kAvatarComponentCanvasWidth ||
      header.canvasHeight != kAvatarComponentCanvasHeight ||
      component.rgba.size() != header.decodedBytes ||
      canvasRgba.size() != static_cast<std::size_t>(header.canvasWidth) *
          header.canvasHeight * 4u) return false;
  for (std::size_t y = 0; y < header.height; ++y) {
    for (std::size_t x = 0; x < header.width; ++x) {
      auto source = std::array<std::uint8_t, 4>{};
      const auto sourceOffset = (y * header.width + x) * 4u;
      std::memcpy(source.data(), component.rgba.data() + sourceOffset, 4);
      if (header.kind == AvatarComponentKind::Hair) tintHairPixel(source.data(), tint);
      if (header.kind == AvatarComponentKind::Face) tintSkinPixel(source.data(), tint);
      const auto destinationOffset =
          ((static_cast<std::size_t>(header.y) + y) * header.canvasWidth + header.x + x) * 4u;
      blendAvatarPixel(canvasRgba.data() + destinationOffset, source.data());
    }
  }
  return true;
}

}  // namespace gridopoly::pi
