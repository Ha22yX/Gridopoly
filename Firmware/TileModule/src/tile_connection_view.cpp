#include "tile_connection_view.h"

namespace gridopoly::tile {

TileConnectionView tileConnectionView(TileNetworkLink link) {
  switch (link) {
    case TileNetworkLink::Starting:
      return {"STARTING", "INITIALIZING", "Preparing tile module",
              {88, 167, 235}};
    case TileNetworkLink::WifiConnecting:
      return {"CONNECTING", "WI-FI", "Joining Gridopoly network",
              {88, 167, 235}};
    case TileNetworkLink::ServerConnecting:
      return {"WI-FI CONNECTED", "REGISTERING", "Contacting game server",
              {242, 196, 83}};
    case TileNetworkLink::OnlineAuto:
    case TileNetworkLink::OnlineManual:
    case TileNetworkLink::NoFreeTile:
      return {"SERVER ONLINE", "WAITING FOR TILE", "No tile assigned yet",
              {82, 220, 183}};
    case TileNetworkLink::Fault:
      return {"RECONNECTING", "SERVER ERROR", "Retrying automatically",
              {239, 113, 104}};
  }
  return {"RECONNECTING", "SERVER ERROR", "Retrying automatically",
          {239, 113, 104}};
}

}  // namespace gridopoly::tile
