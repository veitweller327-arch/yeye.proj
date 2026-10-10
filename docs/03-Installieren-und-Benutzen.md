# 3. Installieren und in DaVinci Resolve benutzen

## Installieren (Doppelklick)

1. **DaVinci Resolve schließen.**
2. `dist/TimingShiftFX-1.0.0-Windows-Setup.exe` herunterladen
   (auf GitHub die Datei anklicken → Download-Symbol "Download raw file").
3. Doppelklicken.
   * SmartScreen sagt evtl. **"Der Computer wurde durch Windows geschützt"** –
     das liegt daran, dass die Datei nicht digital signiert ist.
     → **"Weitere Informationen" → "Trotzdem ausführen"**.
   * Administrator-Frage mit **Ja** bestätigen.
4. Durch den Assistenten klicken → **Fertig stellen**.
5. Resolve starten.

Ergebnis (zur Kontrolle im Explorer):
`C:\Program Files\Common Files\OFX\Plugins\TimingShiftFX.ofx.bundle\Contents\Win64\TimingShiftFX.ofx`

**Ohne Setup.exe:** `dist/TimingShiftFX-1.0.0-Windows.zip` entpacken →
`Install-TimingShiftFX.bat` doppelklicken.

**Deinstallieren:** Windows-Einstellungen → Apps → "Timing Shift FX (OpenFX plugin)".

## Prüfschritt 1: Lädt Resolve das Plugin?

1. Resolve → Menü **DaVinci Resolve → Preferences → System → Video Plugins**.
2. In der Liste muss **Timing Shift FX** stehen und angehakt sein.
   Wenn nicht → [04-Fehlerbehebung.md](04-Fehlerbehebung.md).

## Effekt auf einen Clip anwenden – Edit-Seite

1. Unten die **Edit**-Seite wählen.
2. Oben links **Effects** (Effects Library) öffnen.
3. **Toolbox → OpenFX** bzw. **OpenFX → Filters** aufklappen, dort die Gruppe
   **Timing Shift FX**. Am schnellsten: oben im Effects-Panel die **Lupe** und
   `Timing` eintippen.
4. **Timing Shift FX** mit der Maus auf einen Clip in der Timeline ziehen.
5. Clip anklicken → oben rechts **Inspector** → Reiter **Effects**.
   Dort stehen alle Regler.

## Effekt anwenden – Color-Seite

1. **Color**-Seite → einen Node auswählen (oder mit `Alt+S` einen neuen
   seriellen Node anlegen – empfohlen, damit der Effekt getrennt bleibt).
2. Oben rechts **Effects** öffnen → **Timing Shift FX** auf den Node ziehen.
3. Die Regler erscheinen im Effects-Panel unter **Settings**.

## Prüfschritt 2: Funktioniert der Zeitversatz?

1. Einen Clip mit **Bewegung** verwenden (statische Bilder zeigen keinen
   Zeitversatz).
2. Preset **Music Video Extreme** wählen → in den Bändern sieht man deutlich
   ein "älteres" Bild, Ghost-Echos ziehen hinterher.
3. **Timing Offset** auf 0 → das Zeitecho verschwindet, nur räumliche
   Effekte bleiben. Auf 8 → das Echo wird deutlich größer.
4. Einen Frame zweimal abspielen/rendern → das Bild muss **identisch** sein
   (deterministisch).

## Alle Regler erklärt

Alle Pixelwerte beziehen sich auf ein **1920 Pixel breites Bild**. Bei UHD
werden sie automatisch verdoppelt – der Look bleibt gleich, egal ob HD, UHD
oder Proxy-Wiedergabe. Jeder Regler mit Rautensymbol ◆ lässt sich in Resolve
**keyframen**.

