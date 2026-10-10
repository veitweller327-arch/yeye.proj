# 6. Abgleich mit dem Referenzvideo

Referenz: YouTube-Video „Recreate the out-of-sync film effect digitally (ARRI
Timing Shift Box)“ (20 Min.). Ausgewertet wurden **Einzelbilder** (Inspector,
Node-Bäume, Vorher/Nachher). Der Ton konnte nicht transkribiert werden – was
nur gesprochen erklärt wird, ist deshalb nicht berücksichtigt.

## Was das Video macht

1. Bild in **lineares Licht** umrechnen (Color Space Transform aus dem Log der Kamera).
2. **Directional Blur** (ResolveFX, Typ „Realistic“) entlang der Laufrichtung
   des Films → helle Bereiche ziehen lange Schlieren, das scharfe Bild bleibt
   sichtbar.
3. Zurück in den Ausgangs-Farbraum.
4. Daraus wird ein Fusion-Effekt für die Edit-Seite gebaut („Timeshift Effect“
   und „Timeshift Effect with Shake“).

## Regler: Video ↔ Plugin 1.2

| Regler im Video | Im Plugin 1.2 | Hinweis |
|-----------------|---------------|---------|
| Input Color Space (z. B. Sony S-Gamut3.Cine) | – (nicht nötig) | Ein Blur ist eine lineare Operation; der Farbraum (Gamut) ändert das Ergebnis nicht. Entscheidend ist nur die Gamma-Kurve. |
| Input Gamma (z. B. Sony S-Log3) | **Input Gamma** | Linear, Rec.709/Gamma 2.4, Sony S-Log3, ARRI LogC3, ARRI LogC4, DaVinci Intermediate |
| Blur Strength | **Blur Strength** | 1.0 = 150 px bei 1080p (abgestimmt auf den Look im Video) |
| Blur Angle (−90) | **Blur Angle** | Bei uns: 90 = nach oben (Standard), −90 = nach unten, 0 = rechts |
| Symmetric Blur | **Symmetric Blur** | Schlieren in beide Richtungen |
| Dropdown Blur-Verhalten: Wrap-Around / Replicate | **Blur Edges** | Wrap-Around (Standard, „am realistischsten“ laut Video) / Replicate |
| Cleanup Amount (Bereich, 2 Werte) | **Cleanup Low / Cleanup High** | ⚠ Bedeutung im Video nicht eindeutig erkennbar. Bei uns: dunkle Bereiche unterhalb des Bereichs bleiben sauber (ohne Schlieren). |
| Shake (eigene Effekt-Variante) | **Enable Shake** | Bei uns ein Schalter im selben Effekt |
| Shake Smoothness (5.0) | **Shake Smoothness** | Frames zwischen zwei Zufallswerten |
| Blur Strength Min (0.5) | **Blur Strength Min** | |
| Blur Strength Max (1.0) | **Blur Strength Max** | |
| Reseed (Button) | **Reseed** | Neues, reproduzierbares Zufallsmuster |
| – | **Ghost (Time Offset)**: Ghost Amount, Distance, Direction, Timing Offset | Zusatz aus unseren früheren Versionen, standardmäßig **aus** (Amount 0) |

**Ergebnis:** Alle im Video sichtbaren Regler sind vorhanden – bis auf
„Input Color Space“, der für diesen Effekt keine Wirkung hätte.

## Optischer Vergleich

Mit dem unbearbeiteten Bild aus dem Video als Eingang wurden Anteil des
scharfen Bildes (55 %) und Schlierenlänge (Blur Strength 1.0 = 150 px @1080p)
so abgestimmt, dass die **Standardwerte** dem Ergebnis im Video entsprechen.
Vorschau selbst erzeugen:

```bash
python tools/make_preview.py mein_bild.png vorschau            # Beispiel-Looks
python tools/make_preview.py mein_bild.png vorschau strength=1.5 symmetric=1
```

## Unterschiede / offene Punkte

* **Blur-Typ „Realistic“** von Resolve ist nicht dokumentiert; unser Modell
  (55 % scharfes Bild + 45 % gleichmäßige Schliere in linearem Licht) sieht
  im Vergleich sehr ähnlich aus, ist aber nicht pixelgleich.
* **Cleanup Amount** ist eine Interpretation (siehe Tabelle).
* Der Fusion-Effekt im Video läuft auf der Edit-Seite; unser OFX-Plugin läuft
  auf Edit- **und** Color-Seite.
