# 2. Selbst bauen (Windows, Visual Studio)

> Du musst nichts bauen, um den Effekt zu benutzen – der fertige Installer liegt
> in `dist/`. Diese Anleitung brauchst du nur, wenn du den Code änderst.

## A) Windows 10/11 mit Visual Studio 2022 (empfohlen)

### Einmalig installieren

1. **Visual Studio 2022 Community** (kostenlos):
   <https://visualstudio.microsoft.com/de/downloads/>
   Im Installer den Workload **"Desktopentwicklung mit C++"** anhaken.
   Darin sind der MSVC-Compiler **und CMake** enthalten.
2. **Git für Windows**: <https://git-scm.com/download/win>
3. Optional für den Installer: **NSIS 3** <https://nsis.sourceforge.io/Download>

Die OpenFX-SDK-Header sind bereits im Projekt (`external/openfx/include`),
du musst das SDK **nicht** separat herunterladen.

### Bauen

1. Startmenü → **"Developer PowerShell for VS 2022"** öffnen.
2. Projekt holen und bauen:

   ```powershell
   cd $HOME\Documents
   git clone https://github.com/veitweller327-arch/yeye.proj.git
   cd yeye.proj
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

3. **Prüfen** (Unit-Tests + Mini-Host, der die echte DLL lädt):

   ```powershell
   ctest --test-dir build -C Release --output-on-failure
   ```

   Erwartet: `100% tests passed, 0 tests failed out of 2`.

4. Ergebnis: `build\TimingShiftFX.ofx.bundle\Contents\Win64\TimingShiftFX.ofx`

### Installieren nach dem Bauen

Variante 1 – Installer bauen (NSIS installiert):

```powershell
& "${env:ProgramFiles(x86)}\NSIS\makensis.exe" "-DBUNDLE_DIR=$PWD\build\TimingShiftFX.ofx.bundle" "-DOUT_FILE=$PWD\TimingShiftFX-Setup.exe" installer\TimingShiftFX.nsi
.\TimingShiftFX-Setup.exe
```

Variante 2 – direkt kopieren (PowerShell **als Administrator**):

```powershell
$dst = "$env:CommonProgramFiles\OFX\Plugins"
New-Item -ItemType Directory -Force $dst | Out-Null
Remove-Item -Recurse -Force "$dst\TimingShiftFX.ofx.bundle" -ErrorAction SilentlyContinue
Copy-Item -Recurse build\TimingShiftFX.ofx.bundle $dst
```

Danach Resolve **neu starten**.

## B) Cross-Build unter Linux (so wurde `dist/` erzeugt)

```bash
pip install ziglang          # Zig dient als Windows-C++-Compiler
sudo apt install nsis zip    # Installer + ZIP
scripts/package_windows.sh   # → dist/*.exe und dist/*.zip
```

Linux-Tests (auch Mock-Host mit der Linux-Version des Plugins):

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

## C) Automatischer Build auf GitHub

`.github/workflows/build.yml` baut bei jedem Push mit **MSVC auf Windows**,
führt beide Tests aus und lädt Plugin + Setup.exe als Artefakt hoch
(GitHub → Reiter "Actions" → letzter Lauf → "Artifacts").

## Vorschaubilder ohne Resolve

```bash
pip install pillow numpy
python tools/make_preview.py mein_bild.png vorschau
# → vorschau_subtle.png, vorschau_musicvideo.png
```

## Was jede Datei macht

| Datei | Zweck |
|-------|-------|
| `CMakeLists.txt` | Bauanleitung für CMake: Kern-Bibliothek, Plugin (`.ofx`), Tests, Bundle-Ordner |
| `src/TimingShiftCore.h` | Alle Einstellungen (`Settings`), Bild-Strukturen, Presets |
| `src/TimingShiftCore.cpp` | Der eigentliche Effekt (6 Schritte pro Zeile, Multithreading) |
| `src/TimingShiftPlugin.cpp` | OFX-Schnittstelle zu Resolve |
| `tests/core_tests.cpp` | Prüft Determinismus, keine schwarzen Ränder, Alpha, HD/UHD-Skalierung, Benchmark |
| `tests/mock_host.cpp` | Lädt die echte `.ofx`-Datei und spielt alle OFX-Aktionen durch |
| `installer/TimingShiftFX.nsi` | Setup.exe-Skript (installiert nach `Common Files\OFX\Plugins`) |
