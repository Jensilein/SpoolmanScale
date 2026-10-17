#!/usr/bin/env python3
import json, re, requests
SM = "http://192.168.178.40:7912/api/v1"
spools = requests.get(SM + "/spool", timeout=10).json()
uid_re = re.compile(r"\b(?:[0-9A-Fa-f]{2}:){3,6}[0-9A-Fa-f]{2}\b|\b[0-9A-Fa-f]{14}\b|\b[0-9A-Fa-f]{8}\b")
seen = {}
warn = []
for s in sorted(spools, key=lambda x: x["id"]):
    sid = s["id"]; f = s.get("filament") or {}; v = f.get("vendor") or {}
    name = f.get("name") or ""; vend = v.get("name") or ""
    farbe = f.get("color_hex") or ("mehrfarbig " + (f.get("multi_color_hexes") or ""))
    print(f"\n#{sid} {'[ARCHIVIERT] ' if s.get('archived') else ''}{vend} | {name} | {f.get('material')} | {farbe}")
    print(f"   Rest {s.get('remaining_weight')} g | Start {s.get('initial_weight')} g | Spule {s.get('spool_weight')} g"
          f" | Filament-Gewicht-Spule {f.get('spool_weight')} | Ort: {s.get('location') or '-'}")
    uids = []
    for k, val in (s.get("extra") or {}).items():
        try: val = json.loads(val)
        except Exception: pass
        print(f"   extra.{k} = {val}")
        for u in uid_re.findall(str(val)):
            uids.append(u.upper().replace(":", ""))
    if s.get("archived"): continue
    for u in uids:
        if len(u) == 14 and u.startswith("04"): warn.append(f"#{sid}: NTAG-UID {u} noch eingetragen")
        if u in seen and seen[u] != sid: warn.append(f"UID {u} an #{seen[u]} UND #{sid}")
        seen[u] = sid
    if len(uids) > 2: warn.append(f"#{sid}: {len(uids)} UIDs eingetragen (erwartet 2)")
    if len(uids) == 0: warn.append(f"#{sid}: kein Tag eingetragen")
    if not s.get("spool_weight"): warn.append(f"#{sid}: Spulengewicht fehlt")
    if f.get("spool_weight"): warn.append(f"#{sid}: 'Gewicht Spule' beim Filament gesetzt ({f.get('spool_weight')} g)")
    if vend and vend.lower() in name.lower(): warn.append(f"#{sid}: Hersteller steht im Filamentnamen")
    if not s.get("location"): warn.append(f"#{sid}: kein Ort")
    if s.get("initial_weight") and s.get("remaining_weight") and s["remaining_weight"] > s["initial_weight"] + 0.5:
        warn.append(f"#{sid}: Rest > Startgewicht")
print("\n===== AUFFAELLIGKEITEN =====")
print("\n".join(warn) if warn else "keine")
