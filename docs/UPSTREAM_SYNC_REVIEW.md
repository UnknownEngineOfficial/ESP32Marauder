# Upstream Sync Review — v1.14.1 → v1.14.3

Datum: 2026-08-17
Analysiert von: Remy (OpenClaw)

## Zusammenfassung

Der Fork ist auf upstream **v1.14.1** synced. Aktuelle Original-Version ist **v1.14.3**.
Delta: 2 Releases, **50 Commits, 37 Dateien**.

**Entscheidung: KEIN Sync nötig.** Kein funktionaler Grund vorhanden — der einzige
echte Angriffs-Fix (#1464) ist im Fork bereits de facto umgesetzt (korrekte
Funktionsweise, nur ohne die defensive Hilfsfunktion).

---

## Delta-Analyse (v1.14.1 → v1.14.3)

### Funktional relevante Änderungen

**v1.14.2**
- ESP32-C5 (5 GHz) Support + neue Hardware-Targets (Mini v3, Dual Mini C5, M5 Cardputer ADV)
- PR #1464: Beacon-Channel-Preserving für gespeicherte SSIDs (siehe unten)
- PR #1465: Upload-Fortschritt + WDG-Fehler-Feedback (Wardrive/WiGLE-Uploads)
- PR #1463: Scan-Text-Clipping-Fix (Display-UI, große Screens)

**v1.14.3**
- PR #1471: Scan-Text-Clipping-Fix (Display-Kosmetik)
- PR #1472/#1473: Firmware-Version-Bump

### Für den Fork NICHT relevant (CI/Infra/Tooling)

Firmware-Unit-Test-Foundation (#1416), Coverage-Badges (#1418/#1419/#1451),
v8 Release-Handoff + scoped Tokens (#1454–#1458), Installer-Asset-Packaging (#1459).

---

## PR #1464 — "Preserve beacon channels for saved SSIDs" (Detail-Review)

Fixe: `esp32_marauder/WiFiScan.cpp`

### Was der Fix tut

Extrahiert die Kanal-Schreibung in eine defensive Hilfsfunktion mit Bounds-Check:

```cpp
const size_t channel_index = 50 + ssid_length;
if ((frame == nullptr) || (channel_index >= frame_size))
    return false;
frame[channel_index] = channel;
```

Wird an drei Stellen aufgerufen: `broadcastCustomBeacon` (AccessPoint),
`broadcastCustomBeacon` (ssid/camera), `broadcastRandomSSID`.

### Ist der Bug im Fork aktiv? NEIN.

Der Fork nutzt überall die inline-Form `temp_frame[50 + fullLen] = set_channel;`.
Pruefung der Frame-Groessen gegen den Channel-Offset `50 + fullLen`:

| Funktion | Frame-Size | Channel-Write | Risiko |
|---|---|---|---|
| broadcastCustomBeacon (AccessPoint) | 37 + post_len + fullLen + 1 | innerhalb | OK |
| broadcastCustomBeacon (ssid/camera) | 37 + post_len + fullLen + 1 | innerhalb | OK |
| broadcastRandomSSID | 37 + sizeof(post_base) + fullLen + 1 | innerhalb | OK |

In allen drei Pfaden enthaelt `frame_len` den `fullLen`-Term, daher ist
`50 + fullLen` immer < `frame_size`. Kein Out-of-Bounds-Write moeglich.

Der upstream-Fix ist fuer die konkreten Codepfade des Forks **rein defensiv** —
er schuetzt gegen einen Edge-Case, den die aktuelle Frame-Konstruktion bereits abdeckt.

### Fazit

- Kanal wird bei Custom- und Random-SSID-Beacons korrekt gesetzt.
- CSA/QUIET-Attacken setzen den Kanal via `custom_ssid.channel` korrekt.
- Verhalten ist funktional **gleich** mit dem gefixten upstream.

---

## Empfehlung

**Bleib auf v1.14.1.** Kein funktionaler Grund zu synchen:

1. Einziger echter Angriffs-Fix (#1464) ist de facto bereits vorhanden.
2. ESP32-C5/5-GHz/neue Boards: irrelevant fuer BlackHat-S3 (ESP32-S3, 2.4 GHz).
3. Rest ist CI/Test/Display-Kosmetik.

**Optional:** Den sauberen `setBeaconFrameChannel`-Refactor + Unit-Tests cherry-picken
(schuetzt gegen zukuenftige Frame-Layout-Aenderungen), aber NICHT noetig fuer korrekte
Funktion.
