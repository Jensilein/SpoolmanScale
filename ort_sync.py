#!/usr/bin/env python3
"""
ort_sync.py - traegt in Spoolman bei jeder Spule den Ort ein (Feld "Ort"/location).

Quellen:
  * Bambuddy  -> welche Spule in welchem AMS-Fach des X1C steckt
  * CFSync    -> welche Spule in welchem CFS-Slot der beiden K2 Plus steckt
Spulen, die in keinem Fach stecken, bekommen den Lagerort (Standard "Lager").

Sicherheitsregeln:
  * Ist Bambuddy, CFSync oder Spoolman nicht erreichbar, wird in diesem Lauf
    gar nichts geaendert (sonst landeten alle Spulen kurz im "Lager").
  * Ein CFS-Slot zaehlt nur, wenn die Farbe, die der Drucker vom Tag meldet,
    zur Farbe der verknuepften Spoolman-Spule passt. Sonst: Warnung im Log,
    die Verknuepfung in CFSync ist dann vermutlich veraltet.
  * Steht eine Spule laut Bambuddy und CFSync an zwei Orten, gilt Bambuddy.
  * Ein selbst eingetragener Ort (z. B. "Regal oben") bleibt stehen, solange
    die Spule in keinem Fach steckt.
  * "dry_run": true (Standard beim ersten Einrichten) zeigt nur an, was
    geaendert wuerde.

Aufruf:   python3 ort_sync.py          einmal abgleichen
          python3 ort_sync.py --test   wie dry_run, unabhaengig von der Einstellung
Einstellungen: ort_sync.json im Ordner $FT_DIR (Standard: Home-Verzeichnis) (wird beim ersten Aufruf mit Standardwerten angelegt)
"""
import json
import logging
import os
import re
import sys

import requests

BASE_DIR = os.environ.get("FT_DIR") or os.path.expanduser("~")
CONFIG_PATH = os.path.join(BASE_DIR, "ort_sync.json")
LOG_PATH = os.path.join(BASE_DIR, "ort_sync.log")

DEFAULTS = {
    "dry_run": True,
    "spoolman_url": "http://192.168.178.40:7912",
    "bambuddy_url": "http://192.168.178.40:8109",
    "bambuddy_api_key": "HIER_API_SCHLUESSEL_EINTRAGEN",
    "cfsync_url": "http://192.168.178.86:8005",
    # Anzeigenamen der Drucker im Ort. Schluessel: Name in Bambuddy bzw. CFSync-ID (IP)
    "printer_names": {
        "X1C_Hobbykeller": "X1C",
        "192.168.178.108": "K2 B1E3",
        "192.168.178.194": "K2 BDE6",
    },
    "storage_location": "Lager",
    "color_tolerance": 60,      # maximaler RGB-Abstand Slotfarbe <-> Spoolman-Farbe
    "timeout_s": 10,
}

# Orte, die dieses Skript selbst setzt und daher auch wieder aendern darf.
# AMS-Format wie bei Bambuddy ("X1C - AMS A1"), damit Bambuddy sie als Fach und
# nicht als Lagerplatz erkennt.
MANAGED_RE = re.compile(r"^.+ - (AMS [A-Z]\d|CFS \d[A-D])$")

log = logging.getLogger("ort_sync")
frozen_prefixes = []   # Orte an Druckern ohne Live-Daten: nicht anfassen


def load_config():
    if not os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH, "w") as f:
            json.dump(DEFAULTS, f, indent=2, ensure_ascii=False)
        print(f"Einstellungsdatei angelegt: {CONFIG_PATH}\n"
              f"Bitte dort den Bambuddy-API-Schluessel eintragen und erneut starten.")
        sys.exit(1)
    with open(CONFIG_PATH) as f:
        cfg = json.load(f)
    merged = dict(DEFAULTS)
    merged.update(cfg)
    merged["printer_names"] = {**DEFAULTS["printer_names"], **cfg.get("printer_names", {})}
    return merged


def hex_rgb(h):
    h = (h or "").strip().lstrip("#")
    if not re.fullmatch(r"[0-9a-fA-F]{6}", h):
        return None
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def color_distance(a, b):
    ra, rb = hex_rgb(a), hex_rgb(b)
    if ra is None or rb is None:
        return None
    return sum((x - y) ** 2 for x, y in zip(ra, rb)) ** 0.5


def spool_color(spool):
    fil = spool.get("filament") or {}
    # Nur einfarbige Filamente haben eine vergleichbare Farbe. Ein mehrfarbiges
    # (multi_color_hexes) steht auf dem Tag als Grau 808080, also ohne Pruefung.
    return fil.get("color_hex") or None


def fetch_all(cfg):
    t = cfg["timeout_s"]
    sm = requests.get(cfg["spoolman_url"].rstrip("/") + "/api/v1/spool", timeout=t)
    sm.raise_for_status()
    bb = requests.get(cfg["bambuddy_url"].rstrip("/") + "/api/v1/spoolman/inventory/slot-assignments/all",
                      headers={"X-API-Key": cfg["bambuddy_api_key"]}, timeout=t)
    bb.raise_for_status()
    cf = requests.get(cfg["cfsync_url"].rstrip("/") + "/api/ui/state", timeout=t)
    cf.raise_for_status()
    return sm.json(), bb.json(), cf.json()["result"]["printers"]


