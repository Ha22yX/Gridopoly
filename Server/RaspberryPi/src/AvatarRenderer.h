#pragma once

#include <array>
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
  AvatarRenderer(std::filesystem::path componentRoot, std::filesystem::path outputRoot,
                 std::filesystem::path componentV2Root = {});

  bool valid(std::uint16_t catalogVersion = 0) const {
    return catalogVersion == 0 ? valid_[1] || valid_[2]
        : catalogVersion <= 2 && valid_[catalogVersion];
  }
  const std::filesystem::path& outputRoot() const { return outputRoot_; }
  const std::filesystem::path& componentRoot(std::uint16_t catalogVersion = 1) const {
    return componentRoots_[catalogVersion <= 2 ? catalogVersion : 0];
  }
  AvatarRenderResult render(std::uint32_t roomId, std::uint8_t playerId,
                            std::uint16_t avatarRevision,
                            const gridopoly::protocol::AvatarRecipe& recipe,
                            bool injectFailureBeforePublish = false);
  AvatarPreviewResult renderPreview(const gridopoly::protocol::AvatarRecipe& recipe,
                                    bool injectFailureBeforePublish = false);

 private:
  std::array<std::filesystem::path, 3> componentRoots_{};
  std::filesystem::path outputRoot_;
  std::array<bool, 3> valid_{};
  std::mutex mutex_{};
  bool compose(const gridopoly::protocol::AvatarRecipe& recipe,
               std::vector<std::uint8_t>& rgba) const;
};

}  // namespace gridopoly::pi
