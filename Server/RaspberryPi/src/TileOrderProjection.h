#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gridopoly::pi {

// Only these physical ring layouts have a supported four-corner contract.
// Other board sizes may still use independently anchored open chains.
inline int tileOrderRingCornerSpacing(std::size_t nodeCount) {
  return nodeCount == 16 || nodeCount == 32 ? static_cast<int>(nodeCount / 4) : 0;
}

// A chain is ordered from ORDER IN towards ORDER OUT. Only validated physical
// evidence may construct chains; registration order is never evidence.
struct TileOrderChain {
  std::string chainId;
  std::vector<std::string> moduleIds;
  bool closedRing{};
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

// A closed board has one phase, not independent open-chain segments. Its
// canonical root is a corner; every valid manual anchor must preserve corners.
inline bool tileOrderRingPhase(const TileOrderChain& chain,
    const std::map<std::string, int>& anchors, int boardSize, int& phase) {
  phase = 0;
  const int spacing = tileOrderRingCornerSpacing(chain.moduleIds.size());
  if (!chain.closedRing || spacing == 0 ||
      boardSize != static_cast<int>(chain.moduleIds.size())) return false;
  bool anchored = false;
  for (std::size_t i = 0; i < chain.moduleIds.size(); ++i) {
    const auto a = anchors.find(chain.moduleIds[i]);
    if (a == anchors.end()) continue;
    if (a->second < 0 || a->second >= boardSize) return false;
    const int candidate = (a->second - static_cast<int>(i) + boardSize) % boardSize;
    if (candidate % spacing || (anchored && phase != candidate)) return false;
    phase = candidate; anchored = true;
  }
  return true;
}

// Explicit anchors retain their values. Between anchors use the nearest
// upstream anchor; before the first anchor extrapolate backwards. Every
// contender for a duplicate position is withheld, rather than arbitrarily
// winning by iteration order. Explicit anchors are never moved; legacy auto slots are lower priority.
inline std::vector<TileOrderPlacement> projectTileOrder(
    const std::vector<TileOrderChain>& chains,
    const std::map<std::string, int>& anchors,
    const std::map<std::string, int>& externalAssignments, int boardSize) {
  std::vector<TileOrderPlacement> output;
  std::set<std::string> ringIds;
  if (boardSize <= 0) return output;
  for (const auto& chain : chains) {
    if (chain.closedRing) {
      ringIds.insert(chain.chainId);
      int phase = 0;
      const bool valid = tileOrderRingPhase(chain, anchors, boardSize, phase);
      std::size_t anchor = 0;
      for (std::size_t i = 0; i < chain.moduleIds.size(); ++i) if (anchors.count(chain.moduleIds[i])) { anchor = i; break; }
      for (std::size_t i = 0; i < chain.moduleIds.size(); ++i) {
        TileOrderPlacement p;
        p.moduleId = chain.moduleIds[i]; p.chainId = chain.chainId; p.index = i;
        p.anchorModuleId = chain.moduleIds[anchor]; p.offset = static_cast<int>(i) - static_cast<int>(anchor);
        p.manual = anchors.count(p.moduleId) != 0; p.conflict = !valid;
        p.mapIndex = valid ? (static_cast<int>(i) + phase) % boardSize : -1;
        output.push_back(std::move(p));
      }
      continue;
    }
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
    // Open chains retain manual priority. A ring requires every position,
    // including manual anchors, to be free of external/other-chain conflicts.
    if (p.mapIndex < 0 || (p.manual && !ringIds.count(p.chainId))) continue;
    bool occupied = contenders[p.mapIndex].size() > 1;
    for (const auto& external : externalAssignments) {
      if (external.first != p.moduleId && external.second == p.mapIndex) occupied = true;
    }
    if (occupied) p.conflict = true;
  }
  // A ring is usable only as one consistent complete board.
  for (const auto& chain : chains) if (chain.closedRing) {
    bool conflict = false;
    for (const auto& p : output) if (p.chainId == chain.chainId && p.conflict) conflict = true;
    if (conflict) for (auto& p : output) if (p.chainId == chain.chainId) p.conflict = true;
  }
  return output;
}

}  // namespace gridopoly::pi
