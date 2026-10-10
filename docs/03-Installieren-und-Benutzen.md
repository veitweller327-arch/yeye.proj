# 3. Installieren und in DaVinci Resolve benutzen

## Installieren (Doppelklick)

1. **DaVinci Resolve schließen.**
2. `dist/TimingShiftFX-1.2.0-Windows-Setup.exe` herunterladen
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

**Ohne Setup.exe:** `dist/TimingShiftFX-1.2.0-Windows.zip` entpacken →
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

## Prüfschritt 2: Funktioniert der Effekt?

1. Einen Clip mit **hellen Bereichen** nehmen (Lichter, Metall, Himmel).
2. Effekt anwenden: helle Stellen ziehen **Schlieren nach oben**, das Bild
   bleibt erkennbar (wie im Referenzvideo).
3. **Blur Strength** von 0 auf 2 ziehen → Schlieren werden länger.
4. **Blur Angle** auf −90 → Schlieren nach unten; **Symmetric Blur** an → beide Richtungen.
5. **Enable Shake** an und abspielen → die Schlierenlänge „atmet“ zufällig;
   **Reseed** klicken → anderes Muster.
6. **Ghost Amount** auf 0.4 und einen Clip mit Bewegung → zusätzliches
   zeitversetztes Geisterbild.

## Alle Regler erklärt

Pixelwerte beziehen sich auf ein **1920 Pixel breites Bild**; bei UHD werden
sie automatisch verdoppelt. Jeder Regler lässt sich in Resolve **keyframen**.

### Schlieren (Hauptbereich)

| Regler | Bereich (Standard) | Was er macht | Tipp |
|--------|--------------------|--------------|------|
| **Input Gamma** | Auswahl (Rec.709 / Gamma 2.4) | Sagt dem Effekt, wie das Bild kodiert ist. Die Schlieren werden in **linearem Licht** berechnet – dadurch leuchten Lichter wie echtes Licht auf Film. | Normale Timeline: Rec.709. Ungegradetes Log-Material: passende Log-Kurve (S-Log3, LogC3, LogC4). Color-managed (DWG): DaVinci Intermediate. |
| **Blur Strength** | 0–3, bis 10 (1.0) | Länge der Schlieren. 1.0 = 150 px bei 1080p. Wird ignoriert, solange Shake an ist. | 0.5 subtil, 1 wie im Video, 2+ extrem. |
| **Blur Angle** | −180…180° (90) | Richtung: 90 = nach oben (Filmlaufrichtung), −90 = unten, 0 = rechts, 180 = links. | Genau 90/−90/0/180 rechnen am schnellsten. |
| **Symmetric Blur** | an/aus (aus) | Schlieren in beide Richtungen statt nur in eine. | |
| **Blur Edges** | Wrap-Around / Replicate (Wrap-Around) | Was am Bildrand passiert. Wrap-Around: Schliere läuft am gegenüberliegenden Rand weiter (wie beim Filmstreifen, am realistischsten). Replicate: Randpixel werden wiederholt. | Bei störenden Streifen am Bildrand → Replicate. |
| **Cleanup Low** | 0–1 (0) | Dunkle Bereiche unter diesem Helligkeitswert bleiben **sauber** (ohne Schlieren). | z. B. 0.05 |
| **Cleanup High** | 0–1 (0) | Ab diesem Wert ist der Effekt voll sichtbar; dazwischen weicher Übergang. | z. B. 0.25 |

### Shake (Gruppe)

| Regler | Bereich (Standard) | Was er macht |
|--------|--------------------|--------------|
| **Enable Shake** | an/aus (aus) | Die Schlierenlänge schwankt zufällig über die Zeit – wie der Jitter der echten Timing Shift Box. |
| **Shake Smoothness** | 1–100 Frames (5) | Abstand zwischen zwei Zufallswerten. Größer = ruhiger. |
| **Blur Strength Min** | 0–3 (0.5) | Kürzeste Schliere. |
| **Blur Strength Max** | 0–3 (1.0) | Längste Schliere. |
| **Reseed** | Knopf | Neues Zufallsmuster (bleibt reproduzierbar). |

### Ghost (Time Offset) (Gruppe, standardmäßig aus)

| Regler | Bereich (Standard) | Was er macht |
|--------|--------------------|--------------|
| **Ghost Amount** | 0–1 (0) | Stärke eines zusätzlichen, zeitversetzten Geisterbildes. 0 = aus. |
| **Ghost Distance** | 0–500 px (40) | Wie weit das Geisterbild verschoben ist. |
| **Ghost Direction** | −180…180° (90) | Richtung der Verschiebung. |
| **Timing Offset (frames)** | −12…12 (2) | Welcher Frame im Geisterbild steckt (+2 = 2 Frames zurück). |

## Performance-Tipps

* Schräge **Blur Angle**-Werte und ein aktiver **Ghost** kosten mehr
  Rechenzeit. Beim Schneiden ggf. **Playback → Timeline Proxy Resolution → Half**.
* Für flüssige Wiedergabe: **Playback → Render Cache → User**, dann Clip
  rechtsklicken → **Render Cache Color Output**.
* Long-GOP-Material (H.264/H.265 vom Handy/DSLR) ist beim Zeitzugriff langsam
  → **Optimized Media** erzeugen (Clip rechtsklicken im Media Pool).
