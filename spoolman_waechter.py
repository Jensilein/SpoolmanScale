#!/usr/bin/env python3
"""
spoolman_waechter.py - prueft Spoolman, CFSync und die Dienste auf typische
Eingabe- und Bedienfehler und meldet sie per Pushover.

Spulen
  tag_unbeschrieben  Aufkleber verknuepft ("Tag's"), aber nie als CFS-Tag beschrieben
  ein_tag            nur ein (oder kein) Tag an der Spule, angelegt vor mehr als 1 Stunde
  kein_leergewicht   weder bei Spule, Filament noch Hersteller ein Leergewicht
  ntag               NTAG-UID eingetragen (taugt nicht als CFS-Tag)
  uid_doppelt        dieselbe UID an zwei Spulen
Filamente (nur ab "pruefen_ab" angelegte)
  extid              Externe ID doppelt (geklont, nicht geleert)
  gewicht_spule      "Gewicht Spule" beim Filament gesetzt
  hersteller_im_namen
  keine_farbe
  silk_ohne_pla
CFS (ueber CFSync)
  cfs_ohne_tag       belegter Slot ohne Tag (Zustand 1): K2 zeigt alte Filamentdaten
  cfs_farbe          Slotfarbe passt nicht zur verknuepften Spule
Dienste
  dienst             Spoolman / CFSync / Bambuddy / Spoolman-Waage nicht erreichbar,
                     ort_sync.py meldet Fehler

Laeuft jede Minute. Gemeldet wird ein Problem erst, wenn es seine Wartezeit
lang ununterbrochen besteht (Filamente 2 min, nicht beschriebener Aufkleber
10 min, ...; siehe WARTEZEIT), und dann nur einmal. Ist es behoben, wird es vergessen. Fuer Dienste
kommt zusaetzlich eine Meldung, wenn sie wieder laufen.

Aufruf:   python3 spoolman_waechter.py          pruefen und melden
          python3 spoolman_waechter.py --test   alles anzeigen, nichts senden, nichts merken
          python3 spoolman_waechter.py --testmail  eine Testmail schicken
Einstellungen: spoolman_waechter.json im Ordner $FT_DIR (Standard: Home), beim ersten Lauf angelegt;
               E-Mail wahlweise in mail.json im selben Ordner
               Pushover aus spoolman_waechter.json oder waage_config.json, Adressen aus ort_sync.json
"""
import html
import json
import smtplib
import logging
import os
import re
import sys
import time
from datetime import date, datetime, timezone
from email.message import EmailMessage

import requests

HOME = os.environ.get("FT_DIR") or os.path.expanduser("~")
CFG_PATH = os.path.join(HOME, "spoolman_waechter.json")
STATE_PATH = os.path.join(HOME, ".spoolman_waechter_state.json")
LOG_PATH = os.path.join(HOME, "spoolman_waechter.log")
WAAGE_CONFIG = os.path.join(HOME, "waage_config.json")
ORT_CONFIG = os.path.join(HOME, "ort_sync.json")
ORT_LOG = os.path.join(HOME, "ort_sync.log")
PUSHOVER_API = "https://api.pushover.net/1/messages.json"

DEFAULTS = {
    "pruefen_ab": None,                 # Filamente ab diesem Datum pruefen
    "spoolman_waage_url": "",           # z. B. "http://spoolmanscale.local" - leer = nicht pruefen
    "filastation_waage_url": "",        # z. B. "http://192.168.178.xx:5000" - leer = nicht pruefen
    "pushover_user_key": "",            # leer = aus waage_config.json (low_stock) nehmen
    "pushover_app_token": "",
    # E-Mail (zusaetzlich zu Pushover): alle neuen Funde eines Laufs in einer Mail
    "mail_aktiv": False,
    "mail_server": "",                  # z. B. "mx.freenet.de"
    "mail_port": 587,                   # 587 = STARTTLS, 465 = SSL
    "mail_benutzer": "",                # meist die volle Mailadresse
    "mail_passwort": "",
    "mail_von": "",                     # leer = mail_benutzer
    "mail_an": "",
    "ein_tag_nach_minuten": 60,
    "farbabstand": 60,
    "ignorieren": [],                   # Schluessel aus dem Testlauf, z. B. "spule:9:ein_tag"
    "wartezeiten": {},                  # z. B. {"tag_unbeschrieben": 15} - ueberschreibt WARTEZEIT
}

