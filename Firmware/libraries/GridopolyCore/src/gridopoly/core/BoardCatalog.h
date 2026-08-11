#pragma once

#include "GameModel.h"

namespace gridopoly::core {

class BoardCatalog {
 public:
  static std::size_t count();
  static const BoardDefinition& at(std::size_t index);
  static const BoardDefinition* find(const char* id);
  static const BoardDefinition* findBySize(std::uint8_t tileCount);
};

}  // namespace gridopoly::core
