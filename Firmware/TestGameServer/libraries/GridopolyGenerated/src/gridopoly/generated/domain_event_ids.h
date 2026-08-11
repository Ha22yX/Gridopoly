// Generated from GameData/schemas/domain-events-v1.json. Do not edit.
#pragma once

#include <cstdint>

namespace gridopoly::generated {

enum class DomainEventType : std::uint16_t {
  ROOM_CREATED = 0x0001,
  ROOM_SETUP_STARTED = 0x0002,
  GAME_STARTED = 0x0003,
  ROOM_RESUMED = 0x0004,
  ROOM_CLOSED = 0x0005,
  SEAT_BOUND = 0x0010,
  SEAT_UNBOUND = 0x0011,
  PLAYER_CONNECTION_CHANGED = 0x0012,
  CONTROLLER_CHANGED = 0x0013,
  TURN_STARTED = 0x0100,
  DICE_ROLLED = 0x0101,
  MOVE_STARTED = 0x0102,
  POSITION_CONFIRMED = 0x0103,
  MOVE_COMPLETED = 0x0104,
  MOVE_MANUAL_FALLBACK_OPENED = 0x0105,
  PURCHASE_OFFERED = 0x0200,
  ASSET_PURCHASED = 0x0201,
  AUCTION_OPENED = 0x0202,
  AUCTION_BID_PLACED = 0x0203,
  AUCTION_CLOSED = 0x0204,
  AUCTION_PARTICIPANT_PASSED = 0x0205,
  RENT_CLAIMED = 0x0210,
  PAYMENT_CREATED = 0x0211,
  PAYMENT_COMPLETED = 0x0212,
  DEBT_STARTED = 0x0213,
  DEBT_RESOLVED = 0x0214,
  RENT_WAIVED = 0x0215,
  ASSET_MORTGAGED = 0x0220,
  ASSET_UNMORTGAGED = 0x0221,
  BUILDING_CHANGED = 0x0222,
  BUILDING_DEMAND_CHANGED = 0x0223,
  TRADE_CREATED = 0x0230,
  TRADE_UPDATED = 0x0231,
  TRADE_CLOSED = 0x0232,
  CARD_DRAWN = 0x0240,
  CARD_EFFECT_APPLIED = 0x0241,
  PLAYER_HELD = 0x0250,
  PLAYER_RELEASED = 0x0251,
  PLAYER_BANKRUPT = 0x0260,
  GAME_FINISHED = 0x0261,
  CARD_MULTI_PAYMENT_PROGRESS = 0x0270,
  BOARD_OBSERVATION_RECORDED = 0x0280,
  BOT_DECISION = 0x0290,
};

struct DomainEventFieldLayout {
  std::uint16_t field_id;
  const char* stable_name;
  const char* wire_type;
  bool required;
  bool critical;
  bool private_field;
  const char* cardinality;
  const char* range_min;
  const char* range_max;
  std::uint16_t max_count;
};

inline constexpr DomainEventFieldLayout kRoomCreatedFields[] = {
  {1, "room_id", "U64", true, true, false, "ONE", "1", "18446744073709551614", 0},
  {2, "room_seed", "U64", true, true, false, "ONE", "0", "18446744073709551615", 0},
  {3, "mode", "U8", true, true, false, "ONE", "1", "3", 0},
  {4, "disconnect_policy", "U8", true, true, false, "ONE", "1", "3", 0},
  {5, "map_id", "UTF8", true, true, false, "ONE", "", "", 32},
  {6, "map_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
  {7, "economy_id", "UTF8", true, true, false, "ONE", "", "", 40},
  {8, "content_manifest_hash", "BYTES", true, true, false, "ONE", "", "", 32},
};

inline constexpr DomainEventFieldLayout kRoomSetupStartedFields[] = {
  {1, "setup_rolls", "RECORD_LIST", true, true, false, "MANY", "", "", 30},
  {2, "turn_order", "RECORD_LIST", true, true, false, "MANY", "", "", 6},
};

inline constexpr DomainEventFieldLayout kGameStartedFields[] = {
  {1, "round_number", "U16", true, true, false, "ONE", "1", "65534", 0},
  {2, "active_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "turn_order", "RECORD_LIST", true, true, false, "MANY", "", "", 6},
};

inline constexpr DomainEventFieldLayout kRoomResumedFields[] = {
  {1, "recovery_generation", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "resumed_at_server_ms", "U32", true, true, false, "ONE", "0", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kRoomClosedFields[] = {
  {1, "old_room_id", "U64", true, true, false, "ONE", "1", "18446744073709551614", 0},
  {2, "resulting_lifecycle", "U8", true, true, false, "ONE", "2", "2", 0},
};

inline constexpr DomainEventFieldLayout kSeatBoundFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "seat_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "assignment_version", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {4, "presence_generation", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {5, "controller_type", "U8", true, true, false, "ONE", "0", "3", 0},
};

inline constexpr DomainEventFieldLayout kSeatUnboundFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "seat_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "assignment_version", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {4, "presence_generation", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {5, "reason", "U8", true, true, false, "ONE", "1", "4", 0},
};

inline constexpr DomainEventFieldLayout kPlayerConnectionChangedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "online", "BOOL", true, true, false, "ONE", "", "", 0},
  {3, "assignment_version", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {4, "presence_generation", "U32", true, true, false, "ONE", "1", "4294967294", 0},
};

inline constexpr DomainEventFieldLayout kControllerChangedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "old_controller_type", "U8", true, true, false, "ONE", "0", "3", 0},
  {3, "new_controller_type", "U8", true, true, false, "ONE", "0", "3", 0},
  {4, "control_epoch", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {5, "reason", "U8", true, true, false, "ONE", "1", "4", 0},
  {6, "bot_policy_id", "UTF8", true, true, false, "ONE", "", "", 32},
  {7, "bot_policy_revision", "U16", true, true, false, "ONE", "0", "65534", 0},
};

inline constexpr DomainEventFieldLayout kTurnStartedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "round_number", "U16", true, true, false, "ONE", "1", "65534", 0},
  {3, "turn_phase", "U8", true, true, false, "ONE", "0", "12", 0},
};

