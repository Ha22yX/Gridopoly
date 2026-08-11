#include "app_state.h"

#include "app_config.h"
#include "demo_data.h"

#include <GridopolyCore.h>
#include <GridopolyProtocol.h>
#include <algorithm>
#include <cstring>
#include <limits.h>

namespace {

constexpr uint32_t kActionConfirmPosition = 1u << 1;
constexpr uint32_t kActionRoll = 1u << 0;
constexpr uint32_t kActionBuy = 1u << 2;
constexpr uint32_t kActionDecline = 1u << 3;
constexpr uint32_t kActionEndTurn = 1u << 4;
constexpr uint32_t kActionMortgage = 1u << 5;
constexpr uint32_t kActionUnmortgage = 1u << 6;
constexpr uint32_t kActionBuild = 1u << 7;
constexpr uint32_t kActionSellBuilding = 1u << 8;
constexpr uint32_t kActionTrade = 1u << 9;
constexpr uint32_t kActionPayDebt = 1u << 11;
constexpr uint32_t kActionDeclareBankruptcy = 1u << 12;
constexpr uint32_t kActionAuctionBid = 1u << 13;
constexpr uint32_t kActionAuctionPass = 1u << 14;
constexpr uint32_t kActionAuctionReady = 1u << 15;
constexpr uint32_t kActionCardContinue = 1u << 16;
constexpr uint32_t kDiceResultAnimationMs = 1900u;
constexpr uint32_t kDiceResultHoldMs = 700u;
constexpr uint32_t kExtraRollRewardMs = 2000u;
constexpr uint32_t kArrivalConfirmedHoldMs = 1200u;
constexpr uint32_t kCardDrawMinMs = 1400u;
constexpr uint32_t kCardResultSettleMs = 250u;
constexpr uint32_t kRollFailureHoldMs = 1200u;
constexpr uint32_t kEndTurnExitMs = 320u;
constexpr uint32_t kEndTurnMinimumPresentationMs = 1200u;
constexpr uint32_t kAuctionIntroMs = 1800u;
constexpr uint32_t kAuctionResultMs = 3000u;
constexpr uint32_t kPlayerDetailRequestTimeoutMs = 4500u;
constexpr uint32_t kPendingActionRecoveryMs = 8000u;
constexpr uint32_t kActivityBannerMs = 2800u;
constexpr uint8_t kHairPresetCount = 10;
constexpr uint8_t kHairColorCount = 20;
constexpr uint8_t kFacePresetCount = 10;
constexpr uint8_t kSkinToneCount = 8;
constexpr uint8_t kOutfitPresetCount = 10;
constexpr uint8_t kDebtContinuationReleaseHoldAndMove =
    static_cast<uint8_t>(gridopoly::core::DebtContinuation::ReleaseHoldAndMove);
constexpr uint8_t kDebtPaymentFee = static_cast<uint8_t>(gridopoly::core::EventKind::FeePaid);
constexpr uint8_t kDebtPaymentRent = static_cast<uint8_t>(gridopoly::core::EventKind::RentPaid);
constexpr uint8_t kDebtPaymentCard = static_cast<uint8_t>(gridopoly::core::EventKind::CardApplied);
constexpr uint8_t kTradeFlagSelfConfirmed = 1u << 0;
constexpr uint8_t kTradeFlagSelfLastEdited = 1u << 3;
constexpr uint8_t kTradeFlagTerminal = 1u << 5;
constexpr bool kTradeBackendAvailable =
    GRIDOPOLY_PLAYER_TRANSPORT != GRIDOPOLY_TRANSPORT_ESPNOW;

bool auctionOpening(const AppState &state)
{
    return (state.auctionFlags & 0x03u) == 0x03u ||
           (state.availableActions & kActionAuctionReady) != 0;
}

bool auctionKeyMatches(uint32_t generation, uint8_t assetIndex,
                       uint32_t expectedGeneration, uint8_t expectedAssetIndex)
{
    return generation != 0 && generation == expectedGeneration &&
           assetIndex != 0xFF && assetIndex == expectedAssetIndex;
}

bool auctionGenerationIsNewer(uint32_t candidate, uint32_t reference)
{
    return reference == 0 || static_cast<int32_t>(candidate - reference) > 0;
}

bool auctionProjectionIsOlder(const AppState &state, const TransportEvent &event)
{
    if (event.kind != TransportEventKind::AuthoritySnapshotApplied ||
        event.phase != AuthorityPhase::AwaitAuction ||
        (event.auctionFlags & 0x01u) == 0 || event.auctionGeneration == 0 ||
        state.seenAuctionGeneration == 0) {
        return false;
    }
    if (auctionKeyMatches(event.auctionGeneration, event.auctionAssetIndex,
                          state.completedAuctionGeneration,
                          state.completedAuctionAssetIndex)) {
        return true;
    }
    if (auctionKeyMatches(event.auctionGeneration, event.auctionAssetIndex,
                          state.seenAuctionGeneration, state.seenAuctionAssetIndex)) {
        return false;
    }
    return !auctionGenerationIsNewer(event.auctionGeneration,
                                     state.seenAuctionGeneration);
}

bool currentAuctionCompleted(const AppState &state)
{
    return auctionKeyMatches(state.auctionGeneration, state.auctionAssetIndex,
                             state.completedAuctionGeneration,
                             state.completedAuctionAssetIndex);
}

void clearAuctionRoomLifecycle(AppState &state)
{
    state.auctionPresentation = AuctionPresentationPhase::None;
    state.auctionPresentationUntilMs = 0;
    state.auctionResultAssetIndex = 0xFF;
    state.auctionWinnerPlayerId = 0;
    state.auctionResultAmount = 0;
    state.auctionAssetIndex = 0xFF;
    state.auctionCurrentBidderId = 0;
    state.auctionHighestBidderId = 0;
    state.auctionPassedMask = 0;
    state.auctionFlags = 0;
    state.auctionReadyMask = 0;
    state.auctionRequiredReadyMask = 0;
    state.auctionCurrentBid = 0;
    state.auctionMinimumBid = 0;
    state.auctionGeneration = 0;
    state.seenAuctionGeneration = 0;
    state.seenAuctionAssetIndex = 0xFF;
    state.completedAuctionGeneration = 0;
    state.completedAuctionAssetIndex = 0xFF;
    state.auctionReadyAttemptGeneration = 0;
    state.auctionReadyAttemptAssetIndex = 0xFF;
    state.auctionPassed = false;
}

void clearTradeRoomLifecycle(AppState &state)
{
    state.tradeOffer = TradeOfferState{};
    state.tradeGiveAssetMask = 0;
    state.tradeAmount = 0;
    state.tradeAssetListIndex = 0;
    state.tradeEntryMode = TradeEntryMode::HomeEditable;
    state.tradeReceiverPickerOpen = false;
    state.tradeReceiverPickerIndex = 0;
}

void clearActivityRoomLifecycle(AppState &state)
{
    state.activity = ActivityState{};
    state.activityListIndex = 0;
    state.lastGameEvent = TransportGameEvent{};
}

void clearCardPresentationState(AppState &state)
{
    state.cardPresentation = CardPresentationPhase::None;
    state.cardStartedMs = 0;
    state.cardRevealAtMs = 0;
    state.cardEventSequence = 0;
    state.pendingCardFlags = 0;
    state.cardIndex = 0;
    state.cardFlags = 0;
    state.cardTargetPlayerId = 0;
    state.cardTargetPosition = 0;
    state.cardOutcome = 0;
    state.cardInstanceId = 0;
    state.cardCatalogId = 0;
    state.cardEffectId = 0;
    state.cardAmount = 0;
    state.cardChance = false;
    state.cardResultValid = false;
    state.cardPresentationAcknowledged = false;
    state.cardEffectApplied = false;
}

void clearCardRoomLifecycle(AppState &state)
{
    clearCardPresentationState(state);
    state.seenCardInstanceId = 0;
    state.seenCardDrawEventSequence = 0;
    state.completedCardInstanceId = 0;
    state.completedCardDrawEventSequence = 0;
}

void clearExtraRollRoomLifecycle(AppState &state)
{
    state.extraRollPresentation = ExtraRollPresentationPhase::None;
    state.extraRollRewardStartedMs = 0;
    state.extraRollRewardUntilMs = 0;
    state.extraRollStreak = 0;
    state.extraRollDieA = 0;
    state.extraRollDieB = 0;
}

bool isIdentityPage(ScreenPage page)
{
    return page == ScreenPage::AvatarLoading || page == ScreenPage::AvatarSetup ||
           page == ScreenPage::NameReview ||
           page == ScreenPage::NameHandwriting || page == ScreenPage::PlayerReady;
}

void clearIdentityRoomLifecycle(AppState &state)
{
    state.identity = IdentityState{};
}

bool cardDrawKeyMatches(uint16_t instanceId, uint32_t drawEventSequence,
                        uint16_t expectedInstanceId,
                        uint32_t expectedDrawEventSequence)
{
    return instanceId != 0 && drawEventSequence != 0 &&
           instanceId == expectedInstanceId &&
           drawEventSequence == expectedDrawEventSequence;
}

HomePhase presentedHomePhase(const AppState &state)
{
    if (state.endTurnPresentation == EndTurnPresentationPhase::Exiting) {
        return HomePhase::MyTurnEnd;
    }
    if (state.endTurnPresentation == EndTurnPresentationPhase::WaitingHold) {
        return HomePhase::Waiting;
    }
    return state.homePhase;
}

void clearEndTurnPresentation(AppState &state)
{
    state.endTurnPresentation = EndTurnPresentationPhase::None;
    state.endTurnPresentationStartedMs = 0;
    state.endTurnPresentationUntilMs = 0;
    state.endTurnAccepted = false;
}

void touchRevision(AppState &state)
{
    ++state.revision;
}

bool isOrdinaryPage(ScreenPage page);

bool activityEventVisible(uint8_t kind)
{
    switch (kind) {
        case 2:  // TurnStarted
        case 3:  // DiceRolled
        case 5:  // MoveCompleted
        case 6:  // PassedStart
        case 7:  // PurchaseOffered
        case 8:  // AssetPurchased
        case 9:  // RentPaid
        case 10: // FeePaid
        case 11: // CardApplied
        case 12: // SentToHold
        case 13: // ReleasedFromHold
        case 14: // AssetMortgaged
        case 15: // AssetUnmortgaged
        case 16: // BuildingChanged
        case 17: // AuctionSettled
        case 18: // PlayerBankrupt
        case 19: // TurnEnded
        case 20: // GameFinished
        case 21: // TradeSettled
        case 22: // AuctionStarted
        case 23: // AuctionBid
        case 24: // AuctionPassed
        case 25: // DebtOpened
        case 26: // DebtPaid
        case 28: // TradeCreated
        case 29: // TradeUpdated
        case 30: // TradeClosed
            return true;
        default:
            return false;
    }
}

bool activityPresentationSuppressed(const AppState &state)
{
    return !state.authorityOnline || state.nav.current.page == ScreenPage::Activity ||
           !isOrdinaryPage(state.nav.current.page) || state.modal.kind != ModalKind::None ||
           state.tradeReceiverPickerOpen || state.buttonHeld ||
           state.inlineEditField != InlineEditField::None ||
           state.endTurnPresentation == EndTurnPresentationPhase::Exiting;
}

ActivityEntry *activityEntryAt(AppState &state, uint8_t newestFirstIndex)
{
    if (newestFirstIndex >= state.activity.count) return nullptr;
    const uint8_t index = static_cast<uint8_t>(
        (state.activity.head + kActivityCapacity - 1u - newestFirstIndex) %
        kActivityCapacity
    );
    return &state.activity.entries[index];
}

const ActivityEntry *activityEntryAt(const AppState &state, uint8_t newestFirstIndex)
{
    if (newestFirstIndex >= state.activity.count) return nullptr;
    const uint8_t index = static_cast<uint8_t>(
        (state.activity.head + kActivityCapacity - 1u - newestFirstIndex) %
        kActivityCapacity
    );
    return &state.activity.entries[index];
}

bool dismissActivityBanner(AppState &state);
void dismissModal(AppState &state);

bool eventTouchesSelfOwnedAsset(const AppState &state,
                                const TransportGameEvent &event)
{
    if (state.selfSeatId == 0 || event.actorId == state.selfSeatId) {
        return false;
    }

    uint8_t assetIndex = event.assetIndex;
    // MoveCompleted carries the destination tile in amount rather than an
    // asset index. Resolve it through the current board so the arrival banner
    // itself can identify a self-owned destination before rent follows.
    if (assetIndex == 0xFF && event.kind == 5 && event.amount >= 0) {
        const gridopoly::core::BoardDefinition *board =
            gridopoly::core::BoardCatalog::findBySize(state.boardSize);
        if (board != nullptr && event.amount < board->tileCount) {
            assetIndex = board->tiles[event.amount].assetIndex;
        }
    }
    if (assetIndex == 0xFF) return false;

    // Rent/debt projections identify the creditor even if the matching
    // Authority projection is still one datagram behind the GameEvent.
    if ((event.kind == 9 || event.kind == 25 || event.kind == 26) &&
        event.targetId == state.selfSeatId) {
        return true;
    }

    return state.fullAuthoritySnapshotValid &&
           assetIndex < state.authorityAssetCount &&
           state.authorityAssets[assetIndex].ownerId == state.selfSeatId;
}

void appendActivity(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    if (!activityEventVisible(event.gameEvent.kind) || event.gameEvent.actorId == 0 ||
        event.gameEvent.actorId == state.selfSeatId) {
        return;
    }

    // Sequence de-duplication happens before this function. Therefore a
    // resync record that reaches here is still new to this console, not a
    // duplicate. The HUD is a latest-event surface: keep every record in the
    // history ring while the newest one immediately replaces the old banner.
    for (uint8_t row = 0; row < state.activity.count; ++row) {
        ActivityEntry *pending = activityEntryAt(state, row);
        if (pending != nullptr) pending->announced = true;
    }
    dismissActivityBanner(state);

    ActivityEntry &entry = state.activity.entries[state.activity.head];
    entry.event = event.gameEvent;
    entry.selfOwnedAsset = eventTouchesSelfOwnedAsset(state, event.gameEvent);
    entry.announced = state.nav.current.page == ScreenPage::Activity;
    state.activity.head = static_cast<uint8_t>(
        (state.activity.head + 1u) % kActivityCapacity
    );
    if (state.activity.count < kActivityCapacity) ++state.activity.count;

    if (!entry.announced && !activityPresentationSuppressed(state)) {
        entry.announced = true;
        state.activity.bannerSequence = entry.event.sequence;
        state.activity.bannerUntilMs = nowMs + kActivityBannerMs;
    }
}

bool dismissActivityBanner(AppState &state)
{
    if (state.activity.bannerSequence == 0 && state.activity.bannerUntilMs == 0) return false;
    state.activity.bannerSequence = 0;
    state.activity.bannerUntilMs = 0;
    return true;
}

bool acknowledgeActivityPresentation(AppState &state)
{
    bool changed = dismissActivityBanner(state);
    for (uint8_t row = 0; row < state.activity.count; ++row) {
        ActivityEntry *entry = activityEntryAt(state, row);
        if (entry == nullptr || entry->announced) continue;
        entry->announced = true;
        changed = true;
    }
    return changed;
}

bool startNextActivityBanner(AppState &state, uint32_t nowMs)
{
    if (state.activity.bannerSequence != 0 || activityPresentationSuppressed(state)) return false;
    for (uint8_t row = 0; row < state.activity.count; ++row) {
        ActivityEntry *entry = activityEntryAt(state, row);
        if (entry == nullptr || entry->announced) continue;
        entry->announced = true;
        state.activity.bannerSequence = entry->event.sequence;
        state.activity.bannerUntilMs = nowMs + kActivityBannerMs;
        for (uint8_t olderRow = static_cast<uint8_t>(row + 1);
             olderRow < state.activity.count; ++olderRow) {
            ActivityEntry *older = activityEntryAt(state, olderRow);
            if (older != nullptr) older->announced = true;
        }
        return true;
    }
    return false;
}

bool transportInterruptsInlineEditing(const AppState &state, const TransportEvent &event)
{
    if (event.kind == TransportEventKind::GameEventReceived ||
        event.kind == TransportEventKind::RosterSnapshotApplied) {
        return false;
    }
    if ((event.kind == TransportEventKind::PlayerCardDrawn ||
         event.kind == TransportEventKind::PlayerCardEffectApplied) &&
        event.cardPlayerId != state.selfSeatId) {
        return false;
    }
    return event.kind != TransportEventKind::None;
}

bool hasReachedDeadline(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

bool arrivalFlowLocked(const AppState &state)
{
    if (state.nav.current.page == ScreenPage::DiceStage) {
        return state.rollAnimating || state.rollResolved || state.rollFailed;
    }
    if (state.nav.current.page == ScreenPage::MoveGuide && state.moveArrivalPending) return true;
    if (state.nav.current.page == ScreenPage::TileEvent) {
        return state.authorityPhase == AuthorityPhase::AwaitDebt &&
               !state.landingEventAcknowledged;
    }
    return state.nav.current.page == ScreenPage::CardReveal &&
           (state.cardPresentation == CardPresentationPhase::Drawing ||
            state.cardPresentation == CardPresentationPhase::Revealed);
}

bool validDice(uint8_t dieA, uint8_t dieB)
{
    return dieA >= 1 && dieA <= 6 && dieB >= 1 && dieB <= 6;
}

bool releaseHoldDebt(const AppState &state)
{
    return state.authorityPhase == AuthorityPhase::AwaitDebt &&
           state.debtContinuation == kDebtContinuationReleaseHoldAndMove;
}

bool resyncContinuesCurrentRoll(const AppState &state, const TransportEvent &event,
                                bool roomChanged)
{
    if (roomChanged || event.kind != TransportEventKind::StateSnapshotApplied ||
        !event.resync || event.selfSeatId == 0 ||
        event.activePlayerId != event.selfSeatId) {
        return false;
    }
    // A doubles reward is a local one-shot continuation of the completed roll.
    // Repeated compact resyncs must not navigate away before its local display
    // deadline, otherwise the state remains Reward but the dedicated page is
    // no longer reachable.
    if (state.nav.current.page == ScreenPage::ExtraRollReward &&
        state.extraRollPresentation == ExtraRollPresentationPhase::Reward) {
        return event.phase == AuthorityPhase::AwaitRoll;
    }
    const bool presentationActive = state.rollAnimating || state.rollResolved ||
        state.rollPresentationComplete ||
        state.moveArrivalPending || state.nav.current.page == ScreenPage::DiceStage ||
        state.nav.current.page == ScreenPage::MoveGuide ||
        state.nav.current.page == ScreenPage::TileEvent;
    if (!presentationActive) return false;
    if (event.phase == AuthorityPhase::AwaitMoveConfirm && event.pendingTarget != 0xFF) {
        return state.rollTarget == 0xFF || state.rollTarget == event.pendingTarget;
    }
    // StateSnapshot does not carry DebtContinuation. Preserve an in-flight
    // hold-release roll using the last full Authority projection until the
    // matching full projection refreshes the metadata.
    return event.phase == AuthorityPhase::AwaitDebt &&
           (releaseHoldDebt(state) || state.nav.current.page == ScreenPage::TileEvent ||
            (state.nav.current.page == ScreenPage::MoveGuide && state.moveArrivalPending));
}

void markRollResolved(AppState &state, uint8_t dieA, uint8_t dieB, uint32_t nowMs)
{
    if (!validDice(dieA, dieB) || state.rollResolved) return;
    if (!state.rollAnimating) {
        state.rollAnimating = true;
        state.rollStartedMs = nowMs;
    }
    state.dieA = dieA;
    state.dieB = dieB;
    state.rolledSteps = static_cast<uint8_t>(dieA + dieB);
    if (dieA == dieB) {
        if (state.extraRollPresentation != ExtraRollPresentationPhase::Ready) {
            state.extraRollPresentation = ExtraRollPresentationPhase::Pending;
        }
        state.extraRollRewardStartedMs = 0;
        state.extraRollRewardUntilMs = 0;
        state.extraRollDieA = dieA;
        state.extraRollDieB = dieB;
    } else {
        clearExtraRollRoomLifecycle(state);
    }
    if (state.rolledSteps == 0) return;
    if (!state.rollResolved) {
        state.rollResolved = true;
        state.rollRevealPresented = false;
        state.rollResolvedMs = nowMs;
        const uint32_t settledAtMs = state.rollStartedMs + kDiceResultAnimationMs;
        state.rollRevealMs = hasReachedDeadline(nowMs, settledAtMs) ? nowMs : settledAtMs;
    }
    state.rollFailed = false;
    state.rollFailedUntilMs = 0;
}

void noteArrivalTarget(AppState &state, uint8_t target)
{
    if (target == 0xFF) return;
    if (!state.moveArrivalPending || state.rollTarget != target) {
        state.landingEventAcknowledged = false;
    }
    state.rollTarget = target;
    state.moveArrivalPending = true;
}

bool isOrdinaryPage(ScreenPage page)
{
    switch (page) {
        case ScreenPage::Home:
        case ScreenPage::Assets:
        case ScreenPage::AssetDetail:
        case ScreenPage::Players:
        case ScreenPage::PlayerDetail:
        case ScreenPage::PlayerAssets:
        case ScreenPage::PlayerFinance:
        case ScreenPage::Activity:
        case ScreenPage::Trade:
        case ScreenPage::TradeAssetSelect:
        case ScreenPage::TradeOffer:
        case ScreenPage::DemoLab:
        case ScreenPage::Debt:
            return true;
        case ScreenPage::DiceStage:
        case ScreenPage::ExtraRollReward:
        case ScreenPage::MoveGuide:
        case ScreenPage::TileEvent:
        case ScreenPage::CardReveal:
        case ScreenPage::DebtAssets:
        case ScreenPage::Bankruptcy:
        case ScreenPage::Purchase:
        case ScreenPage::Auction:
        case ScreenPage::AvatarLoading:
        case ScreenPage::AvatarSetup:
        case ScreenPage::NameReview:
        case ScreenPage::NameHandwriting:
        case ScreenPage::PlayerReady:
            return false;
    }
    return false;
}

bool isDebtAssetScreen(ScreenPage page)
{
    return page == ScreenPage::DebtAssets;
}

bool isInteractiveListPage(ScreenPage page)
{
    return isOrdinaryPage(page) || isDebtAssetScreen(page) ||
           page == ScreenPage::Purchase || page == ScreenPage::Auction ||
           page == ScreenPage::MoveGuide || page == ScreenPage::TileEvent ||
           page == ScreenPage::CardReveal || page == ScreenPage::AvatarSetup ||
           page == ScreenPage::NameReview || page == ScreenPage::NameHandwriting;
}

bool clearInlineEditing(AppState &state)
{
    if (state.inlineEditField == InlineEditField::None) return false;
    state.inlineEditField = InlineEditField::None;
    return true;
}

InlineEditField focusedTradeField(const AppState &state)
{
    if (state.nav.current.page != ScreenPage::Trade) return InlineEditField::None;
    if (appTradeReceiverLocked(state)) {
        return state.nav.current.focus == 1 ? InlineEditField::TradeAmount
                                            : InlineEditField::None;
    }
    if (state.nav.current.focus == 2) return InlineEditField::TradeAmount;
    return InlineEditField::None;
}

uint8_t tradeReceiverCandidateCount(const AppState &state)
{
    return state.playerCount > 1 ? static_cast<uint8_t>(state.playerCount - 1) : 0;
}

uint8_t tradeReceiverCandidateAt(const AppState &state, uint8_t candidateIndex)
{
    uint8_t ordinal = 0;
    for (uint8_t playerIndex = 0; playerIndex < state.playerCount; ++playerIndex) {
        if (playerIndex == static_cast<uint8_t>(state.selfSeatId - 1)) continue;
        if (ordinal++ == candidateIndex) return playerIndex;
    }
    return 0xFF;
}

void closeTradeReceiverPicker(AppState &state)
{
    if (!state.tradeReceiverPickerOpen) return;
    state.tradeReceiverPickerOpen = false;
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void openTradeReceiverPicker(AppState &state)
{
    if (appTradeReceiverLocked(state)) return;
    const uint8_t count = tradeReceiverCandidateCount(state);
    if (count == 0) return;
    uint8_t selected = 0;
    for (uint8_t candidate = 0; candidate < count; ++candidate) {
        if (tradeReceiverCandidateAt(state, candidate) == state.tradeReceiver) {
            selected = candidate;
            break;
        }
    }
    clearInlineEditing(state);
    state.tradeReceiverPickerIndex = selected;
    state.tradeReceiverPickerOpen = true;
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void commitTradeReceiverPicker(AppState &state)
{
    if (!state.tradeReceiverPickerOpen) return;
    const uint8_t count = tradeReceiverCandidateCount(state);
    if (state.tradeReceiverPickerIndex >= count) {
        closeTradeReceiverPicker(state);
        return;
    }
    const uint8_t candidate = tradeReceiverCandidateAt(
        state, state.tradeReceiverPickerIndex
    );
    if (candidate != 0xFF) state.tradeReceiver = candidate;
    closeTradeReceiverPicker(state);
}

uint8_t tradeAssetFocus(const AppState &state)
{
    return appTradeReceiverLocked(state) ? 0 : 1;
}

uint8_t tradeSubmitFocus(const AppState &state)
{
    return appTradeReceiverLocked(state) ? 2 : 3;
}

uint8_t contentCount(const AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Home:
            return presentedHomePhase(state) == HomePhase::MyTurn ||
                           presentedHomePhase(state) == HomePhase::MyTurnEnd ? 4 : 3;
        case ScreenPage::Assets: return appVisibleAssetCount(state);
        case ScreenPage::AssetDetail: return appAssetDetailActionCount(state);
        case ScreenPage::Players: return state.playerCount;
        case ScreenPage::PlayerDetail:
            if (state.playerDetail.loadState == PlayerDetailLoadState::Ready) return 4;
            return state.playerDetail.loadState == PlayerDetailLoadState::Loading ? 0 : 1;
        case ScreenPage::PlayerAssets: return state.playerDetail.assetCount;
        case ScreenPage::PlayerFinance: return state.playerDetail.financialRecordCount;
        case ScreenPage::Activity: return state.activity.count;
        case ScreenPage::Trade: return appTradeReceiverLocked(state) ? 3 : 4;
        case ScreenPage::TradeAssetSelect: return appTradeAssetCount(state);
        case ScreenPage::TradeOffer: return appTradeOfferActionCount(state);
        case ScreenPage::DemoLab: return 6;
        case ScreenPage::Debt: return 1;
        case ScreenPage::DiceStage:
            return 0;
        case ScreenPage::ExtraRollReward:
            return 1;
        case ScreenPage::MoveGuide: return 1;
        case ScreenPage::TileEvent: return 1;
        case ScreenPage::CardReveal:
            return state.cardPresentation == CardPresentationPhase::Revealed ? 1 : 0;
        case ScreenPage::DebtAssets: return appVisibleAssetCount(state);
        case ScreenPage::Bankruptcy: return 1;
        case ScreenPage::Purchase: return 2;
        case ScreenPage::Auction:
            return state.auctionPresentation == AuctionPresentationPhase::Live &&
                           !auctionOpening(state) &&
                           !state.auctionPassed && state.decisionPlayerId == state.selfSeatId &&
                           (state.availableActions & kActionAuctionPass) != 0 ? 2 : 0;
        case ScreenPage::AvatarLoading: return 0;
        case ScreenPage::AvatarSetup: return 6;
        case ScreenPage::NameReview: return 3;
        case ScreenPage::NameHandwriting: return 2;
        case ScreenPage::PlayerReady: return 0;
    }
    return 0;
}

uint8_t &listAnchor(AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Assets: return state.assetListIndex;
        case ScreenPage::Players: return state.playerListIndex;
        case ScreenPage::PlayerAssets: return state.playerAssetListIndex;
        case ScreenPage::PlayerFinance: return state.playerFinanceListIndex;
        case ScreenPage::Activity: return state.activityListIndex;
        case ScreenPage::TradeAssetSelect: return state.tradeAssetListIndex;
        case ScreenPage::DemoLab: return state.demoListIndex;
        case ScreenPage::Debt:
        case ScreenPage::DebtAssets: return state.debtListIndex;
        default: return state.nav.current.listAnchor;
    }
}

uint8_t listAnchor(const AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Assets: return state.assetListIndex;
        case ScreenPage::Players: return state.playerListIndex;
        case ScreenPage::PlayerAssets: return state.playerAssetListIndex;
        case ScreenPage::PlayerFinance: return state.playerFinanceListIndex;
        case ScreenPage::Activity: return state.activityListIndex;
        case ScreenPage::TradeAssetSelect: return state.tradeAssetListIndex;
        case ScreenPage::DemoLab: return state.demoListIndex;
        case ScreenPage::Debt:
        case ScreenPage::DebtAssets: return state.debtListIndex;
        default: return state.nav.current.listAnchor;
    }
}

void syncLegacyState(AppState &state)
{
    state.page = state.nav.current.page;
    state.focus = state.nav.current.focus;
}

void saveCurrentListAnchor(AppState &state)
{
    state.nav.current.listAnchor = listAnchor(state, state.nav.current.page);
}

void restoreListAnchor(AppState &state)
{
    const uint8_t count = contentCount(state, state.nav.current.page);
    if (count == 0) return;
    uint8_t anchor = state.nav.current.listAnchor;
    if (anchor >= count) anchor = count - 1;
    listAnchor(state, state.nav.current.page) = anchor;
}

void setFocus(AppState &state, uint8_t focus)
{
    state.nav.current.focus = focus;
    const uint8_t count = contentCount(state, state.nav.current.page);
    if (state.nav.current.page != ScreenPage::Home && focus < count) {
        listAnchor(state, state.nav.current.page) = focus;
        state.nav.current.listAnchor = focus;
    }
    syncLegacyState(state);
    touchRevision(state);
}

void pulseListBoundary(AppState &state, int16_t delta)
{
    state.boundaryPulseDirection = delta < 0 ? -1 : 1;
    ++state.boundaryPulseRevision;
    touchRevision(state);
}

bool pushPage(AppState &state, ScreenPage page, uint8_t focus = 0)
{
    if (state.nav.depth >= sizeof(state.nav.stack) / sizeof(state.nav.stack[0])) return false;
    clearInlineEditing(state);
    state.tradeReceiverPickerOpen = false;
    saveCurrentListAnchor(state);
    state.nav.stack[state.nav.depth++] = state.nav.current;
    state.nav.current = NavigationEntry{page, focus, focus};
    const uint8_t count = contentCount(state, page);
    if (page != ScreenPage::Home && count != 0 && focus < count) {
        listAnchor(state, page) = focus;
    }
    syncLegacyState(state);
    touchRevision(state);
    return true;
}

void openActivity(AppState &state)
{
    if (state.activity.count == 0 || state.nav.current.page == ScreenPage::Activity) return;
    if (!pushPage(state, ScreenPage::Activity, 0)) return;
    for (uint8_t row = 0; row < state.activity.count; ++row) {
        ActivityEntry *entry = activityEntryAt(state, row);
        if (entry != nullptr) entry->announced = true;
    }
    dismissActivityBanner(state);
    touchRevision(state);
}

void openDemoLab(AppState &state)
{
    if (state.nav.depth >= sizeof(state.nav.stack) / sizeof(state.nav.stack[0])) return;
    pushPage(state, ScreenPage::DemoLab);
}

void replaceForcedPage(AppState &state, ScreenPage page)
{
    clearInlineEditing(state);
    state.nav.current = NavigationEntry{page, 0, 0};
    syncLegacyState(state);
    touchRevision(state);
}

void resetHome(AppState &state, uint8_t focus = 0)
{
    clearInlineEditing(state);
    state.nav = NavigationState{};
    state.nav.current = NavigationEntry{ScreenPage::Home, focus, 0};
    syncLegacyState(state);
    touchRevision(state);
}

uint8_t selfDoublesStreak(const AppState &state)
{
    for (uint8_t index = 0; index < state.playerCount && index < 6; ++index) {
        if (state.authorityPlayers[index].playerId == state.selfSeatId) {
            return state.authorityPlayers[index].doublesStreak;
        }
    }
    return 0;
}

void finishExtraRollReward(AppState &state)
{
    state.extraRollPresentation = ExtraRollPresentationPhase::Ready;
    state.extraRollRewardStartedMs = 0;
    state.extraRollRewardUntilMs = 0;
    resetHome(state, 0);
}

bool applyExtraRollAuthority(AppState &state, bool resync, uint32_t nowMs)
{
    const uint8_t streak = selfDoublesStreak(state);
    const bool ready = state.activePlayerId == state.selfSeatId &&
        state.authorityPhase == AuthorityPhase::AwaitRoll && streak != 0 &&
        (state.availableActions & kActionRoll) != 0;
    if (!ready) return false;

    state.extraRollStreak = streak;
    if (state.extraRollPresentation == ExtraRollPresentationPhase::Reward) {
        return true;
    }
    if (state.extraRollPresentation == ExtraRollPresentationPhase::Pending) {
        state.extraRollPresentation = ExtraRollPresentationPhase::Reward;
        state.extraRollRewardStartedMs = nowMs;
        state.extraRollRewardUntilMs = nowMs + kExtraRollRewardMs;
        replaceForcedPage(state, ScreenPage::ExtraRollReward);
        return true;
    }
    if (resync) {
        state.extraRollPresentation = ExtraRollPresentationPhase::Ready;
        state.extraRollRewardStartedMs = 0;
        state.extraRollRewardUntilMs = 0;
        if (state.nav.current.page != ScreenPage::Home) resetHome(state, 0);
        return true;
    }
    if (state.extraRollPresentation == ExtraRollPresentationPhase::None) {
        state.extraRollPresentation = ExtraRollPresentationPhase::Ready;
        if (!isOrdinaryPage(state.nav.current.page)) resetHome(state, 0);
    }
    return true;
}

void presentTradeOfferPage(AppState &state)
{
    clearInlineEditing(state);
    if (state.nav.current.page == ScreenPage::Home) {
        pushPage(state, ScreenPage::TradeOffer);
        return;
    }
    if (state.nav.current.page != ScreenPage::TradeOffer) {
        state.nav.current = NavigationEntry{ScreenPage::TradeOffer, 0, 0};
    } else {
        state.nav.current.focus = 0;
    }
    syncLegacyState(state);
    touchRevision(state);
}

void showToast(AppState &state, const char *message, uint32_t nowMs)
{
    state.toast = message;
    state.toastUntilMs = nowMs + 1800;
    touchRevision(state);
}

uint32_t legalDebtAssetMask(const AppState &state)
{
    uint32_t mask = state.debt.eligibleMask;
    const uint8_t count = state.fullAuthoritySnapshotValid ? state.authorityAssetCount : kAssetCount;
    for (uint8_t index = 0; index < count; ++index) {
        const bool locked = state.fullAuthoritySnapshotValid
            ? state.authorityAssets[index].ownerId != state.selfSeatId
            : kAssets[index].ruleLocked;
        if (appAssetMortgaged(state, index) || appAssetBuildingLevel(state, index) != 0 || locked) {
            mask &= ~(static_cast<uint32_t>(1u) << index);
        }
    }
    return mask;
}

int32_t saturateInt32(int64_t value)
{
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(value);
}

int32_t unmortgageCost(const AppState &state, uint8_t assetIndex)
{
    const int64_t mortgageValue = appAssetMortgageValue(state, assetIndex);
    return saturateInt32(mortgageValue + (mortgageValue + 9) / 10);
}

uint8_t commandIndex(TransportCommandKind kind)
{
    return static_cast<uint8_t>(kind);
}

uint32_t commandMask(TransportCommandKind kind)
{
    return static_cast<uint32_t>(1u << commandIndex(kind));
}

bool queueCommand(AppState &state, TransportCommandKind kind, uint32_t transactionId, uint32_t nowMs,
                   uint32_t assetMask = 0, uint8_t targetPosition = 0,
                   uint8_t assetIndex = 0xFF, int32_t argument = 0,
                   uint8_t targetPlayerId = 0)
{
    const uint8_t index = commandIndex(kind);
    const uint32_t mask = commandMask(kind);
    if (!state.authorityOnline || !state.boardCatalogCompatible || state.commandCount == 4 ||
        (state.pendingCommandMask & mask) != 0) return false;

    TransportCommand &command = state.commandQueue[state.commandTail];
    command = TransportCommand{};
    command.kind = kind;
    command.requestId = state.nextRequestId++;
    command.stateVersion = state.stateVersion;
    command.transactionId = transactionId;
    command.clientTimeMs = nowMs;
    command.assetMask = assetMask;
    command.assetIndex = assetIndex;
    command.argument = argument;
    command.targetPosition = targetPosition;
    command.targetPlayerId = targetPlayerId;
    state.commandTail = static_cast<uint8_t>((state.commandTail + 1) % 4);
    ++state.commandCount;
    state.pendingCommandMask |= mask;
    state.pendingRequestIds[index] = command.requestId;
    if (kind == TransportCommandKind::PayNow) state.pendingPayNowStartedMs = nowMs;
    return true;
}

TransportCommandKind tradeCommandKind(TransportTradeOperation operation)
{
    switch (operation) {
        case TransportTradeOperation::Query: return TransportCommandKind::TradeQuery;
        case TransportTradeOperation::Create: return TransportCommandKind::TradeCreate;
        case TransportTradeOperation::Update: return TransportCommandKind::TradeUpdate;
        case TransportTradeOperation::Confirm: return TransportCommandKind::TradeConfirm;
        case TransportTradeOperation::Reject: return TransportCommandKind::TradeReject;
        case TransportTradeOperation::Cancel: return TransportCommandKind::TradeCancel;
    }
    return TransportCommandKind::TradeQuery;
}

uint32_t tradePendingMask()
{
    return commandMask(TransportCommandKind::TradeQuery) |
           commandMask(TransportCommandKind::TradeCreate) |
           commandMask(TransportCommandKind::TradeUpdate) |
           commandMask(TransportCommandKind::TradeConfirm) |
           commandMask(TransportCommandKind::TradeReject) |
           commandMask(TransportCommandKind::TradeCancel);
}

bool queueTradeCommand(AppState &state, TransportTradeOperation operation, uint32_t nowMs,
                       uint8_t targetPlayerId, uint32_t selfAssetMask,
                       int32_t selfCash, uint32_t counterpartyAssetMask,
                       int32_t counterpartyCash, uint32_t tradeId,
                       uint16_t revision)
{
    const TransportCommandKind kind = tradeCommandKind(operation);
    const uint8_t index = commandIndex(kind);
    const bool mutation = operation != TransportTradeOperation::Query;
    if (!state.authorityOnline || !state.boardCatalogCompatible ||
        (mutation && (!state.authoritySnapshotValid || state.stateVersion == 0)) ||
        state.commandCount == 4 ||
        (state.pendingCommandMask & tradePendingMask()) != 0) {
        return false;
    }

    TransportCommand &command = state.commandQueue[state.commandTail];
    command = TransportCommand{};
    command.kind = kind;
    command.requestId = state.nextRequestId++;
    command.stateVersion = state.stateVersion;
    command.clientTimeMs = nowMs;
    command.assetMask = selfAssetMask;
    command.counterpartyAssetMask = counterpartyAssetMask;
    command.argument = selfCash;
    command.counterpartyArgument = counterpartyCash;
    command.tradeId = tradeId;
    command.tradeRevision = revision;
    command.tradeOperation = operation;
    command.targetPlayerId = targetPlayerId;
    state.commandTail = static_cast<uint8_t>((state.commandTail + 1) % 4);
    ++state.commandCount;
    state.pendingCommandMask |= commandMask(kind);
    state.pendingRequestIds[index] = command.requestId;
    return true;
}

void clearPendingRequest(AppState &state, uint32_t requestId)
{
    if (requestId == 0) return;
    for (uint8_t index = 0;
         index < sizeof(state.pendingRequestIds) / sizeof(state.pendingRequestIds[0]);
         ++index) {
        if (state.pendingRequestIds[index] != requestId) continue;
        state.pendingRequestIds[index] = 0;
        if (index == commandIndex(TransportCommandKind::PayNow)) {
            state.pendingPayNowStartedMs = 0;
        }
        state.pendingCommandMask &= ~(static_cast<uint32_t>(1u) << index);
        return;
    }
}

void clearPendingKind(AppState &state, TransportCommandKind kind)
{
    const uint8_t index = commandIndex(kind);
    state.pendingRequestIds[index] = 0;
    if (kind == TransportCommandKind::PayNow) state.pendingPayNowStartedMs = 0;
    state.pendingCommandMask &= ~commandMask(kind);
}

void clearPendingTradeRequests(AppState &state)
{
    clearPendingKind(state, TransportCommandKind::TradeQuery);
    clearPendingKind(state, TransportCommandKind::TradeCreate);
    clearPendingKind(state, TransportCommandKind::TradeUpdate);
    clearPendingKind(state, TransportCommandKind::TradeConfirm);
    clearPendingKind(state, TransportCommandKind::TradeReject);
    clearPendingKind(state, TransportCommandKind::TradeCancel);
}

bool requestPlayerDetail(AppState &state, uint32_t nowMs)
{
    const uint8_t playerId = static_cast<uint8_t>(state.selectedPlayer + 1);
    state.playerDetail = PlayerDetailState{};
    state.playerDetail.loadState = PlayerDetailLoadState::Loading;
    state.playerDetail.playerId = playerId;
    state.playerDetail.requestedAtMs = nowMs;
    state.playerAssetListIndex = 0;
    state.playerFinanceListIndex = 0;
    if (!queueCommand(state, TransportCommandKind::PlayerDetailRequest, 0, nowMs,
                      0, 0, 0xFF, 0, playerId)) {
        state.playerDetail.loadState = PlayerDetailLoadState::Failed;
        touchRevision(state);
        return false;
    }
    state.playerDetail.requestId =
        state.pendingRequestIds[commandIndex(TransportCommandKind::PlayerDetailRequest)];
    touchRevision(state);
    return true;
}

void clearStaleSubmissions(AppState &state)
{
    state.pendingCommandMask = 0;
    for (uint32_t &requestId : state.pendingRequestIds) requestId = 0;
    state.pendingPayNowStartedMs = 0;
    state.modal.submitting = false;
    state.identity.pendingRequestId = 0;
}

bool queueIdentityRequest(AppState &state, TransportIdentityOperation operation,
                          uint32_t nowMs)
{
    const bool mutation = operation != TransportIdentityOperation::Query;
    const TransportCommandKind kind = TransportCommandKind::IdentityRequest;
    const uint8_t index = commandIndex(kind);
    const uint32_t mask = commandMask(kind);
    if (!state.authorityOnline || state.commandCount == 4 ||
        (state.pendingCommandMask & mask) != 0 ||
        (mutation && (state.stateVersion == 0 || state.identity.ownSeatRevision == 0))) {
        return false;
    }

    TransportCommand &command = state.commandQueue[state.commandTail];
    command = TransportCommand{};
    command.kind = kind;
    command.requestId = state.nextRequestId++;
    command.stateVersion = mutation ? state.stateVersion : 0;
    command.clientTimeMs = nowMs;
    command.identityOperation = operation;
    command.identitySeatRevision = state.identity.ownSeatRevision;
    command.avatarRecipe = state.identity.draftRecipe;
    strncpy(command.identityName, state.identity.draftName,
            sizeof(command.identityName) - 1);
    command.identityName[sizeof(command.identityName) - 1] = '\0';
    command.targetPlayerId = state.selfSeatId;
    state.commandTail = static_cast<uint8_t>((state.commandTail + 1) % 4);
    ++state.commandCount;
    state.pendingCommandMask |= mask;
    state.pendingRequestIds[index] = command.requestId;
    state.identity.pendingRequestId = command.requestId;
    return true;
}

void showIdentityPage(AppState &state, ScreenPage page, uint8_t focus = 0)
{
    clearInlineEditing(state);
    state.tradeReceiverPickerOpen = false;
    dismissModal(state);
    dismissActivityBanner(state);
    state.nav.depth = 0;
    memset(state.nav.stack, 0, sizeof(state.nav.stack));
    state.nav.current = NavigationEntry{page, focus, 0};
    state.identity.focus = focus;
    syncLegacyState(state);
}

uint8_t wrapIdentityValue(uint8_t current, int16_t delta, uint8_t maximum)
{
    if (maximum == 0 || delta == 0) return current;
    int32_t value = current == 0 ? 1 : current;
    value = (value - 1 + delta) % maximum;
    if (value < 0) value += maximum;
    return static_cast<uint8_t>(value + 1);
}

void applyIdentityDraftDelta(AppState &state, int16_t delta)
{
    switch (static_cast<AvatarEditField>(state.nav.current.focus)) {
        case AvatarEditField::HairPreset:
            state.identity.draftRecipe.hairPresetId = wrapIdentityValue(
                state.identity.draftRecipe.hairPresetId, delta, kHairPresetCount);
            break;
        case AvatarEditField::HairColor:
            state.identity.draftRecipe.hairColorId = wrapIdentityValue(
                state.identity.draftRecipe.hairColorId, delta, kHairColorCount);
            break;
        case AvatarEditField::FacePreset:
            state.identity.draftRecipe.facePresetId = wrapIdentityValue(
                state.identity.draftRecipe.facePresetId, delta, kFacePresetCount);
            break;
        case AvatarEditField::SkinTone:
            state.identity.draftRecipe.skinToneId = wrapIdentityValue(
                state.identity.draftRecipe.skinToneId, delta, kSkinToneCount);
            break;
        case AvatarEditField::OutfitPreset:
            state.identity.draftRecipe.outfitPresetId = wrapIdentityValue(
                state.identity.draftRecipe.outfitPresetId, delta, kOutfitPresetCount);
            break;
        case AvatarEditField::Confirm:
            return;
    }
    state.identity.draftDirty = true;
    touchRevision(state);
}

void applyIdentitySnapshot(AppState &state, const TransportEvent &event,
                           uint32_t nowMs)
{
    if (event.selfSeatId != 0) state.selfSeatId = event.selfSeatId;
    state.identity.authorityPhase = event.identityPhase;
    state.identity.lastResult = event.identityResult;
    state.identity.revision = event.identityRevision;
    state.identity.seatCount = event.identitySeatCount > 6 ? 6 : event.identitySeatCount;
    state.identity.humanMask = event.identityHumanMask;
    state.identity.avatarReadyMask = event.identityAvatarReadyMask;
    state.identity.nameReadyMask = event.identityNameReadyMask;
    state.identity.readyMask = event.identityReadyMask;
    state.identity.onlineMask = event.identityOnlineMask;
    if (event.identity != nullptr) {
        memcpy(state.identity.seats, event.identity->seats,
               sizeof(state.identity.seats));
        if (state.rosterSnapshotValid) {
            for (uint8_t index = 0; index < state.identity.seatCount; ++index) {
                strncpy(state.identity.seats[index].name, state.rosterNames[index], 16);
                state.identity.seats[index].name[16] = '\0';
            }
        }
    }

    const uint8_t selfIndex = state.selfSeatId == 0
        ? 0xFF : static_cast<uint8_t>(state.selfSeatId - 1);
    if (selfIndex < state.identity.seatCount) {
        const TransportIdentitySeat &self = state.identity.seats[selfIndex];
        state.identity.ownSeatRevision = self.seatRevision;
        if (!state.identity.draftInitialized) {
            state.identity.draftRecipe = normalizedTransportAvatarRecipe(self.recipe);
            state.identity.confirmedRecipe = state.identity.draftRecipe;
            state.identity.draftInitialized = true;
            state.identity.draftDirty = false;
        }
    }

    const uint32_t identityRequestId =
        state.pendingRequestIds[commandIndex(TransportCommandKind::IdentityRequest)];
    if (event.requestId != 0 && event.requestId == identityRequestId) {
        clearPendingRequest(state, event.requestId);
        state.identity.pendingRequestId = 0;
        if (event.identityResult != TransportIdentityResult::Ok) {
            state.identity.phase = IdentityClientPhase::AvatarEditing;
            showToast(state,
                      event.identityResult == TransportIdentityResult::DuplicateName
                          ? "NAME ALREADY IN USE"
                          : "SETUP CHANGED - TRY AGAIN",
                      nowMs);
        }
    }

    if (event.identityPhase == TransportIdentityPhase::Inactive ||
        event.identityPhase == TransportIdentityPhase::Active) {
        state.identity.phase = IdentityClientPhase::Inactive;
        state.identity.countdownDeadlineMs = 0;
        state.identity.avatarEditOverride = false;
        if (isIdentityPage(state.nav.current.page)) resetHome(state, 0);
        return;
    }

    if (selfIndex >= state.identity.seatCount) return;
    const uint8_t selfBit = static_cast<uint8_t>(1u << selfIndex);
    const TransportIdentitySeat &self = state.identity.seats[selfIndex];
    const bool seatProvesAvatarReady =
        (self.flags & gridopoly::protocol::IdentitySeatAvatarFinal) != 0;
    const bool seatProvesNameReady =
        (self.flags & gridopoly::protocol::IdentitySeatNameFinal) != 0;
    const bool rawStageProvesAvatarReady =
        event.identitySelfStage >= static_cast<uint8_t>(
            gridopoly::protocol::IdentitySeatStage::NameSetup);
    const bool rawStageProvesNameReady =
        event.identitySelfStage >= static_cast<uint8_t>(
            gridopoly::protocol::IdentitySeatStage::Ready);
    const bool stageProvesAvatarReady =
        event.identityPhase == TransportIdentityPhase::AwaitName ||
        event.identityPhase == TransportIdentityPhase::Ready ||
        event.identityPhase == TransportIdentityPhase::Countdown;
    const bool stageProvesNameReady =
        event.identityPhase == TransportIdentityPhase::Ready ||
        event.identityPhase == TransportIdentityPhase::Countdown;
    const bool avatarReady = (state.identity.avatarReadyMask & selfBit) != 0 ||
                             seatProvesAvatarReady || rawStageProvesAvatarReady ||
                             stageProvesAvatarReady;
    const bool nameReady = (state.identity.nameReadyMask & selfBit) != 0 ||
                           seatProvesNameReady || rawStageProvesNameReady ||
                           stageProvesNameReady;

    // Completion snapshots are authoritative even when the immediate response
    // was lost. Clear the stale local request before selecting the next page.
    if ((avatarReady &&
         (state.identity.phase == IdentityClientPhase::AvatarEditing ||
          state.identity.phase == IdentityClientPhase::AvatarSubmitting)) ||
        (nameReady && state.identity.phase == IdentityClientPhase::NameSubmitting)) {
        clearPendingKind(state, TransportCommandKind::IdentityRequest);
        state.identity.pendingRequestId = 0;
    }

    if (!avatarReady) {
        state.identity.avatarEditOverride = false;
        state.identity.phase = state.identity.pendingRequestId != 0 ||
                                       event.identityPhase == TransportIdentityPhase::GeneratingAvatar
                                   ? IdentityClientPhase::AvatarSubmitting
                                   : (state.identity.avatarAssetsReady
                                          ? IdentityClientPhase::AvatarEditing
                                          : IdentityClientPhase::AvatarLoading);
        showIdentityPage(state,
                         state.identity.phase == IdentityClientPhase::AvatarLoading
                             ? ScreenPage::AvatarLoading : ScreenPage::AvatarSetup,
                         state.identity.focus < 6 ? state.identity.focus : 0);
        return;
    }

    state.identity.confirmedRecipe = normalizedTransportAvatarRecipe(
        state.identity.seats[selfIndex].recipe);
    if (!state.identity.draftDirty) {
        state.identity.draftRecipe = state.identity.confirmedRecipe;
    }
    if (!nameReady) {
        if (state.identity.avatarEditOverride &&
            (state.nav.current.page == ScreenPage::AvatarLoading ||
             state.nav.current.page == ScreenPage::AvatarSetup)) {
            if (state.nav.current.page == ScreenPage::AvatarSetup) {
                state.identity.phase = IdentityClientPhase::AvatarEditing;
            }
            return;
        }
        state.identity.avatarEditOverride = false;
        if (state.identity.phase != IdentityClientPhase::NameHandwriting) {
            state.identity.phase = state.identity.pendingRequestId != 0
                ? IdentityClientPhase::NameSubmitting : IdentityClientPhase::NameReview;
            showIdentityPage(state, ScreenPage::NameReview,
                             state.identity.focus < 3 ? state.identity.focus : 0);
        }
        return;
    }

    state.identity.phase = event.identityPhase == TransportIdentityPhase::Countdown
        ? IdentityClientPhase::Countdown : IdentityClientPhase::Ready;
    state.identity.countdownDeadlineMs =
        event.identityPhase == TransportIdentityPhase::Countdown ? event.deadlineMs : 0;
    showIdentityPage(state, ScreenPage::PlayerReady, 0);
}

bool reconcileCompletedAvatarSubmission(AppState &state)
{
    if ((state.nav.current.page != ScreenPage::AvatarLoading &&
         state.nav.current.page != ScreenPage::AvatarSetup) ||
        state.identity.avatarEditOverride ||
        state.selfSeatId == 0) {
        return false;
    }

    const uint8_t selfIndex = static_cast<uint8_t>(state.selfSeatId - 1);
    if (selfIndex >= state.identity.seatCount) return false;
    const uint8_t selfBit = static_cast<uint8_t>(1u << selfIndex);
    const TransportIdentitySeat &self = state.identity.seats[selfIndex];
    const bool avatarReady =
        (state.identity.avatarReadyMask & selfBit) != 0 ||
        (self.flags & gridopoly::protocol::IdentitySeatAvatarFinal) != 0;
    if (!avatarReady) return false;

    clearPendingKind(state, TransportCommandKind::IdentityRequest);
    state.identity.pendingRequestId = 0;
    state.identity.editingValue = false;
    state.identity.confirmedRecipe = normalizedTransportAvatarRecipe(self.recipe);
    if (!state.identity.draftDirty) {
        state.identity.draftRecipe = state.identity.confirmedRecipe;
    }

    const bool nameReady =
        (state.identity.nameReadyMask & selfBit) != 0 ||
        (self.flags & gridopoly::protocol::IdentitySeatNameFinal) != 0;
    if (!nameReady) {
        state.identity.phase = IdentityClientPhase::NameReview;
        showIdentityPage(state, ScreenPage::NameReview, 0);
    } else {
        state.identity.phase =
            state.identity.authorityPhase == TransportIdentityPhase::Countdown
                ? IdentityClientPhase::Countdown : IdentityClientPhase::Ready;
        showIdentityPage(state, ScreenPage::PlayerReady, 0);
    }
    return true;
}

bool isDebtMortgageCompletion(const AppState &state, const TransportEvent &event)
{
    const uint8_t mortgageIndex = commandIndex(TransportCommandKind::MortgageBatchRequest);
    return state.modal.kind == ModalKind::DebtMortgageConfirm && state.modal.submitting &&
           event.requestId != 0 && event.requestId == state.debt.submittedMortgageRequestId &&
           event.requestId == state.pendingRequestIds[mortgageIndex] &&
           event.transactionId == state.debt.transactionId &&
           event.assetMask == state.debt.submittedMortgageMask;
}

bool isVoluntaryMortgageCompletion(const AppState &state, const TransportEvent &event)
{
    const uint8_t mortgageIndex = commandIndex(TransportCommandKind::MortgageBatchRequest);
    return state.modal.kind == ModalKind::VoluntaryMortgage && state.modal.submitting &&
           event.requestId != 0 && event.requestId == state.pendingRequestIds[mortgageIndex] &&
           (state.modal.transactionId == 0 || event.transactionId == state.modal.transactionId) &&
           event.assetMask == (static_cast<uint32_t>(1u) << state.selectedAsset);
}

bool isGenericModalCompletion(const AppState &state, const TransportEvent &event)
{
    if (event.kind != TransportEventKind::CommandCompleted ||
        event.requestId == 0 || !state.modal.submitting) {
        return false;
    }
    if (state.modal.kind == ModalKind::CollectRent) {
        return event.requestId ==
            state.pendingRequestIds[commandIndex(TransportCommandKind::ClaimRent)];
    }
    if (state.modal.kind == ModalKind::VoluntaryUnmortgage) {
        return event.requestId ==
            state.pendingRequestIds[commandIndex(TransportCommandKind::UnmortgageRequest)];
    }
    if (state.modal.kind == ModalKind::DebtSellBuildingConfirm) {
        return event.requestId ==
            state.pendingRequestIds[commandIndex(TransportCommandKind::SellBuildingRequest)];
    }
    return false;
}

bool isActiveModalRequest(const AppState &state, uint32_t requestId)
{
    if (requestId == 0 || !state.modal.submitting) return false;
    switch (state.modal.kind) {
        case ModalKind::CollectRent:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::ClaimRent)];
        case ModalKind::TradeCreate:
        case ModalKind::TradeAction:
            return requestId == state.pendingRequestIds[
                commandIndex(tradeCommandKind(state.modal.tradeOperation))];
        case ModalKind::ForcedPayment:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)];
        case ModalKind::VoluntaryMortgage:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
        case ModalKind::VoluntaryUnmortgage:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::UnmortgageRequest)];
        case ModalKind::DebtMortgageConfirm:
            return requestId == state.debt.submittedMortgageRequestId &&
                   requestId == state.pendingRequestIds[
                       commandIndex(TransportCommandKind::MortgageBatchRequest)];
        case ModalKind::DebtSellBuildingConfirm:
            return requestId == state.pendingRequestIds[
                commandIndex(TransportCommandKind::SellBuildingRequest)];
        case ModalKind::None:
            return false;
    }
    return false;
}

