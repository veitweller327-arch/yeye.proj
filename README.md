# Timing Shift FX – Out-of-Sync-Film-Effekt für DaVinci Resolve 20 Studio

Ein nativer **OpenFX-(OFX)-Videoeffekt in C++** für DaVinci Resolve 20 Studio
unter Windows 10/11. Er erzeugt den Look einer Filmkamera, deren Verschluss
nicht mehr synchron zum Filmtransport läuft: **helle Bereiche ziehen in
linearem Licht lange Schlieren in Laufrichtung**, optional mit zufällig
schwankender Länge (Shake) und einem zeitversetzten Geisterbild.
Die Regler entsprechen dem Referenzvideo ([Abgleich](docs/06-Video-Abgleich.md)).

Du ziehst ihn auf der **Edit**- oder **Color**-Seite auf einen Clip und stellst
ihn im **Inspector** ein.

> Inspiriert von den visuellen Eigenschaften analoger Timing-Fehler (z. B. wie
> man sie mit einer ARRI Timing Shift Box erzeugt). Eigenständige Entwicklung,
> **keine** Verbindung zu ARRI, kein ARRI-Code.

## ⬇️ Download & Installation

| Datei | Wofür |
|-------|-------|
| [`dist/TimingShiftFX-1.2.0-Windows-Setup.exe`](dist/TimingShiftFX-1.2.0-Windows-Setup.exe) | **Doppelklicken → installiert.** Empfohlen. |
| [`dist/TimingShiftFX-1.2.0-Windows.zip`](dist/TimingShiftFX-1.2.0-Windows.zip) | Alternative: entpacken, `Install-TimingShiftFX.bat` doppelklicken |
| [`dist/TimingShiftGhost-1.1.0.drfx`](dist/TimingShiftGhost-1.1.0.drfx) | **DRFX-Version 1.1** (Fusion-Effekt, nur der einfache Ghost-Effekt, noch **ohne** die Video-Regler): bei geöffnetem Resolve doppelklicken → "Install". [Details](docs/05-DRFX-Fusion-Version.md) |

1. Resolve schließen → Setup.exe doppelklicken → (SmartScreen: "Weitere
   Informationen" → "Trotzdem ausführen") → Admin-Frage mit "Ja" → Fertig.
2. Resolve starten → im Effects-Panel nach `Timing` suchen → **Timing Shift FX**
   auf einen Clip ziehen → **Inspector**.

Ausführlich: **[docs/03-Installieren-und-Benutzen.md](docs/03-Installieren-und-Benutzen.md)**

## Die Regler

| Regler | Was er macht |
|--------|--------------|
| **Input Gamma** | Gamma des Eingangsbilds (Rec.709, S-Log3, LogC3/4, DaVinci Intermediate, Linear) – die Schlieren werden in linearem Licht berechnet |
| **Blur Strength** | Länge der Schlieren (1.0 = 150 px bei 1080p) |
| **Blur Angle** | Richtung: 90 = nach oben, −90 = nach unten, 0 = rechts |
| **Symmetric Blur** | Schlieren in beide Richtungen |
| **Blur Edges** | Wrap-Around (realistisch, wie der Filmstreifen) oder Replicate |
| **Cleanup Low / High** | Dunkle Bereiche bleiben sauber |
| **Shake**: Enable, Smoothness, Blur Strength Min/Max, Reseed | Schlierenlänge schwankt zufällig über die Zeit |
| **Ghost (Time Offset)**: Amount, Distance, Direction, Timing Offset | Optionales zeitversetztes Geisterbild (Standard: aus) |

Alle Regler sind keyframebar. Der Look ist in HD, UHD und Proxy gleich, der
Alpha-Kanal bleibt erhalten, und jeder Frame sieht bei jeder Wiedergabe
identisch aus.

## Was getestet ist – und was nicht

| Test | Ergebnis |
|------|----------|
| Bildkern-Unit-Tests: alle Gamma-Kurven (inkl. Referenzwerte), Schlierenrichtung/-länge, Symmetric, Wrap-Around vs. Replicate, Cleanup, Shake (Bereich, Glätte, Reseed), Ghost, Determinismus, keine Ränder/NaN, Alpha, HD/UHD | ✅ bestanden |
| Echte Plugin-Binary in einem Mini-OFX-Host (Linux): Laden, alle Regler, Reseed-Button, Zeitzugriff inkl. Clipgrenzen, `isIdentity`, Rendern, Bildfreigabe | ✅ bestanden |
| Optischer Abgleich der Standardwerte mit dem Referenzvideo | ✅ sehr ähnlich ([Details](docs/06-Video-Abgleich.md)) |
| **Windows-DLL** im selben Mini-Host unter **Wine** | ✅ bestanden |
| Setup.exe unter Wine: Installation, Laden aus dem Installationsordner, Deinstallation | ✅ bestanden |
| DRFX: Lua-Syntax, Node-Verbindungen und Expressions (`scripts/check_fusion_setting.lua`) | ✅ bestanden |
| **In DaVinci Resolve 20 Studio** (OFX und DRFX) | ⏳ **noch nicht** – hier gibt es kein Resolve. Das ist dein Prüfschritt 1. |

CPU-Rechenzeit pro Frame (4 Kerne, Blur Strength 1, ohne Dekodieren):
1080p 58 ms (senkrecht) bis 118 ms (schräger Winkel); 2160p 252–588 ms.

## Dokumentation

1. [Architektur & Machbarkeit](docs/01-Architektur.md)
2. [Selbst bauen](docs/02-Bauen.md) – Visual Studio 2022 + CMake
3. [Installieren & Benutzen](docs/03-Installieren-und-Benutzen.md)
4. [Fehlerbehebung](docs/04-Fehlerbehebung.md)
5. [DRFX-Version (Fusion)](docs/05-DRFX-Fusion-Version.md)
6. [Abgleich mit dem Referenzvideo](docs/06-Video-Abgleich.md)

## Abhängigkeiten

* **Zum Benutzen:** Windows 10/11 x64, DaVinci Resolve 20 (Studio). Nichts weiter.
* **Zum Bauen:** Visual Studio 2022 (Workload "Desktopentwicklung mit C++").
  OpenFX-Header liegen bei (`external/openfx`, BSD-3-Clause).
* **Optional:** NSIS 3 (Installer), Python + Pillow + NumPy (Vorschaubilder),
  Zig (Cross-Build unter Linux).

## Schnellstart für Entwickler

```bash
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
