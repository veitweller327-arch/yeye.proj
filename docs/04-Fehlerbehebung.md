# 4. Fehlerbehebung

## Der Effekt erscheint nicht in Resolve

Der Reihe nach prüfen:

1. **Resolve komplett neu gestartet?** Resolve sucht nur beim Start nach Plugins.
2. **Pfad korrekt?** Exakt diese Datei muss existieren:
   `C:\Program Files\Common Files\OFX\Plugins\TimingShiftFX.ofx.bundle\Contents\Win64\TimingShiftFX.ofx`
   Häufige Fehler: Ordner doppelt verschachtelt (`...\TimingShiftFX.ofx.bundle\TimingShiftFX.ofx.bundle\...`),
   ZIP nicht entpackt, Datei in `Program Files (x86)` gelandet.
3. **Preferences → System → Video Plugins:** Ist "Timing Shift FX" gelistet
   und **angehakt**? Wenn er dort steht, aber deaktiviert ist: anhaken, Resolve
   neu starten.
4. **Suche benutzen:** Im Effects-Panel auf die Lupe klicken und `Timing`
   eingeben – die Kategorie kann je nach Resolve-Version anders heißen.
5. **Plugin-Cache zurücksetzen** (Resolve merkt sich, welche Plugins es schon
   kennt; ein früher fehlgeschlagener Ladeversuch kann hängen bleiben):
   * Resolve schließen.
   * `Win + R` → `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support` → Enter.
   * Die Datei `OFXPluginCacheV2.xml` (Name kann je nach Version leicht
     abweichen, z. B. `OFXPluginCache*.xml`) umbenennen, z. B. in `.bak`.
   * Resolve starten – der Cache wird neu aufgebaut.
6. **Antivirus:** Manche Virenscanner blockieren unsignierte DLLs. Den Ordner
   `C:\Program Files\Common Files\OFX\Plugins\TimingShiftFX.ofx.bundle`
   als Ausnahme eintragen.

## Plugin-Ladefehler / Resolve meldet einen Fehler beim Start

* Das Plugin benötigt **Windows 10 oder 11, 64 Bit**. Es braucht keine
  Visual-C++-Redistributables (geprüft: es importiert nur `KERNEL32.dll`
  und die Windows-Universal-CRT `api-ms-win-crt-*.dll`, die in Windows 10/11
  eingebaut ist).
* Auf sehr alten, nie aktualisierten Windows-10-Installationen kann die
  Universal CRT fehlen → Windows Update ausführen.
* Resolve-Log ansehen: `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\logs\ResolveDebug.txt`
  → nach `TimingShift` oder `OFX` suchen. Die Zeilen bitte mitschicken, wenn
  du Hilfe brauchst.

## Effekt ist da, aber Bild ist rot / "Media Offline" / Fehler-Symbol

* Das Plugin akzeptiert **32-Bit-Float RGBA**. Resolve liefert OFX-Plugins
  standardmäßig genau das. Wenn Resolve trotzdem einen Formatfehler meldet,
  bitte melden (mit Resolve-Version und Projekteinstellungen).

## Ruckelige Wiedergabe

* **Ghost Distortion** reduzieren (größter Einfluss auf die Rechenzeit).
* Timeline-Proxy-Auflösung reduzieren, Render Cache nutzen, Optimized Media
  für H.264/H.265 (siehe [03-Installieren-und-Benutzen.md](03-Installieren-und-Benutzen.md)).

## GPU-Probleme

Version 1.1 rechnet **auf der CPU** (alle Kerne). Resolve schiebt dafür das
Bild von der GPU in den Arbeitsspeicher und zurück. Folgen:

* Der Effekt funktioniert unabhängig von Grafikkarte/Treiber
  (NVIDIA/AMD/Intel, CUDA/OpenCL egal).
* Die Wiedergabe ist langsamer als bei GPU-Effekten.
* Wenn Resolve generell GPU-Fehler zeigt ("GPU memory full"), liegt das nicht
  an diesem Plugin – Grafikkartentreiber aktualisieren, in
  Preferences → Memory and GPU den GPU-Modus prüfen.

Ein GPU-Pfad (CUDA/OpenCL) ist als Stufe 4 geplant – siehe
[01-Architektur.md](01-Architektur.md).

## Projekte mit Version 1.0

Version 1.1 hat andere Regler. In Projekten, in denen schon Version 1.0
benutzt wurde, den Effekt einmal entfernen und neu auf den Clip ziehen.

## Der Zeitversatz ist am Clipanfang/-ende "eingefroren"

Gewollt: Vor dem ersten bzw. nach dem letzten verfügbaren Frame wird der
erste/letzte Frame verwendet, statt ein schwarzes Bild zu zeigen.

## Ergebnis ändert sich beim erneuten Abspielen

Sollte nicht passieren – der Effekt enthält keinen Zufall und ist für gleiche
Regler + gleiche Position deterministisch (getestet). Prüfen, ob Regler keyframes haben oder ob Resolve
die Frames an einer anderen Clipposition neu berechnet (z. B. nach Trimmen /
Retime). Bitte melden, falls es trotzdem auftritt.
