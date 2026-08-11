#include "AvatarRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "IdentityModel.h"

namespace gridopoly::pi {
namespace {

constexpr std::size_t kCanvasWidth = 320;
constexpr std::size_t kCanvasHeight = 320;
constexpr std::size_t kCanvasBytes = kCanvasWidth * kCanvasHeight * 4;
constexpr std::size_t kPreviewWidth = kAvatarComponentCanvasWidth;
constexpr std::size_t kPreviewHeight = kAvatarComponentCanvasHeight;
constexpr std::size_t kPreviewBytes = kPreviewWidth * kPreviewHeight * 4;
constexpr std::array<std::uint8_t, 3> kBackground{0x06, 0x10, 0x17};

void appendBig32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value >> 24));
  output.push_back(static_cast<std::uint8_t>(value >> 16));
  output.push_back(static_cast<std::uint8_t>(value >> 8));
  output.push_back(static_cast<std::uint8_t>(value));
}

std::uint64_t fnv64(const std::uint8_t* bytes, std::size_t length) {
  std::uint64_t hash = 1469598103934665603ull;
  for (std::size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash == 0 ? 1 : hash;
}

std::uint32_t adler32(const std::uint8_t* bytes, std::size_t length) {
  std::uint32_t first = 1;
  std::uint32_t second = 0;
  for (std::size_t index = 0; index < length; ++index) {
    first = (first + bytes[index]) % 65521u;
    second = (second + first) % 65521u;
  }
  return (second << 16) | first;
}

void appendPngChunk(std::vector<std::uint8_t>& png, const char type[4],
                    const std::vector<std::uint8_t>& data) {
  appendBig32(png, static_cast<std::uint32_t>(data.size()));
  const auto crcStart = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), data.begin(), data.end());
  appendBig32(png, gridopoly::protocol::crc32(png.data() + crcStart, 4 + data.size()));
}

std::vector<std::uint8_t> pngRgba(std::uint32_t width, std::uint32_t height,
                                  const std::vector<std::uint8_t>& rgba) {
  if (rgba.size() != static_cast<std::size_t>(width) * height * 4) return {};
  std::vector<std::uint8_t> filtered;
  filtered.reserve(rgba.size() + height);
  for (std::uint32_t row = 0; row < height; ++row) {
    filtered.push_back(0);
    const auto offset = static_cast<std::size_t>(row) * width * 4;
    filtered.insert(filtered.end(), rgba.begin() + offset, rgba.begin() + offset + width * 4);
  }
  std::vector<std::uint8_t> deflate{0x78, 0x01};
  std::size_t offset = 0;
  while (offset < filtered.size()) {
    const auto count = static_cast<std::uint16_t>(
        std::min<std::size_t>(65535, filtered.size() - offset));
    const bool final = offset + count == filtered.size();
    deflate.push_back(final ? 1 : 0);
    deflate.push_back(static_cast<std::uint8_t>(count));
    deflate.push_back(static_cast<std::uint8_t>(count >> 8));
    const auto inverse = static_cast<std::uint16_t>(~count);
    deflate.push_back(static_cast<std::uint8_t>(inverse));
    deflate.push_back(static_cast<std::uint8_t>(inverse >> 8));
    deflate.insert(deflate.end(), filtered.begin() + offset, filtered.begin() + offset + count);
    offset += count;
  }
  appendBig32(deflate, adler32(filtered.data(), filtered.size()));

  std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<std::uint8_t> header;
  appendBig32(header, width);
  appendBig32(header, height);
  header.insert(header.end(), {8, 6, 0, 0, 0});
  appendPngChunk(png, "IHDR", header);
  appendPngChunk(png, "IDAT", deflate);
  appendPngChunk(png, "IEND", {});
  return png;
}

