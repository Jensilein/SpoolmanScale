# FilaStation-Werkzeuge

Hilfsskripte rund um Spoolman, CFSync und Bambuddy. Nicht Teil der Firmware der Spoolman-Waage.

| Datei | Zweck |
|---|---|
| `ort_sync.py` | Trägt in Spoolman den Ort jeder Spule ein (AMS über Bambuddy, CFS über CFSync) |
| `spoolman_waechter.py` | Prüft Spoolman, CFSync und die Dienste auf typische Eingabe- und Bedienfehler und meldet sie per Pushover |
| `runner.py` | Startet `ort_sync.py` (alle 5 min) und `spoolman_waechter.py` (alle 15 min); holt die Skripte täglich um 4 Uhr frisch von hier |
| `compose.yaml` | Projekt `filament-tools` für den Container Manager der Synology |
| `pruefung.py` | Gesamtübersicht aller Spulen mit Auffälligkeiten (von Hand) |

Einstellungen und Logs liegen im Ordner des Projekts (`$FT_DIR`): `ort_sync.json`, `spoolman_waechter.json`, `*.log`.