bool isForcedPaymentCompletion(const AppState &state, const TransportEvent &event)
{
    return event.kind == TransportEventKind::PaymentCompleted &&
           state.modal.kind == ModalKind::ForcedPayment && state.modal.submitting &&
           event.requestId != 0 &&
           event.requestId ==
               state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)] &&
           (state.modal.transactionId == 0 ||
            event.transactionId == state.modal.transactionId);
}

bool isMatchingBankruptcyResolution(const AppState &state, const TransportEvent &event)
{
    return event.kind == TransportEventKind::BankruptcyResolved && state.debt.bankruptcyPending &&
           state.debt.transactionId != 0 &&
           event.transactionId == state.debt.transactionId;
}

void showModal(AppState &state, ModalKind kind, bool cancelAllowed, const char *title,
               const char *counterparty, const char *purpose, int32_t amount, uint32_t transactionId,
               uint32_t deadlineMs,
               TransportTradeOperation tradeOperation = TransportTradeOperation::Create)
{
    state.modal = ModalState{};
    state.modal.kind = kind;
    state.modal.focus = ModalFocus::Confirm;
    state.modal.cancelAllowed = cancelAllowed;
    state.modal.transactionId = transactionId;
    state.modal.deadlineMs = deadlineMs;
    state.modal.amount = amount;
    state.modal.title = title;
    state.modal.counterparty = counterparty;
    state.modal.purpose = purpose;
    state.modal.tradeOperation = tradeOperation;
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void dismissModal(AppState &state)
{
    state.modal = ModalState{};
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void enterBankruptcy(AppState &state)
{
    dismissModal(state);
    replaceForcedPage(state, ScreenPage::Bankruptcy);
}

void back(AppState &state)
{
    if (state.tradeReceiverPickerOpen) {
        closeTradeReceiverPicker(state);
        return;
    }
    if (state.modal.kind != ModalKind::None) {
        if (state.modal.cancelAllowed && !state.modal.submitting &&
            state.modal.focus == ModalFocus::Cancel) {
            dismissModal(state);
        }
        return;
    }
    if (!appCanNavigateBack(state)) return;
    const ScreenPage leavingPage = state.nav.current.page;
    clearInlineEditing(state);
    state.nav.current = state.nav.stack[--state.nav.depth];
    if (leavingPage == ScreenPage::PlayerDetail) {
        clearPendingKind(state, TransportCommandKind::PlayerDetailRequest);
        state.playerDetail = PlayerDetailState{};
        state.playerAssetListIndex = 0;
        state.playerFinanceListIndex = 0;
    }
    restoreListAnchor(state);
    syncLegacyState(state);
    touchRevision(state);
}

void submitModal(AppState &state, uint32_t nowMs)
{
    if (state.modal.submitting) return;
    switch (state.modal.kind) {
        case ModalKind::CollectRent:
            if (queueCommand(state, TransportCommandKind::ClaimRent, state.modal.transactionId, nowMs)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::ForcedPayment:
            if (state.modal.focus == ModalFocus::ResolveAssets) {
                showModal(state, ModalKind::DebtMortgageConfirm, false, "Resolve assets", "City Bank",
                          "Mortgage assets", state.modal.amount, state.modal.transactionId, 0);
            } else if (queueCommand(state, TransportCommandKind::PayNow, state.modal.transactionId, nowMs)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::VoluntaryMortgage:
            if (queueCommand(state, TransportCommandKind::MortgageBatchRequest, state.modal.transactionId, nowMs,
                             static_cast<uint32_t>(1u << state.selectedAsset))) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::VoluntaryUnmortgage:
            if (queueCommand(state, TransportCommandKind::UnmortgageRequest,
                             state.modal.transactionId, nowMs, 0, 0,
                             state.selectedAsset)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::TradeCreate:
            {
                const bool updating = state.modal.tradeOperation == TransportTradeOperation::Update;
                const uint32_t otherAssets = updating
                    ? state.tradeOffer.counterpartyAssetMask : 0;
                const int32_t otherCash = updating
                    ? state.tradeOffer.counterpartyGivesCash : 0;
                const uint32_t tradeId = updating ? state.tradeOffer.tradeId : 0;
                const uint16_t revision = updating ? state.tradeOffer.revision : 0;
                if (queueTradeCommand(state, state.modal.tradeOperation, nowMs,
                                      static_cast<uint8_t>(state.tradeReceiver + 1),
                                      state.tradeGiveAssetMask, state.tradeAmount,
                                      otherAssets, otherCash, tradeId, revision)) {
                    state.modal.submitting = true;
                } else {
                    showToast(state, state.stateVersion == 0
                                         ? "WAITING FOR GAME SYNC"
                                         : "TRADE REQUEST ALREADY PENDING", nowMs);
                }
            }
            return;
        case ModalKind::TradeAction:
            if (queueTradeCommand(state, state.modal.tradeOperation, nowMs,
                                  state.tradeOffer.counterpartyId, 0, 0, 0, 0,
                                  state.tradeOffer.tradeId,
                                  state.tradeOffer.revision)) {
                state.modal.submitting = true;
            } else {
                showToast(state, state.stateVersion == 0
                                     ? "WAITING FOR GAME SYNC"
                                     : "TRADE REQUEST ALREADY PENDING", nowMs);
            }
            return;
        case ModalKind::DebtMortgageConfirm:
            if (appDebtCanConfirm(state) &&
                queueCommand(state, TransportCommandKind::MortgageBatchRequest, state.debt.transactionId, nowMs,
                             state.debt.selectedMask)) {
                state.modal.submitting = true;
                state.debt.submittedMortgageRequestId =
                    state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
                state.debt.submittedMortgageMask = state.debt.selectedMask;
            }
            return;
        case ModalKind::DebtSellBuildingConfirm:
            if (appDebtBuildingSaleEligible(state, state.selectedAsset) &&
                queueCommand(state, TransportCommandKind::SellBuildingRequest,
                             state.debt.transactionId, nowMs, 0, 0,
                             state.selectedAsset)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::None:
            return;
    }
}

void presentPostArrivalState(AppState &state, uint32_t nowMs);

void activate(AppState &state, uint32_t nowMs)
{
    if (state.modal.kind != ModalKind::None) return;
    if (state.nav.current.page == ScreenPage::Home &&
        state.endTurnPresentation != EndTurnPresentationPhase::None) return;

    const ScreenPage page = state.nav.current.page;
    const uint8_t focus = state.nav.current.focus;
    if (appFocusIsFooter(state)) {
        if (page == ScreenPage::DebtAssets && appDebtCanConfirm(state)) {
            showModal(state, ModalKind::DebtMortgageConfirm, false, "Confirm mortgages", "City Bank",
                      "Mortgage selected assets", appDebtSelectedProceeds(state), state.debt.transactionId, 0);
            return;
        }
        back(state);
        return;
    }

    switch (page) {
        case ScreenPage::Home: {
            const bool ownTurn = state.homePhase == HomePhase::MyTurn ||
                                 state.homePhase == HomePhase::MyTurnEnd;
            if (ownTurn && focus == 0) {
                if (state.homePhase == HomePhase::MyTurnEnd) {
                    if (queueCommand(state, TransportCommandKind::EndTurnRequest, 0, nowMs)) {
                        state.endTurnPresentation = EndTurnPresentationPhase::Exiting;
                        state.endTurnPresentationStartedMs = nowMs;
                        state.endTurnPresentationUntilMs =
                            nowMs + kEndTurnMinimumPresentationMs;
                        state.endTurnAccepted = false;
                        state.nav.current.focus = 0;
                        syncLegacyState(state);
                        touchRevision(state);
                    }
                    return;
                }
                if (queueCommand(state, TransportCommandKind::RollRequest, 0, nowMs)) {
                    clearExtraRollRoomLifecycle(state);
                    clearCardPresentationState(state);
                    state.rollOrigin = state.position;
                    state.rollTarget = 0xFF;
                    state.dieA = 0;
                    state.dieB = 0;
                    state.rolledSteps = 0;
                    state.rollStartedMs = nowMs;
                    state.rollResolvedMs = 0;
                    state.rollRevealMs = 0;
                    state.rollFailedUntilMs = 0;
                    state.arrivalContinueAtMs = 0;
                    state.rollAnimating = true;
                    state.rollResolved = false;
                    state.rollRevealPresented = false;
                    state.rollPresentationComplete = false;
                    state.rollFailed = false;
                    state.moveArrivalPending = false;
                    state.moveArrivalConfirmed = false;
                    state.landingEventAcknowledged = false;
                    replaceForcedPage(state, ScreenPage::DiceStage);
                    showToast(state, "骰子请求已发送", nowMs);
                }
                return;
            }
            const uint8_t menuFocus = ownTurn ? focus - 1 : focus;
            if (menuFocus == 0) pushPage(state, ScreenPage::Assets);
            else if (menuFocus == 1) pushPage(state, ScreenPage::Players);
            else if (menuFocus == 2) {
                if (!kTradeBackendAvailable) {
                    showToast(state, "TRADE REQUIRES WI-FI SERVER", nowMs);
                    return;
                }
                state.tradeEntryMode = TradeEntryMode::HomeEditable;
                if (state.playerCount > 1 &&
                    (state.tradeReceiver >= state.playerCount ||
                     state.tradeReceiver == static_cast<uint8_t>(state.selfSeatId - 1))) {
                    state.tradeReceiver = static_cast<uint8_t>(state.selfSeatId % state.playerCount);
                }
                state.tradeGiveAssetMask = 0;
                state.tradeAmount = 0;
                state.tradeAssetListIndex = 0;
                pushPage(state, ScreenPage::Trade);
            }
            return;
        }
        case ScreenPage::Assets:
            state.selectedAsset = appVisibleAssetIndex(state, focus);
            pushPage(state, ScreenPage::AssetDetail);
            return;
        case ScreenPage::AssetDetail:
            {
                const AssetDetailAction action = appAssetDetailActionAt(state, focus);
                if (!appAssetDetailActionEnabled(state, action)) {
                    showToast(state, "ACTION NOT AVAILABLE", nowMs);
                    return;
                }
                if (action == AssetDetailAction::Build ||
                    action == AssetDetailAction::SellBuilding) {
                    const TransportCommandKind kind = action == AssetDetailAction::Build
                        ? TransportCommandKind::BuildRequest
                        : TransportCommandKind::SellBuildingRequest;
                    if (queueCommand(state, kind, 0, nowMs, 0, 0, state.selectedAsset)) {
                        showToast(state, action == AssetDetailAction::Build
                                             ? "BUILDING..." : "SELLING BUILDING...", nowMs);
                    }
                    return;
                }
                if (action == AssetDetailAction::Trade) {
                    if (!kTradeBackendAvailable) {
                        showToast(state, "TRADE REQUIRES WI-FI SERVER", nowMs);
                        return;
                    }
                    state.tradeEntryMode = TradeEntryMode::HomeEditable;
                    if (state.playerCount > 1 &&
                        (state.tradeReceiver >= state.playerCount ||
                         state.tradeReceiver == static_cast<uint8_t>(state.selfSeatId - 1))) {
                        state.tradeReceiver =
                            static_cast<uint8_t>(state.selfSeatId % state.playerCount);
                    }
                    state.tradeGiveAssetMask = static_cast<uint32_t>(1u) << state.selectedAsset;
                    state.tradeAmount = 0;
                    state.tradeAssetListIndex = 0;
                    pushPage(state, ScreenPage::Trade);
                    return;
                }
                if (appAssetMortgaged(state, state.selectedAsset)) {
                    showModal(state, ModalKind::VoluntaryUnmortgage, true,
                              "CONFIRM REDEEM", "CITY BANK",
                              appAssetDisplayName(state, state.selectedAsset),
                              unmortgageCost(state, state.selectedAsset), 0, 0);
                    return;
                }
                showModal(state, ModalKind::VoluntaryMortgage, true,
                          "CONFIRM MORTGAGE", "CITY BANK",
                          appAssetDisplayName(state, state.selectedAsset),
                          appAssetMortgageValue(state, state.selectedAsset), 0, 0);
            }
            return;
        case ScreenPage::Players:
            state.selectedPlayer = focus;
            if (pushPage(state, ScreenPage::PlayerDetail)) requestPlayerDetail(state, nowMs);
            return;
        case ScreenPage::PlayerDetail:
            if (state.playerDetail.loadState != PlayerDetailLoadState::Ready) {
                if (state.playerDetail.loadState != PlayerDetailLoadState::Loading) {
                    requestPlayerDetail(state, nowMs);
                }
                return;
            }
            if (focus == 0) {
                pushPage(state, ScreenPage::PlayerAssets);
            } else if (focus == 1) {
                pushPage(state, ScreenPage::PlayerFinance);
            } else if (focus == 2) {
                if (!kTradeBackendAvailable) {
                    showToast(state, "TRADE REQUIRES WI-FI SERVER", nowMs);
                    return;
                }
                if (state.selectedPlayer == static_cast<uint8_t>(state.selfSeatId - 1)) {
                    showToast(state, "CHOOSE ANOTHER PLAYER", nowMs);
                    return;
                }
                state.tradeReceiver = state.selectedPlayer;
                state.tradeEntryMode = TradeEntryMode::PlayerLocked;
                state.tradeGiveAssetMask = 0;
                state.tradeAmount = 0;
                state.tradeAssetListIndex = 0;
                pushPage(state, ScreenPage::Trade);
            } else {
                state.nav.current.focus = 0;
                syncLegacyState(state);
                requestPlayerDetail(state, nowMs);
            }
            return;
        case ScreenPage::PlayerAssets:
        case ScreenPage::PlayerFinance:
        case ScreenPage::Activity:
            return;
        case ScreenPage::Trade:
            {
                if (!appTradeReceiverLocked(state) && focus == 0) {
                    openTradeReceiverPicker(state);
                    return;
                }
                const InlineEditField field = focusedTradeField(state);
                if (field != InlineEditField::None) {
                    state.inlineEditField = state.inlineEditField == field
                                                ? InlineEditField::None
                                                : field;
                    touchRevision(state);
                } else if (focus == tradeAssetFocus(state)) {
                    pushPage(state, ScreenPage::TradeAssetSelect);
                } else if (focus == tradeSubmitFocus(state)) {
                    if (state.tradeGiveAssetMask == 0 && state.tradeAmount == 0) {
                        showToast(state, "ADD ASSETS OR CASH", nowMs);
                        return;
                    }
                    showModal(state, ModalKind::TradeCreate, true, "SEND TRADE",
                              appPlayerDisplayName(state, state.tradeReceiver),
                              state.tradeEntryMode == TradeEntryMode::CounterLocked
                                  ? "COUNTER OFFER" : "ASSETS AND CASH",
                              state.tradeAmount, 0, 0,
                              state.tradeEntryMode == TradeEntryMode::CounterLocked
                                  ? TransportTradeOperation::Update
                                  : TransportTradeOperation::Create);
                }
            }
            return;
        case ScreenPage::TradeAssetSelect:
            {
                const uint8_t assetIndex = appTradeAssetIndex(state, focus);
                if (!appTradeAssetEligible(state, assetIndex)) {
                    showToast(state, "SELL BUILDINGS OR REDEEM FIRST", nowMs);
                    return;
                }
                state.tradeGiveAssetMask ^= static_cast<uint32_t>(1u) << assetIndex;
                touchRevision(state);
            }
            return;
        case ScreenPage::TradeOffer:
            if (!state.tradeOffer.active) {
                back(state);
                return;
            }
            if ((state.tradeOffer.flags & kTradeFlagSelfLastEdited) != 0) {
                if (focus == 0) {
                    showModal(state, ModalKind::TradeAction, true, "CANCEL OFFER",
                              appPlayerDisplayName(
                                  state, static_cast<uint8_t>(state.tradeOffer.counterpartyId - 1)),
                              "WITHDRAW CURRENT OFFER", 0, state.tradeOffer.tradeId, 0,
                              TransportTradeOperation::Cancel);
                }
                return;
            }
            if ((state.tradeOffer.flags & kTradeFlagSelfConfirmed) != 0) return;
            if (focus == 0) {
                showModal(state, ModalKind::TradeAction, true, "ACCEPT TRADE",
                          appPlayerDisplayName(
                              state, static_cast<uint8_t>(state.tradeOffer.counterpartyId - 1)),
                          "RECEIVE OFFER", state.tradeOffer.selfGivesCash,
                          state.tradeOffer.tradeId, 0,
                          TransportTradeOperation::Confirm);
            } else if (focus == 1) {
                state.tradeEntryMode = TradeEntryMode::CounterLocked;
                state.tradeReceiver = static_cast<uint8_t>(state.tradeOffer.counterpartyId - 1);
                state.tradeGiveAssetMask = state.tradeOffer.selfAssetMask;
                state.tradeAmount = state.tradeOffer.selfGivesCash;
                state.tradeAssetListIndex = 0;
                pushPage(state, ScreenPage::Trade);
            } else if (focus == 2) {
                showModal(state, ModalKind::TradeAction, true, "REJECT TRADE",
                          appPlayerDisplayName(
                              state, static_cast<uint8_t>(state.tradeOffer.counterpartyId - 1)),
                          "DECLINE CURRENT OFFER", 0, state.tradeOffer.tradeId, 0,
                          TransportTradeOperation::Reject);
            }
            return;
        case ScreenPage::DemoLab:
            if (focus == 0) { state.homePhase = HomePhase::Waiting; state.turnsUntilYou = 3; resetHome(state, 3); }
            else if (focus == 1) { state.homePhase = HomePhase::NextPlayer; state.turnsUntilYou = 1; resetHome(state, 3); }
            else if (focus == 2) { state.homePhase = HomePhase::MyTurn; state.turnsUntilYou = 0; resetHome(state, 4); }
            else if (focus == 3) showModal(state, ModalKind::CollectRent, true, "等待收租", "砾川",
                                            "霓虹港湾地租", 120, 0, nowMs + 20000);
            else if (focus == 4) showModal(state, ModalKind::ForcedPayment, false, "即将付款", "岑蓝",
                                            "天穹广场地租", 175, 0, nowMs + 10000);
            else replaceForcedPage(state, ScreenPage::DebtAssets);
            return;
        case ScreenPage::Debt:
            pushPage(state, ScreenPage::Assets);
            return;
        case ScreenPage::DebtAssets:
            {
                const uint8_t assetIndex = appVisibleAssetIndex(state, focus);
                state.selectedAsset = assetIndex;
                if (appDebtBuildingSaleEligible(state, assetIndex)) {
                    showModal(state, ModalKind::DebtSellBuildingConfirm, false,
                              "SELL BUILDING", "CITY BANK",
                              "LIQUIDATE ONE BUILDING",
                              appDebtBuildingSaleProceeds(state, assetIndex),
                              state.debt.transactionId, 0);
                } else if (appAssetBuildingLevel(state, assetIndex) != 0) {
                    showToast(state, "SELL EVENLY ACROSS GROUP", nowMs);
                } else if (appDebtAssetEligible(state, assetIndex)) {
                    state.debt.selectedMask ^= static_cast<uint32_t>(1u) << assetIndex;
                    touchRevision(state);
                } else {
                    showToast(state, "ASSET NOT AVAILABLE", nowMs);
                }
            }
            return;
        case ScreenPage::DiceStage:
            return;
        case ScreenPage::ExtraRollReward:
            finishExtraRollReward(state);
            return;
        case ScreenPage::MoveGuide:
            if (state.moveArrivalConfirmed) {
                state.arrivalContinueAtMs = nowMs;
                return;
            }
            if ((state.availableActions & kActionConfirmPosition) != 0 &&
                queueCommand(state, TransportCommandKind::MoveManualConfirmRequest,
                             0, nowMs, 0, state.rollTarget)) {
                showToast(state, "CHECKING POSITION", nowMs);
            }
            return;
        case ScreenPage::TileEvent:
            state.landingEventAcknowledged = true;
            presentPostArrivalState(state, nowMs);
            return;
        case ScreenPage::CardReveal:
            if (state.cardPresentation == CardPresentationPhase::Revealed &&
                state.cardResultValid && state.cardInstanceId != 0 &&
                (state.availableActions & kActionCardContinue) != 0) {
                if (queueCommand(state, TransportCommandKind::CardContinueRequest,
                                 state.cardInstanceId, nowMs, 0, 0, 0xFF,
                                 static_cast<int32_t>(state.cardInstanceId))) {
                    state.cardPresentationAcknowledged = true;
                    state.cardPresentation = CardPresentationPhase::Settling;
                    state.pendingCardFlags = 0x0Fu;
                    syncLegacyState(state);
                    touchRevision(state);
                }
            }
            return;
        case ScreenPage::Purchase:
            if (focus == 0 && (state.availableActions & kActionBuy) != 0) {
                if (queueCommand(state, TransportCommandKind::BuyRequest, 0, nowMs)) {
                    showToast(state, "PURCHASING...", nowMs);
                } else {
                    showToast(state, state.authorityOnline ? "REQUEST ALREADY PENDING"
                                                           : "RECONNECTING...", nowMs);
                }
            } else if (focus == 1 && (state.availableActions & kActionDecline) != 0) {
                if (queueCommand(state, TransportCommandKind::DeclinePurchaseRequest, 0, nowMs)) {
                    showToast(state, "STARTING AUCTION", nowMs);
                } else {
                    showToast(state, state.authorityOnline ? "REQUEST ALREADY PENDING"
                                                           : "RECONNECTING...", nowMs);
                }
            } else {
                showToast(state, "ACTION NOT AVAILABLE", nowMs);
            }
            return;
        case ScreenPage::Auction:
            if (state.auctionPresentation != AuctionPresentationPhase::Live) return;
            if (focus == 0 && (state.availableActions & kActionAuctionBid) != 0) {
                if (queueCommand(state, TransportCommandKind::AuctionBidRequest, 0, nowMs,
                                 0, 0, state.auctionAssetIndex,
                                 state.auctionMinimumBid)) {
                    showToast(state, "BID SENT", nowMs);
                }
            } else if (focus == 1 && (state.availableActions & kActionAuctionPass) != 0) {
                if (queueCommand(state, TransportCommandKind::AuctionPassRequest, 0, nowMs,
                                 0, 0, state.auctionAssetIndex)) {
                    state.auctionPassed = true;
                    showToast(state, "PASSING BID", nowMs);
                }
            }
            return;
        case ScreenPage::Bankruptcy:
            if ((state.availableActions & kActionDeclareBankruptcy) != 0) {
                if (queueCommand(state, TransportCommandKind::DeclareBankruptcyRequest,
                                 0, nowMs)) {
                    state.debt.bankruptcyPending = true;
                    showToast(state, "DECLARING BANKRUPTCY", nowMs);
                }
            }
            return;
        case ScreenPage::AvatarSetup:
            if (state.identity.phase == IdentityClientPhase::AvatarSubmitting) return;
            if (focus < static_cast<uint8_t>(AvatarEditField::Confirm)) {
                state.identity.editingValue = !state.identity.editingValue;
                touchRevision(state);
                return;
            }
            state.identity.editingValue = false;
            if (queueIdentityRequest(state, TransportIdentityOperation::ConfirmAvatar,
                                     nowMs)) {
                state.identity.avatarEditOverride = false;
                state.identity.phase = IdentityClientPhase::AvatarSubmitting;
                touchRevision(state);
            } else {
                showToast(state, state.authorityOnline ? "WAITING FOR SETUP SYNC"
                                                       : "RECONNECTING...", nowMs);
            }
            return;
        case ScreenPage::NameReview:
            if (state.identity.phase == IdentityClientPhase::NameSubmitting) return;
            if (focus == 0) {
                state.identity.phase = IdentityClientPhase::NameHandwriting;
                state.identity.focus = 0;
                showIdentityPage(state, ScreenPage::NameHandwriting, 0);
                touchRevision(state);
            } else if (focus == 1) {
                if (state.identity.draftName[0] == '\0') {
                    showToast(state, "WRITE A NAME FIRST", nowMs);
                    return;
                }
                if (queueIdentityRequest(state, TransportIdentityOperation::ConfirmName,
                                         nowMs)) {
                    state.identity.phase = IdentityClientPhase::NameSubmitting;
                    touchRevision(state);
                } else {
                    showToast(state, state.authorityOnline ? "WAITING FOR SETUP SYNC"
                                                           : "RECONNECTING...", nowMs);
                }
            } else {
                state.identity.avatarEditOverride = true;
                state.identity.focus = 0;
                if (state.identity.avatarAssetsReady) {
                    state.identity.phase = IdentityClientPhase::AvatarEditing;
                    showIdentityPage(state, ScreenPage::AvatarSetup, 0);
                } else {
                    state.identity.phase = IdentityClientPhase::AvatarLoading;
                    showIdentityPage(state, ScreenPage::AvatarLoading, 0);
                }
                touchRevision(state);
            }
            return;
        case ScreenPage::NameHandwriting:
            if (focus == 0) {
                const size_t length = strlen(state.identity.draftName);
                if (length != 0) {
                    state.identity.draftName[length - 1] = '\0';
                    state.identity.nameDirty = true;
                    touchRevision(state);
                }
            } else {
                state.identity.phase = IdentityClientPhase::NameReview;
                state.identity.focus = 0;
                showIdentityPage(state, ScreenPage::NameReview, 0);
                touchRevision(state);
            }
            return;
        case ScreenPage::PlayerReady:
            return;
    }
}

void beginPress(AppState &state, uint32_t nowMs)
{
    state.buttonHeld = true;
    state.buttonDownMs = nowMs;
    state.holdActionConsumed = false;
    if (state.modal.kind != ModalKind::None && !state.modal.submitting &&
        (state.modal.focus == ModalFocus::Confirm ||
         state.modal.focus == ModalFocus::ResolveAssets)) {
        state.modal.holding = true;
        state.modal.holdStartMs = nowMs;
    }
}

void endPress(AppState &state, uint32_t nowMs)
{
    if (!state.buttonHeld) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    bool consumed = state.holdActionConsumed;
    state.buttonHeld = false;
    if (state.modal.kind != ModalKind::None) {
        const bool canSubmit = state.modal.holding && !state.modal.submitting &&
            (state.modal.focus == ModalFocus::Confirm ||
             state.modal.focus == ModalFocus::ResolveAssets);
        if (!consumed && canSubmit && heldMs >= kConfirmHoldMs) {
            state.holdActionConsumed = true;
            consumed = true;
            submitModal(state, nowMs);
        }
        state.modal.holding = false;
        if (!consumed && heldMs <= kShortPressMaxMs && state.modal.cancelAllowed &&
            state.modal.focus == ModalFocus::Cancel) {
            dismissModal(state);
        }
        return;
    }
    if (consumed) return;
    if (heldMs <= kShortPressMaxMs) activate(state, nowMs);
    else if (heldMs >= kBackPressMs && heldMs < kHomePressMs && isOrdinaryPage(state.nav.current.page)) back(state);
}

void moveFocus(AppState &state, int16_t delta)
{
    if (state.tradeReceiverPickerOpen) {
        const uint8_t count = tradeReceiverCandidateCount(state);
        if (count == 0 || delta == 0) return;
        const uint8_t focusCount = static_cast<uint8_t>(count + 1);
        int32_t next = static_cast<int32_t>(state.tradeReceiverPickerIndex) + delta;
        while (next < 0) next += focusCount;
        next %= focusCount;
        state.tradeReceiverPickerIndex = static_cast<uint8_t>(next);
        touchRevision(state);
        return;
    }
    if (state.modal.kind != ModalKind::None) {
        if (delta != 0 && state.modal.cancelAllowed && !state.modal.submitting) {
            state.modal.focus = state.modal.focus == ModalFocus::Confirm ? ModalFocus::Cancel : ModalFocus::Confirm;
            touchRevision(state);
        }
        return;
    }
    if (state.nav.current.page == ScreenPage::AvatarSetup &&
        state.identity.editingValue) {
        applyIdentityDraftDelta(state, delta);
        return;
    }
    if (state.nav.current.page == ScreenPage::Trade &&
        state.inlineEditField != InlineEditField::None) {
        if (delta == 0) return;
        if (state.inlineEditField == InlineEditField::TradeReceiver) {
            if (appTradeReceiverLocked(state)) return;
            const int32_t direction = delta < 0 ? -1 : 1;
            int32_t receiver = state.tradeReceiver;
            for (int16_t step = 0; step < std::abs(delta); ++step) {
                do {
                    receiver += direction;
                    receiver %= state.playerCount;
                    if (receiver < 0) receiver += state.playerCount;
                } while (state.playerCount > 1 &&
                         receiver == static_cast<int32_t>(state.selfSeatId - 1));
            }
            state.tradeReceiver = static_cast<uint8_t>(receiver);
        } else if (state.inlineEditField == InlineEditField::TradeAmount) {
            int64_t amount = static_cast<int64_t>(state.tradeAmount) +
                             static_cast<int64_t>(delta) * 50;
            if (amount < 0) amount = 0;
            if (amount > state.money) amount = state.money;
            state.tradeAmount = static_cast<int32_t>(amount);
        }
        touchRevision(state);
        return;
    }
    const uint8_t count = appFocusCount(state);
    if (count == 0 || delta == 0 || !isInteractiveListPage(state.nav.current.page)) return;

    if (state.nav.current.page == ScreenPage::AssetDetail) {
        const int16_t direction = delta < 0 ? -1 : 1;
        int16_t next = state.nav.current.focus;
        for (int16_t step = 0; step < std::abs(delta); ++step) {
            do {
                next += direction;
            } while (next >= 0 && next < 4 &&
                     !appAssetDetailActionVisible(
                         state, appAssetDetailActionAt(state, static_cast<uint8_t>(next))));
            if (next < 0 || next >= count) {
                pulseListBoundary(state, delta);
                return;
            }
        }
        setFocus(state, static_cast<uint8_t>(next));
        return;
    }

    const int32_t requested = static_cast<int32_t>(state.nav.current.focus) + delta;
    int32_t next = requested;
    if (state.nav.current.page == ScreenPage::Home) {
        while (next < 0) next += count;
        next %= count;
    } else {
        if (next < 0) next = 0;
        if (next >= count) next = count - 1;
        if (next == state.nav.current.focus && next != requested) {
            pulseListBoundary(state, delta);
            return;
        }
    }
    setFocus(state, static_cast<uint8_t>(next));
}

void selectListItem(AppState &state, int16_t item, uint32_t nowMs)
{
    if (!isInteractiveListPage(state.nav.current.page) || state.nav.current.page == ScreenPage::Home) return;
    const uint8_t content = appPageContentCount(state);
    if (item < 0 || item >= content) return;
    if (state.nav.current.page == ScreenPage::Trade &&
        state.inlineEditField != InlineEditField::None &&
        state.nav.current.focus != static_cast<uint8_t>(item)) {
        clearInlineEditing(state);
        setFocus(state, static_cast<uint8_t>(item));
        return;
    }
    if (state.nav.current.focus == static_cast<uint8_t>(item)) activate(state, nowMs);
    else setFocus(state, static_cast<uint8_t>(item));
}

void selectFooter(AppState &state, uint32_t nowMs)
{
    if (state.nav.current.page == ScreenPage::Home ||
        !isInteractiveListPage(state.nav.current.page)) return;
    const uint8_t footerFocus = contentCount(state, state.nav.current.page);
    if (state.nav.current.focus != footerFocus) {
        clearInlineEditing(state);
        setFocus(state, footerFocus);
        return;
    }
    activate(state, nowMs);
}

uint8_t turnsBeforeSelf(const TransportEvent &event)
{
    if (event.selfSeatId == 0 || event.activePlayerId == 0 ||
        event.playerCount == 0 || event.activePlayerId == event.selfSeatId) return 0;
    uint8_t turns = 0;
    uint8_t seat = event.activePlayerId;
    for (uint8_t checked = 0; checked < event.playerCount; ++checked) {
        if (seat == event.selfSeatId) break;
        bool bankrupt = false;
        for (uint8_t index = 0; index < event.playerCount; ++index) {
            if (event.players[index].playerId == seat) {
                bankrupt = (event.players[index].flags & 0x02u) != 0;
                break;
            }
        }
        if (!bankrupt) ++turns;
        seat = static_cast<uint8_t>(seat % event.playerCount + 1);
    }
    return turns;
}

void presentDebtAuthorityState(AppState &state, uint32_t nowMs)
{
    state.rollAnimating = false;
    state.debt.amountDue = state.debtAmount;
    state.debt.cashBefore = state.money;
    const uint8_t assetCount = state.fullAuthoritySnapshotValid
        ? state.authorityAssetCount : kAssetCount;
    state.debt.eligibleMask = (state.availableActions & kActionMortgage) != 0
        ? (assetCount >= 32 ? 0xFFFFFFFFu : ((1u << assetCount) - 1u)) : 0;
    state.debt.selectedMask &= legalDebtAssetMask(state);
    if ((state.availableActions & kActionPayDebt) != 0) {
        if (state.modal.kind == ModalKind::None) {
            const bool holdRelease = releaseHoldDebt(state);
            const char *title = holdRelease ? "RELEASE FEE" : "PAYMENT REQUIRED";
            const char *counterparty = state.debtCreditorId == 0
                ? "CITY BANK" : appPlayerNameById(state, state.debtCreditorId);
            const char *purpose = "SETTLE DEBT";
            if (holdRelease) purpose = "RELEASE FROM HOLD";
            else if (state.debtPaymentEvent == kDebtPaymentRent) purpose = "PAY RENT";
            else if (state.debtPaymentEvent == kDebtPaymentFee) purpose = "CITY FEE";
            else if (state.debtPaymentEvent == kDebtPaymentCard) purpose = "CARD PAYMENT";
            showModal(state, ModalKind::ForcedPayment, false, title, counterparty,
                      purpose, state.debtAmount, state.stateVersion, 0);
        }
        return;
    }
    if (state.modal.kind == ModalKind::DebtSellBuildingConfirm &&
        state.modal.submitting) return;
    if ((state.availableActions & kActionDeclareBankruptcy) != 0) {
        if (state.nav.current.page != ScreenPage::Bankruptcy) {
            replaceForcedPage(state, ScreenPage::Bankruptcy);
        }
    } else if (state.nav.current.page != ScreenPage::DebtAssets) {
        replaceForcedPage(state, ScreenPage::DebtAssets);
    }
}

void clearRollFlow(AppState &state)
{
    state.rollAnimating = false;
    state.rollResolved = false;
    state.rollRevealPresented = false;
    state.rollPresentationComplete = false;
    state.rollFailed = false;
    state.rollRevealMs = 0;
    state.rollFailedUntilMs = 0;
    state.arrivalContinueAtMs = 0;
    state.moveArrivalPending = false;
    state.moveArrivalConfirmed = false;
    clearCardPresentationState(state);
}

void setCardResult(AppState &state, uint8_t deckId, uint8_t cardIndex,
                   uint8_t flags, uint16_t instanceId, uint16_t catalogId,
                   uint16_t effectId, int32_t amount, uint8_t targetPlayerId,
                   uint8_t targetPosition, uint32_t eventSequence)
{
    state.cardChance = deckId == 1;
    state.cardIndex = cardIndex;
    state.cardFlags = flags;
    state.cardInstanceId = instanceId;
    state.cardCatalogId = catalogId;
    state.cardEffectId = effectId;
    state.cardAmount = amount;
    state.cardTargetPlayerId = targetPlayerId;
    state.cardTargetPosition = targetPosition;
    state.cardEventSequence = eventSequence;
    state.cardResultValid = instanceId != 0 && deckId >= 1 && deckId <= 2 && cardIndex < 8;
}

void beginCardPresentation(AppState &state, uint32_t nowMs, bool restoreWithoutAnimation)
{
    if (!state.cardResultValid) return;
    state.cardPresentationAcknowledged = false;
    state.cardEffectApplied = false;
    state.cardStartedMs = nowMs;
    if (restoreWithoutAnimation) {
        state.cardPresentation = CardPresentationPhase::Revealed;
        state.cardRevealAtMs = 0;
    } else {
        state.cardPresentation = CardPresentationPhase::Drawing;
        state.cardRevealAtMs = nowMs + kCardDrawMinMs;
    }
    replaceForcedPage(state, ScreenPage::CardReveal);
}

void applyPendingCardProjection(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    const bool ownPendingCard = (event.pendingCardFlags & 0x01u) != 0 &&
        event.pendingCardPlayerId == state.selfSeatId;
    if (!ownPendingCard) {
        state.pendingCardFlags = event.pendingCardFlags;
        return;
    }

    const bool completedCard = cardDrawKeyMatches(
        event.pendingCardInstanceId, event.pendingCardDrawEventSequence,
        state.completedCardInstanceId, state.completedCardDrawEventSequence);
    if (completedCard) return;
    state.pendingCardFlags = event.pendingCardFlags;

    const bool seenCard = cardDrawKeyMatches(
        event.pendingCardInstanceId, event.pendingCardDrawEventSequence,
        state.seenCardInstanceId, state.seenCardDrawEventSequence);
    const bool sameInstance = state.cardResultValid &&
        state.cardInstanceId == event.pendingCardInstanceId;
    setCardResult(state, event.pendingCardDeckId, event.pendingCardIndex,
                  event.resync ? gridopoly::protocol::PlayerCardFlagReplay : 0,
                  event.pendingCardInstanceId, event.pendingCardCatalogId,
                  event.pendingCardEffectId, event.pendingCardDisplayAmount,
                   event.pendingCardTargetPlayerId, event.pendingCardTargetPosition,
                   event.pendingCardDrawEventSequence);
    if (!seenCard) {
        state.seenCardInstanceId = event.pendingCardInstanceId;
        state.seenCardDrawEventSequence = event.pendingCardDrawEventSequence;
    }

    const bool settlement = (event.pendingCardFlags & 0x0Fu) == 0x0Fu;
    if (settlement) {
        state.cardPresentationAcknowledged = true;
        state.cardPresentation = CardPresentationPhase::Settling;
        state.cardRevealAtMs = 0;
        if (event.resync || state.nav.current.page == ScreenPage::CardReveal) {
            replaceForcedPage(state, ScreenPage::CardReveal);
        }
        return;
    }

    const bool arrivalCheckpointVisible = state.nav.current.page == ScreenPage::DiceStage ||
                                          state.nav.current.page == ScreenPage::MoveGuide;
    if (event.resync) {
        beginCardPresentation(state, nowMs, true);
    } else if (!seenCard && !sameInstance && !arrivalCheckpointVisible &&
               state.authorityPhase == AuthorityPhase::AwaitCard) {
        beginCardPresentation(state, nowMs, false);
    }
}

void applyPlayerCardPresentation(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    if (event.cardInstanceId == 0) return;
    if (event.kind == TransportEventKind::PlayerCardEffectApplied &&
        event.cardPlayerId != state.selfSeatId) return;
    const bool sameInstance = state.cardResultValid &&
                              state.cardInstanceId == event.cardInstanceId;
    if (event.kind == TransportEventKind::PlayerCardEffectApplied) {
        if (event.cardInstanceId == state.completedCardInstanceId) return;
        const bool knownInstance = sameInstance ||
            event.cardInstanceId == state.seenCardInstanceId;
        if (!knownInstance) return;
    } else {
        const bool completedCard = cardDrawKeyMatches(
            event.cardInstanceId, event.cardEventSequence,
            state.completedCardInstanceId, state.completedCardDrawEventSequence);
        const bool duplicateDraw = cardDrawKeyMatches(
            event.cardInstanceId, event.cardEventSequence,
            state.seenCardInstanceId, state.seenCardDrawEventSequence);
        if (completedCard || duplicateDraw) return;
    }
    if (sameInstance && event.cardEventSequence < state.cardEventSequence) return;
    setCardResult(state, event.cardDeckId, event.cardIndex, event.cardFlags,
                  event.cardInstanceId, event.cardCatalogId, event.cardEffectId,
                  event.cardAmount, event.cardTargetPlayerId,
                  event.cardTargetPosition, event.cardEventSequence);

    if (event.kind == TransportEventKind::PlayerCardEffectApplied) {
        state.completedCardInstanceId = event.cardInstanceId;
        state.completedCardDrawEventSequence = state.seenCardDrawEventSequence;
        state.cardOutcome = event.cardOutcome;
        state.cardEffectApplied = true;
        state.pendingCardFlags = 0;
        clearPendingKind(state, TransportCommandKind::CardContinueRequest);
        if (state.nav.current.page == ScreenPage::CardReveal) {
            state.cardPresentation = CardPresentationPhase::Settling;
        }
        touchRevision(state);
        return;
    }

    state.seenCardInstanceId = event.cardInstanceId;
    state.seenCardDrawEventSequence = event.cardEventSequence;
    state.pendingCardFlags = 0x03u;
    state.cardOutcome = 0;
    if (event.resync) {
        // Authority v3 follows this private replay and decides Revealed versus Settling.
        touchRevision(state);
        return;
    }
    const bool replay = event.resync ||
                        (event.cardFlags & gridopoly::protocol::PlayerCardFlagReplay) != 0;
    if (replay) {
        if (state.cardPresentation != CardPresentationPhase::Settling) {
            beginCardPresentation(state, nowMs, true);
        }
    } else if (state.cardPresentation == CardPresentationPhase::Drawing) {
        const uint32_t minimumReveal = state.cardStartedMs + kCardDrawMinMs;
        state.cardRevealAtMs = hasReachedDeadline(nowMs, minimumReveal)
            ? nowMs + kCardResultSettleMs : minimumReveal;
    } else if (state.nav.current.page != ScreenPage::DiceStage &&
               state.nav.current.page != ScreenPage::MoveGuide) {
        beginCardPresentation(state, nowMs, false);
    }
    touchRevision(state);
}

bool landingPaymentEventNeeded(const AppState &state)
{
    if (state.landingEventAcknowledged || !state.moveArrivalConfirmed ||
        state.decisionPlayerId != state.selfSeatId ||
        state.authorityPhase != AuthorityPhase::AwaitDebt || releaseHoldDebt(state) ||
        state.debtPaymentEvent == kDebtPaymentCard) {
        return false;
    }
    if (state.debtPaymentEvent == kDebtPaymentRent ||
        state.debtPaymentEvent == kDebtPaymentFee) {
        return true;
    }
    const ArrivalKind arrival = appArrivalKind(state);
    if (arrival == ArrivalKind::Fee) return true;
    if (arrival != ArrivalKind::Asset) return false;
    const uint8_t ownerId = appArrivalOwnerId(state);
    return ownerId != 0 && ownerId != state.selfSeatId;
}

void presentPostArrivalState(AppState &state, uint32_t nowMs)
{
    const ArrivalKind arrival = appArrivalKind(state);
    const bool cardArrival = arrival == ArrivalKind::CityEvent ||
                             arrival == ArrivalKind::CivicFund;
    if (cardArrival && !state.cardPresentationAcknowledged) {
        if (state.cardPresentation == CardPresentationPhase::None) {
            state.cardPresentation = CardPresentationPhase::Drawing;
            state.cardStartedMs = nowMs;
            state.cardRevealAtMs = state.cardResultValid ? nowMs + kCardDrawMinMs : 0;
            if (!state.cardResultValid) state.cardChance = arrival == ArrivalKind::CityEvent;
        }
        replaceForcedPage(state, ScreenPage::CardReveal);
        return;
    }
    const bool ownTurn = state.activePlayerId == state.selfSeatId;
    const bool ownDecision = state.decisionPlayerId == state.selfSeatId;
    if (landingPaymentEventNeeded(state)) {
        replaceForcedPage(state, ScreenPage::TileEvent);
        return;
    }
    clearRollFlow(state);
    if (ownTurn && state.authorityPhase == AuthorityPhase::AwaitPurchase) {
        replaceForcedPage(state, ScreenPage::Purchase);
    } else if (state.authorityPhase == AuthorityPhase::AwaitAuction) {
        replaceForcedPage(state, ScreenPage::Auction);
    } else if (ownDecision && state.authorityPhase == AuthorityPhase::AwaitDebt) {
        presentDebtAuthorityState(state, nowMs);
    } else if (ownTurn && state.authorityPhase == AuthorityPhase::AwaitMoveConfirm) {
        state.moveArrivalPending = true;
        state.moveArrivalConfirmed = false;
        replaceForcedPage(state, ScreenPage::MoveGuide);
    } else {
        resetHome(state, 0);
    }
}

void finishDicePresentation(AppState &state, uint32_t nowMs)
{
    state.rollAnimating = false;
    if (releaseHoldDebt(state)) {
        state.rollPresentationComplete = true;
        presentDebtAuthorityState(state, nowMs);
        return;
    }
    if (state.moveArrivalPending && state.rollTarget != 0xFF) {
        replaceForcedPage(state, ScreenPage::MoveGuide);
        if (state.moveArrivalConfirmed) {
            state.arrivalContinueAtMs = nowMs + kArrivalConfirmedHoldMs;
        }
    } else {
        presentPostArrivalState(state, nowMs);
    }
}

void beginAuctionIntro(AppState &state, uint32_t nowMs)
{
    state.auctionPresentation = AuctionPresentationPhase::Intro;
    state.auctionPresentationUntilMs = nowMs + kAuctionIntroMs;
    state.auctionResultAssetIndex = 0xFF;
    state.auctionWinnerPlayerId = 0;
    state.auctionResultAmount = 0;
    state.auctionPassed = false;
    state.auctionReadyAttemptGeneration = 0;
    state.auctionReadyAttemptAssetIndex = 0xFF;
}

void beginAuctionResult(AppState &state, uint8_t assetIndex, uint8_t winnerPlayerId,
                        int32_t amount, uint32_t generation, uint32_t nowMs)
{
    if (generation != 0 && assetIndex != 0xFF) {
        state.completedAuctionGeneration = generation;
        state.completedAuctionAssetIndex = assetIndex;
    }
    state.auctionPresentation = AuctionPresentationPhase::Result;
    state.auctionPresentationUntilMs = nowMs + kAuctionResultMs;
    state.auctionResultAssetIndex = assetIndex;
    state.auctionWinnerPlayerId = winnerPlayerId;
    state.auctionResultAmount = amount;
    state.auctionPassed = false;
    state.rollAnimating = false;
    replaceForcedPage(state, ScreenPage::Auction);
}

void finishAuctionResult(AppState &state, uint32_t nowMs)
{
    state.auctionPresentation = AuctionPresentationPhase::None;
    state.auctionPresentationUntilMs = 0;
    state.auctionResultAssetIndex = 0xFF;
    state.auctionWinnerPlayerId = 0;
    state.auctionResultAmount = 0;

    const bool ownTurn = state.activePlayerId == state.selfSeatId;
    const bool ownDecision = state.decisionPlayerId == state.selfSeatId;
    if (state.authorityPhase == AuthorityPhase::AwaitAuction &&
        state.auctionGeneration != 0 && !currentAuctionCompleted(state)) {
        state.auctionPresentation = AuctionPresentationPhase::Live;
        state.auctionPresentationUntilMs = 0;
        replaceForcedPage(state, ScreenPage::Auction);
    } else if (ownTurn && state.authorityPhase == AuthorityPhase::AwaitMoveConfirm &&
               state.rollTarget != 0xFF) {
        replaceForcedPage(state, ScreenPage::MoveGuide);
    } else if (ownTurn && state.authorityPhase == AuthorityPhase::AwaitPurchase) {
        replaceForcedPage(state, ScreenPage::Purchase);
    } else if (ownDecision && state.authorityPhase == AuthorityPhase::AwaitDebt) {
        presentDebtAuthorityState(state, nowMs);
    } else {
        state.rollResolved = false;
        resetHome(state, 0);
    }
}

void applyAuthoritySnapshot(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    const AuthorityPhase previousPhase = state.authorityPhase;
    const uint8_t previousAuctionAssetIndex = state.auctionAssetIndex;
    const uint32_t previousAuctionGeneration = state.auctionGeneration;
    const uint8_t previousAuctionHighestBidderId = state.auctionHighestBidderId;
    const int32_t previousAuctionCurrentBid = state.auctionCurrentBid;
    state.money = event.cash;
    state.position = event.playerPosition;
    if (event.playerCount == 0) return;
    state.selfSeatId = event.selfSeatId;
    state.activePlayerId = event.activePlayerId;
    state.decisionPlayerId = event.decisionPlayerId;
    state.playerCount = event.playerCount;
    state.boardSize = event.boardSize;
    state.availableActions = event.availableActions;
    state.authorityPhase = event.phase;
    state.tileAssetIndex = event.tileAssetIndex;
    state.tileOwnerId = event.tileOwnerId;
    state.tileBuildingLevel = event.tileBuildingLevel;
    state.tileFlags = event.tileFlags;
    state.debtCreditorId = event.debtCreditorId;
    state.debtAssetIndex = event.debtAssetIndex;
    state.auctionAssetIndex = event.auctionAssetIndex;
    state.auctionHighestBidderId = event.auctionHighestBidderId;
    state.debtAmount = event.debtAmount;
    state.auctionCurrentBid = event.auctionCurrentBid;
    state.auctionMinimumBid = event.auctionMinimumBid;
    for (uint8_t index = 0; index < 6; ++index) {
        AuthorityPlayerSummary compact = event.players[index];
        // StateSnapshot intentionally omits the full-only hold/doubles fields.
        // Preserve them until the matching AuthoritySnapshot arrives instead
        // of briefly turning an earned extra roll back into a normal turn.
        if (state.fullAuthoritySnapshotValid && compact.playerId != 0 &&
            state.authorityPlayers[index].playerId == compact.playerId) {
            compact.failedHoldRolls = state.authorityPlayers[index].failedHoldRolls;
            compact.doublesStreak = state.authorityPlayers[index].doublesStreak;
        }
        state.authorityPlayers[index] = compact;
    }
    state.authoritySnapshotValid = event.playerCount != 0;
    state.turnsUntilYou = turnsBeforeSelf(event);
    const bool ownTurn = event.activePlayerId == event.selfSeatId;
    const bool canEndTurn = ownTurn && (event.availableActions & kActionEndTurn) != 0;
    state.homePhase = ownTurn
        ? (canEndTurn ? HomePhase::MyTurnEnd : HomePhase::MyTurn)
        : (state.turnsUntilYou == 1 ? HomePhase::NextPlayer : HomePhase::Waiting);
    if (state.endTurnPresentation != EndTurnPresentationPhase::None && !ownTurn) {
        state.endTurnAccepted = true;
    }

    const bool compactAuctionChanged = event.phase == AuthorityPhase::AwaitAuction &&
        (previousPhase != AuthorityPhase::AwaitAuction ||
         previousAuctionAssetIndex != event.auctionAssetIndex);
    if (compactAuctionChanged) {
        state.auctionGeneration = 0;
        state.auctionFlags = 0;
        state.auctionReadyMask = 0;
        state.auctionRequiredReadyMask = 0;
        state.auctionReadyAttemptGeneration = 0;
        state.auctionReadyAttemptAssetIndex = 0xFF;
        state.auctionPassed = false;
        if (state.auctionPresentation != AuctionPresentationPhase::Result) {
            state.auctionPresentation = AuctionPresentationPhase::None;
            state.auctionPresentationUntilMs = 0;
        }
    }

    // Identity setup owns the entire player display. Game projections may
    // continue arriving while the lobby converges, but they must never pull a
    // console out of avatar, name, or ready/countdown pages.
    if (state.identity.phase != IdentityClientPhase::Inactive) return;

    const bool ownDecision = event.decisionPlayerId == event.selfSeatId;
    if (previousPhase == AuthorityPhase::AwaitDebt &&
        event.phase != AuthorityPhase::AwaitDebt) {
        clearPendingKind(state, TransportCommandKind::PayNow);
        if (state.modal.kind == ModalKind::ForcedPayment) dismissModal(state);
    }
    if (ownTurn && event.phase == AuthorityPhase::AwaitMoveConfirm &&
        event.pendingTarget != 0xFF) {
        noteArrivalTarget(state, event.pendingTarget);
        if (state.rollPresentationComplete) {
            state.rollAnimating = false;
            state.moveArrivalConfirmed = false;
            state.arrivalContinueAtMs = 0;
            if (state.nav.current.page != ScreenPage::MoveGuide) {
                replaceForcedPage(state, ScreenPage::MoveGuide);
            }
        } else {
            if (!state.rollAnimating) {
                state.rollAnimating = true;
                state.rollStartedMs = nowMs;
                state.rollOrigin = event.playerPosition;
            }
            if (state.nav.current.page != ScreenPage::DiceStage &&
                state.nav.current.page != ScreenPage::MoveGuide) {
                replaceForcedPage(state, ScreenPage::DiceStage);
            }
        }
    } else if (ownTurn && event.phase == AuthorityPhase::TurnEnd &&
               state.extraRollPresentation == ExtraRollPresentationPhase::Pending) {
        // A doubles result only earns another roll when authority returns to
        // AwaitRoll. TurnEnd is the authoritative denial path (notably the
        // third consecutive double), so it must escape the retained dice
        // presentation instead of exposing an invalid fourth roll.
        clearRollFlow(state);
        clearExtraRollRoomLifecycle(state);
        resetHome(state, 0);
    } else if (arrivalFlowLocked(state)) {
        if (state.moveArrivalPending && event.phase != AuthorityPhase::AwaitMoveConfirm) {
            state.moveArrivalConfirmed = true;
            if (state.nav.current.page == ScreenPage::MoveGuide &&
                state.arrivalContinueAtMs == 0) {
                state.arrivalContinueAtMs = nowMs + kArrivalConfirmedHoldMs;
            }
        }
    } else if (previousPhase == AuthorityPhase::AwaitAuction &&
               event.phase != AuthorityPhase::AwaitAuction) {
        beginAuctionResult(state, previousAuctionAssetIndex,
                           previousAuctionHighestBidderId,
                           previousAuctionCurrentBid,
                           previousAuctionGeneration, nowMs);
    } else if (ownTurn && event.phase == AuthorityPhase::AwaitPurchase) {
        state.rollAnimating = false;
        if (state.nav.current.page != ScreenPage::Purchase) {
            replaceForcedPage(state, ScreenPage::Purchase);
        }
    } else if (event.phase == AuthorityPhase::AwaitAuction) {
        state.rollAnimating = false;
        const bool generationKnown = previousAuctionGeneration != 0 &&
            previousAuctionAssetIndex == event.auctionAssetIndex &&
            !auctionKeyMatches(previousAuctionGeneration, previousAuctionAssetIndex,
                               state.completedAuctionGeneration,
                               state.completedAuctionAssetIndex);
        if (generationKnown &&
            state.auctionPresentation != AuctionPresentationPhase::Result &&
            state.nav.current.page != ScreenPage::Auction) {
            replaceForcedPage(state, ScreenPage::Auction);
        }
    } else if (ownDecision && event.phase == AuthorityPhase::AwaitCard) {
        state.rollAnimating = false;
        if (state.cardResultValid && state.nav.current.page != ScreenPage::DiceStage &&
            state.nav.current.page != ScreenPage::MoveGuide &&
            state.nav.current.page != ScreenPage::CardReveal) {
            beginCardPresentation(state, nowMs, false);
        }
    } else if (ownDecision && event.phase == AuthorityPhase::AwaitDebt) {
        presentDebtAuthorityState(state, nowMs);
    } else if (previousPhase == AuthorityPhase::AwaitCard &&
               event.phase != AuthorityPhase::AwaitCard &&
               state.nav.current.page == ScreenPage::CardReveal &&
               state.cardPresentationAcknowledged) {
        presentPostArrivalState(state, nowMs);
    } else if (ownTurn && (event.phase == AuthorityPhase::AwaitRoll ||
                           event.phase == AuthorityPhase::TurnEnd)) {
        clearRollFlow(state);
        const bool extraRollHandled = event.phase == AuthorityPhase::AwaitRoll &&
            applyExtraRollAuthority(state, event.resync, nowMs);
        if (!extraRollHandled) clearExtraRollRoomLifecycle(state);
        if (!extraRollHandled && previousPhase != event.phase &&
            !isOrdinaryPage(state.nav.current.page)) {
            resetHome(state, 0);
        }
    } else if (!ownTurn && !ownDecision && !isOrdinaryPage(state.nav.current.page)) {
        clearExtraRollRoomLifecycle(state);
        clearRollFlow(state);
        resetHome(state, 0);
    }
    if (!ownTurn && state.extraRollPresentation != ExtraRollPresentationPhase::None) {
        clearExtraRollRoomLifecycle(state);
    }
    const uint8_t homeCount = contentCount(state, ScreenPage::Home);
    if (state.nav.current.page == ScreenPage::Home && state.nav.current.focus >= homeCount) {
        state.nav.current.focus = homeCount == 0 ? 0 : static_cast<uint8_t>(homeCount - 1);
        syncLegacyState(state);
    }
}

void applyFullAuthorityProjection(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    const AuthorityPhase previousPhase = state.authorityPhase;
    const uint8_t previousAuctionAssetIndex = state.auctionAssetIndex;
    const uint32_t previousAuctionGeneration = state.auctionGeneration;
    const uint8_t previousAuctionHighestBidderId = state.auctionHighestBidderId;
    const int32_t previousAuctionCurrentBid = state.auctionCurrentBid;
    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(event.boardSize);
    const uint32_t expectedHash = board == nullptr ? 0 : gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(board->id), std::strlen(board->id));
    state.boardIdHash = event.boardIdHash;
    state.boardCatalogCompatible = board != nullptr && expectedHash == event.boardIdHash;
    state.fullAuthoritySnapshotValid = true;
    state.authoritySnapshotValid = true;
    state.playerCount = event.playerCount;
    state.boardSize = event.boardSize;
    state.activePlayerId = event.activePlayerId;
    state.decisionPlayerId = event.decisionPlayerId;
    state.authorityPhase = event.phase;
    state.winnerPlayerId = event.winnerPlayerId;
    state.authorityAssetCount = event.assetCount;
    state.lastEventSequence = event.lastEventSequence;
    for (uint8_t index = 0; index < 6; ++index) state.authorityPlayers[index] = event.players[index];
    for (uint8_t index = 0; index < 28; ++index) state.authorityAssets[index] = event.assets[index];
    for (uint8_t index = 0; index < event.playerCount; ++index) {
        if (event.players[index].playerId != state.selfSeatId) continue;
        state.money = event.players[index].cash;
        state.position = event.players[index].position;
        break;
    }
    state.debtCreditorId = event.debtCreditorId;
    state.debtAssetIndex = event.debtAssetIndex;
    state.debtPaymentEvent = event.debtPaymentEvent;
    state.debtContinuation = event.debtContinuation;
    state.debtDieA = event.debtDieA;
    state.debtDieB = event.debtDieB;
    state.debtAmount = event.debtAmount;
    if (event.phase == AuthorityPhase::AwaitDebt &&
        event.debtDebtorId == state.selfSeatId &&
        (state.availableActions & kActionMortgage) != 0) {
        state.debt.eligibleMask = 0;
        for (uint8_t index = 0; index < event.assetCount; ++index) {
            if (event.assets[index].ownerId == state.selfSeatId) {
                state.debt.eligibleMask |= static_cast<uint32_t>(1u) << index;
            }
        }
        state.debt.selectedMask &= legalDebtAssetMask(state);
    }
    applyPendingCardProjection(state, event, nowMs);

    const bool activeAuction = event.phase == AuthorityPhase::AwaitAuction &&
        (event.auctionFlags & 0x01u) != 0 && event.auctionGeneration != 0 &&
        event.auctionAssetIndex != 0xFF;
    state.auctionAssetIndex = event.auctionAssetIndex;
    state.auctionCurrentBidderId = event.auctionCurrentBidderId;
    state.auctionHighestBidderId = event.auctionHighestBidderId;
    state.auctionPassedMask = event.auctionPassedMask;
    state.auctionFlags = event.auctionFlags;
    state.auctionReadyMask = event.auctionReadyMask;
    state.auctionRequiredReadyMask = event.auctionRequiredReadyMask;
    state.auctionCurrentBid = event.auctionCurrentBid;
    state.auctionGeneration = event.auctionGeneration;

    if (state.identity.phase != IdentityClientPhase::Inactive) return;

    if (activeAuction) {
        const bool seenKey = auctionKeyMatches(
            event.auctionGeneration, event.auctionAssetIndex,
            state.seenAuctionGeneration, state.seenAuctionAssetIndex);
        if (!seenKey) {
            state.seenAuctionGeneration = event.auctionGeneration;
            state.seenAuctionAssetIndex = event.auctionAssetIndex;
            state.auctionReadyAttemptGeneration = 0;
            state.auctionReadyAttemptAssetIndex = 0xFF;
            beginAuctionIntro(state, nowMs);
            if (!arrivalFlowLocked(state)) replaceForcedPage(state, ScreenPage::Auction);
        } else if (!currentAuctionCompleted(state)) {
            if (state.auctionPresentation == AuctionPresentationPhase::None ||
                state.auctionPresentation == AuctionPresentationPhase::OpeningWait) {
                state.auctionPresentation = AuctionPresentationPhase::Live;
                state.auctionPresentationUntilMs = 0;
                state.nav.current.focus = 0;
                syncLegacyState(state);
            }
            if (!arrivalFlowLocked(state) && state.nav.current.page != ScreenPage::Auction) {
                replaceForcedPage(state, ScreenPage::Auction);
            }
        }
    } else if (previousPhase == AuthorityPhase::AwaitAuction &&
               event.phase != AuthorityPhase::AwaitAuction &&
               previousAuctionGeneration != 0 &&
               state.auctionPresentation != AuctionPresentationPhase::Result) {
        beginAuctionResult(state, previousAuctionAssetIndex,
                           previousAuctionHighestBidderId,
                           previousAuctionCurrentBid,
                           previousAuctionGeneration, nowMs);
    }

    if (event.auctionGeneration != previousAuctionGeneration ||
        event.auctionAssetIndex != previousAuctionAssetIndex) {
        state.auctionReadyAttemptGeneration = 0;
        state.auctionReadyAttemptAssetIndex = 0xFF;
    }
    if (state.selfSeatId >= 1 && state.selfSeatId <= 6) {
        state.auctionPassed = (event.auctionPassedMask & (1u << (state.selfSeatId - 1))) != 0;
    }
    const bool ownPendingMove = (event.pendingMoveFlags & 1u) != 0 &&
                                event.pendingMovePlayerId == state.selfSeatId &&
                                event.pendingTarget != 0xFF;
    if (ownPendingMove) {
        state.rollOrigin = event.pendingMoveOrigin;
        noteArrivalTarget(state, event.pendingTarget);
        if (state.rollPresentationComplete) {
            state.rollAnimating = false;
            state.moveArrivalConfirmed = false;
            state.arrivalContinueAtMs = 0;
            if (state.nav.current.page != ScreenPage::MoveGuide) {
                replaceForcedPage(state, ScreenPage::MoveGuide);
            }
        } else {
            if (!state.rollAnimating) {
                state.rollAnimating = true;
                state.rollStartedMs = nowMs;
            }
            markRollResolved(state, event.pendingMoveDieA, event.pendingMoveDieB, nowMs);
        }
    } else if (state.moveArrivalPending && state.rollResolved &&
               event.phase != AuthorityPhase::AwaitMoveConfirm) {
        state.moveArrivalConfirmed = true;
        if (state.nav.current.page == ScreenPage::MoveGuide &&
            state.arrivalContinueAtMs == 0) {
            state.arrivalContinueAtMs = nowMs + kArrivalConfirmedHoldMs;
        }
    }
    const bool ownReleaseHoldDebt = event.phase == AuthorityPhase::AwaitDebt &&
        event.debtDebtorId == state.selfSeatId &&
        event.debtContinuation == kDebtContinuationReleaseHoldAndMove;
    if (ownReleaseHoldDebt && state.nav.current.page == ScreenPage::DiceStage &&
        state.rollAnimating) {
        markRollResolved(state, event.debtDieA, event.debtDieB, nowMs);
    }
    if (!state.boardCatalogCompatible) {
        resetHome(state, 0);
        showToast(state, "CONTENT VERSION MISMATCH", nowMs);
    }
    // The compact StateSnapshot does not carry doublesStreak. Re-evaluate once
    // the matching full projection has restored it so either projection order
    // converges on the same one-shot reward presentation.
    if (event.activePlayerId == state.selfSeatId &&
        event.phase == AuthorityPhase::AwaitRoll) {
        applyExtraRollAuthority(state, event.resync, nowMs);
    }
}

void applyRosterProjection(AppState &state, const TransportEvent &event)
{
    for (uint8_t index = 0; index < 6; ++index) state.rosterNames[index][0] = '\0';
    for (uint8_t index = 0; index < event.playerCount; ++index) {
        const uint8_t playerId = event.players[index].playerId;
        if (playerId == 0 || playerId > 6) continue;
        std::memcpy(state.rosterNames[playerId - 1], event.playerNames[index], 17);
        state.rosterNames[playerId - 1][16] = '\0';
        if (playerId <= state.identity.seatCount) {
            strncpy(state.identity.seats[playerId - 1].name,
                    state.rosterNames[playerId - 1], 16);
            state.identity.seats[playerId - 1].name[16] = '\0';
        }
    }
    state.rosterSnapshotValid = true;
    if (state.identity.phase != IdentityClientPhase::Inactive &&
        !state.identity.nameDirty && state.identity.draftName[0] == '\0' &&
        state.selfSeatId >= 1 && state.selfSeatId <= 6) {
        strncpy(state.identity.draftName, state.rosterNames[state.selfSeatId - 1],
                sizeof(state.identity.draftName) - 1);
        state.identity.draftName[sizeof(state.identity.draftName) - 1] = '\0';
    }
}

void applyGameEventPresentation(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    if (event.gameEvent.sequence <= state.lastGameEvent.sequence) return;
    state.lastGameEvent = event.gameEvent;
    appendActivity(state, event, nowMs);
    const bool ownActor = event.gameEvent.actorId == state.selfSeatId;
    switch (event.gameEvent.kind) {
        case 3: // DiceRolled
            if (event.gameEvent.actorId == state.selfSeatId) {
                const uint8_t dieA = static_cast<uint8_t>(event.gameEvent.detail & 0xFFu);
                const uint8_t dieB = static_cast<uint8_t>((event.gameEvent.detail >> 8) & 0xFFu);
                if (state.nav.current.page == ScreenPage::DiceStage && state.rollAnimating &&
                    dieA >= 1 && dieA <= 6 && dieB >= 1 && dieB <= 6) {
                    markRollResolved(state, dieA, dieB, nowMs);
                } else {
                    state.dieA = dieA;
                    state.dieB = dieB;
                    state.rolledSteps = static_cast<uint8_t>(event.gameEvent.amount);
                    if (dieA == dieB && dieA >= 1 && dieA <= 6) {
                        if (state.extraRollPresentation != ExtraRollPresentationPhase::Ready) {
                            state.extraRollPresentation = ExtraRollPresentationPhase::Pending;
                        }
                        state.extraRollDieA = dieA;
                        state.extraRollDieB = dieB;
                    }
                }
                if (state.extraRollPresentation == ExtraRollPresentationPhase::Pending &&
                    state.authorityPhase == AuthorityPhase::AwaitRoll) {
                    applyExtraRollAuthority(state, false, nowMs);
                }
            }
            break;
        case 8: if (ownActor) showToast(state, "PROPERTY PURCHASED", nowMs); break;
        case 9: if (ownActor) showToast(state, "RENT PAID", nowMs); break;
        case 10: if (ownActor) showToast(state, "FEE PAID", nowMs); break;
        case 11: // CardApplied: dedicated PlayerCardEvent owns all card UI state.
        case 27: // CardDrawn: private reveal details never come from the compact batch.
            break;
        case 14: if (ownActor) showToast(state, "ASSET MORTGAGED", nowMs); break;
        case 15: if (ownActor) showToast(state, "MORTGAGE RELEASED", nowMs); break;
        case 16: if (ownActor) showToast(state, "BUILDING UPDATED", nowMs); break;
        case 17:
            if (state.auctionPresentation == AuctionPresentationPhase::Result) {
                state.auctionResultAssetIndex = event.gameEvent.assetIndex;
                state.auctionWinnerPlayerId = event.gameEvent.actorId;
                state.auctionResultAmount = event.gameEvent.amount;
            }
            break;
        case 18: if (ownActor) showToast(state, "PLAYER BANKRUPT", nowMs); break;
        case 21:
            if (state.tradeOffer.active &&
                state.tradeOffer.tradeId == event.gameEvent.detail) {
                clearPendingTradeRequests(state);
                clearTradeRoomLifecycle(state);
                if (state.nav.current.page == ScreenPage::TradeOffer ||
                    state.nav.current.page == ScreenPage::Trade ||
                    state.nav.current.page == ScreenPage::TradeAssetSelect) {
                    resetHome(state);
                }
            }
            if (ownActor || event.gameEvent.targetId == state.selfSeatId) {
                showToast(state, "TRADE SETTLED", nowMs);
            }
            break;
        case 22: break;
        case 23: if (ownActor) showToast(state, "NEW HIGHEST BID", nowMs); break;
        case 24: if (ownActor) showToast(state, "PLAYER PASSED", nowMs); break;
        case 28: if (ownActor) showToast(state, "TRADE OFFER CREATED", nowMs); break;
        case 29: if (ownActor) showToast(state, "TRADE OFFER UPDATED", nowMs); break;
        case 30:
            if (state.tradeOffer.active &&
                state.tradeOffer.tradeId == event.gameEvent.detail) {
                clearPendingTradeRequests(state);
                clearTradeRoomLifecycle(state);
                if (state.nav.current.page == ScreenPage::TradeOffer ||
                    state.nav.current.page == ScreenPage::Trade ||
                    state.nav.current.page == ScreenPage::TradeAssetSelect) {
                    resetHome(state);
                }
            }
            break;
        default: break;
    }
    startNextActivityBanner(state, nowMs);
    touchRevision(state);
}

} // namespace

void appInit(AppState &state, uint32_t nowMs)
{
    state = AppState{};
    for (uint8_t index = 0; index < kPlayerCount; ++index) {
        state.authorityPlayers[index].playerId = static_cast<uint8_t>(index + 1);
        state.authorityPlayers[index].position = kPlayers[index].position;
        state.authorityPlayers[index].cash = kPlayers[index].money;
        state.authorityPlayers[index].flags = 0x04u;
    }
    state.toast = "旋转浏览 · 按下选择";
    state.toastUntilMs = nowMs + 1500;
    syncLegacyState(state);
}

HomePhase appPresentedHomePhase(const AppState &state)
{
    return presentedHomePhase(state);
}

bool appAuctionOpening(const AppState &state)
{
    return auctionOpening(state);
}

bool appIdentityActive(const AppState &state)
{
    return state.identity.phase != IdentityClientPhase::Inactive;
}

uint32_t appIdentityCountdownRemainingMs(const AppState &state, uint32_t nowMs)
{
    if (state.identity.phase != IdentityClientPhase::Countdown ||
        state.identity.countdownDeadlineMs == 0 ||
        hasReachedDeadline(nowMs, state.identity.countdownDeadlineMs)) {
        return 0;
    }
    return state.identity.countdownDeadlineMs - nowMs;
}

uint8_t appIdentityReadyCount(const AppState &state)
{
    uint8_t count = 0;
    uint8_t mask = state.identity.readyMask;
    while (mask != 0) {
        count = static_cast<uint8_t>(count + (mask & 1u));
        mask >>= 1;
    }
    return count;
}

void appUpdateAvatarPreloadProgress(AppState &state, uint8_t readyCount,
                                    uint8_t totalCount, bool complete)
{
    const uint8_t normalizedTotal = totalCount == 0 ? 30 : totalCount;
    bool changed = state.identity.avatarPreloadReadyCount != readyCount ||
                   state.identity.avatarPreloadTotalCount != normalizedTotal;
    state.identity.avatarPreloadReadyCount = readyCount;
    state.identity.avatarPreloadTotalCount = normalizedTotal;
    if (!complete) {
        if (changed) touchRevision(state);
        return;
    }

    if (!state.identity.avatarAssetsReady) {
        state.identity.avatarAssetsReady = true;
        changed = true;
    }
    if (state.nav.current.page == ScreenPage::AvatarLoading) {
        bool avatarFinal = false;
        bool nameFinal = false;
        if (state.selfSeatId > 0 && state.selfSeatId <= state.identity.seatCount) {
            const uint8_t selfIndex = static_cast<uint8_t>(state.selfSeatId - 1u);
            const uint8_t selfBit = static_cast<uint8_t>(1u << selfIndex);
            const TransportIdentitySeat &self = state.identity.seats[selfIndex];
            avatarFinal = (state.identity.avatarReadyMask & selfBit) != 0 ||
                          (self.flags & gridopoly::protocol::IdentitySeatAvatarFinal) != 0;
            nameFinal = (state.identity.nameReadyMask & selfBit) != 0 ||
                        (self.flags & gridopoly::protocol::IdentitySeatNameFinal) != 0;
        }

        if (avatarFinal && !state.identity.avatarEditOverride) {
            if (nameFinal) {
                state.identity.phase =
                    state.identity.authorityPhase == TransportIdentityPhase::Countdown
                        ? IdentityClientPhase::Countdown : IdentityClientPhase::Ready;
                showIdentityPage(state, ScreenPage::PlayerReady, 0);
            } else {
                state.identity.phase = IdentityClientPhase::NameReview;
                showIdentityPage(state, ScreenPage::NameReview, 0);
            }
        } else {
            state.identity.phase = IdentityClientPhase::AvatarEditing;
            showIdentityPage(state, ScreenPage::AvatarSetup, 0);
        }
        changed = true;
    }
    if (changed) touchRevision(state);
}

bool appIdentityAppendCharacter(AppState &state, char character)
{
    const bool allowed = (character >= 'A' && character <= 'Z') ||
                         (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '-' || character == ' ';
    if (!allowed) return false;
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    const size_t length = strlen(state.identity.draftName);
    if (length >= sizeof(state.identity.draftName) - 1) return false;
    state.identity.draftName[length] = character;
    state.identity.draftName[length + 1] = '\0';
    state.identity.nameDirty = true;
    touchRevision(state);
    return true;
}

bool appIdentityDeleteCharacter(AppState &state)
{
    const size_t length = strlen(state.identity.draftName);
    if (length == 0) return false;
    state.identity.draftName[length - 1] = '\0';
    state.identity.nameDirty = true;
    touchRevision(state);
    return true;
}

uint16_t appEndTurnExitProgressPermille(const AppState &state, uint32_t nowMs)
{
    if (state.endTurnPresentation != EndTurnPresentationPhase::Exiting) return 0;
    const uint32_t elapsed = nowMs - state.endTurnPresentationStartedMs;
    if (elapsed >= kEndTurnExitMs) return 1000;
    return static_cast<uint16_t>(elapsed * 1000u / kEndTurnExitMs);
}

bool appDiceResultVisible(const AppState &state, uint32_t nowMs)
{
    return state.rollResolved && state.rollRevealMs != 0 &&
           hasReachedDeadline(nowMs, state.rollRevealMs);
}

bool appCanNavigateBack(const AppState &state)
{
    return isOrdinaryPage(state.nav.current.page) && state.nav.depth != 0;
}

bool appDebtAssetEligible(const AppState &state, uint8_t assetIndex)
{
    const uint8_t count = state.fullAuthoritySnapshotValid ? state.authorityAssetCount : kAssetCount;
    if (assetIndex >= count) return false;
    return (legalDebtAssetMask(state) & (static_cast<uint32_t>(1u) << assetIndex)) != 0;
}

bool appDebtAssetSelected(const AppState &state, uint8_t assetIndex)
{
    return appDebtAssetEligible(state, assetIndex) &&
           (state.debt.selectedMask & (static_cast<uint32_t>(1u) << assetIndex)) != 0;
}

int32_t appDebtShortfall(const AppState &state)
{
    const int64_t difference = static_cast<int64_t>(state.debt.amountDue) -
                               static_cast<int64_t>(state.debt.cashBefore);
    return difference <= 0 ? 0 : saturateInt32(difference);
}

int32_t appDebtSelectedProceeds(const AppState &state)
{
    int64_t proceeds = 0;
    const uint32_t mask = state.debt.selectedMask & legalDebtAssetMask(state);
    const uint8_t count = state.fullAuthoritySnapshotValid ? state.authorityAssetCount : kAssetCount;
    for (uint8_t index = 0; index < count; ++index) {
        if ((mask & (static_cast<uint32_t>(1u) << index)) != 0) {
            proceeds += appAssetMortgageValue(state, index);
        }
    }
    return saturateInt32(proceeds);
}

int32_t appDebtPostMortgageBalance(const AppState &state)
{
    const int64_t balance = static_cast<int64_t>(state.debt.cashBefore) +
                            static_cast<int64_t>(appDebtSelectedProceeds(state));
    return saturateInt32(balance);
}

bool appDebtCanConfirm(const AppState &state)
{
    return appDebtSelectedProceeds(state) >= appDebtShortfall(state);
}

uint8_t appVisibleAssetCount(const AppState &state)
{
    if (!state.fullAuthoritySnapshotValid) return kAssetCount;
    uint8_t count = 0;
    for (uint8_t index = 0; index < state.authorityAssetCount; ++index) {
        if (state.authorityAssets[index].ownerId == state.selfSeatId) ++count;
    }
    return count;
}

uint8_t appVisibleAssetIndex(const AppState &state, uint8_t row)
{
    if (!state.fullAuthoritySnapshotValid) return row < kAssetCount ? row : 0xFF;
    uint8_t visible = 0;
    for (uint8_t index = 0; index < state.authorityAssetCount; ++index) {
        if (state.authorityAssets[index].ownerId != state.selfSeatId) continue;
        if (visible++ == row) return index;
    }
    return 0xFF;
}

const char *appPlayerDisplayName(const AppState &state, uint8_t playerIndex)
{
    if (playerIndex >= 6) return "PLAYER";
    if (state.rosterSnapshotValid && state.rosterNames[playerIndex][0] != '\0') {
        return state.rosterNames[playerIndex];
    }
    return kPlayers[playerIndex].name;
}

const char *appPlayerNameById(const AppState &state, uint8_t playerId)
{
    if (playerId == 0) return "CITY BANK";
    if (playerId > 6) return "PLAYER";
    const uint8_t playerIndex = static_cast<uint8_t>(playerId - 1);
    if (state.rosterSnapshotValid && state.rosterNames[playerIndex][0] != '\0') {
        return state.rosterNames[playerIndex];
    }
    return "PLAYER";
}

uint8_t appActivityCount(const AppState &state)
{
    return state.activity.count;
}

const ActivityEntry *appActivityEntryAt(const AppState &state, uint8_t newestFirstIndex)
{
    return activityEntryAt(state, newestFirstIndex);
}

const ActivityEntry *appActivityBannerEntry(const AppState &state)
{
    if (state.activity.bannerSequence == 0) return nullptr;
    for (uint8_t row = 0; row < state.activity.count; ++row) {
        const ActivityEntry *entry = activityEntryAt(state, row);
        if (entry != nullptr && entry->event.sequence == state.activity.bannerSequence) {
            return entry;
        }
    }
    return nullptr;
}

bool appActivityBannerVisible(const AppState &state, uint32_t nowMs)
{
    return state.activity.bannerSequence != 0 && state.activity.bannerUntilMs != 0 &&
           !hasReachedDeadline(nowMs, state.activity.bannerUntilMs) &&
           !activityPresentationSuppressed(state) && appActivityBannerEntry(state) != nullptr;
}

const gridopoly::core::AssetDefinition *authorityAssetDefinition(const AppState &state,
                                                                 uint8_t assetIndex)
{
    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (board == nullptr || assetIndex >= board->assetCount) return nullptr;
    return &board->assets[assetIndex];
}

const GridCityVisualDefinition *appAssetVisual(const AppState &state, uint8_t assetIndex)
{
    const auto *asset = authorityAssetDefinition(state, assetIndex);
    return asset == nullptr ? nullptr : gridCityVisualById(asset->id);
}

const GridCityVisualDefinition *appTileVisual(const AppState &state, uint8_t position)
{
    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (board == nullptr || position >= board->tileCount) return nullptr;
    return gridCityVisualById(board->tiles[position].id);
}

const char *appAssetDisplayName(const AppState &state, uint8_t assetIndex)
{
    const GridCityVisualDefinition *visual = appAssetVisual(state, assetIndex);
    if (visual != nullptr) return visual->name;
    if (state.fullAuthoritySnapshotValid) return "ASSET";
    return assetIndex < kAssetCount ? kAssets[assetIndex].name : "ASSET";
}

const char *appTileDisplayName(const AppState &state, uint8_t position)
{
    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (board == nullptr || position >= board->tileCount) return "UNKNOWN TILE";
    const auto &tile = board->tiles[position];
    const GridCityVisualDefinition *visual = gridCityVisualById(tile.id);
    if (visual != nullptr) return visual->name;
    if (tile.assetIndex != 0xFF) return appAssetDisplayName(state, tile.assetIndex);
    switch (tile.kind) {
        case gridopoly::core::TileKind::Start: return "CITY START";
        case gridopoly::core::TileKind::CityEvent: return "CHANCE";
        case gridopoly::core::TileKind::CivicFund: return "COMMUNITY CHEST";
        case gridopoly::core::TileKind::Fee: return "CITY FEE";
        case gridopoly::core::TileKind::Hold: return "HOLDING AREA";
        case gridopoly::core::TileKind::Rest: return "CITY REST";
        case gridopoly::core::TileKind::GoToHold: return "GO TO HOLD";
        case gridopoly::core::TileKind::Property:
        case gridopoly::core::TileKind::Transit:
        case gridopoly::core::TileKind::Utility: return tile.id;
    }
    return tile.id;
}

const gridopoly::core::TileDefinition *arrivalTileDefinition(const AppState &state)
{
    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (board == nullptr || state.rollTarget >= board->tileCount) return nullptr;
    return &board->tiles[state.rollTarget];
}

ArrivalKind appArrivalKind(const AppState &state)
{
    const auto *tile = arrivalTileDefinition(state);
    if (tile == nullptr) return ArrivalKind::Unknown;
    switch (tile->kind) {
        case gridopoly::core::TileKind::Start: return ArrivalKind::Start;
        case gridopoly::core::TileKind::Property:
        case gridopoly::core::TileKind::Transit:
        case gridopoly::core::TileKind::Utility: return ArrivalKind::Asset;
        case gridopoly::core::TileKind::CityEvent: return ArrivalKind::CityEvent;
        case gridopoly::core::TileKind::CivicFund: return ArrivalKind::CivicFund;
        case gridopoly::core::TileKind::Fee: return ArrivalKind::Fee;
        case gridopoly::core::TileKind::Hold: return ArrivalKind::Hold;
        case gridopoly::core::TileKind::Rest: return ArrivalKind::Rest;
        case gridopoly::core::TileKind::GoToHold: return ArrivalKind::GoToHold;
    }
    return ArrivalKind::Unknown;
}

uint8_t appArrivalAssetIndex(const AppState &state)
{
    const auto *tile = arrivalTileDefinition(state);
    return tile == nullptr ? 0xFF : tile->assetIndex;
}

const char *appArrivalDisplayName(const AppState &state)
{
    const uint8_t assetIndex = appArrivalAssetIndex(state);
    if (assetIndex != 0xFF) return appAssetDisplayName(state, assetIndex);
    const GridCityVisualDefinition *visual = appTileVisual(state, state.rollTarget);
    if (visual != nullptr) return visual->name;
    switch (appArrivalKind(state)) {
        case ArrivalKind::Start: return "GRID CENTRAL";
        case ArrivalKind::CityEvent: return "CHANCE";
        case ArrivalKind::CivicFund: return "COMMUNITY CHEST";
        case ArrivalKind::Fee: return "CITY FEE";
        case ArrivalKind::Hold: return "HOLD / VISITING";
        case ArrivalKind::Rest: return "FREE PLAZA";
        case ArrivalKind::GoToHold: return "GO TO HOLD";
        case ArrivalKind::Asset:
        case ArrivalKind::Unknown: return "BOARD TILE";
    }
    return "BOARD TILE";
}

uint8_t appArrivalOwnerId(const AppState &state)
{
    const uint8_t assetIndex = appArrivalAssetIndex(state);
    if (assetIndex == 0xFF) return 0;
    if (state.fullAuthoritySnapshotValid && assetIndex < state.authorityAssetCount) {
        return state.authorityAssets[assetIndex].ownerId;
    }
    return state.tileAssetIndex == assetIndex ? state.tileOwnerId : 0;
}

int32_t appArrivalAmount(const AppState &state)
{
    const auto *tile = arrivalTileDefinition(state);
    return tile == nullptr ? 0 : tile->amount;
}

int32_t appAssetValue(const AppState &state, uint8_t assetIndex)
{
    if (state.fullAuthoritySnapshotValid) {
        const auto *asset = authorityAssetDefinition(state, assetIndex);
        return asset == nullptr ? 0 : asset->economy.price;
    }
    return assetIndex < kAssetCount ? kAssets[assetIndex].value : 0;
}

int32_t appAssetRent(const AppState &state, uint8_t assetIndex)
{
    if (state.fullAuthoritySnapshotValid) {
        const auto *asset = authorityAssetDefinition(state, assetIndex);
        if (asset == nullptr) return 0;
        const uint8_t level = appAssetBuildingLevel(state, assetIndex) > 5
            ? 5 : appAssetBuildingLevel(state, assetIndex);
        return asset->economy.rent[level];
    }
    return assetIndex < kAssetCount ? kAssets[assetIndex].rent : 0;
}

int32_t appAssetMortgageValue(const AppState &state, uint8_t assetIndex)
{
    if (state.fullAuthoritySnapshotValid) {
        const auto *asset = authorityAssetDefinition(state, assetIndex);
        return asset == nullptr ? 0 : asset->economy.mortgageValue;
    }
    return assetIndex < kAssetCount ? kAssets[assetIndex].mortgageValue : 0;
}

int32_t appAssetBuildingCost(const AppState &state, uint8_t assetIndex)
{
    const auto *asset = authorityAssetDefinition(state, assetIndex);
    return asset == nullptr ? 0 : asset->economy.buildingCost;
}

uint8_t appAssetBuildingLevel(const AppState &state, uint8_t assetIndex)
{
    if (state.fullAuthoritySnapshotValid) {
        return assetIndex < state.authorityAssetCount
            ? state.authorityAssets[assetIndex].buildingLevel : 0;
    }
    return assetIndex < kAssetCount ? kAssets[assetIndex].buildingLevel : 0;
}

bool appAssetMortgaged(const AppState &state, uint8_t assetIndex)
{
    if (state.fullAuthoritySnapshotValid) {
        return assetIndex < state.authorityAssetCount &&
               (state.authorityAssets[assetIndex].flags & 1u) != 0;
    }
    return assetIndex < kAssetCount && kAssets[assetIndex].mortgaged;
}

bool appAssetGroupProgress(const AppState &state, uint8_t assetIndex,
                           uint8_t &owned, uint8_t &total)
{
    owned = 0;
    total = 0;
    const auto *definition = authorityAssetDefinition(state, assetIndex);
    const auto *board = gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (definition == nullptr || board == nullptr ||
        definition->kind != gridopoly::core::TileKind::Property ||
        definition->group == gridopoly::core::kNoGroup) {
        return false;
    }
    for (uint8_t index = 0; index < board->assetCount; ++index) {
        if (board->assets[index].kind != gridopoly::core::TileKind::Property ||
            board->assets[index].group != definition->group) continue;
        ++total;
        if (state.fullAuthoritySnapshotValid && index < state.authorityAssetCount &&
            state.authorityAssets[index].ownerId == state.selfSeatId) {
            ++owned;
        }
    }
    return total != 0;
}

namespace {

bool assetOwnedBySelf(const AppState &state, uint8_t assetIndex)
{
    if (!state.fullAuthoritySnapshotValid) return assetIndex < kAssetCount;
    return assetIndex < state.authorityAssetCount &&
           state.authorityAssets[assetIndex].ownerId == state.selfSeatId;
}

bool groupHasMortgage(const AppState &state, uint8_t assetIndex)
{
    const auto *definition = authorityAssetDefinition(state, assetIndex);
    const auto *board = gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (definition == nullptr || board == nullptr ||
        definition->group == gridopoly::core::kNoGroup) return appAssetMortgaged(state, assetIndex);
    for (uint8_t index = 0; index < board->assetCount; ++index) {
        if (board->assets[index].group == definition->group && appAssetMortgaged(state, index)) {
            return true;
        }
    }
    return false;
}

bool groupHasBuildings(const AppState &state, uint8_t assetIndex)
{
    const auto *definition = authorityAssetDefinition(state, assetIndex);
    const auto *board = gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (definition == nullptr || board == nullptr ||
        definition->group == gridopoly::core::kNoGroup) {
        return appAssetBuildingLevel(state, assetIndex) != 0;
    }
    for (uint8_t index = 0; index < board->assetCount; ++index) {
        if (board->assets[index].group == definition->group &&
            appAssetBuildingLevel(state, index) != 0) return true;
    }
    return false;
}

bool buildEvenlyAllowed(const AppState &state, uint8_t assetIndex)
{
    const auto *definition = authorityAssetDefinition(state, assetIndex);
    const auto *board = gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (definition == nullptr || board == nullptr ||
        definition->group == gridopoly::core::kNoGroup) return false;
    uint8_t minimum = 5;
    for (uint8_t index = 0; index < board->assetCount; ++index) {
        if (board->assets[index].group == definition->group) {
            minimum = std::min(minimum, appAssetBuildingLevel(state, index));
        }
    }
    return appAssetBuildingLevel(state, assetIndex) <= minimum;
}

bool sellEvenlyAllowed(const AppState &state, uint8_t assetIndex)
{
    const auto *definition = authorityAssetDefinition(state, assetIndex);
    const auto *board = gridopoly::core::BoardCatalog::findBySize(state.boardSize);
    if (definition == nullptr || board == nullptr ||
        definition->group == gridopoly::core::kNoGroup) return false;
    uint8_t maximum = 0;
    for (uint8_t index = 0; index < board->assetCount; ++index) {
        if (board->assets[index].group == definition->group) {
            maximum = std::max(maximum, appAssetBuildingLevel(state, index));
        }
    }
    return appAssetBuildingLevel(state, assetIndex) >= maximum;
}

bool actionExposed(const AppState &state, uint32_t action)
{
    return !state.authoritySnapshotValid || (state.availableActions & action) != 0;
}

} // namespace

bool appDebtBuildingSaleEligible(const AppState &state, uint8_t assetIndex)
{
    return assetOwnedBySelf(state, assetIndex) &&
           (state.availableActions & kActionSellBuilding) != 0 &&
           appAssetBuildingLevel(state, assetIndex) != 0 &&
           sellEvenlyAllowed(state, assetIndex);
}

int32_t appDebtBuildingSaleProceeds(const AppState &state, uint8_t assetIndex)
{
    if (appAssetBuildingLevel(state, assetIndex) == 0) return 0;
    return std::max<int32_t>(0, appAssetBuildingCost(state, assetIndex) / 2);
}

uint8_t appAssetDetailActionCount(const AppState &state)
{
    (void)state;
    return 4;
}

AssetDetailAction appAssetDetailActionAt(const AppState &state, uint8_t actionIndex)
{
    (void)state;
    if (actionIndex == 1) return AssetDetailAction::Build;
    if (actionIndex == 2) return AssetDetailAction::SellBuilding;
    if (actionIndex == 3) return AssetDetailAction::Trade;
    return AssetDetailAction::MortgageOrRedeem;
}

bool appAssetDetailActionVisible(const AppState &state, AssetDetailAction action)
{
    const uint8_t assetIndex = state.selectedAsset;
    switch (action) {
        case AssetDetailAction::MortgageOrRedeem:
            return true;
        case AssetDetailAction::Build: {
            uint8_t owned = 0;
            uint8_t total = 0;
            return appAssetGroupProgress(state, assetIndex, owned, total) &&
                   owned == total && appAssetBuildingLevel(state, assetIndex) < 5;
        }
        case AssetDetailAction::SellBuilding:
            return appAssetBuildingLevel(state, assetIndex) != 0;
        case AssetDetailAction::Trade:
            return !appAssetMortgaged(state, assetIndex);
    }
    return false;
}

bool appAssetDetailActionEnabled(const AppState &state, AssetDetailAction action)
{
    const uint8_t assetIndex = state.selectedAsset;
    if (!appAssetDetailActionVisible(state, action) ||
        !assetOwnedBySelf(state, assetIndex)) return false;
    switch (action) {
        case AssetDetailAction::MortgageOrRedeem:
            if (appAssetMortgaged(state, assetIndex)) {
                return actionExposed(state, kActionUnmortgage) &&
                       state.money >= unmortgageCost(state, assetIndex);
            }
            return actionExposed(state, kActionMortgage) &&
                   appAssetBuildingLevel(state, assetIndex) == 0 &&
                   !groupHasBuildings(state, assetIndex);
        case AssetDetailAction::Build:
            return actionExposed(state, kActionBuild) &&
                   !groupHasMortgage(state, assetIndex) &&
                   appAssetBuildingLevel(state, assetIndex) < 5 &&
                   buildEvenlyAllowed(state, assetIndex) &&
                   state.money >= appAssetBuildingCost(state, assetIndex);
        case AssetDetailAction::SellBuilding:
            return actionExposed(state, kActionSellBuilding) &&
                   appAssetBuildingLevel(state, assetIndex) != 0 &&
                   sellEvenlyAllowed(state, assetIndex);
        case AssetDetailAction::Trade:
            return kTradeBackendAvailable && actionExposed(state, kActionTrade) &&
                   !appAssetMortgaged(state, assetIndex) &&
                   !groupHasBuildings(state, assetIndex);
    }
    return false;
}

uint8_t appTradeAssetCount(const AppState &state)
{
    return appVisibleAssetCount(state);
}

uint8_t appTradeAssetIndex(const AppState &state, uint8_t row)
{
    return appVisibleAssetIndex(state, row);
}

bool appTradeAssetEligible(const AppState &state, uint8_t assetIndex)
{
    return assetOwnedBySelf(state, assetIndex) && !appAssetMortgaged(state, assetIndex) &&
           appAssetBuildingLevel(state, assetIndex) == 0 && !groupHasBuildings(state, assetIndex);
}

bool appTradeAssetSelected(const AppState &state, uint8_t assetIndex)
{
    return assetIndex < 32 && appTradeAssetEligible(state, assetIndex) &&
           (state.tradeGiveAssetMask & (static_cast<uint32_t>(1u) << assetIndex)) != 0;
}

uint8_t appPageContentCount(const AppState &state)
{
    return contentCount(state, state.nav.current.page);
}

uint8_t appFocusCount(const AppState &state)
{
    const uint8_t content = appPageContentCount(state);
    if (state.nav.current.page == ScreenPage::Home) return content;
    if (state.nav.current.page == ScreenPage::Purchase ||
        state.nav.current.page == ScreenPage::Auction ||
        state.nav.current.page == ScreenPage::MoveGuide ||
        state.nav.current.page == ScreenPage::CardReveal ||
        state.nav.current.page == ScreenPage::AvatarSetup ||
        state.nav.current.page == ScreenPage::NameReview ||
        state.nav.current.page == ScreenPage::NameHandwriting ||
        state.nav.current.page == ScreenPage::PlayerReady) return content;
    return isInteractiveListPage(state.nav.current.page) ? content + 1 : 1;
}

bool appFocusIsFooter(const AppState &state)
{
    return state.nav.current.page != ScreenPage::Home &&
           (isOrdinaryPage(state.nav.current.page) || isDebtAssetScreen(state.nav.current.page)) &&
           state.nav.current.focus == appPageContentCount(state);
}

bool appInlineFieldEditing(const AppState &state, InlineEditField field)
{
    return field != InlineEditField::None && state.inlineEditField == field;
}

bool appTradeReceiverLocked(const AppState &state)
{
    return state.tradeEntryMode == TradeEntryMode::PlayerLocked ||
           state.tradeEntryMode == TradeEntryMode::CounterLocked;
}

bool appTradeSupported()
{
    return kTradeBackendAvailable;
}

bool appTradeOfferWaiting(const AppState &state)
{
    return state.tradeOffer.active &&
           (state.tradeOffer.flags & (kTradeFlagSelfLastEdited |
                                      kTradeFlagSelfConfirmed)) != 0;
}

uint8_t appTradeOfferActionCount(const AppState &state)
{
    if (!state.tradeOffer.active) return 0;
    if ((state.tradeOffer.flags & kTradeFlagSelfLastEdited) != 0) return 1;
    if ((state.tradeOffer.flags & kTradeFlagSelfConfirmed) != 0) return 0;
    return 3;
}

uint8_t appTradeReceiverCandidateCount(const AppState &state)
{
    return tradeReceiverCandidateCount(state);
}

uint8_t appTradeReceiverCandidateAt(const AppState &state, uint8_t candidateIndex)
{
    return tradeReceiverCandidateAt(state, candidateIndex);
}

void appHandleUiEvent(AppState &state, const UiEvent &event, uint32_t nowMs)
{
    syncLegacyState(state);
    if (acknowledgeActivityPresentation(state)) touchRevision(state);
    if (state.nav.current.page == ScreenPage::Home &&
        state.endTurnPresentation != EndTurnPresentationPhase::None) return;
    switch (event.kind) {
        case UiEventKind::ActivateFocused:
            activate(state, nowMs);
            return;
        case UiEventKind::SelectHomeAction:
            if (state.nav.current.page != ScreenPage::Home) return;
            if (event.value < 0 || event.value >= contentCount(state, ScreenPage::Home)) return;
            {
                const uint8_t actionFocus = static_cast<uint8_t>(event.value);
                if (state.nav.current.focus != actionFocus) {
                    setFocus(state, actionFocus);
                    return;
                }
            }
            activate(state, nowMs);
            return;
        case UiEventKind::SelectListItem:
            selectListItem(state, event.value, nowMs);
            return;
        case UiEventKind::SelectFooter:
            selectFooter(state, nowMs);
            return;
        case UiEventKind::Back:
            if (state.modal.kind != ModalKind::None && state.modal.cancelAllowed &&
                !state.modal.submitting) {
                dismissModal(state);
                return;
            }
            back(state);
            return;
        case UiEventKind::HoldDown:
            beginPress(state, nowMs);
            return;
        case UiEventKind::HoldUp:
            endPress(state, nowMs);
            return;
        case UiEventKind::ListPrevious:
            moveFocus(state, -1);
            return;
        case UiEventKind::ListNext:
            moveFocus(state, 1);
            return;
    }
}

void appHandleInput(AppState &state, const InputEvent &event, uint32_t nowMs)
{
    syncLegacyState(state);
    if (acknowledgeActivityPresentation(state)) touchRevision(state);
    if (state.tradeReceiverPickerOpen) {
        if (event.kind == InputKind::Rotate) {
            moveFocus(state, event.delta);
        } else if (event.kind == InputKind::ButtonDown) {
            state.buttonHeld = true;
            state.buttonDownMs = event.timestampMs;
        } else if (event.kind == InputKind::ButtonUp && state.buttonHeld) {
            const uint32_t heldMs = event.timestampMs - state.buttonDownMs;
            state.buttonHeld = false;
            if (heldMs <= kShortPressMaxMs) commitTradeReceiverPicker(state);
            else closeTradeReceiverPicker(state);
        }
        return;
    }
    if (state.nav.current.page == ScreenPage::Home &&
        state.endTurnPresentation != EndTurnPresentationPhase::None) return;
    if (event.kind == InputKind::ButtonDown) {
        beginPress(state, event.timestampMs);
        return;
    }
    if (event.kind == InputKind::ButtonUp) {
        endPress(state, event.timestampMs);
        return;
    }
    if (event.kind != InputKind::Rotate) return;
    moveFocus(state, event.delta);
}

bool appPollCommand(AppState &state, TransportCommand &command)
{
    if (state.commandCount == 0) return false;
    command = state.commandQueue[state.commandHead];
    state.commandHead = static_cast<uint8_t>((state.commandHead + 1) % 4);
    --state.commandCount;
    return true;
}

void appHandleTransportEvent(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    const bool authoritativeProjection =
        event.kind == TransportEventKind::StateSnapshotApplied ||
        event.kind == TransportEventKind::AuthoritySnapshotApplied ||
        event.kind == TransportEventKind::RosterSnapshotApplied ||
        event.kind == TransportEventKind::IdentitySnapshotReceived;
    const bool roomChanged = authoritativeProjection && event.roomId != 0 &&
        state.authorityRoomId != 0 && state.authorityRoomId != event.roomId;
    if (authoritativeProjection && event.roomId != 0) {
        if (roomChanged) {
            clearAuctionRoomLifecycle(state);
            clearTradeRoomLifecycle(state);
            clearActivityRoomLifecycle(state);
            clearCardRoomLifecycle(state);
            clearExtraRollRoomLifecycle(state);
            clearIdentityRoomLifecycle(state);
            state.landingEventAcknowledged = false;
        }
        state.authorityRoomId = event.roomId;
    }
    const bool preserveRollPresentation =
        resyncContinuesCurrentRoll(state, event, roomChanged);
    const bool preserveActivityNavigation =
        event.kind == TransportEventKind::StateSnapshotApplied && event.resync &&
        !roomChanged && state.nav.current.page == ScreenPage::Activity;
    const bool preserveIdentityNavigation =
        !roomChanged && state.identity.phase != IdentityClientPhase::Inactive &&
        isIdentityPage(state.nav.current.page);
    if (authoritativeProjection && !roomChanged && event.stateVersion != 0 &&
        event.stateVersion < state.stateVersion) return;
    if (auctionProjectionIsOlder(state, event)) return;
    const bool authoritativeResync = authoritativeProjection && event.resync;
    const bool playerDetailResponse = event.kind == TransportEventKind::PlayerDetailReceived;
    const bool debtMortgageCompletion = isDebtMortgageCompletion(state, event);
    const bool voluntaryMortgageCompletion = isVoluntaryMortgageCompletion(state, event);
    const bool genericModalCompletion = isGenericModalCompletion(state, event);
    const bool forcedPaymentCompletion = isForcedPaymentCompletion(state, event);
    const bool matchingBankruptcyResolution = isMatchingBankruptcyResolution(state, event);
    const bool matchingActiveModalRequest = isActiveModalRequest(state, event.requestId);
    if (authoritativeProjection && !authoritativeResync &&
        (state.debt.bankruptcyPending || state.debt.bankruptcyResolved)) return;
    if (!authoritativeProjection) {
        if (state.debt.bankruptcyResolved) return;
        if (state.debt.bankruptcyPending && !matchingBankruptcyResolution) return;
        if (event.kind == TransportEventKind::MortgageBatchCompleted &&
            !debtMortgageCompletion && !voluntaryMortgageCompletion) return;
        if (event.kind == TransportEventKind::PaymentCompleted && !forcedPaymentCompletion) return;
        if (event.kind == TransportEventKind::BankruptcyResolved && !matchingBankruptcyResolution) return;
        if (event.kind == TransportEventKind::TradeResponseReceived &&
            event.stateVersion != 0 && event.stateVersion < state.stateVersion) {
            const bool matchingTradeModal = isActiveModalRequest(state, event.requestId);
            clearPendingRequest(state, event.requestId);
            if (matchingTradeModal) {
                dismissModal(state);
                showToast(state, "OFFER CHANGED - REVIEW AGAIN", nowMs);
            }
            return;
        }
        if (!playerDetailResponse && event.stateVersion != 0 &&
            event.stateVersion < state.stateVersion) return;
    }
    if (event.stateVersion != 0 &&
        (roomChanged || authoritativeResync || event.stateVersion > state.stateVersion)) {
        state.stateVersion = event.stateVersion;
    }
    if (transportInterruptsInlineEditing(state, event) && clearInlineEditing(state)) {
        touchRevision(state);
    }

    const bool matchesPayNow = event.requestId != 0 &&
                              event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)];
    const bool matchesRoll = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::RollRequest)];
    const bool matchesMortgageRequest = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
    const bool matchesAuctionPass = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::AuctionPassRequest)];
    const bool matchesAuctionReady = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::AuctionReadyRequest)];
    const bool matchesPlayerDetail = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::PlayerDetailRequest)];
    const bool matchesEndTurn = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::EndTurnRequest)];
    const bool matchesCardContinue = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[
            commandIndex(TransportCommandKind::CardContinueRequest)];
    bool matchesAnyPendingRequest = false;
    if (event.requestId != 0) {
        for (uint32_t pendingRequestId : state.pendingRequestIds) {
            if (event.requestId == pendingRequestId) {
                matchesAnyPendingRequest = true;
                break;
            }
        }
    }
    if (event.kind == TransportEventKind::CommandCompleted &&
        !genericModalCompletion && !matchesAnyPendingRequest) return;
    if (event.kind == TransportEventKind::CommandRejected && !matchesAnyPendingRequest) return;

    switch (event.kind) {
        case TransportEventKind::None:
            return;
        case TransportEventKind::ConnectionLost:
            state.authorityOnline = false;
            state.tradeReceiverPickerOpen = false;
            state.commandHead = 0;
            state.commandTail = 0;
            state.commandCount = 0;
            clearStaleSubmissions(state);
            clearPendingKind(state, TransportCommandKind::PlayerDetailRequest);
            state.playerDetail.loadState = PlayerDetailLoadState::Failed;
            state.playerDetail.requestId = 0;
            state.auctionReadyAttemptGeneration = 0;
            state.auctionReadyAttemptAssetIndex = 0xFF;
            clearEndTurnPresentation(state);
            if (state.extraRollPresentation == ExtraRollPresentationPhase::Pending ||
                state.extraRollPresentation == ExtraRollPresentationPhase::Reward) {
                state.extraRollPresentation = ExtraRollPresentationPhase::Ready;
                state.extraRollRewardStartedMs = 0;
                state.extraRollRewardUntilMs = 0;
                if (state.nav.current.page == ScreenPage::ExtraRollReward) resetHome(state, 0);
            }
            showToast(state, "CONNECTION LOST - RETRYING", nowMs);
            return;
        case TransportEventKind::StateSnapshotApplied:
            if (event.resync) {
                state.commandHead = 0;
                state.commandTail = 0;
                state.commandCount = 0;
                clearStaleSubmissions(state);
                dismissModal(state);
                state.debt.transactionId = 0;
                state.debt.amountDue = 0;
                state.debt.cashBefore = 0;
                state.debt.selectedMask = 0;
                state.debt.eligibleMask = 0;
                state.debt.submittedMortgageRequestId = 0;
                state.debt.submittedMortgageMask = 0;
                state.debt.bankruptcyPending = false;
                state.debt.bankruptcyResolved = false;
                state.playerDetail = PlayerDetailState{};
                state.auctionReadyAttemptGeneration = 0;
                state.auctionReadyAttemptAssetIndex = 0xFF;
                if (!preserveRollPresentation && !preserveActivityNavigation &&
                    !preserveIdentityNavigation) {
                    state.nav.current.page = ScreenPage::Home;
                    state.nav.current.focus = 0;
                    state.nav.current.listAnchor = 0;
                    state.nav.depth = 0;
                }
                state.inlineEditField = InlineEditField::None;
                state.tradeReceiverPickerOpen = false;
                if (!preserveRollPresentation) {
                    state.rollAnimating = false;
                    state.rollResolved = false;
                    state.rollRevealPresented = false;
                    state.rollPresentationComplete = false;
                    state.rollFailed = false;
                    state.moveArrivalPending = false;
                    state.moveArrivalConfirmed = false;
                }
                clearCardPresentationState(state);
                clearEndTurnPresentation(state);
                if (!preserveRollPresentation) {
                    state.rollRevealMs = 0;
                    state.rollFailedUntilMs = 0;
                    state.arrivalContinueAtMs = 0;
                    state.rollOrigin = 0;
                    state.rollTarget = 0xFF;
                    state.dieA = 0;
                    state.dieB = 0;
                    state.rolledSteps = 0;
                }
                syncLegacyState(state);
            }
            applyAuthoritySnapshot(state, event, nowMs);
            state.authorityOnline = true;
            touchRevision(state);
            return;
        case TransportEventKind::IdentitySnapshotReceived:
            applyIdentitySnapshot(state, event, nowMs);
            state.authorityOnline = true;
            touchRevision(state);
            return;
        case TransportEventKind::AuthoritySnapshotApplied:
            applyFullAuthorityProjection(state, event, nowMs);
            state.authorityOnline = true;
            touchRevision(state);
            return;
        case TransportEventKind::RosterSnapshotApplied:
            applyRosterProjection(state, event);
            touchRevision(state);
            return;
        case TransportEventKind::GameEventReceived:
            applyGameEventPresentation(state, event, nowMs);
            return;
        case TransportEventKind::PlayerCardDrawn:
        case TransportEventKind::PlayerCardEffectApplied:
            applyPlayerCardPresentation(state, event, nowMs);
            return;
        case TransportEventKind::PlayerDetailReceived:
            if (!matchesPlayerDetail) return;
            if (event.detailPlayerId != static_cast<uint8_t>(state.selectedPlayer + 1) ||
                event.detailPlayerId != state.playerDetail.playerId ||
                event.requestId != state.playerDetail.requestId || event.playerDetail == nullptr) {
                return;
            }
            clearPendingRequest(state, event.requestId);
            state.playerDetail.loadState = PlayerDetailLoadState::Ready;
            state.playerDetail.requestId = 0;
            state.playerDetail.stateVersion = event.stateVersion;
            state.playerDetail.cash = event.detailCash;
            state.playerDetail.position = event.detailPosition;
            state.playerDetail.assetCount = event.detailAssetCount > kPlayerDetailAssetCapacity
                ? kPlayerDetailAssetCapacity : event.detailAssetCount;
            state.playerDetail.financialRecordCount =
                event.financialRecordCount > kPlayerFinanceCapacity
                    ? kPlayerFinanceCapacity : event.financialRecordCount;
            memcpy(state.playerDetail.assets, event.playerDetail->assets,
                   sizeof(state.playerDetail.assets));
            memcpy(state.playerDetail.financialRecords, event.playerDetail->financialRecords,
                   sizeof(state.playerDetail.financialRecords));
            state.nav.current.focus = 0;
            syncLegacyState(state);
            touchRevision(state);
            return;
        case TransportEventKind::TradeResponseReceived:
            {
                const bool matchingRequest = event.requestId != 0 &&
                    matchingActiveModalRequest;
                clearPendingRequest(state, event.requestId);
                if (matchingRequest) dismissModal(state);

                const bool activeStatus =
                    event.tradeStatus == TransportTradeStatus::Offered ||
                    event.tradeStatus == TransportTradeStatus::Countered;
                if (activeStatus && event.tradeId != 0 &&
                    event.tradeCounterpartyId != 0) {
                    const bool duplicateOffer = event.tradeResult == TransportTradeResult::Ok &&
                        state.nav.current.page == ScreenPage::TradeOffer &&
                        state.tradeOffer.active &&
                        state.tradeOffer.tradeId == event.tradeId &&
                        state.tradeOffer.revision == event.tradeRevision &&
                        state.tradeOffer.status == event.tradeStatus &&
                        state.tradeOffer.flags == event.tradeFlags &&
                        state.tradeOffer.counterpartyId == event.tradeCounterpartyId &&
                        state.tradeOffer.selfAssetMask == event.assetMask &&
                        state.tradeOffer.counterpartyAssetMask == event.counterpartyAssetMask &&
                        state.tradeOffer.selfGivesCash == event.tradeSelfGivesCash &&
                        state.tradeOffer.counterpartyGivesCash ==
                            event.tradeCounterpartyGivesCash;
                    if (duplicateOffer) return;
                    state.tradeOffer.active = true;
                    state.tradeOffer.tradeId = event.tradeId;
                    state.tradeOffer.revision = event.tradeRevision;
                    state.tradeOffer.expiresAtMs = nowMs + event.tradeExpiresInMs;
                    state.tradeOffer.selfAssetMask = event.assetMask;
                    state.tradeOffer.counterpartyAssetMask = event.counterpartyAssetMask;
                    state.tradeOffer.selfGivesCash = event.tradeSelfGivesCash;
                    state.tradeOffer.counterpartyGivesCash =
                        event.tradeCounterpartyGivesCash;
                    state.tradeOffer.status = event.tradeStatus;
                    state.tradeOffer.flags = event.tradeFlags;
                    state.tradeOffer.counterpartyId = event.tradeCounterpartyId;
                    state.tradeOffer.confirmedMask = event.tradeConfirmedMask;
                    state.tradeOffer.originatorId = event.tradeOriginatorId;
                    presentTradeOfferPage(state);
                }

                if (event.tradeResult != TransportTradeResult::Ok) {
                    if (event.tradeResult == TransportTradeResult::NoActiveTrade) {
                        const bool tradePage = state.nav.current.page == ScreenPage::TradeOffer ||
                            state.nav.current.page == ScreenPage::Trade ||
                            state.nav.current.page == ScreenPage::TradeAssetSelect;
                        const bool tradeModal = state.modal.kind == ModalKind::TradeCreate ||
                            state.modal.kind == ModalKind::TradeAction;
                        const bool hadTrade = state.tradeOffer.active ||
                            tradePage || tradeModal || matchingRequest ||
                            (state.pendingCommandMask & tradePendingMask()) != 0 ||
                            state.tradeEntryMode == TradeEntryMode::CounterLocked;
                        if (!hadTrade) return;
                        if (tradeModal) dismissModal(state);
                        clearPendingTradeRequests(state);
                        clearTradeRoomLifecycle(state);
                        if (tradePage) resetHome(state);
                        showToast(state, "TRADE CLOSED", nowMs);
                    } else if (event.tradeResult == TransportTradeResult::RevisionStale ||
                               event.tradeResult == TransportTradeResult::StateVersionStale) {
                        showToast(state, "OFFER CHANGED - REVIEW AGAIN", nowMs);
                    } else if (event.tradeResult == TransportTradeResult::NotEnoughCash) {
                        showToast(state, "NOT ENOUGH CASH", nowMs);
                    } else if (event.tradeResult == TransportTradeResult::AssetUnavailable) {
                        showToast(state, "ASSET NO LONGER AVAILABLE", nowMs);
                    } else if (event.tradeResult == TransportTradeResult::ParticipantBusy) {
                        showToast(state, "PLAYER ALREADY TRADING", nowMs);
                    } else {
                        showToast(state, "TRADE REQUEST REJECTED", nowMs);
                    }
                    touchRevision(state);
                    return;
                }

                const bool terminal = (event.tradeFlags & kTradeFlagTerminal) != 0 ||
                    event.tradeStatus == TransportTradeStatus::Settled ||
                    event.tradeStatus == TransportTradeStatus::Rejected ||
                    event.tradeStatus == TransportTradeStatus::Cancelled ||
                    event.tradeStatus == TransportTradeStatus::Expired ||
                    event.tradeStatus == TransportTradeStatus::Invalidated;
                if (terminal) {
                    const TransportTradeStatus terminalStatus = event.tradeStatus;
                    const bool tradePage = state.nav.current.page == ScreenPage::TradeOffer ||
                        state.nav.current.page == ScreenPage::Trade ||
                        state.nav.current.page == ScreenPage::TradeAssetSelect;
                    const bool hadTrade = state.tradeOffer.active || matchingRequest ||
                        tradePage ||
                        state.tradeEntryMode == TradeEntryMode::CounterLocked;
                    if (state.modal.kind == ModalKind::TradeCreate ||
                        state.modal.kind == ModalKind::TradeAction) {
                        dismissModal(state);
                    }
                    clearPendingTradeRequests(state);
                    clearTradeRoomLifecycle(state);
                    if (tradePage) resetHome(state);
                    const char *message = "TRADE CLOSED";
                    if (terminalStatus == TransportTradeStatus::Settled) {
                        message = "TRADE COMPLETE";
                    } else if (terminalStatus == TransportTradeStatus::Rejected) {
                        message = "TRADE REJECTED";
                    } else if (terminalStatus == TransportTradeStatus::Cancelled) {
                        message = "TRADE CANCELLED";
                    } else if (terminalStatus == TransportTradeStatus::Expired) {
                        message = "TRADE EXPIRED";
                    } else if (terminalStatus == TransportTradeStatus::Invalidated) {
                        message = "TRADE NO LONGER VALID";
                    }
                    if (hadTrade) showToast(state, message, nowMs);
                }
                touchRevision(state);
            }
            return;
        case TransportEventKind::RollResult:
            clearPendingRequest(state, event.requestId);
            if (validDice(event.dieA, event.dieB)) {
                markRollResolved(state, event.dieA, event.dieB, nowMs);
            }
            state.position = event.playerPosition;
            touchRevision(state);
            return;
        case TransportEventKind::MoveGuidanceStarted:
            noteArrivalTarget(state, event.targetPosition);
            touchRevision(state);
            return;
        case TransportEventKind::RfidPositionConfirmed:
            clearPendingRequest(state, event.requestId);
            state.position = event.observedPosition != 0 ? event.observedPosition : event.targetPosition;
            noteArrivalTarget(state, event.targetPosition);
            state.moveArrivalConfirmed = true;
            if (state.nav.current.page == ScreenPage::MoveGuide &&
                state.arrivalContinueAtMs == 0) {
                state.arrivalContinueAtMs = nowMs + kArrivalConfirmedHoldMs;
            }
            touchRevision(state);
            return;
        case TransportEventKind::RfidPositionRejected:
            clearPendingRequest(state, event.requestId);
            return;
        case TransportEventKind::PaymentRequired:
            if (arrivalFlowLocked(state)) {
                state.debtAmount = event.amount;
                state.debt.amountDue = event.amount;
                state.debt.cashBefore = state.money;
                state.debt.transactionId = event.transactionId;
                return;
            }
            if (state.modal.kind == ModalKind::ForcedPayment &&
                state.modal.submitting &&
                (state.pendingCommandMask & commandMask(TransportCommandKind::PayNow)) != 0) {
                state.modal.amount = event.amount;
                state.modal.deadlineMs = event.deadlineMs;
                state.modal.counterparty = event.targetName;
                state.modal.purpose = "Authority payment";
                touchRevision(state);
                return;
            }
            showModal(state, ModalKind::ForcedPayment, false, "Payment required", event.targetName,
                      "Authority payment", event.amount, event.transactionId, event.deadlineMs);
            return;
        case TransportEventKind::PaymentCompleted:
            clearPendingKind(state, TransportCommandKind::PayNow);
            state.money = event.cash;
            dismissModal(state);
            touchRevision(state);
            return;
        case TransportEventKind::DebtResolutionRequired:
            {
                const bool refresh = state.nav.current.page == ScreenPage::DebtAssets &&
                                     event.transactionId == state.debt.transactionId;
                const uint32_t previousSelection = state.debt.selectedMask;
                if (!refresh) state.debt = DebtState{};
                state.debt.transactionId = event.transactionId;
                state.debt.amountDue = event.amount;
                state.debt.cashBefore = event.cash;
                state.availableActions = event.availableActions;
                state.debt.eligibleMask = (event.availableActions & kActionMortgage) != 0
                    ? event.assetMask : 0;
                state.debt.selectedMask = refresh ? previousSelection & legalDebtAssetMask(state) : 0;
                if (arrivalFlowLocked(state)) return;
                if ((event.availableActions & kActionDeclareBankruptcy) != 0) {
                    dismissModal(state);
                    replaceForcedPage(state, ScreenPage::Bankruptcy);
                    return;
                }
                if (refresh && state.debt.selectedMask != previousSelection) {
                    showToast(state,
                              "\xE8\xB5\x84\xE4\xBA\xA7\xE7\x8A\xB6\xE6\x80\x81\xE5\xB7\xB2\xE5\x8F\x98\xE5\x8C\x96\xEF\xBC\x8C\xE8\xAF\xB7\xE9\x87\x8D\xE6\x96\xB0\xE9\x80\x89\xE6\x8B\xA9",
                              nowMs);
                }
                dismissModal(state);
                replaceForcedPage(state, ScreenPage::DebtAssets);
            }
            return;
        case TransportEventKind::MortgageBatchCompleted:
            if (debtMortgageCompletion) {
                clearPendingKind(state, TransportCommandKind::MortgageBatchRequest);
                state.debt.submittedMortgageRequestId = 0;
                state.debt.submittedMortgageMask = 0;
                state.money = event.cash;
                showModal(state, ModalKind::ForcedPayment, false, "Payment required", "City Bank",
                          "Authority payment", state.debt.amountDue, state.debt.transactionId, nowMs + 10000);
            } else if (voluntaryMortgageCompletion) {
                clearPendingKind(state, TransportCommandKind::MortgageBatchRequest);
                state.money = event.cash;
                dismissModal(state);
                showToast(state, "Mortgage complete", nowMs);
            }
            touchRevision(state);
            return;
        case TransportEventKind::BankruptcyResolved:
            state.money = event.cash;
            state.debt.bankruptcyPending = false;
            state.debt.bankruptcyResolved = true;
            enterBankruptcy(state);
            touchRevision(state);
            return;
        case TransportEventKind::CommandCompleted:
            {
                const ModalKind completedKind = state.modal.kind;
                clearPendingRequest(state, event.requestId);
                if (matchesEndTurn) {
                    state.endTurnAccepted = true;
                    touchRevision(state);
                    return;
                }
                if (matchesCardContinue) {
                    state.cardPresentationAcknowledged = true;
                    if (state.nav.current.page == ScreenPage::CardReveal) {
                        state.cardPresentation = CardPresentationPhase::Settling;
                    }
                    touchRevision(state);
                    return;
                }
                if (genericModalCompletion) {
                    dismissModal(state);
                    const char *message = "Trade sent";
                    if (completedKind == ModalKind::CollectRent) message = "Rent claimed";
                    else if (completedKind == ModalKind::VoluntaryUnmortgage) {
                        message = "Property redeemed";
                    } else if (completedKind == ModalKind::DebtSellBuildingConfirm) {
                        message = "BUILDING SOLD";
                    }
                    showToast(state, message, nowMs);
                    if (completedKind == ModalKind::DebtSellBuildingConfirm) {
                        presentDebtAuthorityState(state, nowMs);
                    }
                } else if (!matchesAuctionReady) {
                    showToast(state, "ACTION COMPLETE", nowMs);
                }
            }
            return;
        case TransportEventKind::CommandRejected:
            clearPendingRequest(state, event.requestId);
            if (matchesEndTurn) {
                clearEndTurnPresentation(state);
                state.nav.current.focus = 0;
                syncLegacyState(state);
                showToast(state, "END TURN FAILED - RETRY", nowMs);
                return;
            }
            if (matchesCardContinue) {
                state.cardPresentationAcknowledged = false;
                state.pendingCardFlags = 0x03u;
                if (state.cardResultValid) {
                    state.cardPresentation = CardPresentationPhase::Revealed;
                    replaceForcedPage(state, ScreenPage::CardReveal);
                }
                showToast(state, "CARD CONTINUE FAILED - RETRY", nowMs);
                return;
            }
            if (matchesPlayerDetail) {
                state.playerDetail.loadState = PlayerDetailLoadState::Failed;
                state.playerDetail.requestId = 0;
                state.nav.current.focus = 0;
                syncLegacyState(state);
                showToast(state, "DETAIL REQUEST FAILED", nowMs);
                return;
            }
            if (matchesRoll && state.nav.current.page == ScreenPage::DiceStage &&
                !state.rollResolved) {
                state.rollAnimating = false;
                state.rollFailed = true;
                state.rollFailedUntilMs = nowMs + kRollFailureHoldMs;
                state.rollRevealMs = 0;
                touchRevision(state);
            }
            if (matchesAuctionPass) state.auctionPassed = false;
            if (matchesPayNow && event.error == TransportError::InsufficientCash &&
                state.modal.kind == ModalKind::ForcedPayment) {
                state.modal.submitting = false;
                state.modal.insufficient = true;
                state.modal.focus = ModalFocus::ResolveAssets;
                state.modal.deadlineMs = 0;
                touchRevision(state);
                return;
            }
            if (matchesMortgageRequest && state.modal.kind == ModalKind::DebtMortgageConfirm) {
                state.debt.submittedMortgageRequestId = 0;
                state.debt.submittedMortgageMask = 0;
            }
            if (matchingActiveModalRequest && state.modal.submitting) {
                state.modal.submitting = false;
                touchRevision(state);
            }
            showToast(state, event.error == TransportError::StaleState
                                 ? "SERVER TIMEOUT - RETRY"
                                 : "ACTION NOT AVAILABLE",
                      nowMs);
            return;
    }
}

