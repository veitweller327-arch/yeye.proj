# 1. Architektur und Machbarkeit

Diese Datei beantwortet die erste Aufgabe: **Kann ein natives OpenFX-Plugin die
zeitlichen Effekte in DaVinci Resolve 20 Studio zuverlässig umsetzen?**

## Kurze Antwort

**Ja.** Der Ghost-Effekt mit Zeitversatz lässt sich vollständig als natives
OFX-Plugin umsetzen. Es braucht keine zweite Architektur (kein Fusion, kein
externes Programm). Nur **GPU-Beschleunigung** ist bewusst auf eine spätere
Stufe verschoben (siehe unten).

> **Version 1.2:** Umgebaut nach dem Referenzvideo: Schlieren in linearem
> Licht (Input Gamma, Blur Strength/Angle, Symmetric, Wrap-Around/Replicate,
> Cleanup) plus Shake (Min/Max/Smoothness/Reseed); der Ghost bleibt als
> optionale Gruppe. Abgleich: [06-Video-Abgleich.md](06-Video-Abgleich.md).
>
> **Version 1.1:** Auf Wunsch auf den reinen Ghost-Effekt reduziert. Jitter,
> Random Seed, Bänder, RGB-Split, Streaks, Presets und Quality-Auswahl wurden
> entfernt. Es gibt nur noch fünf Regler: Ghost Amount, Ghost Distance,
> Ghost Direction, Ghost Distortion, Timing Offset.

## Wie Resolve ein OFX-Plugin benutzt

```
 DaVinci Resolve (Host)                     TimingShiftFX.ofx (unser Plugin, eine DLL)
 ─────────────────────                      ────────────────────────────────────────
 Start: durchsucht                     ──►  OfxGetPlugin()        "wer bist du?"
 C:\Program Files\Common Files\OFX\Plugins  describe / describeInContext
                                             → Name, Clips, 5 Inspector-Regler
 Effekt auf Clip gezogen               ──►  createInstance
 Frame wird gebraucht                  ──►  getFramesNeeded      "ich brauche Frame t und t-2"
                                       ──►  isIdentity           "nichts zu tun?" (spart Zeit)
                                       ──►  render               holt Frames, rechnet, schreibt
```

## Bewertung der Funktionen

| Funktion | Native OFX? | Wie umgesetzt | Einschränkungen |
|----------|-------------|---------------|-----------------|
| Zeitversatz (Timing Offset) | **Ja** | `kOfxImageEffectPropTemporalClipAccess` + Aktion `getFramesNeeded` + `clipGetImage(t − Offset)` | Resolve muss einen zusätzlichen Frame dekodieren → langsamer bei H.264/H.265. An Clipgrenzen wird der erste/letzte Frame verwendet. |
| Ghost-Verschiebung (Distance + Direction) | **Ja** | Geisterbild wird mit Sub-Pixel-Genauigkeit (bilinear) in beliebigem Winkel verschoben | – |
| Schlieren (Blur Strength/Angle, Symmetric, Edges) | **Ja** | Box-Blur in linearem Licht: senkrecht/waagerecht exakt mit laufenden Summen, schräge Winkel über 2^n gleichmäßige Abtastungen | Schräge Winkel sind etwa doppelt so teuer |
| Input Gamma / Cleanup / Shake | **Ja** | Nachschlagetabellen für die Gamma-Kurven; Shake = deterministisches, geglättetes Zufallsrauschen | – |
| Stärke (Amount) | **Ja** | Überblendung Geisterbild über das Original | – |
| GPU | **Ja, aber später** | Resolve unterstützt CUDA/OpenCL/Metal-OFX-Rendering | Version 1.1 rechnet auf der CPU (Multithreading) |

### Warum Fusion nicht nötig ist

Eine `.drfx`-Datei (das ist das "Doppelklick-Paket" von DaVinci Resolve) kann
**nur Fusion-Vorlagen und Skripte** enthalten – keine kompilierten Plugins.
Ein Fusion-Makro könnte einen Teil des Looks nachbauen (TimeSpeed-Node für den
Versatz), wäre aber deutlich langsamer, schwerer zu bedienen und hätte keinen
echten deterministischen Zufall pro Zeile. Für das native Plugin übernimmt
**`TimingShiftFX-1.2.0-Windows-Setup.exe`** die Rolle des Doppelklick-Pakets.

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
* **Render-Reihenfolge / Cache:** Das Ergebnis hängt nur von den Reglern und
  den Quell-Frames ab (kein Zufall). Es ist also bei jeder Wiedergabe und jedem Export
  identisch – egal, in welcher Reihenfolge Resolve die Frames anfordert.
* **Bewegungsartefakte:** Bei schneller Bewegung weicht das Geisterbild
  stärker vom Original ab (das ist der gewollte Look). Bei Timing Offset 0
  gibt es keinen Zeitversatz, nur die räumliche Verschiebung.
* **Performance (CPU, 4 Kerne, Messung aus `core_tests`, Blur Strength 1):**
  1080p: 58 ms (senkrecht) / 88 ms (+ Ghost) / 118 ms (Winkel 75°);
  2160p: 252 / 513 / 588 ms pro Frame – plus Dekodieren durch Resolve.
  Mehr Kerne = schneller.

## Stufenplan

| Stufe | Inhalt | Status |
|-------|--------|--------|
| 1 | Minimalversion: Plugin lädt, Regler im Inspector, Timing Offset + Ghosting | ✅ fertig |
| 2 | Ghost-Effekt mit Stärke, Abstand, Richtung, Verzerrung; Installer | ✅ fertig (Version 1.1) |
| 2b | Look und Regler nach Referenzvideo (Schlieren, Shake, Wrap-Around, Cleanup) | ✅ fertig (Version 1.2) |
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
