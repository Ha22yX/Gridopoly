#include "../../Firmware/TestGameServer/src/NetworkRecoveryPolicy.h"

#include <cassert>
#include <iostream>

using namespace gridopoly::server;

int main() {
  assert(chooseNetworkRecovery(false, 9, false, true) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 2, false, false) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 3, false, false) == NetworkRecoveryAction::RestartServices);
  assert(chooseNetworkRecovery(true, 4, true, false) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 5, true, false) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 255, true, false) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 0, false, true) == NetworkRecoveryAction::RestartServices);
  assert(chooseNetworkRecovery(true, 0, true, true) == NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 0, false, false, true, false, false) ==
         NetworkRecoveryAction::RestartServices);
  assert(chooseNetworkRecovery(true, 0, false, false, true, true, false) ==
         NetworkRecoveryAction::None);
  assert(chooseNetworkRecovery(true, 0, false, false, true, true, true) ==
         NetworkRecoveryAction::ReconnectSta);
  assert(chooseNetworkRecovery(false, 9, true, true, true, true, true) ==
         NetworkRecoveryAction::None);
  std::cout << "GRIDOPOLY_NETWORK_RECOVERY_POLICY_TESTS_PASS\n";
  return 0;
}