std::array<std::uint8_t, 4> bilinearPixel(const std::vector<std::uint8_t>& source,
                                          std::size_t x, std::size_t y,
                                          std::size_t outputWidth,
                                          std::size_t outputHeight) {
  const auto sourceX = (static_cast<double>(x) + 0.5) * kCanvasWidth / outputWidth - 0.5;
  const auto sourceY = (static_cast<double>(y) + 0.5) * kCanvasHeight / outputHeight - 0.5;
  const auto x0 = static_cast<std::size_t>(std::max(0.0, std::floor(sourceX)));
  const auto y0 = static_cast<std::size_t>(std::max(0.0, std::floor(sourceY)));
  const auto x1 = std::min(x0 + 1, kCanvasWidth - 1);
  const auto y1 = std::min(y0 + 1, kCanvasHeight - 1);
  const auto fx = std::clamp(sourceX - std::floor(sourceX), 0.0, 1.0);
  const auto fy = std::clamp(sourceY - std::floor(sourceY), 0.0, 1.0);
  std::array<std::uint8_t, 4> result{};
  for (std::size_t channel = 0; channel < 4; ++channel) {
    const auto p00 = source[(y0 * kCanvasWidth + x0) * 4 + channel];
    const auto p10 = source[(y0 * kCanvasWidth + x1) * 4 + channel];
    const auto p01 = source[(y1 * kCanvasWidth + x0) * 4 + channel];
    const auto p11 = source[(y1 * kCanvasWidth + x1) * 4 + channel];
    const auto top = p00 * (1.0 - fx) + p10 * fx;
    const auto bottom = p01 * (1.0 - fx) + p11 * fx;
    result[channel] = static_cast<std::uint8_t>(std::clamp(
        std::lround(top * (1.0 - fy) + bottom * fy), 0l, 255l));
  }
  return result;
}

void appendRgb565(std::vector<std::uint8_t>& output, const std::uint8_t* rgba) {
  const auto alpha = static_cast<std::uint32_t>(rgba[3]);
  std::array<std::uint8_t, 3> color{};
  for (std::size_t channel = 0; channel < color.size(); ++channel) {
    color[channel] = static_cast<std::uint8_t>((rgba[channel] * alpha +
        kBackground[channel] * (255u - alpha) + 127u) / 255u);
  }
  const auto rgb = static_cast<std::uint16_t>(((color[0] & 0xF8u) << 8) |
      ((color[1] & 0xFCu) << 3) | (color[2] >> 3));
  output.push_back(static_cast<std::uint8_t>(rgb));
  output.push_back(static_cast<std::uint8_t>(rgb >> 8));
}

bool writeAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  auto temporary = path;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.flush();
  if (!output) {
    output.close();
    std::filesystem::remove(temporary, error);
    return false;
  }
  output.close();
  std::filesystem::rename(temporary, path, error);
  if (!error) return true;
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  if (error) std::filesystem::remove(temporary, error);
  return !error;
}

std::string hashHex(std::uint64_t hash) {
  std::ostringstream output;
  output << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

}  // namespace

AvatarRenderer::AvatarRenderer(std::filesystem::path componentRoot,
                               std::filesystem::path outputRoot)
    : componentRoot_(std::move(componentRoot)), outputRoot_(std::move(outputRoot)) {
  for (const auto kind : {AvatarComponentKind::Face, AvatarComponentKind::Outfit,
                          AvatarComponentKind::Hair}) {
    for (std::uint8_t preset = 1; preset <= 10; ++preset) {
      AvatarComponent component{};
      if (!loadAvatarComponent(componentRoot_, kind, preset, component)) return;
    }
  }
  valid_ = true;
}

bool AvatarRenderer::compose(const gridopoly::protocol::AvatarRecipe& recipe,
                             std::vector<std::uint8_t>& rgba) const {
  if (!valid_ || !validAvatarRecipe(recipe)) return false;
  AvatarComponent face{};
  AvatarComponent outfit{};
  AvatarComponent hair{};
  if (!loadAvatarComponent(componentRoot_, AvatarComponentKind::Face,
                           recipe.facePresetId, face) ||
      !loadAvatarComponent(componentRoot_, AvatarComponentKind::Outfit,
                           recipe.outfitPresetId, outfit) ||
      !loadAvatarComponent(componentRoot_, AvatarComponentKind::Hair,
                           recipe.hairPresetId, hair)) return false;
  rgba.assign(kPreviewBytes, 0);
  return placeAvatarComponent(face, skinPaletteRgb(recipe.skinToneId), rgba) &&
      placeAvatarComponent(outfit, {0, 0, 0}, rgba) &&
      placeAvatarComponent(hair, hairPaletteRgb(recipe.hairColorId), rgba);
}

