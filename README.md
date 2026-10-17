# FilaStation-Werkzeuge

Hilfsskripte rund um Spoolman, CFSync und Bambuddy. Nicht Teil der Firmware der Spoolman-Waage.
Sie laufen im Container `filament-tools` auf der Synology DS920+ (Container Manager).

## Was läuft

| Skript | Takt | Aufgabe |
|---|---|---|
| `ort_sync.py` | alle 5 min (Minute 0, 5, 10 …) | Trägt in Spoolman den Ort jeder Spule ein: AMS-Fach über Bambuddy, CFS-Slot über CFSync, sonst `Lager`. Ändert nichts, wenn ein Dienst fehlt oder ein K2 keine Live-Daten liefert. |
| `spoolman_waechter.py` | alle 15 min (Minute 2, 17, 32, 47) | Prüft Spulen, neue Filamente, CFS-Slots und Dienste auf typische Fehler und meldet sie per Pushover. Meldet per Pushover und E-Mail (mit Links), erst nach zwei Läufen hintereinander und dann nur einmal. |
| `runner.py` | dauerhaft | Taktgeber im Container; lädt die beiden Skripte beim Start und täglich um 4 Uhr frisch aus diesem Zweig. Ohne GitHub läuft die zuletzt geladene Fassung weiter. |
| `pruefung.py` | von Hand | Gesamtübersicht aller Spulen (läuft auf der FilaStation-Waage). |

```
                 +--------------------- DS920+ ----------------------+
                 |  Spoolman  <---------+                            |
                 |  Bambuddy  <-----+   |                            |
                 |  filament-tools (Container)                       |
                 |    runner.py --- alle 5 min ---> ort_sync.py      |
                 |              +-- alle 15 min --> spoolman_waechter.py --> Pushover
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

## Nachschauen

- Container Manager → Container `filament-tools` → Protokoll.
- Testläufe ohne Änderungen (Container → Terminal → `sh`):

      python /app/spoolman_waechter.py --test
      python /app/ort_sync.py --test
      python /app/spoolman_waechter.py --testmail

- Update sofort statt um 4 Uhr: Projekt `filament-tools` → Aktion → Neu starten.

## Neu einrichten

Ordner `docker/filament-tools` anlegen, `compose.yaml`, `ort_sync.json`, `spoolman_waechter.json` und `mail.json` hineinlegen, im Container Manager ein Projekt `filament-tools` mit diesem Ordner und der vorhandenen `compose.yaml` erstellen. Die JSON-Dateien enthalten Schlüssel: mitsichern, nicht veröffentlichen.