def wanted_locations(cfg, spools, bambuddy, cfsync):
    """Liefert {spool_id: ort} fuer alle Spulen, die gerade in einem Fach stecken."""
    names = cfg["printer_names"]
    by_id = {s["id"]: s for s in spools}
    wanted = {}
    frozen_prefixes.clear()

    # AMS ueber Bambuddy
    for a in bambuddy:
        sid = a.get("spoolman_spool_id")
        if sid not in by_id:
            continue
        printer = names.get(a.get("printer_name"), a.get("printer_name") or "Bambu")
        loc = f"{printer} - AMS {chr(65 + int(a['ams_id']))}{int(a['tray_id']) + 1}"
        wanted[sid] = loc

    # CFS ueber CFSync. Massgeblich ist der Live-Zustand vom Drucker (cfs_slots):
    # nur ein belegter Slot zaehlt, und seine Tag-Farbe muss zur Spule passen.
    # Die gespeicherte Verknuepfung (slots) allein bleibt nach dem Herausnehmen stehen.
    cfs_hits = {}
    for p in cfsync:
        st = p.get("state", {}) or {}
        printer = names.get(p.get("id"), st.get("printer_name") or p.get("id"))
        live = st.get("cfs_slots") or {}
        if not st.get("cfs_connected") or not live:
            log.warning(f"CFSync {printer}: keine Live-Daten vom CFS - Orte an diesem Drucker bleiben unveraendert")
            frozen_prefixes.append(f"{printer} - CFS ")
            continue
        for slot, v in (st.get("slots") or {}).items():
            sid = v.get("spoolman_id")
            if not sid or not re.fullmatch(r"\d[A-D]", slot):
                continue
            if sid not in by_id:
                log.warning(f"CFSync {printer} {slot}: verknuepft mit Spule #{sid}, die es in Spoolman nicht gibt")
                continue
            lv = live.get(slot) or {}
            if not lv.get("present"):
                log.info(f"CFSync {printer} {slot}: noch mit #{sid} verknuepft, Slot ist aber leer - zaehlt nicht")
                continue
            d = color_distance(lv.get("color"), spool_color(by_id[sid]))
            if d is not None and d > cfg["color_tolerance"]:
                log.warning(f"CFSync {printer} {slot}: verknuepft mit #{sid}, aber die eingelegte Spule hat die Farbe "
                            f"{lv.get('color')} statt #{spool_color(by_id[sid])} - Verknuepfung in CFSync pruefen")
                continue
            cfs_hits.setdefault(sid, []).append(f"{printer} - CFS {slot}")

    for sid, locs in cfs_hits.items():
        if len(locs) > 1:
            log.warning(f"Spule #{sid} ist in CFSync mehrfach verknuepft ({', '.join(locs)}) - Ort bleibt unveraendert")
            wanted[sid] = None          # None = nicht anfassen
            continue
        if sid in wanted and wanted[sid]:
            log.warning(f"Spule #{sid}: Bambuddy meldet {wanted[sid]}, CFSync {locs[0]} - Bambuddy gilt")
            continue
        wanted[sid] = locs[0]
    return wanted


def run(cfg, dry):
    try:
        spools, bambuddy, cfsync = fetch_all(cfg)
    except Exception as e:
        log.error(f"Abbruch, nichts geaendert: {e}")
        return 1

    wanted = wanted_locations(cfg, spools, bambuddy, cfsync)
    storage = cfg["storage_location"]
    changes = 0
    for s in sorted(spools, key=lambda x: x["id"]):
        if s.get("archived"):
            continue
        sid = s["id"]
        current = (s.get("location") or "").strip()
        if any(current.startswith(fp) for fp in frozen_prefixes) and sid not in wanted:
            continue
        if sid in wanted:
            target = wanted[sid]
            if target is None:
                continue
        else:
            # Steckt in keinem Fach: nur eigene Fach-Orte oder leere Orte auf Lager setzen
            if current and current != storage and not MANAGED_RE.match(current):
                continue
            target = storage
        if current == target:
            continue
        changes += 1
        txt = f"Spule #{sid}: '{current or 'kein Ort'}' -> '{target}'"
        if dry:
            log.info("[Test] " + txt)
            continue
        try:
            r = requests.patch(f"{cfg['spoolman_url'].rstrip('/')}/api/v1/spool/{sid}",
                               json={"location": target}, timeout=cfg["timeout_s"])
            r.raise_for_status()
            log.info(txt)
        except Exception as e:
            log.error(f"{txt} fehlgeschlagen: {e}")
    if changes == 0:
        log.debug("Alle Orte aktuell")
    return 0


def main():
    test = "--test" in sys.argv
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s",
                        handlers=[logging.FileHandler(LOG_PATH), logging.StreamHandler()])
    cfg = load_config()
    sys.exit(run(cfg, dry=test or bool(cfg.get("dry_run"))))


if __name__ == "__main__":
    main()
