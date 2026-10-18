#include "app_state.h"
#include "espnow_player_transport.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace gridopoly::protocol;
static std::vector<uint8_t> transmitted;
static unsigned checks;
static void expect(bool value, const char *name)
{
    ++checks;
    if (!value) { std::printf("FAIL %s\n", name); std::exit(1); }
}
// Stub only radio lifecycle/I/O. Parser, mapping, queue and request encoding
// are extracted verbatim from the production translation unit by the runner.
void EspNowPlayerTransport::begin(uint32_t) {}
bool EspNowPlayerTransport::send(const TransportCommand &, uint32_t) { return false; }
void EspNowPlayerTransport::tick(uint32_t) {}
bool EspNowPlayerTransport::sendFrame(TxKind, const uint8_t *data, size_t size)
{
    transmitted.assign(data, data + size); return true;
}
class Fixture : public EspNowPlayerTransport {
public:
    Fixture() { ready_ = true; linkState_ = LinkState::Online; roomId_ = 100; seatId_ = 1; }
    void receive(const IdentitySnapshot &snapshot) {
        uint8_t payload[kMaxPayloadSize]{}; size_t length = 0;
        expect(encodeIdentitySnapshot(snapshot, payload, sizeof(payload), length), "valid identity fixture encodes");
        DecodedFrame frame{}; frame.payload = payload; frame.header.roomId = roomId_;
        frame.header.payloadLength = static_cast<uint16_t>(length);
        processIdentitySnapshot(frame, 1000);
    }
    bool submit(const TransportCommand &command) {
        pendingIdentity_.active = true; pendingIdentity_.command = command;
        return sendIdentityRequest(1100);
    }
};

int main()
{
    for (uint16_t version : std::array<uint16_t, 2>{1, 2}) {
        Fixture transport;
        IdentitySnapshot snapshot{};
        snapshot.stateVersion = 1; snapshot.identityRevision = 1;
        snapshot.playerCount = 2; snapshot.selfPlayerId = 1;
        snapshot.requiredHumanMask = 3; snapshot.onlineMask = 3;
        snapshot.avatarCatalogVersion = version;
        for (uint8_t index = 0; index < 2; ++index) {
            auto &seat = snapshot.seats[index];
            seat.playerId = index + 1; seat.seatRevision = 1; seat.seatColorId = index + 1;
            seat.flags = IdentitySeatPresent | IdentitySeatHuman | IdentitySeatConnected;
        }
        // Fresh seat has no recipe. The other already-confirmed seat deliberately
        // uses the opposite catalog to catch incorrect room-wide migration.
        auto &other = snapshot.seats[1];
        other.flags |= IdentitySeatAvatarFinal; other.avatarRevision = 2;
        other.avatarContentHash64 = 0x123;
        const uint16_t otherVersion = version == 1 ? 2 : 1;
        other.recipe = AvatarRecipe{otherVersion, 10, 20, 7, 8, 10};
        snapshot.avatarFinalMask = 2;
        transport.receive(snapshot);
        TransportEvent event{};
        expect(transport.poll(event) && event.identity != nullptr, "production parser emits identity snapshot");
        expect(event.identity->seats[0].recipe.catalogVersion == version,
               "unset human recipe inherits room catalog instead of compiled default");
        expect(event.identity->seats[1].recipe.catalogVersion == otherVersion &&
               event.identity->seats[1].recipe.skinToneId == 8,
               "confirmed seat retains own catalog and recipe");
        AppState app{}; appInit(app, 0);
        appHandleTransportEvent(app, event, 1000);
        expect(app.identity.draftRecipe.catalogVersion == version &&
               app.identity.draftRecipe.facePresetId == 1 && app.page == ScreenPage::AvatarLoading,
               "app enters loading with authoritative catalog and normalized IDs");
        const uint8_t total = version == 2 ? 31 : 30;
        appUpdateAvatarPreloadProgress(app, static_cast<uint8_t>(total - 1), total, false);
        expect(app.page == ScreenPage::AvatarLoading && app.identity.avatarPreloadTotalCount == total,
               "editor waits for all catalog components including neck");
        appUpdateAvatarPreloadProgress(app, total, total, true);
        expect(app.page == ScreenPage::AvatarSetup && app.identity.avatarAssetsReady,
               "complete catalog opens editor");
        app.identity.draftRecipe.skinToneId = 8;
        appHandleTouch(app, TouchAction::IdentityConfirm, 1050);
        TransportCommand command{};
        expect(appPollCommand(app, command) && command.identityOperation == TransportIdentityOperation::ConfirmAvatar &&
               command.avatarRecipe.catalogVersion == version && command.avatarRecipe.skinToneId == 8,
               "confirm carries the edited recipe and version");
        expect(transport.submit(command), "production request sender encodes selected catalog");
        DecodedFrame frame{}; IdentityRequest decoded{};
        expect(decodeFrame(transmitted.data(), transmitted.size(), frame) &&
               decodeIdentityRequest(frame.payload, frame.header.payloadLength, decoded) &&
               decoded.avatarCatalogVersion == version && decoded.recipe.avatarCatalogVersion == version &&
               decoded.recipe.skinToneId == 8,
               "actual outgoing frame retains catalog through protocol encoding");
        expect(app.identity.seats[1].recipe.catalogVersion == otherVersion,
               "editing self does not migrate another final avatar");
    }
    std::printf("PASS %u avatar identity/parser/submission checks (default catalog %u)\n",
                checks, kAvatarDefaultCatalogVersion);
    return 0;
}