inline constexpr DomainEventFieldLayout kDiceRolledFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "roll_kind", "U8", true, true, false, "ONE", "0", "3", 0},
  {3, "die_a", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "die_b", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "total", "U8", true, true, false, "ONE", "2", "12", 0},
  {6, "is_double", "BOOL", true, true, false, "ONE", "", "", 0},
  {7, "roll_ordinal", "U8", true, true, false, "ONE", "1", "5", 0},
  {8, "origin_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {9, "computed_target", "U8", true, true, false, "ONE", "0", "255", 0},
  {10, "passed_start", "BOOL", true, true, false, "ONE", "", "", 0},
};

inline constexpr DomainEventFieldLayout kMoveStartedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "movement_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {3, "workflow_revision", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {4, "origin_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {5, "target_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {6, "manual_available_at_ms", "U32", true, true, false, "ONE", "0", "4294967295", 0},
  {7, "movement_deadline_ms", "U32", true, true, false, "ONE", "0", "4294967295", 0},
  {8, "path", "RECORD_LIST", true, true, false, "MANY", "", "", 40},
};

inline constexpr DomainEventFieldLayout kPositionConfirmedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "movement_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {3, "target_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {4, "observed_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {5, "confirmation_method", "U8", true, true, false, "ONE", "0", "2", 0},
  {6, "confirmed_at_ms", "U32", true, true, false, "ONE", "0", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kMoveCompletedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "origin_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {3, "final_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {4, "steps_moved", "U8", true, true, false, "ONE", "0", "40", 0},
  {5, "passed_start", "BOOL", true, true, false, "ONE", "", "", 0},
  {6, "start_award", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {7, "landing_tile_kind", "U8", true, true, false, "ONE", "0", "8", 0},
};

inline constexpr DomainEventFieldLayout kMoveManualFallbackOpenedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "movement_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {3, "target_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {4, "workflow_revision", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {5, "reason", "U8", true, true, false, "ONE", "1", "1", 0},
};

inline constexpr DomainEventFieldLayout kPurchaseOfferedFields[] = {
  {1, "offered_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "tile_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {4, "price", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {5, "offer_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
  {6, "deadline_ms", "U32", true, true, false, "ONE", "1", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kAssetPurchasedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "price", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {4, "cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "asset_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
};

inline constexpr DomainEventFieldLayout kAuctionOpenedFields[] = {
  {1, "auction_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "auction_kind", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "lot_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {4, "asset_id", "U16", true, true, false, "ONE", "0", "65535", 0},
  {5, "building_type", "U8", true, true, false, "ONE", "0", "2", 0},
  {6, "auction_version", "U16", true, true, false, "ONE", "1", "65534", 0},
  {7, "eligible_mask", "U8", true, true, false, "ONE", "1", "63", 0},
  {8, "minimum_next_bid", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {9, "deadline_ms", "U32", true, true, false, "ONE", "1", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kAuctionBidPlacedFields[] = {
  {1, "auction_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "auction_kind", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "lot_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {4, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "bid_amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "auction_version", "U16", true, true, false, "ONE", "1", "65534", 0},
  {7, "target_asset_id", "U16", true, true, false, "ONE", "0", "65535", 0},
};

inline constexpr DomainEventFieldLayout kAuctionClosedFields[] = {
  {1, "auction_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "auction_kind", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "lot_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {4, "status", "U8", true, true, false, "ONE", "2", "4", 0},
  {5, "winner_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {6, "final_bid", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {7, "target_asset_id", "U16", true, true, false, "ONE", "0", "65535", 0},
};

inline constexpr DomainEventFieldLayout kAuctionParticipantPassedFields[] = {
  {1, "auction_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "auction_kind", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "lot_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {4, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "auction_version", "U16", true, true, false, "ONE", "1", "65534", 0},
  {6, "accepted_order", "U16", true, true, false, "ONE", "1", "65534", 0},
  {7, "passed_mask", "U8", true, true, false, "ONE", "1", "63", 0},
};

inline constexpr DomainEventFieldLayout kRentClaimedFields[] = {
  {1, "rent_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "landlord_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {5, "quoted_amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "rent_basis", "U8", true, true, false, "ONE", "1", "3", 0},
  {7, "rent_multiplier_permille", "U16", true, true, false, "ONE", "1", "65535", 0},
  {8, "deadline_ms", "U32", true, true, false, "ONE", "1", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kPaymentCreatedFields[] = {
  {1, "payment_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {4, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "reason", "U8", true, true, false, "ONE", "1", "7", 0},
  {7, "source_asset_id", "U16", true, true, false, "ONE", "0", "65535", 0},
  {8, "deadline_ms", "U32", true, true, false, "ONE", "0", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kPaymentCompletedFields[] = {
  {1, "payment_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {4, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "payer_cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {7, "creditor_cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {8, "reason", "U8", true, true, false, "ONE", "1", "7", 0},
  {9, "allocations", "RECORD_LIST", true, true, false, "MANY", "", "", 6},
};

inline constexpr DomainEventFieldLayout kDebtStartedFields[] = {
  {1, "debt_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "debtor_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {4, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "amount_due", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "cash_available", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {7, "shortfall", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {8, "allowed_actions", "U16", true, true, false, "ONE", "1", "31", 0},
  {9, "eligible_assets", "RECORD_LIST", true, true, false, "MANY", "", "", 28},
};

inline constexpr DomainEventFieldLayout kDebtResolvedFields[] = {
  {1, "debt_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "debtor_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "outcome", "U8", true, true, false, "ONE", "1", "2", 0},
  {4, "amount_paid", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
};

inline constexpr DomainEventFieldLayout kRentWaivedFields[] = {
  {1, "rent_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "landlord_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {5, "quoted_amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "workflow_revision", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {7, "reason", "U8", true, true, false, "ONE", "1", "1", 0},
};

inline constexpr DomainEventFieldLayout kAssetMortgagedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "mortgage_value", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {4, "cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "asset_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
};

inline constexpr DomainEventFieldLayout kAssetUnmortgagedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "redeem_cost", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {4, "cash_after", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "asset_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
};

inline constexpr DomainEventFieldLayout kBuildingChangedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "old_level", "U8", true, true, false, "ONE", "0", "5", 0},
  {4, "new_level", "U8", true, true, false, "ONE", "0", "5", 0},
  {5, "cash_delta", "I32", true, true, false, "ONE", "-2147483648", "2147483647", 0},
  {6, "building_stock_after", "U8", true, true, false, "ONE", "0", "255", 0},
  {7, "landmark_stock_after", "U8", true, true, false, "ONE", "0", "255", 0},
  {8, "asset_revision", "U16", true, true, false, "ONE", "1", "65534", 0},
};

inline constexpr DomainEventFieldLayout kBuildingDemandChangedFields[] = {
  {1, "demand_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "workflow_revision", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {3, "stage", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "actor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "registered_mask", "U8", true, true, false, "ONE", "0", "63", 0},
  {6, "target_asset_id", "U16", true, true, false, "ONE", "0", "65535", 0},
  {7, "target_level", "U8", true, true, false, "ONE", "0", "5", 0},
  {8, "list_cost", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {9, "outcome", "U8", true, true, false, "ONE", "0", "4", 0},
  {10, "intents", "RECORD_LIST", false, true, false, "OPTIONAL", "", "", 6},
};

inline constexpr DomainEventFieldLayout kTradeCreatedFields[] = {
  {1, "trade_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "trade_version", "U16", true, true, false, "ONE", "1", "1", 0},
  {3, "proposer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "counterparty_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "proposer_gives_cash", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {6, "counterparty_gives_cash", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {7, "assets", "RECORD_LIST", true, true, false, "MANY", "", "", 28},
  {8, "cards", "RECORD_LIST", true, true, false, "MANY", "", "", 8},
  {9, "deadline_ms", "U32", true, true, false, "ONE", "1", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kTradeUpdatedFields[] = {
  {1, "trade_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "trade_version", "U16", true, true, false, "ONE", "2", "65534", 0},
  {3, "actor_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "proposer_gives_cash", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "counterparty_gives_cash", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {6, "assets", "RECORD_LIST", true, true, false, "MANY", "", "", 28},
  {7, "cards", "RECORD_LIST", true, true, false, "MANY", "", "", 8},
  {8, "deadline_ms", "U32", true, true, false, "ONE", "1", "4294967295", 0},
};

inline constexpr DomainEventFieldLayout kTradeClosedFields[] = {
  {1, "trade_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "trade_version", "U16", true, true, false, "ONE", "1", "65534", 0},
  {3, "status", "U8", true, true, false, "ONE", "3", "6", 0},
  {4, "proposer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "counterparty_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {6, "assets", "RECORD_LIST", true, true, false, "MANY", "", "", 28},
  {7, "cards", "RECORD_LIST", true, true, false, "MANY", "", "", 8},
};

inline constexpr DomainEventFieldLayout kCardDrawnFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "deck_id", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "card_instance_id", "U16", true, true, true, "ONE", "1", "65534", 0},
  {4, "card_catalog_id", "U16", true, true, true, "ONE", "1", "65534", 0},
  {5, "effect_id", "U16", true, true, true, "ONE", "1", "65534", 0},
  {6, "keepable", "BOOL", true, true, false, "ONE", "", "", 0},
};

inline constexpr DomainEventFieldLayout kCardEffectAppliedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "deck_id", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "card_instance_id", "U16", true, true, false, "ONE", "1", "65534", 0},
  {4, "effect_id", "U16", true, true, false, "ONE", "1", "65534", 0},
  {5, "amount", "I32", true, true, false, "ONE", "-2147483648", "2147483647", 0},
  {6, "target_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {7, "target_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {8, "outcome", "U8", true, true, false, "ONE", "1", "4", 0},
};

inline constexpr DomainEventFieldLayout kPlayerHeldFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "origin_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {3, "hold_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {4, "failed_hold_rolls", "U8", true, true, false, "ONE", "0", "2", 0},
  {5, "reason", "U8", true, true, false, "ONE", "1", "3", 0},
};

inline constexpr DomainEventFieldLayout kPlayerReleasedFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "release_method", "U8", true, true, false, "ONE", "1", "3", 0},
  {3, "card_instance_id", "U16", true, true, false, "ONE", "0", "65535", 0},
  {4, "amount_paid", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "failed_hold_rolls", "U8", true, true, false, "ONE", "0", "2", 0},
};

inline constexpr DomainEventFieldLayout kPlayerBankruptFields[] = {
  {1, "debtor_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {3, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {4, "cash_transferred", "I32", true, true, false, "ONE", "0", "2147483647", 0},
  {5, "eliminated_rank", "U8", true, true, false, "ONE", "1", "6", 0},
  {6, "transfers", "RECORD_LIST", true, true, false, "MANY", "", "", 28},
};

inline constexpr DomainEventFieldLayout kGameFinishedFields[] = {
  {1, "winner_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {2, "reason", "U8", true, true, false, "ONE", "1", "3", 0},
  {3, "ranking", "RECORD_LIST", true, true, false, "MANY", "", "", 6},
};

inline constexpr DomainEventFieldLayout kCardMultiPaymentProgressFields[] = {
  {1, "resolution_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "item_index", "U8", true, true, false, "ONE", "0", "5", 0},
  {3, "target_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {5, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {6, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {7, "amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {8, "old_status", "U8", true, true, false, "ONE", "0", "4", 0},
  {9, "new_status", "U8", true, true, false, "ONE", "1", "4", 0},
  {10, "child_payment_transaction_id", "U32", true, true, false, "ONE", "0", "4294967294", 0},
  {11, "next_pending_index", "U8", true, true, false, "ONE", "0", "255", 0},
};

inline constexpr DomainEventFieldLayout kBoardObservationRecordedFields[] = {
  {1, "observation_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {2, "stage", "U8", true, true, false, "ONE", "1", "2", 0},
  {3, "target_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {4, "target_workflow_revision", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {5, "observed_position", "U8", true, true, false, "ONE", "0", "255", 0},
  {6, "result", "U8", true, true, false, "ONE", "1", "4", 0},
  {7, "original_window_ms", "U32", true, true, false, "ONE", "0", "60000", 0},
  {8, "deadline_instance_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
  {9, "outcome", "U8", true, true, false, "ONE", "0", "5", 0},
};

inline constexpr DomainEventFieldLayout kBotDecisionFields[] = {
  {1, "policy_id", "UTF8", true, true, true, "ONE", "", "", 32},
  {2, "revision", "U16", true, true, true, "ONE", "1", "65534", 0},
  {3, "policy_sha256", "BYTES", true, true, true, "ONE", "", "", 32},
  {4, "decision_index", "U32", true, true, true, "ONE", "1", "4294967294", 0},
  {5, "legal_action_list_sha256", "BYTES", true, true, true, "ONE", "", "", 32},
  {6, "candidate_count", "U8", true, true, true, "ONE", "1", "64", 0},
  {7, "candidates", "RECORD_LIST", true, true, true, "MANY", "", "", 64},
  {8, "selected_index", "U8", true, true, true, "ONE", "0", "63", 0},
  {9, "prng_state_before", "U64", true, true, true, "ONE", "0", "18446744073709551615", 0},
  {10, "prng_increment_before", "U64", true, true, true, "ONE", "1", "18446744073709551615", 0},
  {11, "prng_draw_index_before", "U32", true, true, true, "ONE", "0", "4294967294", 0},
  {12, "prng_state_after", "U64", true, true, true, "ONE", "0", "18446744073709551615", 0},
  {13, "prng_increment_after", "U64", true, true, true, "ONE", "1", "18446744073709551615", 0},
  {14, "prng_draw_index_after", "U32", true, true, true, "ONE", "0", "4294967294", 0},
  {15, "planned_delay_ms", "U32", true, true, true, "ONE", "0", "5000", 0},
};

inline constexpr DomainEventFieldLayout kEventBatchItemFields[] = {
  {1, "event_type", "U16", true, true, false, "ONE", "1", "65535", 0},
  {2, "event_schema", "U16", true, true, false, "ONE", "1", "1", 0},
  {3, "event_flags", "U16", true, true, false, "ONE", "1", "7", 0},
  {4, "event_id", "U64", true, true, false, "ONE", "1", "18446744073709551614", 0},
  {5, "field_payload", "BYTES", true, true, false, "ONE", "", "", 4096},
};

inline constexpr DomainEventFieldLayout kSetupRollItemFields[] = {
  {1, "round", "U8", true, true, false, "ONE", "1", "5", 0},
  {2, "seat_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "die_a", "U8", true, true, false, "ONE", "1", "6", 0},
  {4, "die_b", "U8", true, true, false, "ONE", "1", "6", 0},
};

inline constexpr DomainEventFieldLayout kOrderedPlayerItemFields[] = {
  {1, "ordinal", "U8", true, true, false, "ONE", "0", "5", 0},
  {2, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
};

inline constexpr DomainEventFieldLayout kPathPositionItemFields[] = {
  {1, "ordinal", "U8", true, true, false, "ONE", "0", "39", 0},
  {2, "position", "U8", true, true, false, "ONE", "0", "39", 0},
};

inline constexpr DomainEventFieldLayout kAssetIdItemFields[] = {
  {1, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
};

inline constexpr DomainEventFieldLayout kPaymentAllocationItemFields[] = {
  {1, "ordinal", "U8", true, true, false, "ONE", "0", "5", 0},
  {2, "payer_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "creditor_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {4, "creditor_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "amount", "I32", true, true, false, "ONE", "1", "2147483647", 0},
  {6, "payment_transaction_id", "U32", true, true, false, "ONE", "1", "4294967294", 0},
};

inline constexpr DomainEventFieldLayout kTradeAssetItemFields[] = {
  {1, "side", "U8", true, true, false, "ONE", "1", "2", 0},
  {2, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
};

inline constexpr DomainEventFieldLayout kTradeCardItemFields[] = {
  {1, "side", "U8", true, true, false, "ONE", "1", "2", 0},
  {2, "card_instance_id", "U16", true, true, false, "ONE", "1", "65534", 0},
};

inline constexpr DomainEventFieldLayout kRankingItemFields[] = {
  {1, "rank", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
};

inline constexpr DomainEventFieldLayout kBuildingDemandItemFields[] = {
  {1, "player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {2, "target_asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {3, "target_level", "U8", true, true, false, "ONE", "1", "5", 0},
  {4, "list_cost", "I32", true, true, false, "ONE", "1", "2147483647", 0},
};

inline constexpr DomainEventFieldLayout kBankruptcyTransferItemFields[] = {
  {1, "asset_id", "U16", true, true, false, "ONE", "1", "65535", 0},
  {2, "from_player_id", "U8", true, true, false, "ONE", "1", "6", 0},
  {3, "recipient_kind", "U8", true, true, false, "ONE", "0", "1", 0},
  {4, "recipient_player_id", "U8", true, true, false, "ONE", "0", "6", 0},
  {5, "mortgaged", "BOOL", true, true, false, "ONE", "", "", 0},
};

inline constexpr DomainEventFieldLayout kBotCandidateItemFields[] = {
  {1, "command_type", "U16", true, true, true, "ONE", "4096", "4115", 0},
  {2, "transaction_id", "U32", true, true, true, "ONE", "0", "4294967294", 0},
  {3, "subject_id", "U16", true, true, true, "ONE", "0", "65535", 0},
  {4, "target_id", "U16", true, true, true, "ONE", "0", "65535", 0},
  {5, "amount", "I32", true, true, true, "ONE", "-2147483648", "2147483647", 0},
  {6, "score", "I32", true, true, true, "ONE", "-2147483648", "2147483647", 0},
};

}  // namespace gridopoly::generated
