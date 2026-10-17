#!/usr/bin/env python3
"""
runner.py - startet die Filament-Werkzeuge im Takt (fuer den Docker-Container
"filament-tools" auf der Synology, laeuft aber auch auf jedem anderen Rechner).

  ort_sync.py           alle 5 Minuten  (Minute 0, 5, 10, ...)
  spoolman_waechter.py  alle 15 Minuten (Minute 2, 17, 32, 47)

Beim Start und danach einmal am Tag werden die Skripte frisch aus dem
GitHub-Zweig geladen. Ist GitHub nicht erreichbar, laufen die zuletzt
geladenen weiter. Einstellungen und Logs liegen in $FT_DIR (Standard /data).
Logs ueber 2 MB werden in *.log.1 verschoben.
"""
import os
import subprocess
import sys
import time
import urllib.request
from datetime import datetime

DATA = os.environ.get("FT_DIR", "/data")
APP = os.environ.get("FT_APP", "/app")
BRANCH = os.environ.get("FT_BRANCH", "filastation-tools")
RAW = f"https://raw.githubusercontent.com/Jensilein/SpoolmanScale/{BRANCH}/"
SCRIPTS = ["ort_sync.py", "spoolman_waechter.py"]
JOBS = [  # (Skript, Takt in Minuten, Versatz)
    ("ort_sync.py", 5, 0),
    ("spoolman_waechter.py", 15, 2),
]
LOGS = ["ort_sync.log", "spoolman_waechter.log"]


def say(msg):
    print(f"{datetime.now():%Y-%m-%d %H:%M:%S} [runner] {msg}", flush=True)


def fetch():
    os.makedirs(APP, exist_ok=True)
    for name in SCRIPTS:
        try:
            with urllib.request.urlopen(RAW + name, timeout=20) as r:
                body = r.read()
            tmp = os.path.join(APP, name + ".tmp")
            with open(tmp, "wb") as f:
                f.write(body)
            os.replace(tmp, os.path.join(APP, name))
            say(f"{name} geladen ({len(body)} Byte)")
        except Exception as e:
            if os.path.exists(os.path.join(APP, name)):
                say(f"{name} nicht geladen ({e}) - vorhandene Fassung bleibt")
            else:
                say(f"{name} nicht geladen und keine Fassung vorhanden: {e}")


def rotate():
    for name in LOGS:
        p = os.path.join(DATA, name)
        try:
            if os.path.getsize(p) > 2 * 1024 * 1024:
                os.replace(p, p + ".1")
                say(f"{name} rotiert")
        except FileNotFoundError:
            pass


def run(name):
    path = os.path.join(APP, name)
    if not os.path.exists(path):
        return
    env = dict(os.environ, FT_DIR=DATA)
    try:
        subprocess.run([sys.executable, path], env=env, cwd=DATA, timeout=240)
    except subprocess.TimeoutExpired:
        say(f"{name} nach 4 Minuten abgebrochen")
    except Exception as e:
        say(f"{name} Fehler: {e}")


def main():
    os.makedirs(DATA, exist_ok=True)
    say(f"Start - Einstellungen und Logs in {DATA}, Zweig {BRANCH}")
    fetch()
    last_fetch_day = datetime.now().date()
    last_minute = None
    while True:
        now = datetime.now()
        minute = now.hour * 60 + now.minute
        if minute != last_minute:
            last_minute = minute
            if now.date() != last_fetch_day and now.hour == 4:
                fetch()
                last_fetch_day = now.date()
            rotate()
            for name, every, offset in JOBS:
                if now.minute % every == offset:
                    run(name)
        time.sleep(max(1, 60 - datetime.now().second))


if __name__ == "__main__":
    main()
