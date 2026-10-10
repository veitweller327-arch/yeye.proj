# 1. Architektur und Machbarkeit

Diese Datei beantwortet die erste Aufgabe: **Kann ein natives OpenFX-Plugin die
zeitlichen Effekte in DaVinci Resolve 20 Studio zuverlässig umsetzen?**

## Kurze Antwort

**Ja.** Alle sechs geforderten Effekte lassen sich in einem einzigen nativen
OFX-Plugin umsetzen. Es braucht keine zweite Architektur (kein Fusion, kein
externes Programm). Nur **GPU-Beschleunigung** ist bewusst auf eine spätere
Stufe verschoben (siehe unten).

## Wie Resolve ein OFX-Plugin benutzt

```
 DaVinci Resolve (Host)                     TimingShiftFX.ofx (unser Plugin, eine DLL)
 ─────────────────────                      ────────────────────────────────────────
 Start: durchsucht                     ──►  OfxGetPlugin()        "wer bist du?"
 C:\Program Files\Common Files\OFX\Plugins  describe / describeInContext
                                             → Name, Clips, 27 Inspector-Regler
 Effekt auf Clip gezogen               ──►  createInstance
 Regler verändert                      ──►  instanceChanged      (Look-Presets)
 Frame wird gebraucht                  ──►  getFramesNeeded      "ich brauche Frame t und t-2"
                                       ──►  isIdentity           "nichts zu tun?" (spart Zeit)
                                       ──►  render               holt Frames, rechnet, schreibt
```

## Bewertung der einzelnen Funktionen

| # | Funktion | Native OFX? | Wie umgesetzt | Einschränkungen |
|---|----------|-------------|---------------|-----------------|
| 1 | Timing Shift (Zeitversatz) | **Ja** | `kOfxImageEffectPropTemporalClipAccess` + Aktion `getFramesNeeded` + `clipGetImage(t − Offset)` | Resolve muss zusätzliche Frames dekodieren → langsamer bei H.264/H.265. An Clipgrenzen wird der erste/letzte Frame wiederverwendet. |
| 2 | Horizontale Verschiebung | **Ja** | Unregelmäßige Bänder (zufällige Höhe, Stärke) werden seitlich verschoben und zeigen den **zeitversetzten** Frame | – |
| 3 | RGB-Versatz | **Ja** | R, G, B werden an unterschiedlichen x-Positionen gelesen | Alpha folgt der Geometrie, wird nicht gesplittet |
| 4 | Ghosting / Trails | **Ja** | Bis zu 4 Echo-Frames (Offset, 2×Offset, …) mit abnehmender Deckkraft | Jeder Echo-Frame kostet einen weiteren dekodierten Frame |
| 5 | Analog-Jitter | **Ja** | Deterministisches Hash-Rauschen aus Seed + Frame-Nummer | – |
| 6 | Horizontale Streaks | **Ja** | Zeilenweises "Nachziehen" heller Pixel nach rechts | Nur ganze Frames (keine Tiles), daher keine Nahtstellen |
| – | Presets | **Teilweise** | Resolve hat keinen OFX-Preset-Browser → gelöst über das Auswahlfeld "Look Preset" | Preset überschreibt die Regler; danach frei änderbar |
| – | GPU | **Ja, aber später** | Resolve unterstützt CUDA/OpenCL/Metal-OFX-Rendering | Version 1 rechnet auf der CPU (Multithreading). Siehe "Stufenplan" |

### Warum Fusion nicht nötig ist

Eine `.drfx`-Datei (das ist das "Doppelklick-Paket" von DaVinci Resolve) kann
**nur Fusion-Vorlagen und Skripte** enthalten – keine kompilierten Plugins.
Ein Fusion-Makro könnte einen Teil des Looks nachbauen (TimeSpeed-Node für den
Versatz), wäre aber deutlich langsamer, schwerer zu bedienen und hätte keinen
echten deterministischen Zufall pro Zeile. Für das native Plugin übernimmt
**`TimingShiftFX-1.0.0-Windows-Setup.exe`** die Rolle des Doppelklick-Pakets.

## Bekannte Grenzen (ehrlich)

