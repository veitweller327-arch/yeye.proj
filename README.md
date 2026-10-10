# Timing Shift FX – OpenFX-Effekt für DaVinci Resolve 20 Studio

Ein nativer **OpenFX-(OFX)-Videoeffekt in C++** für DaVinci Resolve 20 Studio
unter Windows 10/11. Er erzeugt den Look analoger Timing-Fehler: zeitversetzte,
seitlich verrutschte Bildbänder, RGB-Versatz, Geisterbilder/Trails,
Analog-Jitter und horizontale Schlieren.

Du ziehst ihn auf der **Edit**- oder **Color**-Seite auf einen Clip und stellst
alles im **Inspector** ein.

> Inspiriert von den visuellen Eigenschaften analoger Timing-Fehler (z. B. wie
> man sie mit einer ARRI Timing Shift Box erzeugt). Eigenständige Entwicklung,
> **keine** Verbindung zu ARRI, kein ARRI-Code.

## ⬇️ Download & Installation (für Anwender)

| Datei | Wofür |
|-------|-------|
| [`dist/TimingShiftFX-1.0.0-Windows-Setup.exe`](dist/TimingShiftFX-1.0.0-Windows-Setup.exe) | **Doppelklicken → installiert.** Empfohlen. |
| [`dist/TimingShiftFX-1.0.0-Windows.zip`](dist/TimingShiftFX-1.0.0-Windows.zip) | Alternative: entpacken, `Install-TimingShiftFX.bat` doppelklicken |

1. Resolve schließen → Setup.exe doppelklicken → (SmartScreen: "Weitere
   Informationen" → "Trotzdem ausführen") → Admin-Frage mit "Ja" → Fertig.
2. Resolve starten → **Effects → OpenFX → Timing Shift FX** (oder im
   Effects-Panel nach `Timing` suchen) → auf einen Clip ziehen → **Inspector**.

Ausführlich: **[docs/03-Installieren-und-Benutzen.md](docs/03-Installieren-und-Benutzen.md)**
(inkl. Erklärung jedes Reglers).

## Funktionen

| Funktion | Regler im Inspector |
|----------|---------------------|
| Look-Presets: *Subtle Cinematic* (Standard) und *Music Video Extreme* | Look Preset |
| Ein/Aus, Gesamtstärke, Mischung mit Original, Qualität/Performance | Effect Enable, Master Intensity, Blend Amount, Quality |
| 1. Zeitversatz (Timing Shift) – echte Nachbar-Frames | Timing Offset (frames) |
| 2. Horizontale, unregelmäßige Band-Verschiebung | Horizontal Displacement, Band Size, Band Density |
| 3. RGB-Kanal-Versatz, je Kanal einstellbar | RGB Split, Red/Green/Blue Offset |
| 4. Ghosting & Trails (bis zu 4 Echos) | Ghosting Amount, Trail Length, Ghost Horizontal Shift |
| 5. Analog-Jitter, deterministisch | Jitter Amount, Jitter Speed, Random Seed |
| 6. Horizontale Streaks (optional) | Enable Streaks, Streak Amount, Streak Length |

Weitere Eigenschaften: alle Regler keyframebar · gleicher Look in HD, UHD und
Proxy-Auflösung · Alpha bleibt erhalten · keine schwarzen Ränder (Kanten werden
verlängert) · gleicher Seed + gleiche Position = identisches Bild · an
Clipgrenzen wird der erste/letzte Frame genutzt statt eines schwarzen Bildes.

## Was getestet ist – und was nicht

| Test | Ergebnis |
|------|----------|
| Bildkern-Unit-Tests (Determinismus, 1 vs. 8 Threads identisch, keine Ränder/NaN, Alpha, Zeitquellen, HD/UHD-Skalierung, Render-Window) | ✅ bestanden |
| Echte Plugin-Binary in einem Mini-OFX-Host (Linux): Laden, alle Regler, `getFramesNeeded` inkl. Clipgrenzen, `isIdentity`, Rendern, Presets, Bildfreigabe | ✅ bestanden |
| **Windows-DLL** (`dist/`) im selben Mini-Host unter **Wine** | ✅ bestanden |
| Setup.exe unter Wine: installiert in `Common Files\OFX\Plugins`, Plugin von dort geladen, Deinstallation entfernt alles | ✅ bestanden |
| **In DaVinci Resolve 20 Studio** | ⏳ **noch nicht** – in dieser Build-Umgebung gibt es kein Resolve. Das ist dein Prüfschritt 1 ([Anleitung](docs/03-Installieren-und-Benutzen.md#prüfschritt-1-lädt-resolve-das-plugin)). |

CPU-Rechenzeit pro Frame (4 Kerne, ohne Dekodieren): 1080p 18–41 ms,
2160p 55–190 ms je nach Quality. GPU-Beschleunigung ist für eine spätere Stufe
geplant ([Begründung](docs/01-Architektur.md)).

## Dokumentation

1. [Architektur & Machbarkeit](docs/01-Architektur.md) – Was geht nativ in OFX, Grenzen, Stufenplan
2. [Selbst bauen](docs/02-Bauen.md) – Visual Studio 2022 + CMake, Schritt für Schritt
3. [Installieren & Benutzen](docs/03-Installieren-und-Benutzen.md) – Resolve-Bedienung, alle Regler
4. [Fehlerbehebung](docs/04-Fehlerbehebung.md) – Effekt fehlt, Ladefehler, GPU, Performance

## Abhängigkeiten

* **Zum Benutzen:** Windows 10/11 x64, DaVinci Resolve 20 (Studio). Nichts weiter.
* **Zum Bauen:** Visual Studio 2022 (Workload "Desktopentwicklung mit C++",
  enthält CMake ≥ 3.16). OpenFX-Header liegen bei (`external/openfx`,
  BSD-3-Clause, © Academy Software Foundation / The Open Effects Association).
* **Optional:** NSIS 3 (Installer), Python + Pillow + NumPy (Vorschaubilder),
  Zig (Cross-Build unter Linux).

## Schnellstart für Entwickler

```bash
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
