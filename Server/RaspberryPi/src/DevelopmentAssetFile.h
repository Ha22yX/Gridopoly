#pragma once

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gridopoly/assets/DevelopmentArtworkCatalog.h>
#include <gridopoly/assets/RemoteArtworkPolicy.h>

namespace gridopoly::pi {

struct DevelopmentAssetFile {
  std::string directory, name;
  std::size_t bytes{};
  std::uint32_t crc32{};
  bool png{};
};

inline bool developmentAssetFile(const std::string& path, DevelopmentAssetFile& file) {
  using namespace gridopoly::assets;
  const std::string prefix = kDevelopmentAssetPrefix;
  if (path.compare(0, prefix.size(), prefix) != 0) return false;
  const auto slash = path.find('/', prefix.size());
  if (slash == std::string::npos) return false;
  file.directory = path.substr(prefix.size(), slash - prefix.size());
  file.png = file.directory == "web";
  if (!file.png && file.directory != "tile" && file.directory != "player") return false;
  file.name = path.substr(slash + 1);
  const char* suffix = file.png ? ".png" : ".rgb565";
  for (const auto& property : kDevelopmentProperties) {
    for (std::uint8_t level = 1; level <= 5; ++level) {
      if (file.name != std::string(property.key) + "-l" + std::to_string(level) + suffix) continue;
      const auto target = file.directory == "tile" ? DevelopmentTarget::Tile : DevelopmentTarget::Player;
      file.bytes = file.png ? property.webPngBytes[level - 1] :
          (target == DevelopmentTarget::Tile ? kTileDevelopmentImageBytes : kPlayerDevelopmentImageBytes);
      file.crc32 = file.png ? property.webPngCrc32[level - 1] : developmentCrc32(&property, level, target);
      return true;
    }
  }
  return false;
}

// All components must be actual directories/files. openat/O_NOFOLLOW also
// closes symlink escape and check/open races; malformed sizes never allocate.
inline bool readDevelopmentAsset(const std::filesystem::path& root,
                                 const DevelopmentAssetFile& file, std::string& body) {
  int directory = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (directory < 0) return false;
  for (const auto* component : {"development", "v1", file.directory.c_str()}) {
    const int next = ::openat(directory, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    ::close(directory);
    if (next < 0) return false;
    directory = next;
  }
  const int descriptor = ::openat(directory, file.name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  ::close(directory);
  if (descriptor < 0) return false;
  struct stat info{};
  bool valid = ::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
      info.st_size >= 0 && static_cast<std::uint64_t>(info.st_size) == file.bytes;
  if (valid) {
    body.resize(file.bytes);
    std::size_t offset = 0;
    while (offset < body.size()) {
      const auto count = ::read(descriptor, &body[offset], body.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) { valid = false; break; }
      offset += static_cast<std::size_t>(count);
    }
    char extra;
    ssize_t count;
    do { count = ::read(descriptor, &extra, 1); } while (count < 0 && errno == EINTR);
    valid = valid && count == 0;
  }
  ::close(descriptor);
  if (!valid || gridopoly::assets::artworkCrc32(
          reinterpret_cast<const std::uint8_t*>(body.data()), body.size()) != file.crc32) {
    body.clear();
    return false;
  }
  return true;
}

}  // namespace gridopoly::pi
