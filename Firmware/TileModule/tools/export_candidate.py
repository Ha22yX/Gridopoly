"""PlatformIO `candidate` target: normal firmware and its actual flash segments.

Does not open serial ports. The release owner binds the profile to physical MACs.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

Import("env")  # noqa: F821 - supplied by PlatformIO/SCons


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def export_candidate(source, target, env):
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    build = Path(env.subst("$BUILD_DIR")).resolve()
    environment = env.subst("$PIOENV")
    profiles = {"tile_esp32s3_long": "tile-long", "tile_esp32s3_corner": "tile-corner"}
    if environment not in profiles:
        raise ValueError("Batch candidate requires an explicit long/corner environment")
    files = [project / "platformio.ini"]
    for folder in ("include", "src", "config", "tools"):
        files += [f for f in (project / folder).rglob("*") if f.is_file()
                  and "__pycache__" not in f.parts]
    # Network credentials can be inherited by existing installations. Record a
    # hash only, never copy secrets into the public manifest or repository.
    inherited = project.parent / "PlayerConsole/config/secrets.local.h"
    if inherited.exists():
        files.append(inherited)
    # Header-only asset catalog/policy are real build inputs, without linking
    # the rest of GridopolyCore into the tile firmware.
    asset_headers = project.parent / "libraries/GridopolyCore/src/gridopoly/assets"
    files += [asset_headers / "DevelopmentArtworkCatalog.h",
              asset_headers / "RemoteArtworkPolicy.h"]
    sources = {str(f.relative_to(project.parent).as_posix()): digest(f)
               for f in sorted(files)}
    source_bytes = json.dumps(sources, sort_keys=True, separators=(",", ":")).encode()
    (build / "candidate-sources.json").write_bytes(source_bytes)
    images = list(env.get("FLASH_EXTRA_IMAGES", []))
    images.append((env.subst("$ESP32_APP_OFFSET"), build / (env.subst("$PROGNAME") + ".bin")))
    segments = []
    for offset, filename in images:
        path = Path(env.subst(str(filename))).resolve()
        offset = int(str(offset), 0) if not isinstance(offset, int) else offset
        segments.append({"offset": offset, "path": str(path), "sha256": digest(path),
                         "size": path.stat().st_size})
    segments.sort(key=lambda s: s["offset"])
    for index, segment in enumerate(segments):
        end = segment["offset"] + segment["size"]
        if end > 16 * 1024 * 1024 or (index + 1 < len(segments) and
                                     end > segments[index + 1]["offset"]):
            raise ValueError("Invalid or overlapping flash segments")
    repo = project.parents[1]
    version = re.search(r'kFirmwareVersion\s*=\s*"([^"]+)"',
                        (project / "include/module_profile.h").read_text(encoding="utf-8"))
    if version is None:
        raise ValueError("Firmware version is missing from module_profile.h")
    main_source = (project / "src/main.cpp").read_text(encoding="utf-8")
    rfid_diagnostic = "rfid_diagnostics.h" in main_source
    revision = subprocess.check_output(
        ["git", "-c", "safe.directory=*", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    manifest = {
        "schemaVersion": 1, "profile": profiles[environment], "environment": environment,
        "firmwareVersion": version.group(1), "hardwareRevision": "unknown", "chip": "esp32s3",
        "flashSize": env.BoardConfig().get("upload.flash_size"),
        "flashMode": env.subst("${__get_board_flash_mode(__env__)}"),
        "flashFrequency": env.subst("${__get_board_f_image(__env__)}"),
        "gitRevision": revision, "sourceDigest": hashlib.sha256(source_bytes).hexdigest(),
        "sourceManifest": str(build / "candidate-sources.json"), "segments": segments,
        "normalFirmware": not rfid_diagnostic, "displayDefaultHz": 8000000,
        "rfidDiagnostic": "stage2" if rfid_diagnostic else "none",
        "displayRotation": 2, "displayRotationDegrees": 180,
        "developmentArtwork": {"catalog": "v1", "storage": "RAM_ONLY",
                               "readyLimit": 5, "stagingLimit": 1, "maximumBytes": 307200},
        "role": "long" if profiles[environment] == "tile-long" else "corner",
        "validation": "Candidate only; no device deployment, LED optical or full-board acceptance implied",
    }
    destination = build / "candidate.json"
    destination.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Candidate manifest: " + str(destination))


env.AddCustomTarget("candidate", "$BUILD_DIR/${PROGNAME}.bin", export_candidate,
                    title="Export tile candidate", description="Build normal firmware and flash manifest")
