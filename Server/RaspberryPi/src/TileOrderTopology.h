#pragma once

#include "TileOrderProjection.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace gridopoly::pi {

struct TileOrderReport {
  std::uint32_t version{1};
  std::string bootId;
  std::uint16_t txSeq{};
  std::string upstreamBootId;
  std::uint16_t upstreamSeq{};
  std::uint32_t ageMs{};
  std::string inputState{"idle"};
  bool valid{};
};

struct TileOrderNode {
  std::string bootId;
  std::string moduleId;
  std::string upstreamModuleId;
  std::string chainId;
  int index{-1};
  std::string status{"stale"};
  std::string conflict;
};

// Physical evidence has its own lease. Reposting the same HTTP body must
// never turn one old decoded pulse frame into a permanently valid edge.
class TileOrderTopology {
 public:
  static constexpr std::uint64_t kEvidenceLeaseMs = 15000;
  static bool validBootId(const std::string& id) {
    return id.size() == 16 && id != "0000000000000000" &&
        std::all_of(id.begin(), id.end(), [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
  }
  static bool validReport(const TileOrderReport& r) {
    return r.version == 1 && validBootId(r.bootId) && r.ageMs <= kEvidenceLeaseMs &&
        (r.inputState == "idle" || r.inputState == "receiving" ||
         r.inputState == "stuckLow") &&
        (r.valid ? validBootId(r.upstreamBootId) :
         (r.upstreamBootId.empty() && r.ageMs == 0 && r.upstreamSeq == 0));
  }
  bool update(const std::string& id, TileOrderReport r, std::uint64_t now) {
    if (!validReport(r)) return false;
    for (auto& c : r.bootId) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    for (auto& c : r.upstreamBootId) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    if (!records_.count(id) && records_.size() >= 64) return false;
    auto& e = records_[id];
    if (e.retiredBoots.count(r.bootId)) return false;
    if (e.report.bootId == r.bootId &&
        static_cast<std::uint16_t>(r.txSeq - e.report.txSeq) >= 0x8000u) return false;
    if (!e.report.bootId.empty() && e.report.bootId != r.bootId) {
      if (e.retiredBoots.size() >= 64) return false;
      e.retiredBoots.insert(e.report.bootId);
      e.observations.clear();
      e.sequenceBoot.clear();
      e.evidenceUntil = 0;
    }
    if (r.valid) {
      auto previous = e.observations.find(r.upstreamBootId);
      if (previous != e.observations.end()) {
        const auto delta = static_cast<std::uint16_t>(r.upstreamSeq - previous->second.sequence);
        if (delta >= 0x8000u) return false;
        // A repeated frame from a previously replaced neighbor is replay, not
        // a newly observed cable connection. The next physical sequence is required.
        if (delta == 0 && !e.sequenceBoot.empty() && e.sequenceBoot != r.upstreamBootId) return false;
        if (delta != 0) {
          previous->second = {r.upstreamSeq,
              r.ageMs < kEvidenceLeaseMs ? now + kEvidenceLeaseMs - r.ageMs : 0};
        } else if (r.ageMs >= kEvidenceLeaseMs) previous->second.until = 0;
      } else {
        if (e.observations.size() >= 64) return false;
        previous = e.observations.emplace(r.upstreamBootId, Observation{r.upstreamSeq,
            r.ageMs < kEvidenceLeaseMs ? now + kEvidenceLeaseMs - r.ageMs : 0}).first;
      }
      e.sequenceBoot = r.upstreamBootId;
      e.evidenceUntil = previous->second.until;
    } else {
      e.evidenceUntil = 0;
      const auto previous = e.observations.find(e.sequenceBoot);
      if (previous != e.observations.end()) previous->second.until = 0;
    }
    e.report = r;
    e.reportUntil = now + kEvidenceLeaseMs;
    return true;
  }
  void suspend(const std::string& id) {
    const auto found = records_.find(id);
    if (found != records_.end()) { found->second.reportUntil = 0; found->second.evidenceUntil = 0;
      for (auto& observation : found->second.observations) observation.second.until = 0; }
  }
  bool capable(const std::string& id) const { return records_.count(id) != 0; }
  std::uint64_t leaseRemaining(std::uint64_t now) const {
    if (records_.empty()) return 0;
    std::uint64_t remaining = kEvidenceLeaseMs;
    bool live = false;
    for (const auto& entry : records_) {
      const auto& r = entry.second;
      if (now >= r.reportUntil) continue;
      live = true;
      auto until = r.reportUntil;
      if (r.report.valid) until = std::min(until, r.evidenceUntil);
      remaining = std::min(remaining, until > now ? until - now : 0);
    }
    return live ? remaining : 0;
  }
  void build(std::uint64_t now, std::vector<TileOrderNode>& nodes,
             std::vector<TileOrderChain>& chains) const {
    nodes.clear(); chains.clear();
    std::map<std::string, std::vector<std::string>> boots;
    std::map<std::string, TileOrderNode> byId;
    std::map<std::string, std::set<std::string>> adjacent, children;
    for (const auto& item : records_) {
      const auto& id = item.first;
      const auto& r = item.second;
      auto& n = byId[id]; n.moduleId = id; n.bootId = r.report.bootId; n.status = "ready";
      if (now >= r.reportUntil || (r.report.valid && now >= r.evidenceUntil)) {
        n.status = "stale"; n.conflict = "expired_evidence";
      } else if (r.report.inputState == "stuckLow") {
        n.status = "conflict"; n.conflict = "stuck_low";
      } else if (!r.report.valid && r.report.inputState != "idle") {
        n.status = "stale"; n.conflict = "incomplete_frame";
      }
      if (now < r.reportUntil) boots[r.report.bootId].push_back(id);
    }
    for (const auto& boot : boots) {
      if (boot.second.size() > 1) for (const auto& id : boot.second) {
        byId[id].status = "conflict"; byId[id].conflict = "duplicate_boot_id";
      }
    }
    for (const auto& item : records_) {
      if (!item.second.report.valid || now >= item.second.reportUntil ||
          now >= item.second.evidenceUntil) continue;
      auto& n = byId[item.first];
      const auto upstream = boots.find(item.second.report.upstreamBootId);
      if (upstream == boots.end()) {
        n.status = "stale"; n.conflict = "unknown_upstream"; continue;
      }
      if (upstream->second.size() != 1) {
        n.status = "conflict"; n.conflict = "ambiguous_upstream"; continue;
      }
      const auto& parent = upstream->second.front();
      n.upstreamModuleId = parent;
      adjacent[item.first].insert(parent); adjacent[parent].insert(item.first);
      children[parent].insert(item.first);
    }
    std::set<std::string> visited;
    for (const auto& item : byId) {
      if (visited.count(item.first)) continue;
      std::vector<std::string> component{item.first}; visited.insert(item.first);
      for (std::size_t i = 0; i < component.size(); ++i) {
        for (const auto& neighbor : adjacent[component[i]]) {
          if (visited.insert(neighbor).second) component.push_back(neighbor);
        }
      }
      std::string status = "ready", reason, head;
      for (const auto& id : component) {
        const auto& n = byId[id];
        if (n.status == "conflict" || (status == "ready" && n.status != "ready")) {
          status = n.status; reason = n.conflict;
        }
        if (children[id].size() > 1) { status = "conflict"; reason = "multiple_downstream"; }
        if (n.upstreamModuleId.empty()) head = id;
      }
      if (head.empty()) { status = "conflict"; reason = "cycle"; }
      if (status != "ready") {
        for (const auto& id : component) { byId[id].status = status; byId[id].conflict = reason; }
        continue;
      }
      TileOrderChain chain; chain.chainId = head;
      std::string cursor = head;
      while (!cursor.empty()) {
        auto& n = byId[cursor]; n.chainId = head; n.index = static_cast<int>(chain.moduleIds.size());
        chain.moduleIds.push_back(cursor);
        cursor = children[cursor].empty() ? std::string{} : *children[cursor].begin();
      }
      chains.push_back(std::move(chain));
    }
    for (const auto& item : byId) nodes.push_back(item.second);
  }

 private:
  struct Observation { std::uint16_t sequence{}; std::uint64_t until{}; };
  struct Record {
    TileOrderReport report;
    std::uint64_t reportUntil{};
    std::uint64_t evidenceUntil{};
    std::string sequenceBoot;
    std::map<std::string, Observation> observations;
    std::set<std::string> retiredBoots;
  };
  std::map<std::string, Record> records_;
};

}  // namespace gridopoly::pi
