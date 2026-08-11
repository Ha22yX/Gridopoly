cmake_minimum_required(VERSION 3.20)
project(Gridopoly LANGUAGES CXX)

option(GRIDOPOLY_BUILD_TESTS "Build Gridopoly host tests" ON)

add_library(gridopoly_core
  Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp
  Firmware/libraries/GridopolyCore/src/gridopoly/core/GameEngine.cpp
)
target_include_directories(gridopoly_core PUBLIC Firmware/libraries/GridopolyCore/src)
target_compile_features(gridopoly_core PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(gridopoly_core PRIVATE /W4 /WX)
else()
  target_compile_options(gridopoly_core PRIVATE -Wall -Wextra -Werror)
endif()

add_library(gridopoly_protocol
  Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp
  Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/UdpEnvelope.cpp
)
target_include_directories(gridopoly_protocol PUBLIC Firmware/libraries/GridopolyProtocol/src)
target_compile_features(gridopoly_protocol PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(gridopoly_protocol PRIVATE /W4 /WX)
else()
  target_compile_options(gridopoly_protocol PRIVATE -Wall -Wextra -Werror)
endif()

add_library(gridopoly_server_projection
  Firmware/TestGameServer/src/PlayerDetailProjection.cpp
)
target_include_directories(gridopoly_server_projection PUBLIC Firmware/TestGameServer/src)
target_link_libraries(gridopoly_server_projection PUBLIC gridopoly_core gridopoly_protocol)
target_compile_features(gridopoly_server_projection PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(gridopoly_server_projection PRIVATE /W4 /WX)
else()
  target_compile_options(gridopoly_server_projection PRIVATE -Wall -Wextra -Werror)
endif()

add_library(gridopoly_identity
  Server/RaspberryPi/src/IdentityModel.cpp
  Server/RaspberryPi/src/FileIdentityStore.cpp
)
target_include_directories(gridopoly_identity PUBLIC Server/RaspberryPi/src)
target_link_libraries(gridopoly_identity PUBLIC gridopoly_protocol)
target_compile_features(gridopoly_identity PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(gridopoly_identity PRIVATE /W4 /WX)
else()
  target_compile_options(gridopoly_identity PRIVATE -Wall -Wextra -Werror)
endif()

add_library(gridopoly_avatar_renderer
  Server/RaspberryPi/src/AvatarComponentCodec.cpp
  Server/RaspberryPi/src/AvatarRenderer.cpp
)
target_include_directories(gridopoly_avatar_renderer PUBLIC Server/RaspberryPi/src)
target_link_libraries(gridopoly_avatar_renderer PUBLIC gridopoly_identity gridopoly_protocol)
target_compile_features(gridopoly_avatar_renderer PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(gridopoly_avatar_renderer PRIVATE /W4 /WX)
else()
  target_compile_options(gridopoly_avatar_renderer PRIVATE -Wall -Wextra -Werror)
endif()

if(GRIDOPOLY_BUILD_TESTS)
  enable_testing()
  add_executable(gridopoly_core_tests tests/host/core_tests.cpp)
  target_link_libraries(gridopoly_core_tests PRIVATE gridopoly_core)
  add_test(NAME gridopoly_core_tests COMMAND gridopoly_core_tests)
  add_executable(gridopoly_protocol_tests tests/host/protocol_tests.cpp)
  target_link_libraries(gridopoly_protocol_tests PRIVATE gridopoly_protocol)
  add_test(NAME gridopoly_protocol_tests COMMAND gridopoly_protocol_tests)
  add_executable(gridopoly_udp_envelope_tests tests/host/udp_envelope_tests.cpp)
  target_link_libraries(gridopoly_udp_envelope_tests PRIVATE gridopoly_protocol)
  add_test(NAME gridopoly_udp_envelope_tests COMMAND gridopoly_udp_envelope_tests)
  add_executable(gridopoly_reliability_policy_tests tests/host/reliability_policy_tests.cpp)
  target_link_libraries(gridopoly_reliability_policy_tests PRIVATE gridopoly_protocol)
  add_test(NAME gridopoly_reliability_policy_tests COMMAND gridopoly_reliability_policy_tests)
  add_executable(gridopoly_network_recovery_policy_tests tests/host/network_recovery_policy_tests.cpp)
  target_compile_features(gridopoly_network_recovery_policy_tests PRIVATE cxx_std_17)
  add_test(NAME gridopoly_network_recovery_policy_tests COMMAND gridopoly_network_recovery_policy_tests)
  add_executable(gridopoly_player_detail_projection_tests tests/host/player_detail_projection_tests.cpp)
  target_link_libraries(gridopoly_player_detail_projection_tests PRIVATE gridopoly_server_projection)
  add_test(NAME gridopoly_player_detail_projection_tests COMMAND gridopoly_player_detail_projection_tests)
  add_executable(gridopoly_identity_model_tests tests/host/identity_model_tests.cpp)
  target_link_libraries(gridopoly_identity_model_tests PRIVATE gridopoly_identity)
  add_test(NAME gridopoly_identity_model_tests COMMAND gridopoly_identity_model_tests)
  add_executable(gridopoly_avatar_renderer_tests tests/host/avatar_renderer_tests.cpp)
  target_link_libraries(gridopoly_avatar_renderer_tests PRIVATE gridopoly_avatar_renderer)
  target_compile_definitions(gridopoly_avatar_renderer_tests PRIVATE
    GRIDOPOLY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME gridopoly_avatar_renderer_tests COMMAND gridopoly_avatar_renderer_tests)
  add_executable(gridopoly_avatar_component_codec_tests
    tests/host/avatar_component_codec_tests.cpp
    Server/RaspberryPi/src/AvatarComponentCodec.cpp
  )
  target_include_directories(gridopoly_avatar_component_codec_tests PRIVATE
    Server/RaspberryPi/src
  )
  target_link_libraries(gridopoly_avatar_component_codec_tests PRIVATE gridopoly_protocol)
  target_compile_features(gridopoly_avatar_component_codec_tests PRIVATE cxx_std_17)
  target_compile_definitions(gridopoly_avatar_component_codec_tests PRIVATE
    GRIDOPOLY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(gridopoly_avatar_component_codec_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(gridopoly_avatar_component_codec_tests PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME gridopoly_avatar_component_codec_tests
    COMMAND gridopoly_avatar_component_codec_tests)
  add_executable(gridopoly_player_avatar_component_client_tests
    tests/host/player_avatar_component_client_tests.cpp
  )
  target_link_libraries(gridopoly_player_avatar_component_client_tests PRIVATE
    gridopoly_avatar_renderer
  )
  target_compile_features(gridopoly_player_avatar_component_client_tests PRIVATE cxx_std_17)
  target_compile_definitions(gridopoly_player_avatar_component_client_tests PRIVATE
    GRIDOPOLY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  if(MSVC)
    target_compile_options(gridopoly_player_avatar_component_client_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(gridopoly_player_avatar_component_client_tests PRIVATE
      -Wall -Wextra -Werror)
  endif()
  add_test(NAME gridopoly_player_avatar_component_client_tests
    COMMAND gridopoly_player_avatar_component_client_tests)
  add_executable(gridopoly_identity_authority_tests
    tests/host/identity_authority_tests.cpp
    Server/RaspberryPi/src/AuthorityService.cpp
    Server/RaspberryPi/src/FileStateStore.cpp
  )
  target_include_directories(gridopoly_identity_authority_tests PRIVATE
    Server/RaspberryPi/src
    Firmware/TestGameServer/src
  )
  target_link_libraries(gridopoly_identity_authority_tests PRIVATE
    gridopoly_core
    gridopoly_protocol
    gridopoly_server_projection
    gridopoly_avatar_renderer
    gridopoly_identity
  )
  target_compile_features(gridopoly_identity_authority_tests PRIVATE cxx_std_17)
  target_compile_definitions(gridopoly_identity_authority_tests PRIVATE
    _CRT_SECURE_NO_WARNINGS
    GRIDOPOLY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(gridopoly_identity_authority_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(gridopoly_identity_authority_tests PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME gridopoly_identity_authority_tests COMMAND gridopoly_identity_authority_tests)
endif()

if(UNIX AND NOT APPLE)
  add_executable(gridopoly_server
    Server/RaspberryPi/src/main.cpp
    Server/RaspberryPi/src/AuthorityService.cpp
    Server/RaspberryPi/src/FileStateStore.cpp
    Server/RaspberryPi/src/UdpPlayerServer.cpp
    Server/RaspberryPi/src/HttpServer.cpp
  )
  target_include_directories(gridopoly_server PRIVATE
    Server/RaspberryPi/src
    Firmware/TestGameServer/src
  )
  target_link_libraries(gridopoly_server PRIVATE
    gridopoly_core
    gridopoly_protocol
    gridopoly_server_projection
    gridopoly_identity
    gridopoly_avatar_renderer
    pthread
  )
  target_compile_features(gridopoly_server PRIVATE cxx_std_17)
  target_compile_options(gridopoly_server PRIVATE -Wall -Wextra -Werror)
  install(TARGETS gridopoly_server RUNTIME DESTINATION bin)

  if(GRIDOPOLY_BUILD_TESTS)
    add_executable(gridopoly_udp_server_integration_tests
      tests/host/udp_server_integration_tests.cpp
      Server/RaspberryPi/src/AuthorityService.cpp
      Server/RaspberryPi/src/FileStateStore.cpp
      Server/RaspberryPi/src/UdpPlayerServer.cpp
    )
    target_include_directories(gridopoly_udp_server_integration_tests PRIVATE
      Server/RaspberryPi/src
      Firmware/TestGameServer/src
    )
    target_link_libraries(gridopoly_udp_server_integration_tests PRIVATE
      gridopoly_core
      gridopoly_protocol
      gridopoly_server_projection
      gridopoly_identity
      gridopoly_avatar_renderer
      pthread
    )
    target_compile_features(gridopoly_udp_server_integration_tests PRIVATE cxx_std_17)
    target_compile_options(gridopoly_udp_server_integration_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gridopoly_udp_server_integration_tests COMMAND gridopoly_udp_server_integration_tests)

    add_executable(gridopoly_http_asset_integration_tests
      tests/host/http_asset_integration_tests.cpp
      Server/RaspberryPi/src/AuthorityService.cpp
      Server/RaspberryPi/src/FileStateStore.cpp
      Server/RaspberryPi/src/UdpPlayerServer.cpp
      Server/RaspberryPi/src/HttpServer.cpp
    )
    target_include_directories(gridopoly_http_asset_integration_tests PRIVATE
      Server/RaspberryPi/src
      Firmware/TestGameServer/src
    )
    target_link_libraries(gridopoly_http_asset_integration_tests PRIVATE
      gridopoly_core
      gridopoly_protocol
      gridopoly_server_projection
      gridopoly_identity
      gridopoly_avatar_renderer
      pthread
    )
    target_compile_features(gridopoly_http_asset_integration_tests PRIVATE cxx_std_17)
    target_compile_definitions(gridopoly_http_asset_integration_tests PRIVATE
      GRIDOPOLY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(gridopoly_http_asset_integration_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gridopoly_http_asset_integration_tests COMMAND gridopoly_http_asset_integration_tests)

    add_executable(gridopoly_authority_persistence_tests
      tests/host/authority_persistence_tests.cpp
      Server/RaspberryPi/src/AuthorityService.cpp
      Server/RaspberryPi/src/FileStateStore.cpp
    )
    target_include_directories(gridopoly_authority_persistence_tests PRIVATE
      Server/RaspberryPi/src
      Firmware/TestGameServer/src
    )
    target_link_libraries(gridopoly_authority_persistence_tests PRIVATE
      gridopoly_core
      gridopoly_protocol
      gridopoly_server_projection
      gridopoly_identity
      gridopoly_avatar_renderer
      pthread
    )
    target_compile_features(gridopoly_authority_persistence_tests PRIVATE cxx_std_17)
    target_compile_options(gridopoly_authority_persistence_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gridopoly_authority_persistence_tests COMMAND gridopoly_authority_persistence_tests)

    add_executable(gridopoly_trade_authority_tests
      tests/host/trade_authority_tests.cpp
      Server/RaspberryPi/src/AuthorityService.cpp
      Server/RaspberryPi/src/FileStateStore.cpp
    )
    target_include_directories(gridopoly_trade_authority_tests PRIVATE
      Server/RaspberryPi/src
      Firmware/TestGameServer/src
    )
    target_link_libraries(gridopoly_trade_authority_tests PRIVATE
      gridopoly_core
      gridopoly_protocol
      gridopoly_server_projection
      gridopoly_identity
      gridopoly_avatar_renderer
      pthread
    )
    target_compile_features(gridopoly_trade_authority_tests PRIVATE cxx_std_17)
    target_compile_options(gridopoly_trade_authority_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gridopoly_trade_authority_tests COMMAND gridopoly_trade_authority_tests)
  endif()
endif()