* **Getestet** wurde: Bildkern (Unit-Tests), die echte Plugin-Binary in einem
  nachgebauten OFX-Host unter Linux **und** die Windows-DLL unter Wine,
  sowie der Installer (Installieren/Laden/Deinstallieren) unter Wine.
* **Nicht getestet** (in dieser Umgebung unmöglich): das Laden in
  DaVinci Resolve 20 Studio selbst. Das ist Prüfschritt 1 bei dir – siehe
  [03-Installieren-und-Benutzen.md](03-Installieren-und-Benutzen.md).
* **Zeitzugriff:** Resolve liefert Nachbar-Frames des *Quell-Clips*. Auf der
  Color-Seite enthalten diese die Korrekturen der Nodes *vor* dem Effekt-Node.
  Wird ein Frame verweigert (z. B. außerhalb des Mediums), nutzt der Effekt den
  aktuellen Frame – es entsteht nie ein schwarzes oder kaputtes Bild.
* **Retime/Speed-Ramps:** Bei Clips mit Geschwindigkeitsänderung beziehen sich
  "Frames" auf die Zeit, die Resolve dem Plugin meldet; das Ergebnis kann sich
  anders anfühlen als bei normalen Clips.
* **Render-Reihenfolge / Cache:** Das Ergebnis hängt nur von Seed, Reglern und
  der Frame-Nummer ab. Es ist also bei jeder Wiedergabe und jedem Export
  identisch – egal, in welcher Reihenfolge Resolve die Frames anfordert.
* **Bewegungsartefakte:** Bei schneller Bewegung zeigen Ghosting und Bänder
  natürlich größere Unterschiede (das ist der gewollte Look). Bei Timing
  Offset 0 gibt es keinen Zeitversatz, nur räumliche Effekte.
* **Performance (CPU, 4 Kerne, Messung aus `core_tests`):**
  1080p: 18 ms (Draft) / 32 ms (Normal) / 41 ms (Best);
  2160p: 55 ms / 128 ms / 190 ms pro Frame – plus das Dekodieren der
  Zusatzframes durch Resolve. Mehr Kerne = schneller. Für UHD-Echtzeit:
  Quality "Draft" beim Schneiden, "Best" beim Export, oder Render Cache.

## Stufenplan

| Stufe | Inhalt | Status |
|-------|--------|--------|
| 1 | Minimalversion: Plugin lädt, Regler im Inspector, Timing Offset + Ghosting | ✅ fertig |
| 2 | Alle 6 Effekte, Presets, Qualitätsstufen, Installer | ✅ fertig (dieser Stand) |
| 3 | Test in Resolve 20 Studio durch dich, Feinabstimmung der Standardwerte | ⏳ nächster Schritt |
| 4 | GPU-Pfad (CUDA für NVIDIA, OpenCL als Fallback) über `kOfxImageEffectPropCudaRenderSupported` / `OpenCLRenderSupported`; CPU-Pfad bleibt als Fallback | geplant |
| 5 | Optional: Code-Signatur des Installers (kein SmartScreen-Hinweis mehr) | optional |

## Projektaufbau

```
src/TimingShiftCore.h/.cpp   Bildverarbeitung – kennt kein OFX, ist testbar
src/TimingShiftPlugin.cpp    OFX-Anbindung: Regler, Zeitzugriff, Render-Aufruf
external/openfx/include      Offizielle OpenFX-Header (BSD-Lizenz, Academy Software Foundation)
tests/core_tests.cpp         Unit-Tests + Benchmark des Bildkerns
tests/mock_host.cpp          Mini-OFX-Host: lädt die ECHTE .ofx-Datei wie Resolve
tools/preview.cpp + .py      Vorschaubilder ohne Resolve rendern
installer/                   NSIS-Setup, Install-/Uninstall-.bat, Info.plist
scripts/                     Windows-Cross-Build (Zig) und Paketierung
dist/                        Fertige Downloads (Setup.exe, ZIP)
```

### Warum die reine C-API statt der OFX-C++-"Support"-Bibliothek?

Die C-API braucht nur die Header-Dateien, keine zusätzlich zu kompilierende
Bibliothek. Das macht den Build für Einsteiger einfacher und die DLL klein
(~260 KB) und ohne Abhängigkeiten (nur Windows-Systembibliotheken).
