; Lumen installer (NSIS 3). Builds on Windows (CI) or Linux (apt install nsis):
;   makensis -DVERSION=0.10.18 -DSRCDIR=path\to\Lumen -DOUTFILE=Lumen-setup.exe installer/lumen.nsi
; SRCDIR is the packaged folder (lumen.exe, DLLs, effects, examples, docs).

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SRCDIR
  !define SRCDIR "..\dist\Lumen"
!endif
!ifndef OUTFILE
  !define OUTFILE "Lumen-${VERSION}-setup.exe"
!endif

!define APP "Lumen"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Lumen"

Name "${APP} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP}"
; The folder of a previous install is read in .onInit, in the 64-bit registry
; view it was written to (InstallDirRegKey reads the 32-bit one, so a custom
; folder was forgotten and a second copy went to Program Files).
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "Lumen ${VERSION}"

!define MUI_ICON "..\assets\lumen.ico"
!define MUI_UNICON "..\assets\lumen.ico"
!define MUI_ABORTWARNING
; Started through Explorer: not as administrator (an elevated Lumen cannot take
; files dragged from Explorer).
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchLumen

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "English"

LangString SecDesktop ${LANG_PORTUGUESEBR} "Atalho na área de trabalho"
LangString SecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString SecAssoc ${LANG_PORTUGUESEBR} "Abrir arquivos .lumen com o Lumen"
LangString SecAssoc ${LANG_ENGLISH} "Open .lumen files with Lumen"
LangString Need64 ${LANG_PORTUGUESEBR} "O Lumen precisa do Windows 64 bits."
LangString Need64 ${LANG_ENGLISH} "Lumen requires 64-bit Windows."
LangString ProjectType ${LANG_PORTUGUESEBR} "Projeto Lumen"
LangString ProjectType ${LANG_ENGLISH} "Lumen project"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP $(Need64)
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all ; shortcuts for every user of the machine
  ReadRegStr $0 HKLM "Software\${APP}" "InstallDir"
  StrCmp $0 "" +2
    StrCpy $INSTDIR $0
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function LaunchLumen
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\lumen.exe"'
FunctionEnd

Section "Lumen" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; An update overwrites the shipped effects and keeps the user's own .fs files
  ; (EFEITOS.md tells users to put downloaded effects in this folder).
  File /r "${SRCDIR}\*.*"
  File "..\assets\lumen.ico"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\Lumen.lnk" "$INSTDIR\lumen.exe" "" "$INSTDIR\lumen.ico"
  CreateShortcut "$SMPROGRAMS\${APP}\Lumen (console de diagnóstico).lnk" "$INSTDIR\lumen-console.exe" "--diagnose" "$INSTDIR\lumen.ico"
  CreateShortcut "$SMPROGRAMS\${APP}\Documentação.lnk" "$INSTDIR\docs"
  CreateShortcut "$SMPROGRAMS\${APP}\Desinstalar.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Lumen"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Lumen"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\lumen.ico"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "$(SecDesktop)" SecDesk
  CreateShortcut "$DESKTOP\Lumen.lnk" "$INSTDIR\lumen.exe" "" "$INSTDIR\lumen.ico"
SectionEnd

Section "$(SecAssoc)" SecAssocFiles
  WriteRegStr HKLM "Software\Classes\.lumen" "" "Lumen.Project"
  WriteRegStr HKLM "Software\Classes\Lumen.Project" "" "$(ProjectType)"
  WriteRegStr HKLM "Software\Classes\Lumen.Project\DefaultIcon" "" "$INSTDIR\lumen.ico"
  WriteRegStr HKLM "Software\Classes\Lumen.Project\shell\open\command" "" '"$INSTDIR\lumen.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Lumen.lnk"
  RMDir /r "$SMPROGRAMS\${APP}"
  ; Only what the installer put there: projects and media saved elsewhere stay,
  ; and so do the user's effects (the folder goes only when it is empty).
  ; Shipped effects (the unit tests check this list against effects/):
  Delete "$INSTDIR\effects\Aneis.fs"
  Delete "$INSTDIR\effects\Bordas.fs"
  Delete "$INSTDIR\effects\Brilho.fs"
  Delete "$INSTDIR\effects\Caleidoscopio.fs"
  Delete "$INSTDIR\effects\ChromaKey.fs"
  Delete "$INSTDIR\effects\Degrade.fs"
  Delete "$INSTDIR\effects\Desfoque.fs"
  Delete "$INSTDIR\effects\DeslocarRGB.fs"
  Delete "$INSTDIR\effects\Duotone.fs"
  Delete "$INSTDIR\effects\Espelho.fs"
  Delete "$INSTDIR\effects\Limiar.fs"
  Delete "$INSTDIR\effects\Niveis.fs"
  Delete "$INSTDIR\effects\Onda.fs"
  Delete "$INSTDIR\effects\Pixelizar.fs"
  Delete "$INSTDIR\effects\Posterizar.fs"
  Delete "$INSTDIR\effects\Rastro.fs"
  Delete "$INSTDIR\effects\RecorteSuave.fs"
  Delete "$INSTDIR\effects\Ruido.fs"
  Delete "$INSTDIR\effects\T_Circulo.fs"
  Delete "$INSTDIR\effects\T_Cortina.fs"
  Delete "$INSTDIR\effects\T_Dissolver.fs"
  Delete "$INSTDIR\effects\T_Glitch.fs"
  Delete "$INSTDIR\effects\T_Pixels.fs"
  Delete "$INSTDIR\effects\T_Zoom.fs"
  Delete "$INSTDIR\effects\Varredura.fs"
  Delete "$INSTDIR\effects\Vinheta.fs"
  RMDir "$INSTDIR\effects"
  RMDir /r "$INSTDIR\examples"
  RMDir /r "$INSTDIR\docs"
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\lumen.ico"
  Delete "$INSTDIR\README.md"
  RMDir "$INSTDIR"
  ReadRegStr $0 HKLM "Software\Classes\.lumen" ""
  StrCmp $0 "Lumen.Project" 0 +3
    DeleteRegKey HKLM "Software\Classes\.lumen"
    DeleteRegKey HKLM "Software\Classes\Lumen.Project"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${APP}"
SectionEnd
