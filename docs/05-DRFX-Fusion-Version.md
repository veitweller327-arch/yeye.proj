# 5. DRFX-Version (Fusion-Effekt, Doppelklick-Installation)

`dist/TimingShiftGhost-1.1.0.drfx` enthält denselben Ghost-Effekt mit
denselben fünf Reglern – aber als **Fusion-Effekt** statt als OFX-Plugin.

## Warum zwei Versionen?

Eine `.drfx`-Datei ist ein Paketformat von DaVinci Resolve, das **nur
Fusion-Vorlagen** (Makros, Titel, Übergänge, Effekte) enthalten kann. Ein
kompiliertes OFX-Plugin (die `.ofx`-Datei aus der Setup.exe) passt technisch
nicht hinein. Deshalb ist der Effekt hier aus Fusion-Nodes nachgebaut:

| | DRFX (Fusion) | Setup.exe (OFX) |
|---|---|---|
| Installation | Doppelklick auf `.drfx` | Doppelklick auf Setup.exe (Admin-Rechte) |
| Wo in Resolve | Edit-Seite → Effects → Toolbox / Effects → **Timing Shift FX → Timing Shift Ghost** | Effects → OpenFX → Timing Shift FX |
| Color-Seite | nein (Fusion-Effekte nur auf der Edit-/Cut-Seite) | ja |
| Regler | Ghost Amount, Distance, Direction, Distortion, Timing Offset | gleich |
| Geschwindigkeit | GPU (Fusion-Nodes) | CPU |
| Verzerrung | Fusion-Directional-Blur (sehr weich) | eigene Berechnung |

Beide Versionen können gleichzeitig installiert sein.

## Installieren

1. DaVinci Resolve **öffnen**.
2. `TimingShiftGhost-1.1.0.drfx` **doppelklicken**
   (oder per Drag & Drop auf das Resolve-Fenster ziehen).
3. Resolve fragt, ob das Bundle installiert werden soll → **Install**.
4. Resolve neu starten, falls der Effekt nicht sofort erscheint.

## Benutzen

1. **Edit**-Seite → oben links **Effects**.
2. Im Suchfeld `Ghost` eingeben → **Timing Shift Ghost**.
3. Auf einen Clip in der Timeline ziehen.
4. Clip anklicken → **Inspector** → Reiter **Effects** → die fünf Regler.

## Wie der Fusion-Effekt aufgebaut ist

```
Clip ─┬─> TimeSpeed (Delay = Timing Offset)
      │      -> Transform (verschiebt um Distance in Direction, Ränder: Duplicate)
      │      -> Directional Blur (Centered, Länge = Distance × Distortion)
      └──────────> Dissolve (Mix = Ghost Amount) ─> Ausgabe
```

Die Quelle liegt in `fusion/Timing Shift Ghost.setting` (Textdatei, lesbar).
Pixelwerte beziehen sich auf 1920×1080 und skalieren bei anderen
16:9-Auflösungen proportional mit.

## Getestet / nicht getestet

* ✅ Gültige Lua-Syntax, alle Node-Verbindungen und Expressions verweisen auf
  existierende Nodes/Regler (`scripts/check_fusion_setting.lua`).
* ⏳ **Nicht in Resolve getestet** (hier gibt es kein Resolve). Falls der
  Effekt nicht erscheint oder einen Fehler zeigt, bitte melden – mit der
  Meldung aus **Workspace → Console**.
* Bekannte Unsicherheit: Ob die Verzerrung genau wie in der OFX-Version
  "von halbem bis vollem Abstand" reicht, hängt davon ab, wie Fusion die
  Länge beim Directional Blur zählt; der Look ist ähnlich, aber nicht pixelgleich.
* Bei Seitenverhältnissen ungleich 16:9 ist die vertikale Verschiebung leicht
  anders skaliert als die horizontale.

## Deinstallieren

Resolve-Menü **Workspace → Bundles** (bzw. **DaVinci Resolve → Bundles** je
nach Version) → "TimingShiftGhost" entfernen. Alternativ die Datei
`Timing Shift Ghost.setting` aus
`%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Templates\Edit\Effects\Timing Shift FX\`
löschen.
