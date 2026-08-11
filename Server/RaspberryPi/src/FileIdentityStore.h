#pragma once

#include <filesystem>

#include "IdentityModel.h"

namespace gridopoly::pi {

class FileIdentityStore {
 public:
  explicit FileIdentityStore(std::filesystem::path path);

  bool restore(IdentityRoomState& state) const;
  bool save(const IdentityRoomState& state) const;
  bool clear() const;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace gridopoly::pi
