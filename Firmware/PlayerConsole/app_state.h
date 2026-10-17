#pragma once

#include "app_types.h"
#include "grid_city_visual_catalog.h"

void appInit(AppState &state, uint32_t nowMs, bool waitForAuthority = false);
bool appAwaitingGame(const AppState &state);
void appHandleInput(AppState &state, const InputEvent &event, uint32_t nowMs);
void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs);
// Queue backlog can contain a physical release whose timestamp is already past.
// Defer only held-input actions until it is consumed; other deadlines still run.
void appTick(AppState &state, uint32_t nowMs, bool inputPending = false);
void appNotifyFramePresented(AppState &state, uint32_t nowMs);
bool appCanNavigateBack(const AppState &state);
uint8_t appPageContentCount(const AppState &state);
uint8_t appFocusCount(const AppState &state);
bool appFocusIsFooter(const AppState &state);
bool appInlineFieldEditing(const AppState &state, InlineEditField field);
bool appTradeReceiverLocked(const AppState &state);
uint8_t appTradeReceiverCandidateCount(const AppState &state);
uint8_t appTradeReceiverCandidateAt(const AppState &state, uint8_t candidateIndex);
bool appTradeSupported();
uint8_t appTradeOfferActionCount(const AppState &state);
bool appTradeOfferWaiting(const AppState &state);
uint8_t appAssetDetailActionCount(const AppState &state);
AssetDetailAction appAssetDetailActionAt(const AppState &state, uint8_t actionIndex);
bool appAssetDetailActionVisible(const AppState &state, AssetDetailAction action);
bool appAssetDetailActionEnabled(const AppState &state, AssetDetailAction action);
bool appAssetGroupProgress(const AppState &state, uint8_t assetIndex,
                           uint8_t &owned, uint8_t &total);
uint8_t appTradeAssetCount(const AppState &state);
uint8_t appTradeAssetIndex(const AppState &state, uint8_t row);
bool appTradeAssetEligible(const AppState &state, uint8_t assetIndex);
bool appTradeAssetSelected(const AppState &state, uint8_t assetIndex);
bool appAuctionOpening(const AppState &state);
bool appIdentityActive(const AppState &state);
bool appTagPairBlocksIdentity(const AppState &state);
bool appTagPairCanManage(const AppState &state);
bool appSelfBankrupt(const AppState &state);
bool appRequestTagPair(AppState &state, TransportTagPairOperation operation, uint32_t nowMs);
uint32_t appIdentityCountdownRemainingMs(const AppState &state, uint32_t nowMs);
uint8_t appIdentityReadyCount(const AppState &state);
void appUpdateAvatarPreloadProgress(AppState &state, uint8_t readyCount,
                                    uint8_t totalCount, bool complete);
bool appIdentityAppendCharacter(AppState &state, char character);
bool appIdentityDeleteCharacter(AppState &state);
void appIdentityFinishHandwriting(AppState &state);
void appHandleUiEvent(AppState &state, const UiEvent &event, uint32_t nowMs);
bool appPollCommand(AppState &state, TransportCommand &command);
void appHandleTransportEvent(AppState &state, const TransportEvent &event, uint32_t nowMs);
HomePhase appPresentedHomePhase(const AppState &state);
uint16_t appEndTurnExitProgressPermille(const AppState &state, uint32_t nowMs);
uint16_t appHoldProgressPermille(const AppState &state, uint32_t nowMs);
uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs);
bool appDebtAssetEligible(const AppState &state, uint8_t assetIndex);
bool appDebtAssetSelected(const AppState &state, uint8_t assetIndex);
bool appDebtBuildingSaleEligible(const AppState &state, uint8_t assetIndex);
int32_t appDebtBuildingSaleProceeds(const AppState &state, uint8_t assetIndex);
uint16_t appBankBuildingStock(const AppState &state);
uint16_t appBankLandmarkStock(const AppState &state);
bool appBuildingGroupSaleEligible(const AppState &state, uint8_t assetIndex);
int32_t appBuildingGroupSaleProceeds(const AppState &state, uint8_t assetIndex);
int32_t appDebtShortfall(const AppState &state);
int32_t appDebtSelectedProceeds(const AppState &state);
int32_t appDebtPostMortgageBalance(const AppState &state);
bool appDebtCanConfirm(const AppState &state);
uint8_t appVisibleAssetCount(const AppState &state);
uint8_t appVisibleAssetIndex(const AppState &state, uint8_t row);
const char *appPlayerDisplayName(const AppState &state, uint8_t playerIndex);
const char *appPlayerNameById(const AppState &state, uint8_t playerId);
uint8_t appActivityCount(const AppState &state);
const ActivityEntry *appActivityEntryAt(const AppState &state, uint8_t newestFirstIndex);
const ActivityEntry *appActivityBannerEntry(const AppState &state);
bool appActivityBannerVisible(const AppState &state, uint32_t nowMs);
const GridCityVisualDefinition *appAssetVisual(const AppState &state, uint8_t assetIndex);
const GridCityVisualDefinition *appTileVisual(const AppState &state, uint8_t position);
const char *appAssetDisplayName(const AppState &state, uint8_t assetIndex);
const char *appTileDisplayName(const AppState &state, uint8_t position);
int32_t appAssetValue(const AppState &state, uint8_t assetIndex);
int32_t appAssetRent(const AppState &state, uint8_t assetIndex);
int32_t appAssetMortgageValue(const AppState &state, uint8_t assetIndex);
int32_t appAssetBuildingCost(const AppState &state, uint8_t assetIndex);
uint8_t appAssetBuildingLevel(const AppState &state, uint8_t assetIndex);
// Artwork must not infer development from legacy demo defaults. 0xFF = unknown.
uint8_t appAssetArtworkLevel(const AppState &state, uint8_t assetIndex);
uint8_t appTileArtworkLevel(const AppState &state, uint8_t position);
bool appAssetMortgaged(const AppState &state, uint8_t assetIndex);
bool appDiceResultVisible(const AppState &state, uint32_t nowMs);
ArrivalKind appArrivalKind(const AppState &state);
const char *appArrivalDisplayName(const AppState &state);
uint8_t appArrivalAssetIndex(const AppState &state);
uint8_t appArrivalOwnerId(const AppState &state);
int32_t appArrivalAmount(const AppState &state);

// One action list drives Home input, count, rendering and focus reconciliation.
uint8_t appHomeActions(const AppState &state, HomeAction (&actions)[5]);
bool appHomeActionsEqual(const AppState &a, const AppState &b);
bool appBuildSnapshotCurrent(const AppState &state);
uint8_t appHomeBuildAsset(const AppState &state); // 0xFF unless current authority qualifies
uint8_t appBuildGroupCount(const AppState &state);
uint8_t appBuildGroupAssetAt(const AppState &state, uint8_t index);
bool appBuildContextCurrent(const AppState &state);
// Null means one level may be requested; text is a user-visible blocking reason.
const char *appAssetBuildReason(const AppState &state, uint8_t assetIndex);
int32_t appAssetNextRent(const AppState &state, uint8_t assetIndex);
