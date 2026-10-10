# Timing Shift FX – Ghost-Effekt für DaVinci Resolve 20 Studio

Ein nativer **OpenFX-(OFX)-Videoeffekt in C++** für DaVinci Resolve 20 Studio
unter Windows 10/11. Er legt ein **zeitlich versetztes, verschobenes und
verzerrtes Geisterbild** über den Clip – der Look analoger Timing-Fehler.

Du ziehst ihn auf der **Edit**- oder **Color**-Seite auf einen Clip und stellst
ihn im **Inspector** ein.

> Inspiriert von den visuellen Eigenschaften analoger Timing-Fehler (z. B. wie
> man sie mit einer ARRI Timing Shift Box erzeugt). Eigenständige Entwicklung,
> **keine** Verbindung zu ARRI, kein ARRI-Code.

## ⬇️ Download & Installation

| Datei | Wofür |
|-------|-------|
| [`dist/TimingShiftFX-1.1.0-Windows-Setup.exe`](dist/TimingShiftFX-1.1.0-Windows-Setup.exe) | **Doppelklicken → installiert.** Empfohlen. |
| [`dist/TimingShiftFX-1.1.0-Windows.zip`](dist/TimingShiftFX-1.1.0-Windows.zip) | Alternative: entpacken, `Install-TimingShiftFX.bat` doppelklicken |

1. Resolve schließen → Setup.exe doppelklicken → (SmartScreen: "Weitere
   Informationen" → "Trotzdem ausführen") → Admin-Frage mit "Ja" → Fertig.
2. Resolve starten → im Effects-Panel nach `Timing` suchen → **Timing Shift FX**
   auf einen Clip ziehen → **Inspector**.

Ausführlich: **[docs/03-Installieren-und-Benutzen.md](docs/03-Installieren-und-Benutzen.md)**

## Die Regler (nur diese fünf)

| Regler | Was er macht |
|--------|--------------|
| **Ghost Amount** | Stärke des Geisterbildes (0 = aus, 1 = nur Geisterbild) |
| **Ghost Distance** | Wie weit das Geisterbild verschoben ist (Pixel, bezogen auf 1080p) |
| **Ghost Direction** | Richtung in Grad: 0 = rechts, 90 = oben, 180 = links, −90 = unten |
| **Ghost Distortion** | Zieht das Geisterbild entlang der Richtung in die Länge (0 = scharfe Kopie) |
| **Timing Offset (frames)** | Welcher Frame im Geisterbild zu sehen ist: +2 = 2 Frames zurück, −2 = 2 voraus, 0 = gleicher Frame |

Alle Regler sind keyframebar. Der Look ist in HD, UHD und Proxy gleich, der
Alpha-Kanal bleibt erhalten, es gibt keine schwarzen Ränder, und jeder Frame
sieht bei jeder Wiedergabe identisch aus.

## Was getestet ist – und was nicht

| Test | Ergebnis |
|------|----------|
| Bildkern-Unit-Tests (Richtung/Abstand/Verzerrung exakt, Stärke, Determinismus, keine Ränder/NaN, Alpha, HD/UHD, Render-Window) | ✅ bestanden |
| Echte Plugin-Binary in einem Mini-OFX-Host (Linux): Laden, genau 5 Regler, Zeitzugriff inkl. Clipgrenzen, `isIdentity`, Rendern, Bildfreigabe | ✅ bestanden |
| **Windows-DLL** im selben Mini-Host unter **Wine** | ✅ bestanden |
| Setup.exe unter Wine: Installation, Laden aus dem Installationsordner, Deinstallation | ✅ bestanden |
| **In DaVinci Resolve 20 Studio** | ⏳ **noch nicht** – hier gibt es kein Resolve. Das ist dein Prüfschritt 1. |

CPU-Rechenzeit pro Frame (4 Kerne, ohne Dekodieren, Standardwerte bis Distortion 1):
1080p 12–38 ms, 2160p 50–233 ms.

## Dokumentation

1. [Architektur & Machbarkeit](docs/01-Architektur.md)
2. [Selbst bauen](docs/02-Bauen.md) – Visual Studio 2022 + CMake
3. [Installieren & Benutzen](docs/03-Installieren-und-Benutzen.md)
4. [Fehlerbehebung](docs/04-Fehlerbehebung.md)

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