void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs)
{
    if (state.tradeReceiverPickerOpen) {
        const uint16_t rawPicker = static_cast<uint16_t>(action);
        const uint16_t firstPicker = static_cast<uint16_t>(TouchAction::TradeReceiverOption0);
        if (rawPicker >= firstPicker && rawPicker <=
                static_cast<uint16_t>(TouchAction::TradeReceiverOption4)) {
            const uint8_t candidate = static_cast<uint8_t>(rawPicker - firstPicker);
            if (candidate < tradeReceiverCandidateCount(state)) {
                state.tradeReceiverPickerIndex = candidate;
                commitTradeReceiverPicker(state);
            }
            return;
        }
        if (action == TouchAction::TradeReceiverCancel || action == TouchAction::Back) {
            closeTradeReceiverPicker(state);
        }
        return;
    }
    if (action == TouchAction::ActivityOpen) {
        if (state.modal.kind == ModalKind::None && state.authorityOnline) openActivity(state);
        return;
    }
    if (action == TouchAction::PressDown) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldDown, 0}, nowMs); return; }
    if (action == TouchAction::PressUp) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldUp, 0}, nowMs); return; }
    if (action == TouchAction::Back) {
        appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        return;
    }
    if (state.modal.kind != ModalKind::None) return;

    const uint16_t raw = static_cast<uint16_t>(action);
    if (raw >= static_cast<uint16_t>(TouchAction::ListItem0) &&
        raw < static_cast<uint16_t>(TouchAction::ListItem0) + kPlayerDetailAssetCapacity) {
        appHandleUiEvent(
            state,
            UiEvent{UiEventKind::SelectListItem,
                    static_cast<int16_t>(raw - static_cast<uint16_t>(TouchAction::ListItem0))},
            nowMs
        );
        return;
    }
    if (action == TouchAction::ListPrevious) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, nowMs);
        return;
    }
    if (action == TouchAction::ListNext) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, nowMs);
        return;
    }
    if (action == TouchAction::Back) {
        appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        return;
    }
    if (action == TouchAction::Footer) {
        const UiEventKind kind = state.nav.current.page == ScreenPage::DebtAssets
                                     ? UiEventKind::SelectFooter
                                     : UiEventKind::Back;
        appHandleUiEvent(state, UiEvent{kind, 0}, nowMs);
        return;
    }
    if (raw >= 100 && raw <= 104) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, static_cast<int16_t>(raw - 100)}, nowMs); return; }
    if (raw >= 200 && raw <= 204) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 200)}, nowMs); return; }
    if (raw >= 300 && raw <= 304) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 300)}, nowMs); return; }
    if (action == TouchAction::TradeBack) {
        appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        return;
    }
    if (action == TouchAction::TradeReceiver || action == TouchAction::TradeAssets ||
        action == TouchAction::TradeAmount || action == TouchAction::TradeConfirm) {
        if (action == TouchAction::TradeReceiver && appTradeReceiverLocked(state)) return;
        if (action == TouchAction::TradeReceiver) {
            setFocus(state, 0);
            openTradeReceiverPicker(state);
            return;
        }
        int16_t focus = 0;
        if (action == TouchAction::TradeAssets) focus = tradeAssetFocus(state);
        else if (action == TouchAction::TradeAmount) focus = appTradeReceiverLocked(state) ? 1 : 2;
        else if (action == TouchAction::TradeConfirm) focus = tradeSubmitFocus(state);
        appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, focus}, nowMs);
        return;
    }
    if (raw >= 500 && raw <= 501) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 500)}, nowMs); return; }
    if (raw >= 600 && raw <= 606) {
        if (action == TouchAction::DemoBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        else appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 600)}, nowMs);
        return;
    }
    if (raw >= static_cast<uint16_t>(TouchAction::IdentityRow0) &&
        raw <= static_cast<uint16_t>(TouchAction::IdentityConfirm)) {
        const int16_t focus = static_cast<int16_t>(
            raw - static_cast<uint16_t>(TouchAction::IdentityRow0));
        setFocus(state, static_cast<uint8_t>(focus));
        activate(state, nowMs);
        return;
    }
    if (action == TouchAction::NameEdit || action == TouchAction::NameConfirm ||
        action == TouchAction::NameBack || action == TouchAction::NameDelete ||
        action == TouchAction::HandwritingConfirm) {
        int16_t focus = 0;
        if (action == TouchAction::NameConfirm) focus = 1;
        else if (action == TouchAction::NameBack) focus = 2;
        else if (action == TouchAction::HandwritingConfirm) focus = 1;
        setFocus(state, static_cast<uint8_t>(focus));
        activate(state, nowMs);
        return;
    }
    if (action == TouchAction::DetailPrimary) {
        if (state.nav.current.page == ScreenPage::Home &&
            (state.homePhase == HomePhase::MyTurn ||
             state.homePhase == HomePhase::MyTurnEnd)) {
            setFocus(state, 0);
            activate(state, nowMs);
            return;
        }
        if (state.nav.current.page == ScreenPage::AssetDetail ||
            state.nav.current.page == ScreenPage::PlayerDetail) {
            setFocus(state, 0);
            activate(state, nowMs);
            return;
        }
        appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 0}, nowMs);
        return;
    }
    if (action == TouchAction::DetailSecondary &&
        (state.nav.current.page == ScreenPage::AssetDetail ||
         state.nav.current.page == ScreenPage::PlayerDetail ||
         state.nav.current.page == ScreenPage::TradeOffer ||
         state.nav.current.page == ScreenPage::Purchase ||
         state.nav.current.page == ScreenPage::Auction)) {
        setFocus(state, 1);
        activate(state, nowMs);
        return;
    }
    if (action == TouchAction::DetailTertiary &&
        (state.nav.current.page == ScreenPage::AssetDetail ||
         state.nav.current.page == ScreenPage::PlayerDetail ||
         state.nav.current.page == ScreenPage::TradeOffer)) {
        setFocus(state, 2);
        activate(state, nowMs);
        return;
    }
    if (action == TouchAction::DetailRefresh &&
        (state.nav.current.page == ScreenPage::AssetDetail ||
         state.nav.current.page == ScreenPage::PlayerDetail)) {
        setFocus(state, state.nav.current.page == ScreenPage::AssetDetail
                            ? 3
                            : (state.playerDetail.loadState == PlayerDetailLoadState::Ready ? 3 : 0));
        activate(state, nowMs);
        return;
    }
    if (action == TouchAction::DetailBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
}