| Regler | Bereich (Standard) | Was er macht | Tipp |
|--------|--------------------|--------------|------|
| **Look Preset** | Custom / Subtle Cinematic / Music Video Extreme | Lädt alle Regler auf einen Look. "Custom" ändert nichts. | Erst Preset wählen, dann feintunen. |
| **Effect Enable** | an/aus (an) | Schaltet den ganzen Effekt aus (Resolve überspringt ihn dann – kostet keine Zeit). | Zum Vorher/Nachher-Vergleich. |
| **Master Intensity** | 0–2 (1) | Skaliert alle Störungen gleichzeitig. | Perfekt zum Keyframen: Effekt "einschlagen" lassen. |
| **Blend Amount** | 0–1 (1) | Mischung Original ↔ Effekt. | 0.5 = halb transparent. |
| **Quality** | Draft / Normal / Best (Normal) | Draft: schnell, kantig, 1 Echo. Normal: weich, max. 2 Echos. Best: alle Echos. | Draft beim Schneiden, Best beim Export. |
| **Timing Offset (frames)** | −12…12 (2) | Wie viele Frames das versetzte Bild hinterherhinkt (+) oder vorausläuft (−). | Kern des "Timing Shift"-Looks. |
| **Ghosting Amount** | 0–1 (0.25) | Deckkraft des zeitversetzten Geisterbildes. | 0 = kein Ghost. |
| **Trail Length (echoes)** | 1–4 (1) | Anzahl der Echos (je Echo ein weiterer Offset zurück). | Jedes Echo kostet Rechenzeit. |
| **Ghost Horizontal Shift** | −200…200 px (8) | Seitlicher Versatz jedes Echos. | Negativ = Echos nach links. |
| **Horizontal Displacement** | −400…400 px (24) | Wie weit die gestörten Bänder seitlich verschoben werden. + = rechts, − = links. | Die Stärke variiert pro Band zufällig (analog-typisch). |
| **Band Size** | 2–400 px (40) | Mittlere Höhe der Bänder. | Klein = feines Zittern, groß = grobe Blöcke. |
| **Band Density** | 0–1 (0.35) | Anteil der Bänder, die gestört sind. | 1 = ganzes Bild zeigt den versetzten Frame. |
| **RGB Split** | 0–50 px (3) | Abstand der Farbkanäle. | 2–4 subtil, 10+ deutlich. |
| **Red / Green / Blue Offset** | −1…1 (1 / 0 / −1) | Richtung und Anteil je Kanal (× RGB Split). | Rot und Blau gleich = Farbsaum nur in eine Richtung. |
| **Jitter Amount** | 0–30 px (2) | Wackeln des ganzen Bildes von Frame zu Frame. | 0 = ruhig. |
| **Jitter Speed** | 0–4 (1) | Wie oft sich das Zufallsmuster (Jitter **und** Bänder) ändert. 1 = jeden Frame, 0.5 = jeden 2. Frame (weich), 0 = eingefroren. | Für "rhythmische" Störungen keyframen. |
| **Random Seed** | 0–99999 (1) | Anderes Zufallsmuster, aber immer reproduzierbar. | Zwei Clips mit gleichem Seed sehen gleich gestört aus. |
| **Enable Streaks** | an/aus (aus) | Schaltet die horizontalen Schlieren ein. | |
| **Streak Amount** | 0–1 (0.3) | Stärke der Schlieren (helle Bereiche ziehen nach rechts). | |
| **Streak Length** | 2–1000 px (60) | Länge der Schlieren. | |

### Die zwei mitgelieferten Looks

| Regler | Subtle Cinematic (= Standard) | Music Video Extreme |
|--------|------------------------------|---------------------|
| Timing Offset | 2 | 4 |
| Ghosting / Trail / Shift | 0.25 / 1 / 8 | 0.55 / 3 / 40 |
| Displacement / Band Size / Density | 24 / 40 / 0.35 | 140 / 70 / 0.45 |
| RGB Split | 3 | 12 |
| Jitter Amount / Speed | 2 / 1 | 8 / 1 |
| Streaks | aus | an, 0.6, Länge 220 |
| Quality | Normal | Best |

## Performance-Tipps

* Beim Schneiden **Quality = Draft** und/oder **Playback → Timeline Proxy
  Resolution → Half/Quarter**.
* Für flüssige Wiedergabe: Clip rechtsklicken → **Render Cache Color Output**
  (Color-Seite) bzw. **Playback → Render Cache → User**.
* Long-GOP-Material (H.264/H.265 vom Handy/DSLR) ist beim Zeitzugriff
  langsam → **Optimized Media** erzeugen (Clip rechtsklicken im Media Pool).
