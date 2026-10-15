#pragma once

#include "TileOrderJson.h"
#include <iomanip>
#include <gridopoly/core/GameEngine.h>

namespace {

TileOrderReport orderReport(unsigned boot, unsigned upstream = 0, unsigned sequence = 1) {
  TileOrderReport report;
  auto id = [](unsigned value) { std::ostringstream s; s << std::hex;
    s.width(16); s.fill('0'); s << value; return s.str(); };
  report.bootId = id(boot); report.txSeq = static_cast<std::uint16_t>(sequence);
  report.valid = upstream != 0;
  if (upstream) report.upstreamBootId = id(upstream);
  report.upstreamSeq = upstream ? static_cast<std::uint16_t>(sequence) : 0;
  return report;
}

void verifyOrderPhysicalGraph() {
  TileOrderTopology topology;
  std::vector<TileOrderNode> nodes;
  std::vector<TileOrderChain> chains;
  auto a = orderReport(1), b = orderReport(2, 1), c = orderReport(3, 2);
  assert(topology.update("c", c, 100));
  topology.build(100, nodes, chains);
  assert(nodes[0].status == "stale" && chains.empty());
  assert(topology.update("b", b, 100));
  assert(topology.update("a", a, 100));
  topology.build(100, nodes, chains);
  assert(chains.size() == 1 && chains[0].moduleIds == std::vector<std::string>({"a", "b", "c"}));
  // Repeated HTTP reports cannot extend physical evidence, even with age=0.
  assert(topology.update("b", b, 8100));
  assert(topology.update("a", a, 8100));
  assert(topology.update("c", c, 8100));
  topology.build(15100, nodes, chains);
  assert(chains.size() == 1 && chains[0].moduleIds == std::vector<std::string>({"a"}));
  b.txSeq = b.upstreamSeq = 2; c.txSeq = c.upstreamSeq = 2;
  assert(topology.update("b", b, 15101));
  assert(topology.update("c", c, 15101));
  topology.build(15101, nodes, chains); assert(chains.size() == 1);
  auto older = b; older.txSeq = 1; older.upstreamSeq = 3;
  assert(!topology.update("b", older, 15102));
  // Switching neighbors cannot resurrect an old frame from the former neighbor.
  auto switched = b; switched.upstreamBootId = c.bootId;
  assert(topology.update("b", switched, 15103));
  assert(!topology.update("b", b, 15104));
  auto reboot = orderReport(22, 1);
  assert(topology.update("b", reboot, 15105));
  assert(!topology.update("b", b, 15106));
  topology.build(15106, nodes, chains); assert(chains.size() == 1);
  // C still refers to the retired B boot and cannot silently attach to rebooted B.
  assert(nodes[2].status == "stale" && nodes[2].conflict == "unknown_upstream");

  TileOrderTopology ambiguous;
  assert(ambiguous.update("a", a, 1));
  assert(ambiguous.update("b", orderReport(2, 1), 1));
  assert(ambiguous.update("c", orderReport(3, 1), 1));
  ambiguous.build(1, nodes, chains); assert(chains.empty());
  for (const auto& n : nodes) assert(n.status == "conflict" && n.conflict == "multiple_downstream");
  TileOrderTopology cycle;
  assert(cycle.update("a", orderReport(1, 2), 1));
  assert(cycle.update("b", orderReport(2, 1), 1));
  cycle.build(1, nodes, chains); assert(chains.empty());
  assert(nodes[0].conflict == "cycle");
  TileOrderTopology duplicate;
  assert(duplicate.update("a", orderReport(1), 1));
  assert(duplicate.update("b", orderReport(1), 1));
  duplicate.build(1, nodes, chains); assert(chains.empty());
  assert(nodes[0].conflict == "duplicate_boot_id");

  // Expired B cannot permanently constrain A or create a fork when C replaces it.
  TileOrderTopology replaced;
  assert(replaced.update("a", orderReport(1), 1));
  assert(replaced.update("b", orderReport(2, 1), 1));
  replaced.suspend("b");
  assert(replaced.update("a", orderReport(1, 0, 2), 100));
  assert(replaced.update("c", orderReport(3, 1), 100));
  replaced.build(100, nodes, chains);
  assert(chains.size() == 1 && chains[0].moduleIds == std::vector<std::string>({"a", "c"}));
  // An offline historical nonce cannot poison a current unique nonce mapping.
  assert(replaced.update("d", orderReport(2), 100));
  replaced.build(100, nodes, chains); assert(chains.size() == 2);

  TileOrderTopology wrap;
  assert(wrap.update("a", orderReport(1), 1));
  auto r = orderReport(2, 1, 65535);
  assert(wrap.update("b", r, 1)); r.txSeq = r.upstreamSeq = 0;
  assert(wrap.update("b", r, 2));
  r.txSeq = r.upstreamSeq = 65535; assert(!wrap.update("b", r, 3));
  r.txSeq = r.upstreamSeq = 1; r.ageMs = 15000;
  assert(wrap.update("b", r, 4)); wrap.build(4, nodes, chains);
  assert(chains.size() == 1 && chains[0].moduleIds == std::vector<std::string>({"a"}));
}

void verifyOrderAnchorsAndLeases() {
  std::uint64_t now = 1000;
  auto state = makeState(16);
  TileDebugAssignments service([&] { return now; });
  auto a = orderReport(1), b = orderReport(2, 1), c = orderReport(3, 2), d = orderReport(4, 3);
  // Registration is deliberately C,A,D,B; physical order must still be A,B,C,D.
  assert(service.heartbeat(1, state, "c", "dc", nullptr, false, &c).result);
  assert(service.heartbeat(1, state, "a", "da", nullptr, false, &a).result);
  assert(service.heartbeat(1, state, "d", "dd", nullptr, false, &d).result);
  assert(service.heartbeat(1, state, "b", "db", nullptr, false, &b).result);
  auto view = service.snapshot(1, state);
  assert(view.assignments.empty()); assert(view.orderChains.size() == 1);
  assert(moduleFor(view, "b").orderIndex == 1);
  assert(service.set(1, state, "b", "db", state.board->tiles[0].id));
  view = service.snapshot(1, state);
  assert(assignmentFor(view, "a").mapIndex == 15);
  assert(assignmentFor(view, "a").orderOffset == -1);
  assert(assignmentFor(view, "a").source == TileDebugAssignmentSource::Order);
  assert(assignmentFor(view, "c").mapIndex == 1);
  assert(service.set(1, state, "c", "dc", state.board->tiles[10].id));
  view = service.snapshot(1, state);
  assert(assignmentFor(view, "a").mapIndex == 15);
  assert(assignmentFor(view, "c").mapIndex == 10);
  assert(assignmentFor(view, "d").mapIndex == 11);
  assert(service.clear(1, state, "b"));
  view = service.snapshot(1, state);
  assert(assignmentFor(view, "a").mapIndex == 8);
  assert(assignmentFor(view, "b").mapIndex == 9);
  assert(assignmentFor(view, "b").orderAnchorModuleId == "c");
  assert(service.clear(1, state, "c"));
  assert(service.snapshot(1, state).assignments.empty());
  assert(service.set(1, state, "a", "da", state.board->tiles[5].id));
  assert(service.set(1, state, "c", "dc", state.board->tiles[4].id));
  view = service.snapshot(1, state);
  // D derives tile5 occupied by explicit A; neither skips nor overwrites A.
  assert(!moduleFor(view, "d").assigned && moduleFor(view, "d").orderStatus == "conflict");
  assert(assignmentFor(view, "a").source == TileDebugAssignmentSource::Manual);
  const auto epoch = view.orderEpoch;
  assert(service.snapshot(1, state).orderEpoch == epoch);
  // Expiration withdraws topology and active mappings, preserves only ORDER intent.
  now += TileDebugAssignments::kLeaseMs;
  view = service.snapshot(1, state);
  assert(view.assignments.empty()); assert(view.modules.size() == 2);
  assert(!moduleFor(view, "a").online && moduleFor(view, "a").orderAnchorTileId == state.board->tiles[5].id);
  a.txSeq++;
  assert(service.heartbeat(1, state, "a", "da", nullptr, false, &a).result);
  assert(assignmentFor(service.snapshot(1, state), "a").mapIndex == 5);
  assert(service.clear(1, state, "c")); // offline clear is supported
  assert(service.snapshot(1, state).modules.size() == 1);
  assert(service.snapshot(2, state).assignments.empty()); // room resets manual intent, not guessed auto

  TileDebugAssignments conflict([&] { return now; });
  assert(conflict.heartbeat(5, state, "a", "da", nullptr, false, &a).result);
  assert(conflict.set(5, state, "a", "da", state.board->tiles[5].id));
  now += TileDebugAssignments::kLeaseMs;
  conflict.snapshot(5, state);
  assert(conflict.heartbeat(5, state, "legacy", "dl").result);
  assert(conflict.set(5, state, "legacy", "dl", state.board->tiles[5].id));
  a.txSeq++;
  assert(conflict.heartbeat(5, state, "a", "da", nullptr, false, &a).result);
  view = conflict.snapshot(5, state);
  assert(!moduleFor(view, "a").assigned && moduleFor(view, "a").orderStatus == "conflict");
  assert(assignmentFor(view, "legacy").mapIndex == 5);
  assert(conflict.clear(5, state, "a"));
  assert(moduleFor(conflict.snapshot(5, state), "a").orderAnchorTileId.empty());
}


void verifyOrderPriorityAndSuspendedSegment() {
  std::uint64_t now = 100;
  const auto state = makeState(16);
  TileDebugAssignments service([&] { return now; });
  assert(service.heartbeat(1, state, "legacy", "legacy-device").assignment.mapIndex == 0);
  auto a = orderReport(1), b = orderReport(2, 1);
  assert(service.heartbeat(1, state, "a", "da", nullptr, false, &a).result);
  assert(service.heartbeat(1, state, "b", "db", nullptr, false, &b).result);
  assert(service.set(1, state, "b", "db", state.board->tiles[1].id));
  auto view = service.snapshot(1, state);
  assert(assignmentFor(view, "a").mapIndex == 0);
  assert(!moduleFor(view, "legacy").assigned);
  assert(service.heartbeat(1, state, "legacy", "legacy-device").assignment.mapIndex == 2);
  // The retired receiver boot remains fenced even after its module lease expires.
  auto reboot = orderReport(22, 1);
  assert(service.heartbeat(1, state, "b", "db", nullptr, false, &reboot).result);
  now += TileDebugAssignments::kLeaseMs;
  service.snapshot(1, state);
  assert(service.heartbeat(1, state, "b", "db", nullptr, false, &b).result.code == TileDebugResultCode::StaleOrderReport);
  assert(!moduleFor(service.snapshot(1, state), "b").online);

  TileDebugAssignments segments([&] { return now; });
  auto c = orderReport(3, 2), d = orderReport(4, 3);
  assert(segments.heartbeat(1, state, "a", "da", nullptr, false, &a).result);
  assert(segments.heartbeat(1, state, "b", "db", nullptr, false, &b).result);
  assert(segments.heartbeat(1, state, "c", "dc", nullptr, false, &c).result);
  assert(segments.heartbeat(1, state, "d", "dd", nullptr, false, &d).result);
  assert(segments.set(1, state, "a", "da", state.board->tiles[1].id));
  assert(segments.set(1, state, "c", "dc", state.board->tiles[10].id));
  now += TileDebugAssignments::kLeaseMs;
  segments.snapshot(1, state);
  assert(segments.heartbeat(1, state, "other", "other-device").result);
  assert(segments.set(1, state, "other", "other-device", state.board->tiles[10].id));
  a.txSeq++; b.txSeq++; b.upstreamSeq++; c.txSeq++; c.upstreamSeq++; d.txSeq++; d.upstreamSeq++;
  assert(segments.heartbeat(1, state, "a", "da", nullptr, false, &a).result);
  assert(segments.heartbeat(1, state, "b", "db", nullptr, false, &b).result);
  assert(segments.heartbeat(1, state, "c", "dc", nullptr, false, &c).result);
  assert(segments.heartbeat(1, state, "d", "dd", nullptr, false, &d).result);
  view = segments.snapshot(1, state);
  assert(assignmentFor(view, "b").mapIndex == 2);
  assert(!moduleFor(view, "c").assigned);
  assert(!moduleFor(view, "d").assigned && moduleFor(view, "d").orderConflict == "anchor_unavailable");
  assert(segments.clear(1, state, "c"));
  assert(assignmentFor(segments.snapshot(1, state), "d").mapIndex == 4);
}

void verifyOrderProjectionAndJson() {
  auto p = projectTileOrder({{"x", {"a", "b"}}, {"y", {"c", "d"}}},
      {{"a", 1}, {"c", 1}}, {}, 16);
  assert(p[1].conflict && p[3].conflict && !p[0].conflict && !p[2].conflict);
  std::vector<std::string> ids;
  for (int i = 0; i < 18; ++i) ids.push_back(std::to_string(i));
  p = projectTileOrder({{"x", ids}}, {{"0", 0}}, {}, 16);
  assert(p[1].conflict && p[16].conflict && p[17].conflict);
  const std::string object = "{\"version\":1,\"bootId\":\"ABCDEF0000000001\",\"txSeq\":65535,"
      "\"upstreamBootId\":null,\"upstreamSeq\":0,\"ageMs\":0,\"valid\":false,\"inputState\":\"idle\"}";
  TileOrderReport report; std::size_t cursor = 0;
  report.upstreamBootId = "0000000000000001"; // success must replace a reused output
  assert(parseTileOrderObject(object + "}", cursor, report));
  assert(report.upstreamBootId.empty());
  for (const auto& replacement : std::vector<std::pair<std::string, std::string>>{
       {"65535", "65536"}, {"65535", "1.5"}, {"65535", "-1"}, {"65535", "01"},
       {"false", "falsejunk"}, {"null", "\"0000000000000001\""},
       {"\"version\":1", "\"version\":1,\"version\":1"},
       {"\"ageMs\":0", "\"ageMs\":15001"},
       {"\"upstreamSeq\":0", "\"upstreamSeq\":1"},
       {"\"inputState\":\"idle\"", "\"inputState\":\"bad\""}}) {
    auto bad = object; bad.replace(bad.find(replacement.first), replacement.first.size(), replacement.second);
    cursor = 0; assert(!parseTileOrderObject(bad + "}", cursor, report));
  }
}

void verifyClosedOrderRing(unsigned size) {
  const unsigned spacing = size / 4;
  // Long module IDs sort before all corners: the root must be the least corner.
  auto id = [spacing](unsigned i) { std::ostringstream out;
    out << (i % spacing == 0 ? "ring-corner-" : "aaa-long-")
        << std::setw(2) << std::setfill('0') << i; return out.str(); };
  auto report = [size, spacing](unsigned i, unsigned sequence = 1) {
    auto r = orderReport(i + 1, (i + size - 1) % size + 1, sequence);
    r.moduleRole = i % spacing == 0 ? "corner" : "long"; return r;
  };
  TileOrderTopology topology;
  std::vector<TileOrderNode> nodes; std::vector<TileOrderChain> chains;
  for (unsigned i = size; i-- > 0;) assert(topology.update(id(i), report(i), 100));
  topology.build(100, nodes, chains);
  assert(chains.size() == 1 && chains[0].closedRing && chains[0].chainId == id(0));
  for (unsigned i = 0; i < size; ++i) assert(chains[0].moduleIds[i] == id(i));
  auto plan = projectTileOrder(chains, {}, {}, size);
  assert(plan.size() == size);
  for (unsigned i = 0; i < size; ++i) assert(plan[i].mapIndex == static_cast<int>(i) && !plan[i].conflict);
  plan = projectTileOrder(chains, {{id(spacing), static_cast<int>(2 * spacing)},
      {id(2 * spacing + 1), static_cast<int>(3 * spacing + 1)}}, {}, size);
  for (unsigned i = 0; i < size; ++i) assert(plan[i].mapIndex == static_cast<int>((i + spacing) % size) && !plan[i].conflict);
  // Every corner-preserving rotation can be anchored by any module, not just a corner.
  for (unsigned anchor = 0; anchor < size; ++anchor) for (unsigned shift = 0; shift < size; shift += spacing) {
    plan = projectTileOrder(chains, {{id(anchor), static_cast<int>((anchor + shift) % size)}}, {}, size);
    for (unsigned i = 0; i < size; ++i) assert(!plan[i].conflict &&
        plan[i].mapIndex == static_cast<int>((i + shift) % size));
  }
  for (const auto& anchors : std::vector<std::map<std::string, int>>{
       {{id(0), 1}}, {{id(0), 0}, {id(spacing), static_cast<int>(2 * spacing)}},
       {{id(1), static_cast<int>(spacing)}}, {{id(0), -1}}, {{id(0), static_cast<int>(size)}}}) {
    plan = projectTileOrder(chains, anchors, {}, size);
    for (const auto& p : plan) assert(p.conflict);
  }
  plan = projectTileOrder(chains, {}, {{"outside-manual", 5}}, size);
  for (const auto& p : plan) assert(p.conflict);
  plan = projectTileOrder(chains, {{id(0), 0}}, {{"outside-manual", 0}}, size);
  for (const auto& p : plan) assert(p.conflict);
  for (const unsigned otherSize : {16u, 24u, 32u, 40u}) if (otherSize != size) {
    plan = projectTileOrder(chains, {}, {}, otherSize);
    for (const auto& p : plan) assert(p.conflict && p.mapIndex == -1);
  }
  // Random boot identifiers and registration order must not select START.
  auto reboot = report(spacing, 2); reboot.bootId = "fffffffffffffff0";
  assert(topology.update(id(spacing), reboot, 200));
  auto follower = report(spacing + 1, 2); follower.upstreamBootId = reboot.bootId;
  assert(topology.update(id(spacing + 1), follower, 200));
  topology.build(200, nodes, chains); assert(chains.size() == 1 && chains[0].chainId == id(0));
  assert(!topology.update(id(spacing), report(spacing, 3), 201)); // Retired boot cannot return.
  // An open former ring is withheld, including a still-live head and manual followers.
  auto broken = report(0, 2); broken.valid = false; broken.upstreamBootId.clear(); broken.upstreamSeq = 0;
  assert(topology.update(id(0), broken, 201));
  topology.build(201, nodes, chains); assert(chains.empty());
  for (const auto& n : nodes) assert(n.status == "stale" && n.conflict == "ring_open");
  auto repaired = report(0, 3); assert(topology.update(id(0), repaired, 202));
  topology.build(202, nodes, chains); assert(chains.size() == 1 && chains[0].closedRing);
  auto unknown = report(2, 2); unknown.moduleRole = "unknown";
  assert(topology.update(id(2), unknown, 203));
  topology.build(203, nodes, chains); assert(chains.empty());
  for (const auto& n : nodes) assert(n.status == "conflict");
  TileOrderTopology wrongSpacing;
  for (unsigned i = 0; i < size; ++i) {
    auto r = report(i); if (i == spacing - 1) r.moduleRole = "corner"; if (i == spacing) r.moduleRole = "long";
    assert(wrongSpacing.update(id(i), r, 1));
  }
  wrongSpacing.build(1, nodes, chains); assert(chains.empty());
  for (const auto& n : nodes) assert(n.conflict == "invalid_corner_spacing");

  GameEngine engine; assert(engine.reset(*BoardCatalog::findBySize(static_cast<std::uint8_t>(size)), 1));
  const auto state = engine.state(); std::uint64_t now = 1000;
  TileDebugAssignments service([&] { return now; });
  for (unsigned i = 0; i < size; ++i) {
    const auto r = report(i); assert(service.heartbeat(2, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  auto view = service.snapshot(2, state); assert(view.assignments.size() == size);
  assert(moduleFor(view, id(0).c_str()).moduleRole == "corner" && moduleFor(view, id(0).c_str()).closedRing);
  assert(service.set(2, state, id(0), "dev-" + id(0), state.board->tiles[spacing].id));
  view = service.snapshot(2, state); assert(assignmentFor(view, id(size - 1).c_str()).mapIndex == spacing - 1);
  assert(service.set(2, state, id(0), "dev-" + id(0), state.board->tiles[2 * spacing].id));
  view = service.snapshot(2, state);
  for (unsigned i = 0; i < size; ++i)
    assert(assignmentFor(view, id(i).c_str()).mapIndex == (i + 2 * spacing) % size);
  const auto before = view.revision;
  assert(!service.set(2, state, id(1), "dev-" + id(1), state.board->tiles[2 * spacing + 2].id));
  view = service.snapshot(2, state); assert(view.revision == before && assignmentFor(view, id(1).c_str()).mapIndex == 2 * spacing + 1);
  assert(service.heartbeat(2, state, id(0), "dev-" + id(0), nullptr, false, &broken).result);
  view = service.snapshot(2, state); assert(view.assignments.size() == 1);
  assert(assignmentFor(view, id(0).c_str()).source == TileDebugAssignmentSource::Manual);
  // An anchor entered while a chain was open may become invalid when it closes.
  // Preserve that intent for correction, but do not display any invalid ring mapping.
  TileDebugAssignments closing([&] { return now; });
  for (unsigned i = 0; i < size; ++i) {
    auto r = report(i);
    if (i == 0) { r.valid = false; r.upstreamBootId.clear(); r.upstreamSeq = 0; }
    assert(closing.heartbeat(3, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  assert(closing.set(3, state, id(1), "dev-" + id(1), state.board->tiles[0].id));
  const auto close = report(0, 2);
  assert(closing.heartbeat(3, state, id(0), "dev-" + id(0), nullptr, false, &close).result);
  view = closing.snapshot(3, state); assert(view.assignments.empty() && view.orderStatus == "conflict");
  assert(!moduleFor(view, id(1).c_str()).orderAnchorTileId.empty());
  assert(closing.clear(3, state, id(1)));
  assert(closing.snapshot(3, state).assignments.size() == size);
}

void verifyClosedRingFaults(unsigned size) {
  const unsigned spacing = size / 4;
  auto id = [](unsigned i) { return "fault-" + std::to_string(100 + i); };
  auto report = [size, spacing](unsigned i, unsigned sequence = 1) {
    auto r = orderReport(i + 1, (i + size - 1) % size + 1, sequence);
    r.moduleRole = i % spacing == 0 ? "corner" : "long"; return r;
  };
  std::vector<TileOrderNode> nodes; std::vector<TileOrderChain> chains;
  for (unsigned fault = 0; fault < 4; ++fault) {
    TileOrderTopology topology;
    for (unsigned i = 0; i < size; ++i) {
      auto r = report(i);
      if (fault == 0) r.moduleRole = "long";
      if (fault == 1 && i == spacing) r.moduleRole = "long";
      if (fault == 2 && i == 1) r.moduleRole = "corner";
      if (fault == 3 && i == 1) r.moduleRole = "unknown";
      assert(topology.update(id(i), r, 100));
    }
    topology.build(100, nodes, chains); assert(chains.empty());
    for (const auto& node : nodes) assert(node.status == "conflict" && node.conflict ==
        (fault == 3 ? "unknown_module_role" : "invalid_corner_count"));
  }
  TileOrderTopology topology;
  for (unsigned i = 0; i < size; ++i) assert(topology.update(id(i), report(i), 100));
  topology.build(100, nodes, chains); assert(chains.size() == 1);
  for (unsigned i = 0; i < size; ++i) assert(topology.update(id(i), report(i), 10000));
  topology.build(15100, nodes, chains); assert(chains.empty());
  for (const auto& node : nodes) assert(node.status == "stale" && node.conflict == "expired_evidence");
  // Old self/neighbor sequence evidence cannot repair the expired ring.
  for (unsigned i = 0; i < size; ++i) assert(!topology.update(id(i), report(i, 0), 15101));
  for (unsigned i = 0; i < size; ++i) assert(topology.update(id(i), report(i, 2), 15101));
  topology.build(15101, nodes, chains); assert(chains.size() == 1);
  auto duplicate = report(1, 3); duplicate.bootId = report(0).bootId;
  assert(topology.update(id(1), duplicate, 15102));
  topology.build(15102, nodes, chains); assert(chains.empty());
  auto reboot = report(1, 4); reboot.bootId = "ffffffff00000001";
  auto follower = report(2, 4); follower.upstreamBootId = reboot.bootId;
  assert(topology.update(id(1), reboot, 15103));
  assert(topology.update(id(2), follower, 15103));
  topology.build(15103, nodes, chains); assert(chains.size() == 1 && chains[0].chainId == id(0));
  assert(!topology.update(id(1), report(1, 5), 15104));
  // Two broken edges must not turn former ring fragments into anchored open chains.
  for (unsigned i : {0u, spacing + 1}) {
    auto broken = report(i, 5); broken.valid = false;
    broken.upstreamBootId.clear(); broken.upstreamSeq = 0;
    assert(topology.update(id(i), broken, 15104));
  }
  topology.build(15104, nodes, chains); assert(chains.empty());
  for (const auto& node : nodes) assert(node.conflict == "ring_open");

  const auto state = makeState(static_cast<std::uint8_t>(size));
  const auto other = makeState(size == 16 ? 32 : 16);
  std::uint64_t now = 1000;
  TileDebugAssignments service([&] { return now; });
  auto registerAll = [&](std::uint32_t room, const GameState& game) {
    for (unsigned i = 0; i < size; ++i) {
      const auto r = report(i);
      assert(service.heartbeat(room, game, id(i), "dev-" + id(i), nullptr, false, &r).result);
    }
  };
  registerAll(80, other);
  auto view = service.snapshot(80, other);
  assert(view.assignments.empty() && view.orderStatus == "conflict");
  const auto previous = view.revision;
  assert(!service.set(80, other, id(0), "dev-" + id(0), other.board->tiles[0].id));
  assert(service.snapshot(80, other).revision == previous);
  // Room changes clear manual intent, while still-fresh physical evidence is
  // independent of the selected map and can project onto a matching new board.
  assert(service.snapshot(81, state).assignments.size() == size);
  registerAll(81, state);
  assert(service.snapshot(81, state).assignments.size() == size);
  assert(service.set(81, state, id(0), "dev-" + id(0), state.board->tiles[spacing].id));
  view = service.snapshot(82, state);
  assert(view.assignments.size() == size);
  for (unsigned i = 0; i < size; ++i) {
    assert(assignmentFor(view, id(i).c_str()).mapIndex == i);
    assert(moduleFor(view, id(i).c_str()).orderAnchorTileId.empty());
  }
  now += 15000;
  assert(service.snapshot(83, state).assignments.empty());
  for (unsigned i = 0; i < size; ++i) {
    const auto fresh = report(i, 2);
    assert(service.heartbeat(83, state, id(i), "dev-" + id(i), nullptr, false, &fresh).result);
  }
  assert(service.snapshot(83, state).assignments.size() == size);

  TileDebugAssignments closing([&] { return now; });
  for (unsigned i = 0; i < size; ++i) {
    auto r = report(i);
    if (i == 0) { r.valid = false; r.upstreamBootId.clear(); r.upstreamSeq = 0; }
    assert(closing.heartbeat(90, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  assert(closing.set(90, state, id(0), "dev-" + id(0), state.board->tiles[0].id));
  assert(closing.set(90, state, id(spacing), "dev-" + id(spacing), state.board->tiles[2 * spacing].id));
  const auto close = report(0, 2);
  assert(closing.heartbeat(90, state, id(0), "dev-" + id(0), nullptr, false, &close).result);
  assert(closing.snapshot(90, state).assignments.empty());
  assert(closing.clear(90, state, id(spacing)));
  assert(closing.snapshot(90, state).assignments.size() == size);
}

void verifyUnsupportedRingSizes() {
  for (unsigned size : {2u, 15u, 17u, 63u}) {
    TileOrderTopology topology;
    for (unsigned i = 0; i < size; ++i) {
      auto r = orderReport(i + 1, (i + size - 1) % size + 1);
      r.moduleRole = size >= 4 && i % (size / 4) == 0 ? "corner" : "long";
      assert(topology.update("size-" + std::to_string(i), r, 1));
    }
    std::vector<TileOrderNode> nodes; std::vector<TileOrderChain> chains;
    topology.build(1, nodes, chains); assert(chains.empty());
    for (const auto& node : nodes) assert(node.status == "conflict" && node.conflict == "cycle");
  }
}

void verifyRingOnlineSet(unsigned size) {
  const unsigned spacing = size / 4;
  auto state = makeState(static_cast<std::uint8_t>(size));
  auto id = [](unsigned i) { return "online-" + std::to_string(100 + i); };
  auto report = [size, spacing](unsigned i, unsigned sequence) {
    auto r = orderReport(i + 1, (i + size - 1) % size + 1, sequence);
    r.moduleRole = i % spacing == 0 ? "corner" : "long"; return r;
  };
  // Test each outsider both before and after the ring becomes complete. A
  // legacy reader has no ORDER record at all; it still belongs to the registry.
  for (unsigned outsider = 0; outsider < 4; ++outsider) for (bool first : {false, true}) {
    std::uint64_t now = 1000;
    TileDebugAssignments service([&] { return now; });
    auto extra = [&] {
      auto r = orderReport(1000);
      if (outsider == 2) r.moduleRole = "long";
      if (outsider == 3) { r = orderReport(1000, 999); r.moduleRole = "long"; }
      assert(service.heartbeat(1, state, "outside", "dev-outside", nullptr, false,
          outsider == 0 ? nullptr : &r).result);
    };
    auto registerAll = [&](unsigned seq) {
      for (unsigned i = size; i-- > 0;) {
        const auto r = report(i, seq);
        const auto response = service.heartbeat(1, state, id(i), "dev-" + id(i), nullptr, false, &r);
        assert(response.result);
        if (seq == 1 && (i != 0 || first)) assert(!response.assigned);
      }
    };
    if (first) extra();
    registerAll(1);
    if (!first) assert(service.snapshot(1, state).assignments.size() == size);
    assert(service.set(1, state, id(0), "dev-" + id(0), state.board->tiles[spacing].id));
    const auto before = service.snapshot(1, state).revision;
    if (!first) extra();
    auto view = service.snapshot(1, state);
    if (!first) assert(view.revision > before);
    for (unsigned i = 0; i < size; ++i) {
      const auto& m = moduleFor(view, id(i).c_str());
      assert(m.closedRing && m.orderConflict == "ring_online_set_mismatch");
      assert(m.assigned == (i == 0));
    }
    assert(assignmentFor(view, id(0).c_str()).source == TileDebugAssignmentSource::Manual);
    // Refresh only the ring; expiry of the extra reader must automatically
    // restore the remembered phase without a clear/set or a new game.
    now += TileDebugAssignments::kLeaseMs - 1;
    registerAll(2);
    assert(!moduleFor(service.snapshot(1, state), id(1).c_str()).assigned);
    ++now;
    view = service.snapshot(1, state);
    assert(view.assignments.size() == size && view.modules.size() == size);
    for (unsigned i = 0; i < size; ++i)
      assert(assignmentFor(view, id(i).c_str()).mapIndex == (i + spacing) % size);
    assert(service.snapshot(1, state).revision == view.revision);
  }

  // Two valid rings are not one complete online ring, even if one matches the
  // current map size. The second ring fits the registry's 64-module limit.
  std::uint64_t now = 1000;
  TileDebugAssignments service([&] { return now; });
  for (unsigned i = 0; i < size; ++i) {
    const auto r = report(i, 1);
    const auto response = service.heartbeat(2, state, id(i), "dev-" + id(i), nullptr, false, &r);
    assert(response.result && response.assigned == (i + 1 == size));
  }
  const auto assignedRevision = service.snapshot(2, state).revision;
  auto moving = state;
  moving.phase = GamePhase::AwaitMoveConfirm;
  moving.pendingMove.active = true; moving.pendingMove.playerId = 1;
  moving.pendingMove.origin = 0; moving.pendingMove.target = 1;
  assert(service.movementCue(2, moving, id(1), true).mode == TileMovementCueMode::Destination);
  for (unsigned i = 0; i < 16; ++i) {
    auto r = orderReport(100 + i, 100 + (i + 15) % 16);
    r.moduleRole = i % 4 == 0 ? "corner" : "long";
    const auto name = "second-" + std::to_string(100 + i);
    assert(service.heartbeat(2, state, name, "dev-" + name, nullptr, false, &r).result);
  }
  auto view = service.snapshot(2, state);
  assert(view.assignments.empty() && view.orderChains.size() == 2 && view.revision > assignedRevision);
  for (const auto& m : view.modules)
    assert(m.closedRing && m.orderConflict == "ring_online_set_mismatch");
  auto r = report(1, 2);
  const auto withdrawn = service.heartbeat(2, moving, id(1), "dev-" + id(1), nullptr, true, &r);
  assert(withdrawn.result && !withdrawn.assigned && withdrawn.serverRevision > assignedRevision);
  assert(withdrawn.movementCue.mode == TileMovementCueMode::None);
  now += TileDebugAssignments::kLeaseMs - 1;
  for (unsigned i = 0; i < size; ++i) {
    r = report(i, 3);
    assert(service.heartbeat(2, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  ++now;
  view = service.snapshot(2, state);
  assert(view.assignments.size() == size && view.orderChains.size() == 1);
  // A fork inside the remaining graph is never a valid ring.
  r = report(2, 4); r.upstreamBootId = report(0, 4).bootId;
  assert(service.heartbeat(2, state, id(2), "dev-" + id(2), nullptr, false, &r).result);
  view = service.snapshot(2, state); assert(view.assignments.empty());
  for (const auto& m : view.modules) assert(m.orderConflict == "multiple_downstream");
  r = report(2, 5);
  assert(service.heartbeat(2, state, id(2), "dev-" + id(2), nullptr, false, &r).assigned);
  assert(service.snapshot(2, state).assignments.size() == size);

  // One member loses its HTTP lease while the others keep fresh physical
  // reports. The same boot needs a fresh sequence to restore its expired edge.
  now += TileDebugAssignments::kLeaseMs - 2;
  for (unsigned i = 1; i < size; ++i) {
    r = report(i, 6);
    assert(service.heartbeat(2, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  ++now; assert(service.snapshot(2, state).assignments.empty());
  r = report(0, 3);
  assert(!service.heartbeat(2, state, id(0), "dev-" + id(0), nullptr, false, &r).assigned);
  r = report(0, 7);
  const auto restored = service.heartbeat(2, state, id(0), "dev-" + id(0), nullptr, false, &r);
  assert(restored.assigned && restored.assignment.revision > withdrawn.serverRevision);
  assert(service.snapshot(2, state).assignments.size() == size);
}

void verifyRingActualBoardLayout(unsigned size) {
  const auto state = makeState(static_cast<std::uint8_t>(size));
  std::uint64_t now = 1000;
  TileDebugAssignments service([&] { return now; });
  for (unsigned i = 0; i < size; ++i) {
    auto r = orderReport(i + 1, (i + size - 1) % size + 1);
    r.moduleRole = i % (size / 4) == 0 ? "corner" : "long";
    const auto id = "layout-" + std::to_string(100 + i);
    assert(service.heartbeat(1, state, id, "dev-" + id, nullptr, false, &r).result);
  }
  assert(service.snapshot(1, state).assignments.size() == size);
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto board = *state.board;
    std::vector<TileDefinition> tiles(board.tiles, board.tiles + size);
    board.tiles = tiles.data();
    if (fault == 0) std::swap(tiles[1], tiles[size / 4]); // Four corners, wrong spacing.
    if (fault == 1) tiles[1].kind = TileKind::Rest; // An extra corner on a long side.
    if (fault == 2) tiles[size / 4].kind = TileKind::Start; // Duplicate START, absent HOLD.
    if (fault == 3) std::swap(tiles[0], tiles[size / 4]); // START is not the default root.
    auto invalid = state; invalid.board = &board;
    const auto view = service.snapshot(1, invalid);
    assert(view.assignments.empty());
    for (const auto& m : view.modules)
      assert(m.closedRing && m.orderConflict == "ring_board_layout_mismatch");
    assert(!service.set(1, invalid, "layout-100", "dev-layout-100", board.tiles[0].id));
    assert(service.snapshot(1, state).assignments.size() == size);
  }
}

void verifyTileOrder() {
  for (unsigned size : {16u, 24u, 32u, 40u}) {
    verifyClosedOrderRing(size);
    verifyClosedRingFaults(size);
    verifyRingOnlineSet(size);
    verifyRingActualBoardLayout(size);
  }
  verifyUnsupportedRingSizes();
  verifyOrderPhysicalGraph(); verifyOrderAnchorsAndLeases(); verifyOrderProjectionAndJson();
  verifyOrderPriorityAndSuspendedSegment();
}

}  // namespace
