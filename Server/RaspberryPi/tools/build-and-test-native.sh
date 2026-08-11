#!/bin/bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
BUILD_DIR="${GRIDOPOLY_NATIVE_BUILD_DIR:-$ROOT/build-pi-native}"
CXX="${CXX:-g++}"

COMMON_FLAGS=(
  -std=c++17
  -O2
  -Wall
  -Wextra
  -Werror
  -pthread
  -I"$ROOT/Firmware/libraries/GridopolyCore/src"
  -I"$ROOT/Firmware/libraries/GridopolyProtocol/src"
  -I"$ROOT/Firmware/TestGameServer/src"
  -I"$ROOT/Server/RaspberryPi/src"
)

CORE_SOURCES=(
  "$ROOT/Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp"
  "$ROOT/Firmware/libraries/GridopolyCore/src/gridopoly/core/GameEngine.cpp"
)
PROTOCOL_SOURCES=(
  "$ROOT/Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp"
  "$ROOT/Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/UdpEnvelope.cpp"
)
PROJECTION_SOURCES=(
  "$ROOT/Firmware/TestGameServer/src/PlayerDetailProjection.cpp"
)
AUTHORITY_SOURCES=(
  "$ROOT/Server/RaspberryPi/src/AuthorityService.cpp"
  "$ROOT/Server/RaspberryPi/src/FileStateStore.cpp"
)
IDENTITY_SOURCES=(
  "$ROOT/Server/RaspberryPi/src/IdentityModel.cpp"
  "$ROOT/Server/RaspberryPi/src/FileIdentityStore.cpp"
)
AVATAR_SOURCES=(
  "$ROOT/Server/RaspberryPi/src/AvatarComponentCodec.cpp"
  "$ROOT/Server/RaspberryPi/src/AvatarRenderer.cpp"
)

mkdir -p "$BUILD_DIR"

python3 "$ROOT/Server/RaspberryPi/tools/test-stage-web-tile-assets.py"
python3 "$ROOT/Server/RaspberryPi/tools/verify-avatar-components.py"
python3 "$ROOT/Server/RaspberryPi/tools/test-stage-avatar-components.py"

build_and_run()
{
  local name="$1"
  shift
  echo "GRIDOPOLY_NATIVE_BUILD name=$name"
  "$CXX" "${COMMON_FLAGS[@]}" "$@" -o "$BUILD_DIR/$name"
  "$BUILD_DIR/$name"
}

build_and_run gridopoly_core_tests \
  "${CORE_SOURCES[@]}" \
  "$ROOT/tests/host/core_tests.cpp"

build_and_run gridopoly_protocol_tests \
  "${PROTOCOL_SOURCES[@]}" \
  "$ROOT/tests/host/protocol_tests.cpp"

build_and_run gridopoly_udp_envelope_tests \
  "${PROTOCOL_SOURCES[@]}" \
  "$ROOT/tests/host/udp_envelope_tests.cpp"

build_and_run gridopoly_reliability_policy_tests \
  "${PROTOCOL_SOURCES[@]}" \
  "$ROOT/tests/host/reliability_policy_tests.cpp"

build_and_run gridopoly_network_recovery_policy_tests \
  "$ROOT/tests/host/network_recovery_policy_tests.cpp"

build_and_run gridopoly_player_detail_projection_tests \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "$ROOT/tests/host/player_detail_projection_tests.cpp"

build_and_run gridopoly_identity_model_tests \
  "${PROTOCOL_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "$ROOT/tests/host/identity_model_tests.cpp"

build_and_run gridopoly_avatar_renderer_tests \
  -DGRIDOPOLY_SOURCE_DIR=\"$ROOT\" \
  "${PROTOCOL_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "$ROOT/tests/host/avatar_renderer_tests.cpp"

build_and_run gridopoly_avatar_component_codec_tests \
  -DGRIDOPOLY_SOURCE_DIR=\"$ROOT\" \
  "${PROTOCOL_SOURCES[@]}" \
  "$ROOT/Server/RaspberryPi/src/AvatarComponentCodec.cpp" \
  "$ROOT/tests/host/avatar_component_codec_tests.cpp"

build_and_run gridopoly_identity_authority_tests \
  -DGRIDOPOLY_SOURCE_DIR=\"$ROOT\" \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/tests/host/identity_authority_tests.cpp"

build_and_run gridopoly_authority_persistence_tests \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/tests/host/authority_persistence_tests.cpp"

build_and_run gridopoly_trade_authority_tests \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/tests/host/trade_authority_tests.cpp"

build_and_run gridopoly_udp_server_integration_tests \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/Server/RaspberryPi/src/UdpPlayerServer.cpp" \
  "$ROOT/tests/host/udp_server_integration_tests.cpp"

build_and_run gridopoly_http_asset_integration_tests \
  -DGRIDOPOLY_SOURCE_DIR=\"$ROOT\" \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/Server/RaspberryPi/src/UdpPlayerServer.cpp" \
  "$ROOT/Server/RaspberryPi/src/HttpServer.cpp" \
  "$ROOT/tests/host/http_asset_integration_tests.cpp"

echo "GRIDOPOLY_NATIVE_BUILD name=gridopoly_server"
"$CXX" "${COMMON_FLAGS[@]}" \
  "${CORE_SOURCES[@]}" \
  "${PROTOCOL_SOURCES[@]}" \
  "${PROJECTION_SOURCES[@]}" \
  "${IDENTITY_SOURCES[@]}" \
  "${AVATAR_SOURCES[@]}" \
  "${AUTHORITY_SOURCES[@]}" \
  "$ROOT/Server/RaspberryPi/src/UdpPlayerServer.cpp" \
  "$ROOT/Server/RaspberryPi/src/HttpServer.cpp" \
  "$ROOT/Server/RaspberryPi/src/main.cpp" \
  -o "$BUILD_DIR/gridopoly_server"

echo "GRIDOPOLY_NATIVE_TESTS_PASS build_dir=$BUILD_DIR"
