#pragma once

#include <filesystem>

#include <gridopoly/core/GameEngine.h>

namespace gridopoly::pi {

class FileStateStore {
 public:
  explicit FileStateStore(std::filesystem::path path);

  bool restore(gridopoly::core::GameEngine& engine) const;
  bool save(const gridopoly::core::GameState& state) const;
  bool clear() const;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace gridopoly::pi
