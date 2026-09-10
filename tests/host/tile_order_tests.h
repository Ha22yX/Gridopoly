#pragma once

#include "TileOrderJson.h"

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

void verifyTileOrder() {
  verifyOrderPhysicalGraph(); verifyOrderAnchorsAndLeases(); verifyOrderProjectionAndJson();
  verifyOrderPriorityAndSuspendedSegment();
}

}  // namespace
