<!-- Generated from GameData/schemas/domain-events-v1.json. Do not edit. -->
# Domain events v1

The authoritative domain registry contains 43 events. `BUILDING_DEMAND_STATE` is a console wire event and is intentionally outside this registry.

| Event type | Stable name | Critical | Terminal role | Visibility | Fields |
| ---: | --- | --- | --- | --- | --- |
| `0x0001` | `ROOM_CREATED` | yes | `NONE` | `PUBLIC` | `1:room_id`, `2:room_seed`, `3:mode`, `4:disconnect_policy`, `5:map_id`, `6:map_revision`, `7:economy_id`, `8:content_manifest_hash` |
| `0x0002` | `ROOM_SETUP_STARTED` | yes | `NONE` | `PUBLIC` | `1:setup_rolls`, `2:turn_order` |
| `0x0003` | `GAME_STARTED` | yes | `NONE` | `PUBLIC` | `1:round_number`, `2:active_player_id`, `3:turn_order` |
| `0x0004` | `ROOM_RESUMED` | yes | `NONE` | `PUBLIC` | `1:recovery_generation`, `2:resumed_at_server_ms` |
| `0x0005` | `ROOM_CLOSED` | yes | `NONE` | `PUBLIC` | `1:old_room_id`, `2:resulting_lifecycle` |
| `0x0010` | `SEAT_BOUND` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:seat_id`, `3:assignment_version`, `4:presence_generation`, `5:controller_type` |
| `0x0011` | `SEAT_UNBOUND` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:seat_id`, `3:assignment_version`, `4:presence_generation`, `5:reason` |
| `0x0012` | `PLAYER_CONNECTION_CHANGED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:online`, `3:assignment_version`, `4:presence_generation` |
| `0x0013` | `CONTROLLER_CHANGED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:old_controller_type`, `3:new_controller_type`, `4:control_epoch`, `5:reason`, `6:bot_policy_id`, `7:bot_policy_revision` |
| `0x0100` | `TURN_STARTED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:round_number`, `3:turn_phase` |
| `0x0101` | `DICE_ROLLED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:roll_kind`, `3:die_a`, `4:die_b`, `5:total`, `6:is_double`, `7:roll_ordinal`, `8:origin_position`, `9:computed_target`, `10:passed_start` |
| `0x0102` | `MOVE_STARTED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:movement_transaction_id`, `3:workflow_revision`, `4:origin_position`, `5:target_position`, `6:manual_available_at_ms`, `7:movement_deadline_ms`, `8:path` |
| `0x0103` | `POSITION_CONFIRMED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:movement_transaction_id`, `3:target_position`, `4:observed_position`, `5:confirmation_method`, `6:confirmed_at_ms` |
| `0x0104` | `MOVE_COMPLETED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:origin_position`, `3:final_position`, `4:steps_moved`, `5:passed_start`, `6:start_award`, `7:landing_tile_kind` |
| `0x0105` | `MOVE_MANUAL_FALLBACK_OPENED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:movement_transaction_id`, `3:target_position`, `4:workflow_revision`, `5:reason` |
| `0x0200` | `PURCHASE_OFFERED` | yes | `NONE` | `PUBLIC` | `1:offered_player_id`, `2:asset_id`, `3:tile_position`, `4:price`, `5:offer_revision`, `6:deadline_ms` |
| `0x0201` | `ASSET_PURCHASED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:asset_id`, `3:price`, `4:cash_after`, `5:asset_revision` |
| `0x0202` | `AUCTION_OPENED` | yes | `NONE` | `PUBLIC` | `1:auction_transaction_id`, `2:auction_kind`, `3:lot_id`, `4:asset_id`, `5:building_type`, `6:auction_version`, `7:eligible_mask`, `8:minimum_next_bid`, `9:deadline_ms` |
| `0x0203` | `AUCTION_BID_PLACED` | yes | `NONE` | `PUBLIC` | `1:auction_transaction_id`, `2:auction_kind`, `3:lot_id`, `4:player_id`, `5:bid_amount`, `6:auction_version`, `7:target_asset_id` |
| `0x0204` | `AUCTION_CLOSED` | yes | `NONE` | `PUBLIC` | `1:auction_transaction_id`, `2:auction_kind`, `3:lot_id`, `4:status`, `5:winner_player_id`, `6:final_bid`, `7:target_asset_id` |
| `0x0205` | `AUCTION_PARTICIPANT_PASSED` | yes | `NONE` | `PUBLIC` | `1:auction_transaction_id`, `2:auction_kind`, `3:lot_id`, `4:player_id`, `5:auction_version`, `6:accepted_order`, `7:passed_mask` |
| `0x0210` | `RENT_CLAIMED` | yes | `NONE` | `PUBLIC` | `1:rent_transaction_id`, `2:landlord_player_id`, `3:payer_player_id`, `4:asset_id`, `5:quoted_amount`, `6:rent_basis`, `7:rent_multiplier_permille`, `8:deadline_ms` |
| `0x0211` | `PAYMENT_CREATED` | yes | `NONE` | `PUBLIC` | `1:payment_transaction_id`, `2:payer_player_id`, `3:creditor_kind`, `4:creditor_player_id`, `5:amount`, `6:reason`, `7:source_asset_id`, `8:deadline_ms` |
| `0x0212` | `PAYMENT_COMPLETED` | yes | `NONE` | `PUBLIC` | `1:payment_transaction_id`, `2:payer_player_id`, `3:creditor_kind`, `4:creditor_player_id`, `5:amount`, `6:payer_cash_after`, `7:creditor_cash_after`, `8:reason`, `9:allocations` |
| `0x0213` | `DEBT_STARTED` | yes | `NONE` | `PUBLIC` | `1:debt_transaction_id`, `2:debtor_player_id`, `3:creditor_kind`, `4:creditor_player_id`, `5:amount_due`, `6:cash_available`, `7:shortfall`, `8:allowed_actions`, `9:eligible_assets` |
| `0x0214` | `DEBT_RESOLVED` | yes | `NONE` | `PUBLIC` | `1:debt_transaction_id`, `2:debtor_player_id`, `3:outcome`, `4:amount_paid`, `5:cash_after` |
| `0x0215` | `RENT_WAIVED` | yes | `NONE` | `PUBLIC` | `1:rent_transaction_id`, `2:landlord_player_id`, `3:payer_player_id`, `4:asset_id`, `5:quoted_amount`, `6:workflow_revision`, `7:reason` |
| `0x0220` | `ASSET_MORTGAGED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:asset_id`, `3:mortgage_value`, `4:cash_after`, `5:asset_revision` |
| `0x0221` | `ASSET_UNMORTGAGED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:asset_id`, `3:redeem_cost`, `4:cash_after`, `5:asset_revision` |
| `0x0222` | `BUILDING_CHANGED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:asset_id`, `3:old_level`, `4:new_level`, `5:cash_delta`, `6:building_stock_after`, `7:landmark_stock_after`, `8:asset_revision` |
| `0x0223` | `BUILDING_DEMAND_CHANGED` | yes | `NONE` | `PUBLIC` | `1:demand_transaction_id`, `2:workflow_revision`, `3:stage`, `4:actor_player_id`, `5:registered_mask`, `6:target_asset_id`, `7:target_level`, `8:list_cost`, `9:outcome`, `10:intents` |
| `0x0230` | `TRADE_CREATED` | yes | `NONE` | `PUBLIC` | `1:trade_transaction_id`, `2:trade_version`, `3:proposer_player_id`, `4:counterparty_player_id`, `5:proposer_gives_cash`, `6:counterparty_gives_cash`, `7:assets`, `8:cards`, `9:deadline_ms` |
| `0x0231` | `TRADE_UPDATED` | yes | `NONE` | `PUBLIC` | `1:trade_transaction_id`, `2:trade_version`, `3:actor_player_id`, `4:proposer_gives_cash`, `5:counterparty_gives_cash`, `6:assets`, `7:cards`, `8:deadline_ms` |
| `0x0232` | `TRADE_CLOSED` | yes | `NONE` | `PUBLIC` | `1:trade_transaction_id`, `2:trade_version`, `3:status`, `4:proposer_player_id`, `5:counterparty_player_id`, `6:assets`, `7:cards` |
| `0x0240` | `CARD_DRAWN` | yes | `NONE` | `PRIVATE` | `1:player_id`, `2:deck_id`, `3:card_instance_id`, `4:card_catalog_id`, `5:effect_id`, `6:keepable` |
| `0x0241` | `CARD_EFFECT_APPLIED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:deck_id`, `3:card_instance_id`, `4:effect_id`, `5:amount`, `6:target_player_id`, `7:target_position`, `8:outcome` |
| `0x0250` | `PLAYER_HELD` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:origin_position`, `3:hold_position`, `4:failed_hold_rolls`, `5:reason` |
| `0x0251` | `PLAYER_RELEASED` | yes | `NONE` | `PUBLIC` | `1:player_id`, `2:release_method`, `3:card_instance_id`, `4:amount_paid`, `5:failed_hold_rolls` |
| `0x0260` | `PLAYER_BANKRUPT` | yes | `NONE` | `PUBLIC` | `1:debtor_player_id`, `2:creditor_kind`, `3:creditor_player_id`, `4:cash_transferred`, `5:eliminated_rank`, `6:transfers` |
| `0x0261` | `GAME_FINISHED` | yes | `NONE` | `PUBLIC` | `1:winner_player_id`, `2:reason`, `3:ranking` |
| `0x0270` | `CARD_MULTI_PAYMENT_PROGRESS` | yes | `NONE` | `PUBLIC` | `1:resolution_id`, `2:item_index`, `3:target_player_id`, `4:payer_player_id`, `5:creditor_kind`, `6:creditor_player_id`, `7:amount`, `8:old_status`, `9:new_status`, `10:child_payment_transaction_id`, `11:next_pending_index` |
| `0x0280` | `BOARD_OBSERVATION_RECORDED` | yes | `NONE` | `PUBLIC` | `1:observation_id`, `2:stage`, `3:target_transaction_id`, `4:target_workflow_revision`, `5:observed_position`, `6:result`, `7:original_window_ms`, `8:deadline_instance_id`, `9:outcome` |
| `0x0290` | `BOT_DECISION` | yes | `NONE` | `ADMIN_PRIVATE` | `1:policy_id`, `2:revision`, `3:policy_sha256`, `4:decision_index`, `5:legal_action_list_sha256`, `6:candidate_count`, `7:candidates`, `8:selected_index`, `9:prng_state_before`, `10:prng_increment_before`, `11:prng_draw_index_before`, `12:prng_state_after`, `13:prng_increment_after`, `14:prng_draw_index_after`, `15:planned_delay_ms` |

## Reusable record layouts

### `event_batch_item` (`0x8000`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `event_type` | `U16` | yes | no | 1..65535 |
| 2 | `event_schema` | `U16` | yes | no | 1..1 |
| 3 | `event_flags` | `U16` | yes | no | 1..7 |
| 4 | `event_id` | `U64` | yes | no | 1..18446744073709551614 |
| 5 | `field_payload` | `BYTES` | yes | no | max 4096 |

### `setup_roll_item` (`0x8001`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `round` | `U8` | yes | no | 1..5 |
| 2 | `seat_id` | `U8` | yes | no | 1..6 |
| 3 | `die_a` | `U8` | yes | no | 1..6 |
| 4 | `die_b` | `U8` | yes | no | 1..6 |

### `ordered_player_item` (`0x8002`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `ordinal` | `U8` | yes | no | 0..5 |
| 2 | `player_id` | `U8` | yes | no | 1..6 |

### `path_position_item` (`0x8003`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `ordinal` | `U8` | yes | no | 0..39 |
| 2 | `position` | `U8` | yes | no | 0..39 |

### `asset_id_item` (`0x8004`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `asset_id` | `U16` | yes | no | 1..65535 |

### `payment_allocation_item` (`0x8005`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `ordinal` | `U8` | yes | no | 0..5 |
| 2 | `payer_player_id` | `U8` | yes | no | 1..6 |
| 3 | `creditor_kind` | `U8` | yes | no | 0..1 |
| 4 | `creditor_player_id` | `U8` | yes | no | 0..6 |
| 5 | `amount` | `I32` | yes | no | 1..2147483647 |
| 6 | `payment_transaction_id` | `U32` | yes | no | 1..4294967294 |

### `trade_asset_item` (`0x8006`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `side` | `U8` | yes | no | 1..2 |
| 2 | `asset_id` | `U16` | yes | no | 1..65535 |

### `trade_card_item` (`0x8007`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `side` | `U8` | yes | no | 1..2 |
| 2 | `card_instance_id` | `U16` | yes | no | 1..65534 |

### `ranking_item` (`0x8008`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `rank` | `U8` | yes | no | 1..6 |
| 2 | `player_id` | `U8` | yes | no | 1..6 |

### `building_demand_item` (`0x8009`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `player_id` | `U8` | yes | no | 1..6 |
| 2 | `target_asset_id` | `U16` | yes | no | 1..65535 |
| 3 | `target_level` | `U8` | yes | no | 1..5 |
| 4 | `list_cost` | `I32` | yes | no | 1..2147483647 |

### `bankruptcy_transfer_item` (`0x800A`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `asset_id` | `U16` | yes | no | 1..65535 |
| 2 | `from_player_id` | `U8` | yes | no | 1..6 |
| 3 | `recipient_kind` | `U8` | yes | no | 0..1 |
| 4 | `recipient_player_id` | `U8` | yes | no | 0..6 |
| 5 | `mortgaged` | `BOOL` | yes | no |  |

### `bot_candidate_item` (`0x800B`)

| Field ID | Name | Type | Required | Private | Range / maximum |
| ---: | --- | --- | --- | --- | --- |
| 1 | `command_type` | `U16` | yes | yes | 4096..4115 |
| 2 | `transaction_id` | `U32` | yes | yes | 0..4294967294 |
| 3 | `subject_id` | `U16` | yes | yes | 0..65535 |
| 4 | `target_id` | `U16` | yes | yes | 0..65535 |
| 5 | `amount` | `I32` | yes | yes | -2147483648..2147483647 |
| 6 | `score` | `I32` | yes | yes | -2147483648..2147483647 |
