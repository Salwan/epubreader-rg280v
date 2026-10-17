"""Check production startup/logging and synthetic-power isolation with real smoke rendering."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

executable, assets, music, state, screenshots = map(Path, sys.argv[1:])
shutil.rmtree(state, ignore_errors=True)
state.mkdir(parents=True)
fixture = state / "power-fixture"
(fixture / "jz-battery").mkdir(parents=True)
(fixture / "usb-charger").mkdir()
for name, value in {"jz-battery/type": "Battery", "jz-battery/voltage_now": "3416000",
                    "jz-battery/voltage_min_design": "3400000", "jz-battery/voltage_max_design": "4200000",
                    "usb-charger/type": "USB", "usb-charger/online": "0"}.items():
    (fixture / name).write_text(value + "\n")
help_text = subprocess.check_output([str(executable), "--help"], text=True)
assert "SHUTDOWN TEST" not in help_text and "shortcut" not in help_text
command = [str(executable), "--assets", str(assets), "--book", str(assets / "books/prose-epub3.epub"),
           "--state-dir", str(state), "--music-dir", str(music), "--power-fixture", str(fixture),
           "--smoke-test", str(screenshots)]
environment = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
for run in (1, 2):
    marker = f"previous-launch-{run}"
    for name in ("reader.log", "stderr.log"):
        (state / name).write_text(marker + "\n")
    subprocess.run(command, env=environment, check=True, timeout=45)
    log = (state / "reader.log").read_text()
    errors = (state / "stderr.log").read_text()
    assert marker not in log and marker not in errors, "Production logs did not reset for the current launch"
    assert sum(line.startswith("LAUNCH pid=") for line in log.splitlines()) == 1
    assert "POWER BACKEND enabled=0 device=0 smoke=1 fixture=1" in log
    assert "PROCESS GROUPS" in log and "uid=" in log and "euid=" in log
    assert "SHUTDOWN TEST" not in log
    assert "POWER COMMAND " not in log, "Synthetic power inputs reached a real command"
print("PASS: production smoke, current-launch logs, removed shortcut, and disabled hardware shutdown for host/synthetic inputs")
