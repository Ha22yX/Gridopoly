"""Extract production routines verbatim; fail if an expected boundary moves."""
from pathlib import Path
import sys


def routine(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


root, output = map(Path, sys.argv[1:3])
main = (root / "src/main.cpp").read_text(encoding="utf-8")
network = (root / "src/tile_network.cpp").read_text(encoding="utf-8")
pieces = ["namespace gridopoly::tile {", routine(network, "void copyText("),
          routine(network, "TileNetworkClient::updateTagObservation(")]
# Include the return type as well (void in the pre-fix baseline, bool after fix).
start = network.index("TileNetworkClient::updateTagObservation(")
return_type = network[network.rfind("\n", 0, start) + 1:start]
pieces[-1] = return_type + pieces[-1]
pieces += ["}", routine(main, "enum class TagStatus") + ";",
           routine(main, "TileTagReaderState networkTagReaderState(")]
if "void publishPendingTagObservation(" in main:
    if "publishPendingTagObservation();" not in routine(main, "void loop()"):
        raise ValueError("Tag publication retry is not wired into the production loop")
    pieces.append(routine(main, "void publishPendingTagObservation("))
else:
    pieces.append("void publishPendingTagObservation() {} // pre-fix baseline")
pieces += [routine(main, "void recordTagScan("),
           routine(main, "void publishTagInventory("),
           routine(main, "void restoreSafeDisplay("),
           routine(main, "Rgb playerColor("),
           routine(main, "Rgb scaleColor("),
           routine(main, "void renderLedScene(")]
start = main.index("    const bool page_changed =")
end = main.index("    const bool movement_changed =", start)
pieces.append("bool pageChanged(TileNetworkSnapshot next_network) {\n"
              + main[start:end] + "\nreturn page_changed;\n}")
start = end
end = main.index("    if (page_changed && !gDiagnosticPage)", start)
pieces.append("void consumeMovement(TileNetworkSnapshot next_network, uint32_t now) {\n"
              + main[start:end] + "\n}")
output.write_text("\n\n".join(pieces), encoding="utf-8")

if len(sys.argv) == 4:
    # Exercise the real server projection/serializer against the real device
    # parser. Only the private projectTile qualifier is removed for this fixture.
    repo = root.parents[1]
    server = (repo / "Server/RaspberryPi/src/TileDebugAssignments.cpp").read_text(encoding="utf-8")
    http = (repo / "Server/RaspberryPi/src/HttpServer.cpp").read_text(encoding="utf-8")
    wire = ["namespace gridopoly::pi {", "using namespace gridopoly::core;",
            "using gridopoly::protocol::TagPairState;",
            "using gridopoly::protocol::TagPairSnapshot;"]
    for signature in ("const char* tileKindName(", "const char* artworkKey(",
                      "const char* tileDebugAssignmentSourceName(",
                      "const char* tileMovementCueModeName("):
        wire.append(routine(server, signature))
    wire.append(routine(server, "TileDebugTile TileDebugAssignments::projectTile(")
                .replace("TileDebugAssignments::projectTile(", "projectTile(", 1))
    for signature in ("std::string jsonEscape(", "void appendTileDebugAssignmentJson(",
                      "std::string tileModuleHeartbeatJson("):
        wire.append(routine(http, signature))
    wire += ["}", "namespace gridopoly::tile {"]
    for signature in ("void copyText(", "bool equalsIgnoreCase(",
                      "JsonVariantConst findLegacyAssignment(",
                      "bool TileNetworkClient::parsePayload("):
        wire.append(routine(network, signature))
    wire.append("}")
    Path(sys.argv[3]).write_text("\n\n".join(wire), encoding="utf-8")
