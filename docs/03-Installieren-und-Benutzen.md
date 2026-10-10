# 3. Installieren und in DaVinci Resolve benutzen

## Installieren (Doppelklick)

1. **DaVinci Resolve schließen.**
2. `dist/TimingShiftFX-1.1.0-Windows-Setup.exe` herunterladen
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

**Ohne Setup.exe:** `dist/TimingShiftFX-1.1.0-Windows.zip` entpacken →
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

## Prüfschritt 2: Funktioniert der Ghost-Effekt?

1. Einen Clip mit **Bewegung** verwenden (bei Standbildern zeigt Timing
   Offset keinen Unterschied).
2. **Ghost Amount** auf 0.6, **Ghost Distance** auf 120 → das Geisterbild ist
   deutlich nach oben versetzt.
3. **Ghost Direction** auf 0 → das Geisterbild wandert nach rechts.
4. **Ghost Distortion** von 0 auf 1 ziehen → das Geisterbild wird immer
   stärker in die Länge gezogen.
5. **Timing Offset** auf 0 → das Geisterbild zeigt denselben Frame; auf 6 →
   es hinkt bei Bewegung deutlich hinterher.

## Alle Regler erklärt

Pixelwerte beziehen sich auf ein **1920 Pixel breites Bild**; bei UHD werden
sie automatisch verdoppelt. Jeder Regler lässt sich in Resolve **keyframen**
(Rautensymbol ◆ rechts neben dem Regler).

| Regler | Bereich (Standard) | Was er macht | Tipp |
|--------|--------------------|--------------|------|
| **Ghost Amount** | 0–1 (0.4) | Stärke/Deckkraft des Geisterbildes. 0 = Effekt aus (kostet dann keine Rechenzeit), 1 = man sieht nur noch das Geisterbild. | 0.2–0.4 subtil, 0.6+ deutlich. |
| **Ghost Distance** | 0–500 px (40) | Wie weit das Geisterbild verschoben wird. | Für den Look aus deinem Referenzbild: 60–150. |
| **Ghost Direction** | −180…180° (90) | Richtung der Verschiebung: 0 = rechts, 90 = oben, 180 = links, −90 = unten, 45 = schräg rechts oben. | Lässt sich animieren, z. B. eine langsame Drehung. |
| **Ghost Distortion** | 0–1 (0.3) | Zieht das Geisterbild entlang der Richtung in die Länge: 0 = scharfe Kopie, 0.5 = von halbem bis vollem Abstand verschmiert, 1 = vom Original bis zum vollen Abstand. | Höhere Werte kosten mehr Rechenzeit. |
| **Timing Offset (frames)** | −12…12 (2) | Welcher Frame im Geisterbild steckt: +2 = 2 Frames zurück, −2 = 2 Frames voraus, 0 = derselbe Frame. | Das ist der "Timing Shift": je größer, desto weiter hinkt das Geisterbild bei Bewegung hinterher. |

Ein-/Ausschalten: Resolve hat im Inspector neben jedem Effekt einen eigenen
Schalter; alternativ Ghost Amount auf 0.

## Performance-Tipps

* Mit hoher **Ghost Distortion** und großer **Ghost Distance** wird die
  Berechnung teurer. Beim Schneiden ggf. **Playback → Timeline Proxy
  Resolution → Half**.
* Für flüssige Wiedergabe: **Playback → Render Cache → User**, dann Clip
  rechtsklicken → **Render Cache Color Output**.
* Long-GOP-Material (H.264/H.265 vom Handy/DSLR) ist beim Zeitzugriff langsam
  → **Optimized Media** erzeugen (Clip rechtsklicken im Media Pool).
