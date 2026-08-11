from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).resolve().with_name("stage-avatar-components.py")


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="gridopoly-avatar-stage-") as directory:
        target = Path(directory) / "components-v1"
        target.mkdir()
        subprocess.run([sys.executable, str(SCRIPT), str(target)], check=True)
        assert len(list(target.rglob("*.gavc"))) == 30
        assert len([path for path in target.rglob("*") if path.is_file()]) == 31
        second = subprocess.run(
            [sys.executable, str(SCRIPT), str(target)], capture_output=True, text=True
        )
        assert second.returncode != 0
        assert "target must be an existing empty directory" in second.stdout + second.stderr
    print("AVATAR COMPONENT STAGE TEST PASS")


if __name__ == "__main__":
    main()
