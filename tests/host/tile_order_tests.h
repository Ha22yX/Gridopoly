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

void verifyClosedOrderRing() {
  auto id = [](unsigned i) { std::ostringstream out; out << "ring-" << std::setw(2) << std::setfill('0') << i; return out.str(); };
  auto report = [](unsigned i, unsigned sequence = 1) {
    auto r = orderReport(i + 1, (i + 31) % 32 + 1, sequence);
    r.moduleRole = i % 8 == 0 ? "corner" : "long"; return r;
  };
  TileOrderTopology topology;
  std::vector<TileOrderNode> nodes; std::vector<TileOrderChain> chains;
  for (unsigned i = 32; i-- > 0;) assert(topology.update(id(i), report(i), 100));
  topology.build(100, nodes, chains);
  assert(chains.size() == 1 && chains[0].closedRing && chains[0].chainId == id(0));
  for (unsigned i = 0; i < 32; ++i) assert(chains[0].moduleIds[i] == id(i));
  auto plan = projectTileOrder(chains, {}, {}, 32);
  assert(plan.size() == 32);
  for (unsigned i = 0; i < 32; ++i) assert(plan[i].mapIndex == static_cast<int>(i) && !plan[i].conflict);
  plan = projectTileOrder(chains, {{id(8), 16}, {id(17), 25}}, {}, 32);
  for (unsigned i = 0; i < 32; ++i) assert(plan[i].mapIndex == static_cast<int>((i + 8) % 32) && !plan[i].conflict);
  for (const auto& anchors : std::vector<std::map<std::string, int>>{
       {{id(0), 1}}, {{id(0), 0}, {id(8), 16}}, {{id(1), 8}}}) {
    plan = projectTileOrder(chains, anchors, {}, 32);
    for (const auto& p : plan) assert(p.conflict);
  }
  plan = projectTileOrder(chains, {}, {{"outside-manual", 5}}, 32);
  for (const auto& p : plan) assert(p.conflict);
  plan = projectTileOrder(chains, {}, {}, 16);
  for (const auto& p : plan) assert(p.conflict);
  // Random boot identifiers and registration order must not select START.
  auto reboot = report(8, 2); reboot.bootId = "fffffffffffffff0";
  assert(topology.update(id(8), reboot, 200));
  auto follower = report(9, 2); follower.upstreamBootId = reboot.bootId;
  assert(topology.update(id(9), follower, 200));
  topology.build(200, nodes, chains); assert(chains.size() == 1 && chains[0].chainId == id(0));
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
  for (unsigned i = 0; i < 32; ++i) {
    auto r = report(i); if (i == 7) r.moduleRole = "corner"; if (i == 8) r.moduleRole = "long";
    assert(wrongSpacing.update(id(i), r, 1));
  }
  wrongSpacing.build(1, nodes, chains); assert(chains.empty());
  for (const auto& n : nodes) assert(n.conflict == "invalid_corner_spacing");

  GameEngine engine; assert(engine.reset(*BoardCatalog::findBySize(32), 1));
  const auto state = engine.state(); std::uint64_t now = 1000;
  TileDebugAssignments service([&] { return now; });
  for (unsigned i = 0; i < 32; ++i) {
    const auto r = report(i); assert(service.heartbeat(2, state, id(i), "dev-" + id(i), nullptr, false, &r).result);
  }
  auto view = service.snapshot(2, state); assert(view.assignments.size() == 32);
  assert(moduleFor(view, id(0).c_str()).moduleRole == "corner" && moduleFor(view, id(0).c_str()).closedRing);
  assert(service.set(2, state, id(0), "dev-" + id(0), state.board->tiles[8].id));
  view = service.snapshot(2, state); assert(assignmentFor(view, id(31).c_str()).mapIndex == 7);
  const auto before = view.revision;
  assert(!service.set(2, state, id(1), "dev-" + id(1), state.board->tiles[10].id));
  view = service.snapshot(2, state); assert(view.revision == before && assignmentFor(view, id(1).c_str()).mapIndex == 9);
  assert(service.heartbeat(2, state, id(0), "dev-" + id(0), nullptr, false, &broken).result);
  view = service.snapshot(2, state); assert(view.assignments.size() == 1);
  assert(assignmentFor(view, id(0).c_str()).source == TileDebugAssignmentSource::Manual);
  // An anchor entered while a chain was open may become invalid when it closes.
  // Preserve that intent for correction, but do not display any invalid ring mapping.
  TileDebugAssignments closing([&] { return now; });
  for (unsigned i = 0; i < 32; ++i) {
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
  assert(closing.snapshot(3, state).assignments.size() == 32);
}

void verifyTileOrder() {
  verifyClosedOrderRing();
  verifyOrderPhysicalGraph(); verifyOrderAnchorsAndLeases(); verifyOrderProjectionAndJson();
  verifyOrderPriorityAndSuspendedSegment();
}

}  // namespace
