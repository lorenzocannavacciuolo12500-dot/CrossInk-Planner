#!/usr/bin/env python3
"""Capture README screenshots of the Planner screens in the CrossInk simulator.

Builds nothing: run `pio run -e simulator` first (on Linux add
`PLATFORMIO_BUILD_FLAGS="-lssl -lcrypto"`). The script prepares a throw-away SD
card with sample tasks and a calendar snapshot (dates relative to today), drives
the simulator with CROSSPOINT_SIM_INPUT_SCRIPT and saves PNG files.

    python3 scripts/planner_screenshots.py --out docs/images/planner
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TASKS = [
    ("Restituire i libri in biblioteca", 0, False),
    ("Comprare il regalo per Giulia", 2, False),
    ("Prenotare il treno per Napoli", 5, False),
    ("Finire di leggere il capitolo 12", None, False),
    ("Chiamare l'idraulico", -1, False),
    ("Stampare i biglietti del concerto", None, True),
    ("Pagare la bolletta della luce", None, True),
]

# (day offset, title, location, calendar, start minute or None for all day, end minute)
EVENTS = [
    (0, "Riunione di progetto", "Sala Verde", "Lavoro", 9 * 60 + 30, 10 * 60 + 30),
    (0, "Pranzo con Marta", "Trattoria da Enzo", "Personale", 13 * 60, 14 * 60),
    (0, "Palestra", "", "Personale", 18 * 60 + 30, 19 * 60 + 30),
    (1, "Compleanno di Giulia", "", "Famiglia", None, None),
    (1, "Dentista", "Via Roma 12", "Personale", 16 * 60, 16 * 60 + 45),
    (2, "Consegna relazione", "", "Lavoro", 11 * 60, 12 * 60),
    (3, "Club del libro", "Libreria Mondadori", "Personale", 21 * 60, 22 * 60 + 30),
    (5, "Viaggio a Napoli", "", "Famiglia", None, None),
]


def write_sd(sd: Path, today: dt.date, now_utc: int, offset_min: int) -> None:
    planner = sd / ".crosspoint" / "planner"
    planner.mkdir(parents=True, exist_ok=True)
    (sd / "Books").mkdir(exist_ok=True)

    items = []
    for i, (title, due, done) in enumerate(TASKS):
        items.append({
            "id": f"demo-{i}",
            "title": title,
            "due": (today + dt.timedelta(days=due)).isoformat() if due is not None else "",
            "done": done,
            "deleted": False,
            "updatedAt": now_utc - 600 * i,
            "localRev": 0,
        })
    (planner / "todos.json").write_text(
        json.dumps({"version": 1, "storeId": "demo0001", "revision": 1, "items": items}, ensure_ascii=False),
        encoding="utf-8")

    events = []
    for day, title, location, calendar, start, end in EVENTS:
        date = (today + dt.timedelta(days=day)).isoformat()
        all_day = start is None
        events.append({
            "title": title, "location": location, "calendar": calendar,
            "startDate": date, "startMin": 0 if all_day else start,
            "endDate": date, "endMin": 0 if all_day else end, "allDay": all_day,
        })
    snapshot = {
        "version": 1, "generatedAt": now_utc - 300, "utcOffsetMin": offset_min,
        "rangeStart": today.isoformat(), "rangeEnd": (today + dt.timedelta(days=30)).isoformat(),
        "events": events,
    }
    (planner / "calendar.json").write_text(json.dumps(snapshot, ensure_ascii=False), encoding="utf-8")

    settings = {"language": "IT"}
    (sd / ".crosspoint" / "crossink-settings.json").write_text(json.dumps(settings), encoding="utf-8")


def to_png(bmp: Path, png: Path) -> None:
    from PIL import Image  # pylint: disable=import-outside-toplevel

    with Image.open(bmp) as im:
        im.convert("L").save(png, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default="simulator")
    parser.add_argument("--out", default=str(ROOT / "docs" / "images" / "planner"))
    parser.add_argument("--script", help="Override the input script (ms:KEY;...)")
    parser.add_argument("--shots", help="Override the screenshot schedule (ms:name;...)")
    args = parser.parse_args()

    program = ROOT / ".pio" / "build" / args.env / "program"
    if not program.exists():
        print(f"Simulator not built: {program}", file=sys.stderr)
        return 2

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    offset_min = 60
    now_utc = int(time.time())
    today = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(minutes=offset_min)).date()

    # Calendar <- Home -> To-Do List with the side buttons (UP / DOWN).
    script = args.script or "4000:DOWN;7000:UP;10000:UP;13000:UP;14500:QUIT"
    shots = args.shots or "3500:home;6500:todo;9500:home-again;12500:calendar"

    with tempfile.TemporaryDirectory(prefix="planner-shots-") as tmp_name:
        tmp = Path(tmp_name)
        sd = tmp / "sd"
        write_sd(sd, today, now_utc, offset_min)
        raw = tmp / "raw"
        raw.mkdir()
        schedule = ";".join(f"{item.split(':')[0]}:{raw / (item.split(':')[1] + '.bmp')}" for item in shots.split(";"))

        env = os.environ.copy()
        env.setdefault("SDL_VIDEODRIVER", "dummy")
        env["CROSSPOINT_SIM_SD"] = str(sd)
        env["CROSSPOINT_SIM_INPUT_SCRIPT"] = script
        env["CROSSPOINT_SIM_SCREENSHOTS"] = schedule
        proc = subprocess.run([str(program)], cwd=tmp, env=env, timeout=120,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        (out / "simulator.log").write_text(proc.stdout, encoding="utf-8")
        print(proc.stdout[-4000:])

        saved = 0
        for bmp in sorted(raw.glob("*.bmp")):
            to_png(bmp, out / (bmp.stem + ".png"))
            saved += 1
        shutil.copytree(sd / ".crosspoint" / "planner", out / "sample-sd", dirs_exist_ok=True)
    print(f"Saved {saved} screenshots to {out}")
    return 0 if saved else 1


if __name__ == "__main__":
    raise SystemExit(main())
