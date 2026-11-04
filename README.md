# FilaStation-Werkzeuge

Hilfsskripte rund um Spoolman, CFSync und Bambuddy. Nicht Teil der Firmware der Spoolman-Waage.
Sie laufen im Container `filament-tools` auf der Synology DS920+ (Container Manager).

## Was läuft

| Skript | Takt | Aufgabe |
|---|---|---|
| `ort_sync.py` | alle 5 min (Minute 0, 5, 10 …) | Trägt in Spoolman den Ort jeder Spule ein: AMS-Fach über Bambuddy, CFS-Slot über CFSync, sonst `Lager`. Ändert nichts, wenn ein Dienst fehlt oder ein K2 keine Live-Daten liefert. |
| `spoolman_waechter.py` | jede Minute | Prüft Spulen, neue Filamente, CFS-Slots und Dienste auf typische Fehler. Meldet per Pushover und E-Mail (mit Links), sobald ein Fehler seine Wartezeit lang besteht (siehe unten), und dann nur einmal. |
| `runner.py` | dauerhaft | Taktgeber im Container; lädt die beiden Skripte beim Start und täglich um 4 Uhr frisch aus diesem Zweig. Ohne GitHub läuft die zuletzt geladene Fassung weiter. |
| `pruefung.py` | von Hand | Gesamtübersicht aller Spulen (läuft auf der FilaStation-Waage). |

```
                 +--------------------- DS920+ ----------------------+
                 |  Spoolman  <---------+                            |
                 |  Bambuddy  <-----+   |                            |
                 |  filament-tools (Container)                       |
                 |    runner.py --- alle 5 min ---> ort_sync.py      |
                 |              +-- jede Minute --> spoolman_waechter.py --> Pushover
                 +------------------|--------------------------------+
                                    v
                       CFSync (Mini-PC) --> K2 links / K2 rechts
```

## Ordner auf der Synology: `docker/filament-tools`

| Datei | Inhalt |
|---|---|
| `compose.yaml` | Bauplan des Containers (diese Datei hier im Zweig) |
| `ort_sync.json` | Adressen von Spoolman, Bambuddy, CFSync, Bambuddy-API-Schlüssel, Druckernamen, Lagerort |
| `spoolman_waechter.json` | Pushover-Schlüssel, Prüfbeginn für Filamente, optionale Adressen der Waagen, Liste `ignorieren` |
| `mail.json` | E-Mail-Versand: `mail_server`, `mail_port`, `mail_benutzer`, `mail_passwort`, `mail_von`, `mail_an` |
| `*.log` | Protokolle (über 2 MB → `*.log.1`) |
| `.spoolman_waechter_state.json` | was der Wächter schon gemeldet hat |
| `ort_sync.fails` | wie oft `ort_sync.py` zuletzt hintereinander abgebrochen hat |

## Nachschauen

- Container Manager → Container `filament-tools` → Protokoll.
- Testläufe ohne Änderungen (Container → Terminal → `sh`):

      python /app/spoolman_waechter.py --test
      python /app/ort_sync.py --test
      python /app/spoolman_waechter.py --testmail

- Update sofort statt um 4 Uhr: Projekt `filament-tools` → Aktion → Neu starten.

## Neu einrichten

Ordner `docker/filament-tools` anlegen, `compose.yaml`, `ort_sync.json`, `spoolman_waechter.json` und `mail.json` hineinlegen, im Container Manager ein Projekt `filament-tools` mit diesem Ordner und der vorhandenen `compose.yaml` erstellen. Die JSON-Dateien enthalten Schlüssel: mitsichern, nicht veröffentlichen.

## Wartezeiten des Wächters

Der Wächter prüft jede Minute, meldet einen Fehler aber erst, wenn er so lange ununterbrochen besteht:

| Prüfung | Wartezeit |
|---|---|
| Filament (Externe ID doppelt, Gewicht Spule, Hersteller im Namen, keine Farbe, Silk ohne PLA) | 2 min |
| Spule ohne Leergewicht, NTAG statt MIFARE, UID doppelt | 3 min |
| Aufkleber verknüpft, aber nicht beschrieben | 10 min |
| Spule mit nur einem oder keinem Tag (nicht bei leeren Spulen, Rest 0 g) | 10 min, frühestens 60 min nach Anlegen der Spule |
| CFS-Slot ohne Tag / falsche Farbe | 5 min |
| Dienst nicht erreichbar | 5 min |

Ändern in `spoolman_waechter.json`, z. B. `"wartezeiten": {"tag_unbeschrieben": 15}`.

### Abbrüche von `ort_sync.py`

Der Wächter meldet `ort_sync.py`, sobald in den letzten Zeilen von `ort_sync.log` ein `[ERROR]` steht. Ist Spoolman, Bambuddy oder CFSync nicht erreichbar oder antwortet nicht innerhalb von `timeout_s` (auch nach einem Wiederholversuch), bricht `ort_sync.py` den Lauf ab und ändert nichts. Seit 02.11.2026 gilt dabei:

- 1. und 2. Abbruch in Folge: nur `[WARNING] Abbruch, nichts geaendert (n. Mal in Folge, Alarm ab 3)` – keine Meldung.
- ab dem 3. Abbruch in Folge (bei 5-Minuten-Takt also nach ca. 15 Minuten am Stück): `[ERROR]` – der Wächter meldet.
- Ein erfolgreicher Lauf setzt den Zähler zurück. Der Zähler steht in `ort_sync.fails` im Datenordner; die Schwelle ist `FAILS_ALARM` in `ort_sync.py`.

Hintergrund: Sind beide K2 Plus ausgeschaltet, versucht CFSync jede Minute, sie neu zu erreichen, und beantwortet `/api/ui/state` dabei zeitweise erst nach über 30 s. Am 02.11.2026 führte das zu über 30 Fehlermeldungen am Tag, obwohl nichts kaputt war.

## Live-Log der FilaStation-Waage (`waage_log_web.py`)

Läuft auf der FilaStation-Waage selbst (nicht im Container). Zeigt `~/waage.log` im Browser unter
`http://filastation-waage.local:8090/` – große Schrift (A−/A+), Farben, Filter, Live/Pause, Kopieren –
wie `/log/live` der Spoolman-Waage. `waage.py` bleibt unverändert; Dienst: `waage-log.service`.

## Veraltete CFSync-Verknüpfungen lösen (ort_sync.py)

CFSync löst eine Verknüpfung nur, wenn es das Herausnehmen live sieht. War der K2 dabei aus oder CFSync neu gestartet, bleibt sie im leeren Slot stehen. Meldet der Drucker (mit Live-Daten) einen Slot `unlink_after_min` Minuten lang (Standard 10) ununterbrochen als leer, löst `ort_sync.py` die Verknüpfung über `POST /api/ui/spoolman/unlink` – wie der Knopf „Unlink“ in CFSync. Log: „CFSync … 1C: seit 11 min leer, Verknuepfung mit #24 geloest“. `0` in `ort_sync.json` schaltet das ab; im Testlauf (`--test`) steht nur „[Test] …“. Merker: `ort_sync.stale.json` im Datenordner.
