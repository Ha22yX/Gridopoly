#!/usr/bin/env python3
"""One bounded parent-owned process; one serial handle per operation."""
import json
import sys

from batch_flash import load_profile_images, mac_address
from flash_layout import SECTOR, check_existing_layout

ESPTOOL_VERSION = "5.3.1"
RESULT_PREFIX = "GRIDOPOLY_RESULT "


class EspToolApi:
    def __init__(self):
        import esptool
        from esptool.targets.esp32s3 import ESP32S3ROM
        self.tool = esptool
        self.version = esptool.__version__
        self.loader = ESP32S3ROM

    def __getattr__(self, name):
        return getattr(self.tool, name)


def process_job(job, api=None):
    device, profile = job["device"], job["profile"]
    operation = job["operation"]
    if operation not in {"preflight", "flash"} or device["profile"] != profile["profile"]:
        raise ValueError("invalid worker operation or profile")
    expected = mac_address(device["mac"])
    # Read and hash exactly the immutable bytes passed to both write and verify.
    images = load_profile_images(profile)
    api = EspToolApi() if api is None else api
    if api.version != ESPTOOL_VERSION:
        raise ValueError(f"esptool=={ESPTOOL_VERSION} is required; found {api.version}")

    def identify(esp):
        value = esp.read_mac("BASE_MAC")
        if value is None or len(value) != 6:
            raise ValueError("device did not return a complete factory MAC")
        actual = mac_address(":".join(f"{byte:02x}" for byte in value))
        if actual != expected:
            raise ValueError(f"{device['label']}: expected {expected}, found {actual}")

    # 5.3.1 exposes ESP32S3ROM, not the newer connect_esp convenience function.
    # Its context closes the serial handle even when a later operation fails.
    with api.loader(device["port"], 115200) as rom:
        rom.connect("default-reset", 3)
        identify(rom)
        esp = api.run_stub(rom)
        if esp._port is not rom._port:
            raise RuntimeError("stub changed the serial handle; refusing to write")
        identify(esp)
        api.attach_flash(esp)
        if api.detect_flash_size(esp) != "16MB":
            raise ValueError("device flash capacity is not the required 16MB")
        existing = api.read_flash(esp, 0x8000, SECTOR, no_progress=True)
        check_existing_layout(existing, images)
        identify(esp)
        if operation == "flash":
            # 5.3.1 write_flash otherwise closes/reopens COM after SerialException.
            # A retry could reach another board; fail this worker instead.
            esp.WRITE_FLASH_ATTEMPTS = 1
            esp.change_baud(460800)
            parameters = dict(flash_mode=profile["flashMode"],
                              flash_freq=profile["flashFrequency"],
                              flash_size=profile["flashSize"])
            api.write_flash(esp, images, **parameters, erase_all=False,
                            force=False, no_progress=True)
            api.verify_flash(esp, images, **parameters)
            identify(esp)
        # No reset or close occurs between identification, write and verify.
        api.reset_chip(esp, "hard-reset")
    return {"status": "verified" if operation == "flash" else "preflight_ok",
            "mac": expected, "profile": device["profile"], "esptoolVersion": api.version}


def main():
    try:
        result = process_job(json.load(sys.stdin))
        print(RESULT_PREFIX + json.dumps(result), flush=True)
        return 0
    except Exception as error:
        print(f"WORKER ERROR: {error}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
