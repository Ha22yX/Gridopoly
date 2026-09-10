#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gridopoly::pi {

// A chain is ordered from ORDER IN towards ORDER OUT. Only validated physical
// evidence may construct chains; registration order is never evidence.
struct TileOrderChain {
  std::string chainId;
  std::vector<std::string> moduleIds;
};

struct TileOrderPlacement {
  std::string moduleId;
  std::string chainId;
  std::size_t index{};
  std::string anchorModuleId;
  int offset{};
  int mapIndex{-1};
  bool manual{};
  bool conflict{};
};

// Explicit anchors retain their values. Between anchors use the nearest
// upstream anchor; before the first anchor extrapolate backwards. Every
// contender for a duplicate position is withheld, rather than arbitrarily
// winning by iteration order. Explicit anchors are never moved; legacy auto slots are lower priority.
inline std::vector<TileOrderPlacement> projectTileOrder(
    const std::vector<TileOrderChain>& chains,
    const std::map<std::string, int>& anchors,
    const std::map<std::string, int>& externalAssignments, int boardSize) {
  std::vector<TileOrderPlacement> output;
  if (boardSize <= 0) return output;
  for (const auto& chain : chains) {
    int firstAnchor = -1;
    for (std::size_t i = 0; i < chain.moduleIds.size(); ++i) {
      if (anchors.count(chain.moduleIds[i])) {
        firstAnchor = static_cast<int>(i);
        break;
      }
    }
    int anchorIndex = firstAnchor;
    for (std::size_t i = 0; i < chain.moduleIds.size(); ++i) {
      TileOrderPlacement p;
      p.moduleId = chain.moduleIds[i];
      p.chainId = chain.chainId;
      p.index = i;
      p.manual = anchors.count(p.moduleId) != 0;
      if (p.manual) anchorIndex = static_cast<int>(i);
      if (anchorIndex >= 0) {
        p.anchorModuleId = chain.moduleIds[static_cast<std::size_t>(anchorIndex)];
        p.offset = static_cast<int>(i) - anchorIndex;
        const int raw = anchors.at(p.anchorModuleId) + p.offset;
        p.mapIndex = ((raw % boardSize) + boardSize) % boardSize;
      }
      output.push_back(std::move(p));
    }
  }
  std::map<int, std::vector<std::size_t>> contenders;
  for (std::size_t i = 0; i < output.size(); ++i) {
    if (output[i].mapIndex >= 0) contenders[output[i].mapIndex].push_back(i);
  }
  for (auto& p : output) {
    if (p.manual || p.mapIndex < 0) continue;
    bool occupied = contenders[p.mapIndex].size() > 1;
    for (const auto& external : externalAssignments) {
      if (external.first != p.moduleId && external.second == p.mapIndex) occupied = true;
    }
    if (occupied) p.conflict = true;
  }
  return output;
}

}  // namespace gridopoly::pi
