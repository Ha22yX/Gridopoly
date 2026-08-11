"""Validation and deterministic rendering for the domain-event v1 contract."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from Tools.contracts.contract_model import ContractDocument, ContractError, load_json_value, validate_contract


EVENT_REGISTRY = (
    (0x0001, "ROOM_CREATED"), (0x0002, "ROOM_SETUP_STARTED"), (0x0003, "GAME_STARTED"),
    (0x0004, "ROOM_RESUMED"), (0x0005, "ROOM_CLOSED"), (0x0010, "SEAT_BOUND"),
    (0x0011, "SEAT_UNBOUND"), (0x0012, "PLAYER_CONNECTION_CHANGED"), (0x0013, "CONTROLLER_CHANGED"),
    (0x0100, "TURN_STARTED"), (0x0101, "DICE_ROLLED"), (0x0102, "MOVE_STARTED"),
    (0x0103, "POSITION_CONFIRMED"), (0x0104, "MOVE_COMPLETED"), (0x0105, "MOVE_MANUAL_FALLBACK_OPENED"),
    (0x0200, "PURCHASE_OFFERED"), (0x0201, "ASSET_PURCHASED"), (0x0202, "AUCTION_OPENED"),
    (0x0203, "AUCTION_BID_PLACED"), (0x0204, "AUCTION_CLOSED"), (0x0205, "AUCTION_PARTICIPANT_PASSED"),
    (0x0210, "RENT_CLAIMED"), (0x0211, "PAYMENT_CREATED"), (0x0212, "PAYMENT_COMPLETED"),
    (0x0213, "DEBT_STARTED"), (0x0214, "DEBT_RESOLVED"), (0x0215, "RENT_WAIVED"),
    (0x0220, "ASSET_MORTGAGED"), (0x0221, "ASSET_UNMORTGAGED"), (0x0222, "BUILDING_CHANGED"),
    (0x0223, "BUILDING_DEMAND_CHANGED"), (0x0230, "TRADE_CREATED"), (0x0231, "TRADE_UPDATED"),
    (0x0232, "TRADE_CLOSED"), (0x0240, "CARD_DRAWN"), (0x0241, "CARD_EFFECT_APPLIED"),
    (0x0250, "PLAYER_HELD"), (0x0251, "PLAYER_RELEASED"), (0x0260, "PLAYER_BANKRUPT"),
    (0x0261, "GAME_FINISHED"), (0x0270, "CARD_MULTI_PAYMENT_PROGRESS"),
    (0x0280, "BOARD_OBSERVATION_RECORDED"), (0x0290, "BOT_DECISION"),
)

RECORD_LAYOUTS = frozenset({
    "event_batch_item", "setup_roll_item", "ordered_player_item", "path_position_item",
    "asset_id_item", "payment_allocation_item", "trade_asset_item", "trade_card_item",
    "ranking_item", "building_demand_item", "bankruptcy_transfer_item", "bot_candidate_item",
})

RECORD_LIST_MAXIMA = {
    ("ROOM_SETUP_STARTED", "setup_rolls"): 30,
    ("ROOM_SETUP_STARTED", "turn_order"): 6,
    ("GAME_STARTED", "turn_order"): 6,
    ("MOVE_STARTED", "path"): 40,
    ("PAYMENT_COMPLETED", "allocations"): 6,
    ("DEBT_STARTED", "eligible_assets"): 28,
    ("BUILDING_DEMAND_CHANGED", "intents"): 6,
    ("TRADE_CREATED", "assets"): 28,
    ("TRADE_CREATED", "cards"): 8,
    ("TRADE_UPDATED", "assets"): 28,
    ("TRADE_UPDATED", "cards"): 8,
    ("TRADE_CLOSED", "assets"): 28,
    ("TRADE_CLOSED", "cards"): 8,
    ("PLAYER_BANKRUPT", "transfers"): 28,
    ("GAME_FINISHED", "ranking"): 6,
    ("BOT_DECISION", "candidates"): 64,
}

FIELD_SEQUENCES = {
    "MOVE_MANUAL_FALLBACK_OPENED": ("player_id", "movement_transaction_id", "target_position", "workflow_revision", "reason"),
    "AUCTION_PARTICIPANT_PASSED": ("auction_transaction_id", "auction_kind", "lot_id", "player_id", "auction_version", "accepted_order", "passed_mask"),
    "RENT_WAIVED": ("rent_transaction_id", "landlord_player_id", "payer_player_id", "asset_id", "quoted_amount", "workflow_revision", "reason"),
    "CARD_MULTI_PAYMENT_PROGRESS": ("resolution_id", "item_index", "target_player_id", "payer_player_id", "creditor_kind", "creditor_player_id", "amount", "old_status", "new_status", "child_payment_transaction_id", "next_pending_index"),
    "BOARD_OBSERVATION_RECORDED": ("observation_id", "stage", "target_transaction_id", "target_workflow_revision", "observed_position", "result", "original_window_ms", "deadline_instance_id", "outcome"),
}

HEADER_PATH = "Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/domain_event_ids.h"
MARKDOWN_PATH = "Docs/firmware/generated/domain-events-v1.md"


def validate_domain_event_value(value: object) -> ContractDocument:
    """Validate the meta-contract plus the approved domain/wire semantic boundary."""

    document = validate_contract(value)
    events = [definition for definition in document.definitions if definition.metadata.get("kind") == "EVENT"]
    actual_registry = tuple((definition.id, definition.metadata.get("stable_name")) for definition in events)
    for expected, actual in zip(EVENT_REGISTRY, actual_registry):
        if actual != expected:
            raise ContractError(f"event id 0x{expected[0]:04X} must be {expected[1]}")
    if len(actual_registry) != len(EVENT_REGISTRY):
        raise ContractError(f"domain event registry must contain exactly {len(EVENT_REGISTRY)} events")
    if any(name == "BUILDING_DEMAND_STATE" for _, name in actual_registry):
        raise ContractError("BUILDING_DEMAND_STATE is a wire event, not a domain event")

    layouts = {definition.name for definition in document.definitions if definition.metadata.get("kind") == "RECORD_LAYOUT"}
    if layouts != RECORD_LAYOUTS:
        raise ContractError("record layout registry does not match the approved nested layouts")

    for definition in events:
        stable_name = str(definition.metadata["stable_name"])
        if definition.metadata.get("critical") is not True:
            raise ContractError(f"{stable_name} must be critical")
        if definition.metadata.get("terminal_role") != "NONE":
            raise ContractError(f"{stable_name} terminal_role must be NONE")
        expected_sequence = FIELD_SEQUENCES.get(stable_name)
        if expected_sequence is not None and tuple(field.name for field in definition.fields) != expected_sequence:
            raise ContractError(f"{stable_name} field sequence does not match the approved layout")
        for field in definition.fields:
            missing = {"critical", "private", "visibility", "sort"} - set(field.metadata)
            if missing:
                raise ContractError(f"{stable_name}.{field.name} missing {sorted(missing)[0]}")
            if field.metadata["critical"] is not True:
                raise ContractError(f"{stable_name}.{field.name} must be critical")
            expected_private = stable_name == "BOT_DECISION" or (
                stable_name == "CARD_DRAWN" and field.name in {"card_instance_id", "card_catalog_id", "effect_id"}
            )
            if bool(field.metadata["private"]) != expected_private:
                state = "private" if expected_private else "public"
                raise ContractError(f"{stable_name}.{field.name} must be {state}")
            maximum = RECORD_LIST_MAXIMA.get((stable_name, field.name))
            if maximum is not None and field.metadata.get("max") != maximum:
                raise ContractError(f"{stable_name}.{field.name} max must be {maximum}")
    return document


def generated_outputs(value: object) -> dict[str, bytes]:
    document = validate_domain_event_value(value)
    return {
        HEADER_PATH: _render_header(document).encode("utf-8"),
        MARKDOWN_PATH: _render_markdown(document).encode("utf-8"),
    }


def _render_header(document: ContractDocument) -> str:
    events = [definition for definition in document.definitions if definition.metadata["kind"] == "EVENT"]
    lines = [
        "// Generated from GameData/schemas/domain-events-v1.json. Do not edit.",
        "#pragma once", "", "#include <cstdint>", "", "namespace gridopoly::generated {", "",
        "enum class DomainEventType : std::uint16_t {",
    ]
    lines.extend(f"  {definition.metadata['stable_name']} = 0x{definition.id:04X}," for definition in events)
    lines.extend([
        "};", "",
        "struct DomainEventFieldLayout {",
        "  std::uint16_t field_id;", "  const char* stable_name;", "  const char* wire_type;",
        "  bool required;", "  bool critical;", "  bool private_field;", "  const char* cardinality;",
        "  const char* range_min;", "  const char* range_max;", "  std::uint16_t max_count;", "};", "",
    ])
    for definition in document.definitions:
        symbol = _symbol(definition.name)
        lines.append(f"inline constexpr DomainEventFieldLayout k{symbol}Fields[] = {{")
        for field in definition.fields:
            minimum, maximum = "", ""
            if "range" in field.metadata:
                minimum = str(field.metadata["range"]["min"])
                maximum = str(field.metadata["range"]["max"])
            max_count = int(field.metadata.get("max", 0))
            lines.append(
                f'  {{{field.id}, "{field.name}", "{field.wire_type}", '
                f'{str(field.required).lower()}, {str(bool(field.metadata["critical"])).lower()}, '
                f'{str(bool(field.metadata["private"])).lower()}, "{field.cardinality}", '
                f'"{minimum}", "{maximum}", {max_count}}},'
            )
        lines.extend(["};", ""])
    lines.extend(["}  // namespace gridopoly::generated", ""])
    return "\n".join(lines)


def _render_markdown(document: ContractDocument) -> str:
    events = [definition for definition in document.definitions if definition.metadata["kind"] == "EVENT"]
    layouts = [definition for definition in document.definitions if definition.metadata["kind"] == "RECORD_LAYOUT"]
    lines = [
        "<!-- Generated from GameData/schemas/domain-events-v1.json. Do not edit. -->",
        "# Domain events v1", "",
        "The authoritative domain registry contains 43 events. `BUILDING_DEMAND_STATE` is a console wire event and is intentionally outside this registry.", "",
        "| Event type | Stable name | Critical | Terminal role | Visibility | Fields |", "| ---: | --- | --- | --- | --- | --- |",
    ]
    for definition in events:
        field_names = ", ".join(f"`{field.id}:{field.name}`" for field in definition.fields)
        lines.append(f"| `0x{definition.id:04X}` | `{definition.metadata['stable_name']}` | yes | `{definition.metadata['terminal_role']}` | `{definition.metadata['visibility']}` | {field_names} |")
    lines.extend(["", "## Reusable record layouts", ""])
    for definition in layouts:
        lines.extend([f"### `{definition.name}` (`0x{definition.id:04X}`)", "", "| Field ID | Name | Type | Required | Private | Range / maximum |", "| ---: | --- | --- | --- | --- | --- |"])
        for field in definition.fields:
            bounds = ""
            if "range" in field.metadata:
                bounds = f"{field.metadata['range']['min']}..{field.metadata['range']['max']}"
            elif "max" in field.metadata:
                bounds = f"max {field.metadata['max']}"
            lines.append(f"| {field.id} | `{field.name}` | `{field.wire_type}` | {'yes' if field.required else 'no'} | {'yes' if field.metadata['private'] else 'no'} | {bounds} |")
        lines.append("")
    return "\n".join(lines)


def _symbol(name: str) -> str:
    return "".join(part.capitalize() for part in re.split(r"_+", name))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    value = load_json_value(root / "GameData" / "schemas" / "domain-events-v1.json")
    outputs = generated_outputs(value)
    if args.check:
        drift = [relative for relative, data in outputs.items() if not (root / relative).is_file() or (root / relative).read_bytes() != data]
        for relative in drift:
            print(relative)
        return 1 if drift else 0
    output_root = args.output_root.resolve() if args.output_root else root
    for relative, data in outputs.items():
        target = output_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