AvatarRenderResult AvatarRenderer::render(std::uint32_t roomId, std::uint8_t playerId,
                                          std::uint16_t avatarRevision,
                                          const gridopoly::protocol::AvatarRecipe& recipe,
                                          bool injectFailureBeforePublish) {
  std::lock_guard<std::mutex> lock(mutex_);
  AvatarRenderResult result{};
  if (roomId == 0 || playerId == 0 || playerId > 6 || avatarRevision == 0) return result;
  std::vector<std::uint8_t> composite;
  if (!compose(recipe, composite)) return result;
  std::vector<std::uint8_t> finalSource(kCanvasBytes, 0);
  for (std::size_t y = 0; y < kPreviewHeight; ++y) {
    std::memcpy(finalSource.data() + ((y + 10u) * kCanvasWidth + 50u) * 4u,
                composite.data() + y * kPreviewWidth * 4u, kPreviewWidth * 4u);
  }
  std::vector<std::uint8_t> finalRgba(128u * 128u * 4u);
  std::vector<std::uint8_t> finalRgb565;
  finalRgb565.reserve(128u * 128u * 2u);
  for (std::size_t y = 0; y < 128; ++y) {
    for (std::size_t x = 0; x < 128; ++x) {
      auto pixel = bilinearPixel(finalSource, x, y, 128, 128);
      const auto dx = static_cast<double>(x) - 63.5;
      const auto dy = static_cast<double>(y) - 63.5;
      if (dx * dx + dy * dy > 63.5 * 63.5) pixel = {0, 0, 0, 0};
      std::memcpy(finalRgba.data() + (y * 128 + x) * 4, pixel.data(), 4);
      appendRgb565(finalRgb565, pixel.data());
    }
  }
  result.contentHash64 = fnv64(finalRgba.data(), finalRgba.size());
  result.key = "p" + std::to_string(playerId) + "-a" + std::to_string(avatarRevision) + "-" +
      hashHex(result.contentHash64);
  const auto directory = outputRoot_ / "avatars" / std::to_string(roomId);
  result.pngPath = directory / (result.key + ".png");
  result.rgb565Path = directory / (result.key + ".rgb565");
  if (std::filesystem::is_regular_file(result.pngPath) &&
      std::filesystem::is_regular_file(result.rgb565Path) &&
      std::filesystem::file_size(result.rgb565Path) == finalRgb565.size()) {
    result.ok = true;
    return result;
  }
  if (injectFailureBeforePublish) return result;
  const auto png = pngRgba(128, 128, finalRgba);
  if (png.empty() || !writeAtomic(result.pngPath, png) ||
      !writeAtomic(result.rgb565Path, finalRgb565)) {
    std::error_code error;
    std::filesystem::remove(result.pngPath, error);
    std::filesystem::remove(result.rgb565Path, error);
    return result;
  }
  result.ok = true;
  return result;
}

AvatarPreviewResult AvatarRenderer::renderPreview(
    const gridopoly::protocol::AvatarRecipe& recipe, bool injectFailureBeforePublish) {
  std::lock_guard<std::mutex> lock(mutex_);
  AvatarPreviewResult result{};
  if (!validAvatarRecipe(recipe)) return result;
  result.key = "h" + std::to_string(recipe.hairPresetId) + "-c" +
      std::to_string(recipe.hairColorId) + "-f" + std::to_string(recipe.facePresetId) +
      "-s" + std::to_string(recipe.skinToneId) + "-o" +
      std::to_string(recipe.outfitPresetId);
  // Keep the public /avatar-previews/v1 URL stable while versioning the
  // on-disk compositor cache.  A length-valid preview generated by the old
  // layered renderer must never be reused by the canonical GAVC compositor.
  result.path = outputRoot_ / "avatar-previews" / "gavc-v1" /
      (result.key + ".rgb565");
  if (std::filesystem::is_regular_file(result.path) &&
      std::filesystem::file_size(result.path) == 220u * 300u * 2u) {
    std::ifstream input(result.path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    result.contentHash64 = fnv64(bytes.data(), bytes.size());
    result.ok = true;
    return result;
  }
  std::vector<std::uint8_t> composite;
  if (!compose(recipe, composite)) return result;
  std::vector<std::uint8_t> rgb565;
  rgb565.reserve(kPreviewWidth * kPreviewHeight * 2u);
  for (std::size_t y = 0; y < kPreviewHeight; ++y) {
    for (std::size_t x = 0; x < kPreviewWidth; ++x) {
      const auto* pixel = composite.data() + (y * kPreviewWidth + x) * 4u;
      appendRgb565(rgb565, pixel);
    }
  }
  result.contentHash64 = fnv64(rgb565.data(), rgb565.size());
  if (injectFailureBeforePublish || !writeAtomic(result.path, rgb565)) return result;
  result.ok = true;
  return result;
}

}  // namespace gridopoly::pi
