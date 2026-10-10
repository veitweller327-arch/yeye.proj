; TimingShiftFX.nsi - Windows installer for the Timing Shift FX OpenFX plugin.
;
; Build (Linux:  apt install nsis | Windows: install NSIS 3 from nsis.sourceforge.io):
;   makensis -DBUNDLE_DIR=<path to TimingShiftFX.ofx.bundle> -DOUT_FILE=<setup.exe> installer/TimingShiftFX.nsi
;
; What it does: copies TimingShiftFX.ofx.bundle into
;   C:\Program Files\Common Files\OFX\Plugins\
; (the standard OpenFX folder that DaVinci Resolve scans on start-up)
; and registers an uninstaller in "Apps & Features".

Target amd64-unicode   ; native 64-bit installer (the plugin is x64-only anyway)
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef BUNDLE_DIR
  !define BUNDLE_DIR "..\build\TimingShiftFX.ofx.bundle"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "TimingShiftFX-${VERSION}-Windows-Setup.exe"
!endif

!define PRODUCT "Timing Shift FX"
!define BUNDLE "TimingShiftFX.ofx.bundle"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TimingShiftFX"

Name "${PRODUCT} ${VERSION}"
OutFile "${OUT_FILE}"
RequestExecutionLevel admin
InstallDir "$COMMONFILES64\OFX\Plugins"
SetCompressor /SOLID lzma
BrandingText "${PRODUCT} - OpenFX plugin for DaVinci Resolve"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "${PRODUCT} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the ${PRODUCT} OpenFX effect for DaVinci Resolve (Windows x64).$\r$\n$\r$\nPlease CLOSE DaVinci Resolve before you continue.$\r$\n$\r$\nAfter installing, start Resolve and find the effect in:$\r$\nEffects > OpenFX > Filters > Timing Shift FX"
!define MUI_FINISHPAGE_TITLE "Installation finished"
!define MUI_FINISHPAGE_TEXT "Start DaVinci Resolve. The effect is in Effects > OpenFX > ${PRODUCT}.$\r$\n$\r$\nInstalled to:$\r$\n$INSTDIR\${BUNDLE}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT} requires 64-bit Windows 10 or 11."
    Abort
  ${EndIf}
  SetRegView 64
  StrCpy $INSTDIR "$COMMONFILES64\OFX\Plugins"
FunctionEnd

Section "Install"
  ; Replace any older version completely.
  RMDir /r "$INSTDIR\${BUNDLE}"
  SetOutPath "$INSTDIR\${BUNDLE}\Contents\Win64"
  File "${BUNDLE_DIR}\Contents\Win64\TimingShiftFX.ofx"
  SetOutPath "$INSTDIR\${BUNDLE}\Contents"
  File "${BUNDLE_DIR}\Contents\Info.plist"

  CreateDirectory "$PROGRAMFILES64\TimingShiftFX"
  WriteUninstaller "$PROGRAMFILES64\TimingShiftFX\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT} (OpenFX plugin)"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "yeye.proj"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR\${BUNDLE}"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$PROGRAMFILES64\TimingShiftFX\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  RMDir /r "$COMMONFILES64\OFX\Plugins\${BUNDLE}"
  Delete "$PROGRAMFILES64\TimingShiftFX\Uninstall.exe"
  RMDir "$PROGRAMFILES64\TimingShiftFX"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