void appTick(AppState &state, uint32_t nowMs)
{
    syncLegacyState(state);
    if (reconcileCompletedAvatarSubmission(state)) {
        touchRevision(state);
        return;
    }
    if (state.endTurnPresentation == EndTurnPresentationPhase::Exiting &&
        hasReachedDeadline(nowMs, state.endTurnPresentationStartedMs + kEndTurnExitMs)) {
        state.endTurnPresentation = EndTurnPresentationPhase::WaitingHold;
        state.nav.current.focus = 0;
        syncLegacyState(state);
        touchRevision(state);
    }
    if (state.endTurnPresentation == EndTurnPresentationPhase::WaitingHold &&
        state.endTurnAccepted &&
        hasReachedDeadline(nowMs, state.endTurnPresentationUntilMs)) {
        clearEndTurnPresentation(state);
        state.nav.current.focus = 0;
        syncLegacyState(state);
        touchRevision(state);
    }
    const uint32_t payNowMask = commandMask(TransportCommandKind::PayNow);
    if ((state.pendingCommandMask & payNowMask) != 0) {
        const bool orphanedUiRequest = state.modal.kind == ModalKind::ForcedPayment &&
                                       !state.modal.submitting;
        const uint32_t startedMs = state.pendingPayNowStartedMs;
        const bool timedOut = startedMs != 0 &&
                              hasReachedDeadline(nowMs,
                                  startedMs + kPendingActionRecoveryMs);
        if (orphanedUiRequest || timedOut) {
            clearPendingKind(state, TransportCommandKind::PayNow);
            if (state.modal.kind == ModalKind::ForcedPayment) {
                state.modal.submitting = false;
                state.modal.holding = false;
            }
            showToast(state, "PAYMENT READY - HOLD TO RETRY", nowMs);
        }
    }
    if (state.modal.kind != ModalKind::None && state.modal.holding &&
        !state.modal.submitting && !state.holdActionConsumed &&
        (state.modal.focus == ModalFocus::Confirm ||
         state.modal.focus == ModalFocus::ResolveAssets) &&
        appHoldProgressPermille(state, nowMs) >= 1000) {
        state.holdActionConsumed = true;
        state.modal.holding = false;
        submitModal(state, nowMs);
        touchRevision(state);
    }
    if (state.playerDetail.loadState == PlayerDetailLoadState::Loading &&
        hasReachedDeadline(nowMs, state.playerDetail.requestedAtMs +
                                  kPlayerDetailRequestTimeoutMs)) {
        clearPendingKind(state, TransportCommandKind::PlayerDetailRequest);
        state.playerDetail.loadState = PlayerDetailLoadState::Failed;
        state.playerDetail.requestId = 0;
        state.nav.current.focus = 0;
        syncLegacyState(state);
        showToast(state, "DETAIL REQUEST TIMEOUT", nowMs);
    }
    if (state.nav.current.page == ScreenPage::DiceStage) {
        if (state.rollResolved && !state.rollRevealPresented &&
            appDiceResultVisible(state, nowMs)) {
            state.rollRevealPresented = true;
            touchRevision(state);
        }
        if (state.rollFailed && state.rollFailedUntilMs != 0 &&
            hasReachedDeadline(nowMs, state.rollFailedUntilMs)) {
            clearRollFlow(state);
            resetHome(state, 0);
            return;
        }
        const bool noMoveRollFinished = state.rollTarget == 0xFF &&
            state.authorityPhase == AuthorityPhase::TurnEnd;
        const bool releaseHoldPaymentReady = state.rollTarget == 0xFF &&
            releaseHoldDebt(state);
        if (state.rollResolved &&
            (state.rollTarget != 0xFF || noMoveRollFinished || releaseHoldPaymentReady) &&
            state.rollRevealMs != 0 &&
            hasReachedDeadline(nowMs, state.rollRevealMs + kDiceResultHoldMs)) {
            finishDicePresentation(state, nowMs);
            return;
        }
    }
    if (state.nav.current.page == ScreenPage::ExtraRollReward &&
        state.extraRollPresentation == ExtraRollPresentationPhase::Reward &&
        state.extraRollRewardUntilMs != 0 &&
        hasReachedDeadline(nowMs, state.extraRollRewardUntilMs)) {
        finishExtraRollReward(state);
        return;
    }
    if (state.nav.current.page == ScreenPage::MoveGuide &&
        state.moveArrivalPending && state.moveArrivalConfirmed &&
        state.arrivalContinueAtMs != 0 &&
        state.authorityPhase != AuthorityPhase::AwaitMoveConfirm &&
        hasReachedDeadline(nowMs, state.arrivalContinueAtMs)) {
        presentPostArrivalState(state, nowMs);
        return;
    }
    if (state.nav.current.page == ScreenPage::CardReveal &&
        state.cardPresentation == CardPresentationPhase::Drawing &&
        state.cardResultValid && state.cardRevealAtMs != 0 &&
        hasReachedDeadline(nowMs, state.cardRevealAtMs)) {
        state.cardPresentation = CardPresentationPhase::Revealed;
        state.nav.current.focus = 0;
        syncLegacyState(state);
        touchRevision(state);
        return;
    }
    if (state.nav.current.page == ScreenPage::CardReveal &&
        state.cardPresentation == CardPresentationPhase::Settling &&
        state.cardEffectApplied && state.authorityPhase != AuthorityPhase::AwaitCard) {
        state.cardPresentationAcknowledged = true;
        presentPostArrivalState(state, nowMs);
        return;
    }
    if (state.nav.current.page == ScreenPage::Auction &&
        state.auctionPresentationUntilMs != 0 &&
        hasReachedDeadline(nowMs, state.auctionPresentationUntilMs)) {
        if (state.auctionPresentation == AuctionPresentationPhase::Intro) {
            state.auctionPresentation = AuctionPresentationPhase::Live;
            state.auctionPresentationUntilMs = 0;
            state.nav.current.focus = 0;
            syncLegacyState(state);
            touchRevision(state);
        } else if (state.auctionPresentation == AuctionPresentationPhase::Result) {
            finishAuctionResult(state, nowMs);
        }
        return;
    }
    if (state.activity.bannerSequence != 0 &&
        (hasReachedDeadline(nowMs, state.activity.bannerUntilMs) ||
         activityPresentationSuppressed(state))) {
        dismissActivityBanner(state);
        touchRevision(state);
    }
    if (startNextActivityBanner(state, nowMs)) touchRevision(state);
    if (state.toastUntilMs != 0 && hasReachedDeadline(nowMs, state.toastUntilMs)) {
        state.toastUntilMs = 0;
        touchRevision(state);
    }

    if (state.modal.kind != ModalKind::None && state.modal.deadlineMs != 0 &&
        hasReachedDeadline(nowMs, state.modal.deadlineMs)) {
        if (state.modal.kind == ModalKind::CollectRent) {
            if (state.modal.submitting) return;
            dismissModal(state);
            showToast(state, "收租超时，已放弃", nowMs);
            return;
        }
        if (state.modal.kind == ModalKind::ForcedPayment && !state.modal.submitting) {
            submitModal(state, nowMs);
        }
        return;
    }

    if (!state.buttonHeld || state.holdActionConsumed) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    if (state.modal.kind != ModalKind::None) {
        if (state.modal.holding && heldMs >= kConfirmHoldMs && !state.modal.submitting &&
            (state.modal.focus == ModalFocus::Confirm ||
             state.modal.focus == ModalFocus::ResolveAssets)) {
            state.holdActionConsumed = true;
            submitModal(state, nowMs);
        }
        return;
    }
    if (heldMs < kHomePressMs || !isOrdinaryPage(state.nav.current.page)) return;

    state.holdActionConsumed = true;
    if (state.nav.current.page == ScreenPage::Home) {
        openDemoLab(state);
    } else {
        resetHome(state);
    }
}