# Minuten, die ein Fund ununterbrochen bestehen muss, bevor er gemeldet wird.
# Kurz, wo ein Fehler nach dem Speichern feststeht; laenger, wo der normale
# Ablauf ihn kurz erzeugt (Aufkleber verknuepft, Beschreiben folgt gleich).
WARTEZEIT = {
    "extid": 2, "gewicht_spule": 2, "hersteller_im_namen": 2, "keine_farbe": 2, "silk_ohne_pla": 2,
    "kein_leergewicht": 3, "ntag": 3, "uid_doppelt": 3,
    "tag_unbeschrieben": 10, "ein_tag": 0,     # ein_tag wartet schon ueber das Alter der Spule
    "cfs_ohne_tag": 5, "cfs_farbe": 5,
    "dienst": 5,
}

log = logging.getLogger("waechter")


def load_json(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return default


def save_json(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    os.replace(tmp, path)


def norm(s):
    return re.sub(r"[^a-z0-9+]", "", (s or "").lower())


def norm_uid(u):
    return re.sub(r"[^0-9A-F]", "", (u or "").upper())


def nfc_uids(spool):
    raw = (spool.get("extra") or {}).get("nfc_uid") or ""
    try:
        raw = json.loads(raw)
    except Exception:
        pass
    return [norm_uid(x) for x in re.split(r"[,\s;]+", str(raw or "")) if norm_uid(x)]


def native_uids(spool):
    return [norm_uid(t.get("uid")) for t in (spool.get("tags") or []) if norm_uid(t.get("uid"))]


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


def age_minutes(iso):
    try:
        t = datetime.fromisoformat((iso or "").replace("Z", "+00:00"))
        if t.tzinfo is None:
            t = t.replace(tzinfo=timezone.utc)
        return (datetime.now(timezone.utc) - t).total_seconds() / 60
    except Exception:
        return 1e9


class Findings:
    def __init__(self):
        self.items = {}   # key -> (gruppe, text, link)

    def add(self, key, gruppe, text, link=""):
        self.items[key] = (gruppe, text, link)


def is_bambu(spool):
    vendor = ((spool.get("filament") or {}).get("vendor") or {}).get("name") or ""
    return "bambu" in vendor.lower()


def spool_label(s):
    f = s.get("filament") or {}
    v = (f.get("vendor") or {}).get("name") or ""
    return f"#{s['id']} {v} {f.get('name') or ''}".strip()


# ------------------------------------------------------------------ Spulen
def check_spools(F, cfg, sm, spools):
    owners = {}
    for s in spools:
        if s.get("archived"):
            continue
        sid = s["id"]
        lbl = spool_label(s)
        link = f"{sm}/spool/show/{sid}"
        nat, cfs = native_uids(s), nfc_uids(s)
        allu = sorted(set(nat) | set(cfs))
        for u in allu:
            owners.setdefault(u, []).append(sid)

        if not is_bambu(s):
            unwritten = [u for u in nat if u not in cfs]
            if unwritten:
                F.add(f"spule:{sid}:tag_unbeschrieben", lbl,
                      f"Aufkleber {', '.join(unwritten)} verknuepft, aber nie als CFS-Tag beschrieben "
                      f"- an der Spoolman-Waage beschreiben oder in Spoolman unter Tag's entfernen", link)
            if len(allu) < 2 and age_minutes(s.get("registered")) > cfg["ein_tag_nach_minuten"]:
                F.add(f"spule:{sid}:ein_tag", lbl,
                      f"nur {len(allu)} Tag - zweiten Aufkleber verknuepfen und beschreiben", link)

        f = s.get("filament") or {}
        v = f.get("vendor") or {}
        if not (s.get("spool_weight") or f.get("spool_weight") or v.get("empty_spool_weight")):
            F.add(f"spule:{sid}:kein_leergewicht", lbl,
                  "kein Leergewicht (weder Spule, Filament noch Hersteller) - Rest ist zu hoch", link)

        ntags = [u for u in allu if len(u) == 14 and u.startswith("04")]
        if ntags:
            F.add(f"spule:{sid}:ntag", lbl,
                  f"NTAG {', '.join(ntags)} eingetragen - taugt nicht als CFS-Tag, entfernen", link)

    for u, ids in owners.items():
        if len(set(ids)) > 1:
            ids = sorted(set(ids))
            F.add(f"uid:{u}:uid_doppelt", f"UID {u}",
                  f"steht an mehreren Spulen: {', '.join('#' + str(i) for i in ids)}",
                  f"{sm}/spool/show/{ids[0]}")


# ------------------------------------------------------------------ Filamente
def check_filaments(F, cfg, sm, fils, ab):
    for fil in fils:
        if (fil.get("registered") or "")[:10] < ab:
            continue
        fid = fil["id"]
        vendor = (fil.get("vendor") or {}).get("name") or ""
        name = fil.get("name") or ""
        lbl = f"Filament {vendor} {name}".strip()
        link = f"{sm}/filament/show/{fid}"
        material = (fil.get("material") or "").strip()

        ext = (fil.get("external_id") or "").strip()
        if ext:
            twins = [x for x in fils if x["id"] != fid and (x.get("external_id") or "").strip() == ext]
            if twins:
                F.add(f"filament:{fid}:extid", lbl,
                      f"Externe ID wie bei \"{twins[0].get('name')}\" (#{twins[0]['id']}) - "
                      f"vermutlich geklont, Externe ID leeren", link)
        sw = fil.get("spool_weight")
        if sw:
            F.add(f"filament:{fid}:gewicht_spule", lbl,
                  f"\"Gewicht Spule\" {sw:g} g beim Filament - leeren, ausser die Originalspule "
                  f"dieser Sorte wiegt anders als beim Hersteller", link)
        if vendor and norm(vendor) and norm(vendor) in norm(name):
            F.add(f"filament:{fid}:hersteller_im_namen", lbl,
                  f"Herstellername \"{vendor}\" steht im Namen - entfernen", link)
        if not (fil.get("color_hex") or fil.get("multi_color_hexes")):
            F.add(f"filament:{fid}:keine_farbe", lbl, "keine Farbe eingetragen", link)
        if "silk" in name.lower() and not material.upper().startswith("PLA"):
            F.add(f"filament:{fid}:silk_ohne_pla", lbl,
                  f"\"Silk\" im Namen, Material \"{material}\" - kein Silk-Profil am K2", link)


# ------------------------------------------------------------------ CFS
def check_cfs(F, cfg, sm, spools, printers, ort):
    names = ort.get("printer_names") or {}
    by_id = {s["id"]: s for s in spools}
    cf_url = ort.get("cfsync_url") or ""
    for p in printers:
        st = p.get("state") or {}
        pname = names.get(p.get("id"), st.get("printer_name") or p.get("id"))
        live = st.get("cfs_slots") or {}
        links = st.get("slots") or {}
        if not st.get("cfs_connected") or not live:
            continue
        for slot, lv in live.items():
            if not re.fullmatch(r"\d[A-D]", slot) or not lv.get("present"):
                continue
            if lv.get("state") == 1:
                F.add(f"cfs:{p.get('id')}:{slot}:cfs_ohne_tag", f"{pname} CFS {slot}",
                      "Spule ohne Tag eingelegt - der K2 zeigt die zuletzt eingestellten Filamentdaten. "
                      "Filament am Drucker pruefen oder Aufkleber anbringen", cf_url)
                continue
            sid = (links.get(slot) or {}).get("spoolman_id")
            if not sid or sid not in by_id:
                continue
            f = by_id[sid].get("filament") or {}
            if f.get("multi_color_hexes"):
                continue
            d = color_distance(lv.get("color"), f.get("color_hex"))
            if d is not None and d > cfg["farbabstand"]:
                F.add(f"cfs:{p.get('id')}:{slot}:cfs_farbe", f"{pname} CFS {slot}",
                      f"Slot ist mit #{sid} verknuepft (#{f.get('color_hex')}), eingelegt ist aber "
                      f"Farbe {lv.get('color')} - Verknuepfung in CFSync pruefen", cf_url)


# ------------------------------------------------------------------ Dienste
def check_url(F, key, name, url, headers=None):
    try:
        r = requests.get(url, headers=headers or {}, timeout=8)
        if r.status_code >= 500:
            raise RuntimeError(f"HTTP {r.status_code}")
        return True
    except Exception as e:
        F.add(f"dienst:{key}", "Dienst", f"{name} nicht erreichbar ({str(e)[:80]})", url)
        return False


def check_ort_log(F):
    try:
        with open(ORT_LOG, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4000))
            tail = f.read().decode("utf-8", "replace").splitlines()[-5:]
    except FileNotFoundError:
        return
    if os.path.getmtime(ORT_LOG) < time.time() - 1800:
        return
    errs = [l for l in tail if "[ERROR]" in l]
    if errs:
        F.add("dienst:ort_sync", "Dienst", f"ort_sync.py meldet: {errs[-1][-160:]}")


# ------------------------------------------------------------------ Melden
def send_pushover(po, title, msg, url):
    data = {"token": po["pushover_app_token"], "user": po["pushover_user_key"],
            "title": title[:250], "message": msg[:1000]}
    if url:
        data["url"] = url
        data["url_title"] = "Oeffnen"
    if po.get("pushover_device"):
        data["device"] = po["pushover_device"]
    requests.post(PUSHOVER_API, data=data, timeout=10).raise_for_status()


def mail_ok(cfg):
    return bool(cfg.get("mail_aktiv") and cfg.get("mail_server") and cfg.get("mail_an"))


def send_mail(cfg, subject, blocks):
    """blocks: Liste von (Ueberschrift, [Texte], Link)"""
    plain, rich = [], []
    for title, texts, link in blocks:
        plain.append(title)
        plain += ["  - " + t for t in texts]
        if link:
            plain.append("  " + link)
        plain.append("")
        items = "".join(f"<li>{html.escape(t)}</li>" for t in texts)
        a = (f'<p style="margin:4px 0 0"><a href="{html.escape(link)}">In Spoolman oeffnen</a> '
             f'<span style="color:#888">{html.escape(link)}</span></p>') if link else ""
        rich.append(f'<div style="margin:0 0 16px"><b>{html.escape(title)}</b><ul style="margin:4px 0">'
                    f'{items}</ul>{a}</div>')
    msg = EmailMessage()
    msg["Subject"] = subject
    msg["From"] = cfg.get("mail_von") or cfg.get("mail_benutzer")
    msg["To"] = cfg["mail_an"]
    msg.set_content("\n".join(plain) + "\n-- spoolman_waechter.py")
    msg.add_alternative('<div style="font-family:sans-serif;font-size:14px">' + "".join(rich)
                        + '<p style="color:#888;font-size:12px">spoolman_waechter.py</p></div>',
                        subtype="html")
    port = int(cfg.get("mail_port") or 587)
    if port == 465:
        srv = smtplib.SMTP_SSL(cfg["mail_server"], port, timeout=20)
    else:
        srv = smtplib.SMTP(cfg["mail_server"], port, timeout=20)
        srv.starttls()
    with srv:
        if cfg.get("mail_benutzer"):
            srv.login(cfg["mail_benutzer"], cfg.get("mail_passwort", ""))
        srv.send_message(msg)


def main():
    test = "--test" in sys.argv
    testmail = "--testmail" in sys.argv
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s",
                        handlers=[logging.FileHandler(LOG_PATH), logging.StreamHandler()])

    cfg = dict(DEFAULTS)
    cfg.update(load_json(CFG_PATH, {}))
    # Mail-Einstellungen duerfen auch in einer eigenen Datei mail.json stehen
    # (dann muss spoolman_waechter.json nicht von Hand bearbeitet werden)
    mail_extra = load_json(os.path.join(HOME, "mail.json"), {})
    if mail_extra:
        cfg.update(mail_extra)
        cfg.setdefault("mail_aktiv", True)
        if "mail_aktiv" not in mail_extra:
            cfg["mail_aktiv"] = True
    if not cfg.get("pruefen_ab"):
        cfg["pruefen_ab"] = date.today().isoformat()
    if not os.path.exists(CFG_PATH) and not test:
        save_json(CFG_PATH, cfg)
        log.info(f"Einstellungen angelegt: {CFG_PATH}")

    if testmail:
        if not mail_ok(cfg):
            print("E-Mail nicht eingerichtet: mail_aktiv, mail_server und mail_an in spoolman_waechter.json setzen")
            return 1
        try:
            send_mail(cfg, "Spoolman-Waechter: Testmail",
                      [("Testmail", ["Wenn du das liest, kommen die Meldungen per E-Mail an."], "")])
            print(f"Testmail an {cfg['mail_an']} gesendet")
            return 0
        except Exception as e:
            print(f"Testmail fehlgeschlagen: {e}")
            return 1

    wcfg = load_json(WAAGE_CONFIG, {})
    ort = load_json(ORT_CONFIG, {})
    sm = ((wcfg.get("spoolman") or {}).get("url") or ort.get("spoolman_url")
          or "http://192.168.178.40:7912").rstrip("/")
    po = dict(wcfg.get("low_stock") or {})
    if cfg.get("pushover_user_key") and cfg.get("pushover_app_token"):
        po.update(pushover_user_key=cfg["pushover_user_key"], pushover_app_token=cfg["pushover_app_token"])
    po_ok = bool(po.get("pushover_user_key")) and bool(po.get("pushover_app_token"))

    F = Findings()
    spools = fils = None
    if check_url(F, "spoolman", "Spoolman", sm + "/api/v1/health"):
        try:
            spools = requests.get(sm + "/api/v1/spool", timeout=15).json()
            fils = requests.get(sm + "/api/v1/filament", timeout=15).json()
        except Exception as e:
            F.add("dienst:spoolman", "Dienst", f"Spoolman antwortet fehlerhaft ({str(e)[:80]})", sm)
    if spools is not None:
        check_spools(F, cfg, sm, spools)
        check_filaments(F, cfg, sm, fils, cfg["pruefen_ab"])

    cf = (ort.get("cfsync_url") or "").rstrip("/")
    if cf:
        try:
            r = requests.get(cf + "/api/ui/state", timeout=8)
            r.raise_for_status()
            printers = r.json()["result"]["printers"]
            if spools is not None:
                check_cfs(F, cfg, sm, spools, printers, ort)
        except Exception as e:
            F.add("dienst:cfsync", "Dienst", f"CFSync nicht erreichbar ({str(e)[:80]})", cf)
    bb = (ort.get("bambuddy_url") or "").rstrip("/")
    if bb:
        check_url(F, "bambuddy", "Bambuddy", bb + "/api/v1/spoolman/inventory/slot-assignments/all",
                  {"X-API-Key": ort.get("bambuddy_api_key", "")})
    if cfg.get("spoolman_waage_url"):
        check_url(F, "spoolman_waage", "Spoolman-Waage", cfg["spoolman_waage_url"].rstrip("/") + "/")
    if cfg.get("filastation_waage_url"):
        check_url(F, "filastation_waage", "FilaStation-Waage", cfg["filastation_waage_url"].rstrip("/") + "/")
    check_ort_log(F)

    ign = set(cfg.get("ignorieren") or [])
    current = {k: v for k, v in F.items.items() if k not in ign}

    if test:
        if not current:
            print("Keine Auffaelligkeiten.")
        for k, (g, t, l) in sorted(current.items()):
            print(f"{g}\n   - {t}\n     [{k}]")
        if ign & set(F.items):
            print(f"\n({len(ign & set(F.items))} Meldung(en) per 'ignorieren' stummgeschaltet)")
        return 0

    state = load_json(STATE_PATH, {})
    seit = dict(state.get("seit", {}))          # Schluessel -> erstmals gesehen (Unixzeit)
    gemeldet = dict(state.get("gemeldet", {}))
    jetzt = time.time()
    warte = dict(WARTEZEIT)
    warte.update(cfg.get("wartezeiten") or {})
    for k in current:
        seit.setdefault(k, jetzt)
    seit = {k: t for k, t in seit.items() if k in current}

    def faellig(k):
        art = "dienst" if k.startswith("dienst:") else k.rsplit(":", 1)[-1]
        return jetzt - seit[k] >= 60 * float(warte.get(art, 5)) - 5

    # Neue Meldungen: Wartezeit abgelaufen, noch nicht gemeldet
    neu = {k: v for k, v in current.items() if k not in gemeldet and faellig(k)}
    # Dienste wieder da
    wieder = [k for k in gemeldet if k.startswith("dienst:") and k not in current]

    gruppen = {}
    for k, (g, t, l) in neu.items():
        gruppen.setdefault(g, {"texte": [], "link": l, "keys": []})
        gruppen[g]["texte"].append(t)
        gruppen[g]["keys"].append(k)
    po_fail = set()
    for g, d in gruppen.items():
        msg = "\n".join("• " + t for t in d["texte"])
        log.info(f"{g}: {msg}")
        if po_ok:
            try:
                send_pushover(po, f"Pruefen: {g}", msg, d["link"])
            except Exception as e:
                log.warning(f"Pushover fehlgeschlagen: {e}")
                po_fail.add(g)
    mail_sent = False
    if gruppen and mail_ok(cfg):
        try:
            n = sum(len(d["texte"]) for d in gruppen.values())
            send_mail(cfg, f"Spoolman-Waechter: {n} Punkt(e) pruefen",
                      [(g, d["texte"], d["link"]) for g, d in gruppen.items()])
            mail_sent = True
            log.info(f"E-Mail an {cfg['mail_an']} gesendet")
        except Exception as e:
            log.warning(f"E-Mail fehlgeschlagen: {e}")
    for g, d in gruppen.items():
        if g not in po_fail or mail_sent:
            for k in d["keys"]:
                gemeldet[k] = current[k][1]
    for k in wieder:
        log.info(f"wieder in Ordnung: {gemeldet[k]}")
        if po_ok:
            try:
                send_pushover(po, "Dienst wieder erreichbar", "✓ behoben: " + gemeldet[k], "")
            except Exception as e:
                log.warning(f"Pushover fehlgeschlagen: {e}")
    if wieder and mail_ok(cfg):
        try:
            send_mail(cfg, "Spoolman-Waechter: wieder in Ordnung",
                      [("Wieder in Ordnung", [gemeldet[k] for k in wieder], "")])
        except Exception as e:
            log.warning(f"E-Mail fehlgeschlagen: {e}")

    # Behobenes vergessen, damit es bei erneutem Auftreten wieder gemeldet wird
    gemeldet = {k: v for k, v in gemeldet.items() if k in current}
    save_json(STATE_PATH, {"seit": seit, "gemeldet": gemeldet})
    if not po_ok and neu:
        log.warning("Pushover nicht eingerichtet (waage_config.json -> low_stock) - nur im Log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
