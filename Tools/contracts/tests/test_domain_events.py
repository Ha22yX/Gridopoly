from __future__ import annotations

import json
import copy
import unittest
from pathlib import Path

from Tools.contracts.contract_model import ContractError
from Tools.contracts.domain_events import generated_outputs, validate_domain_event_value

ROOT = Path(__file__).resolve().parents[3]
SCHEMA_PATH = ROOT / "GameData" / "schemas" / "domain-events-v1.json"
REQUIRED_PATH = ROOT / "GameData" / "tests" / "contracts" / "domain-events-required.json"


class DomainEventContractTests(unittest.TestCase):
    def schema(self) -> dict[str, object]:
        return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))

    def test_registry_is_the_exact_approved_domain_event_set(self) -> None:
        required = json.loads(REQUIRED_PATH.read_text(encoding="utf-8"))["events"]
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        actual = {
            (definition["id"], definition["stable_name"])
            for definition in schema["definitions"]
            if definition["kind"] == "EVENT"
        }
        expected = {(event["event_type"], event["stable_name"]) for event in required}

        self.assertSetEqual(actual, expected)

    def test_domain_registry_does_not_absorb_the_wire_event_registry(self) -> None:
        names = {definition["stable_name"] for definition in self.schema()["definitions"] if definition["kind"] == "EVENT"}
        self.assertEqual(len(names), 43)
        self.assertNotIn("BUILDING_DEMAND_STATE", names)

    def test_events_and_fields_freeze_visibility_flags_and_terminal_boundary(self) -> None:
        document = validate_domain_event_value(self.schema())
        events = [definition for definition in document.definitions if definition.metadata["kind"] == "EVENT"]
        self.assertTrue(all(definition.metadata["terminal_role"] == "NONE" for definition in events))
        for definition in events:
            self.assertIn(definition.metadata["visibility"], {"PUBLIC", "PRIVATE", "ADMIN_PRIVATE"})
            for field in definition.fields:
                self.assertEqual(
                    {"critical", "private", "visibility", "sort"} - set(field.metadata),
                    set(),
                    f"missing field metadata for {definition.name}.{field.name}",
                )

    def test_reusable_record_layouts_cover_all_bounded_nested_records(self) -> None:
        layouts = {
            definition.name for definition in validate_domain_event_value(self.schema()).definitions
            if definition.metadata["kind"] == "RECORD_LAYOUT"
        }
        self.assertSetEqual(layouts, {
            "event_batch_item", "setup_roll_item", "ordered_player_item", "path_position_item",
            "asset_id_item", "payment_allocation_item", "trade_asset_item", "trade_card_item",
            "ranking_item", "building_demand_item", "bankruptcy_transfer_item", "bot_candidate_item",
        })

    def test_mutations_fail_with_one_precise_first_error(self) -> None:
        base = self.schema()
        cases: list[tuple[str, dict[str, object], str]] = []

        duplicate = copy.deepcopy(base)
        duplicate["definitions"][1]["id"] = duplicate["definitions"][0]["id"]
        cases.append(("duplicate ID", duplicate, "duplicate definition id 1"))

        renamed = copy.deepcopy(base)
        renamed["definitions"][0]["stable_name"] = "ROOM_CREATED_RENAMED"
        cases.append(("event name", renamed, "event id 0x0001 must be ROOM_CREATED"))

        reordered = copy.deepcopy(base)
        reordered["definitions"][0]["fields"][0], reordered["definitions"][0]["fields"][1] = reordered["definitions"][0]["fields"][1], reordered["definitions"][0]["fields"][0]
        cases.append(("field order", reordered, "fields must be ordered by id"))

        public_card = copy.deepcopy(base)
        card_drawn = next(item for item in public_card["definitions"] if item["stable_name"] == "CARD_DRAWN")
        next(field for field in card_drawn["fields"] if field["name"] == "card_catalog_id")["private"] = False
        cases.append(("PRIVATE bit", public_card, "CARD_DRAWN.card_catalog_id must be private"))

        larger_batch = copy.deepcopy(base)
        setup = next(item for item in larger_batch["definitions"] if item["stable_name"] == "ROOM_SETUP_STARTED")
        next(field for field in setup["fields"] if field["name"] == "setup_rolls")["max"] = 31
        cases.append(("RECORD_LIST max", larger_batch, "ROOM_SETUP_STARTED.setup_rolls max must be 30"))

        terminal = copy.deepcopy(base)
        terminal["definitions"][0]["terminal_role"] = "COMMAND_TERMINAL"
        cases.append(("terminal role", terminal, "ROOM_CREATED terminal_role must be NONE"))

        for label, candidate, message in cases:
            with self.subTest(label=label), self.assertRaisesRegex(ContractError, message):
                validate_domain_event_value(candidate)

    def test_generated_header_and_markdown_match_the_validated_schema(self) -> None:
        outputs = generated_outputs(self.schema())
        header_path = ROOT / "Firmware" / "TestGameServer" / "libraries" / "GridopolyGenerated" / "src" / "gridopoly" / "generated" / "domain_event_ids.h"
        markdown_path = ROOT / "Docs" / "firmware" / "generated" / "domain-events-v1.md"

        self.assertEqual(outputs["Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/domain_event_ids.h"], header_path.read_bytes())
        self.assertEqual(outputs["Docs/firmware/generated/domain-events-v1.md"], markdown_path.read_bytes())
        self.assertIn(b"enum class DomainEventType : std::uint16_t", outputs[next(path for path in outputs if path.endswith(".h"))])


if __name__ == "__main__":
    unittest.main()
