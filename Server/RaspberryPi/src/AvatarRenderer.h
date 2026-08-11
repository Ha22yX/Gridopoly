#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <gridopoly/protocol/Protocol.h>

#include "AvatarComponentCodec.h"

namespace gridopoly::pi {

struct AvatarRenderResult {
  bool ok{};
  std::uint64_t contentHash64{};
  std::string key{};
  std::filesystem::path pngPath{};
  std::filesystem::path rgb565Path{};
};

struct AvatarPreviewResult {
  bool ok{};
  std::uint64_t contentHash64{};
  std::string key{};
  std::filesystem::path path{};
};

class AvatarRenderer {
 public:
  AvatarRenderer(std::filesystem::path componentRoot, std::filesystem::path outputRoot);

  bool valid() const { return valid_; }
  const std::filesystem::path& outputRoot() const { return outputRoot_; }
  const std::filesystem::path& componentRoot() const { return componentRoot_; }
  AvatarRenderResult render(std::uint32_t roomId, std::uint8_t playerId,
                            std::uint16_t avatarRevision,
                            const gridopoly::protocol::AvatarRecipe& recipe,
                            bool injectFailureBeforePublish = false);
  AvatarPreviewResult renderPreview(const gridopoly::protocol::AvatarRecipe& recipe,
                                    bool injectFailureBeforePublish = false);

 private:
  std::filesystem::path componentRoot_;
  std::filesystem::path outputRoot_;
  bool valid_{};
  std::mutex mutex_{};
  bool compose(const gridopoly::protocol::AvatarRecipe& recipe,
               std::vector<std::uint8_t>& rgba) const;
};

}  // namespace gridopoly::pi