void appNotifyFramePresented(AppState &state, uint32_t nowMs)
{
    if (state.nav.current.page != ScreenPage::Auction ||
        (state.auctionPresentation != AuctionPresentationPhase::Intro &&
         state.auctionPresentation != AuctionPresentationPhase::OpeningWait &&
         state.auctionPresentation != AuctionPresentationPhase::Live) ||
        !auctionOpening(state) || state.auctionGeneration == 0 ||
        state.auctionAssetIndex == 0xFF || state.selfSeatId == 0 || state.selfSeatId > 6) {
        return;
    }
    const uint8_t ownBit = static_cast<uint8_t>(1u << (state.selfSeatId - 1));
    if ((state.auctionRequiredReadyMask & ownBit) == 0 ||
        (state.auctionReadyMask & ownBit) != 0 ||
        (state.auctionReadyAttemptGeneration == state.auctionGeneration &&
         state.auctionReadyAttemptAssetIndex == state.auctionAssetIndex)) {
        return;
    }
    if (!queueCommand(state, TransportCommandKind::AuctionReadyRequest, 0, nowMs,
                      0, 0, state.auctionAssetIndex,
                      static_cast<int32_t>(state.auctionGeneration))) {
        return;
    }
    state.auctionReadyAttemptGeneration = state.auctionGeneration;
    state.auctionReadyAttemptAssetIndex = state.auctionAssetIndex;
}

uint16_t appHoldProgressPermille(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None) return 0;
    if (state.modal.submitting) return 1000;
    if (!state.modal.holding) return 0;
    const uint32_t elapsed = nowMs - state.modal.holdStartMs;
    return static_cast<uint16_t>(elapsed >= kConfirmHoldMs ? 1000 : elapsed * 1000 / kConfirmHoldMs);
}

uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None || state.modal.deadlineMs == 0 ||
        hasReachedDeadline(nowMs, state.modal.deadlineMs)) return 0;
    return state.modal.deadlineMs - nowMs;
}
