; Instalador de Lienzo Casero para Windows 10 y 11 (64 bits)
; Se compila con NSIS:  makensis -DEXE=ruta\LienzoCasero.exe -DVERSION=0.1.0 lienzo-casero.nsi

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "0.12.0"
!endif
!ifndef EXE
  !define EXE "..\build\LienzoCasero.exe"
!endif
!ifndef OUTFILE
  !define OUTFILE "LienzoCasero-${VERSION}-instalador.exe"
!endif

!define APPNAME "Lienzo Casero"
!define APPKEY "LienzoCasero"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPKEY}"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPKEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=3082 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=3082 "FileDescription" "Instalador de ${APPNAME}"
VIAddVersionKey /LANG=3082 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=3082 "ProductVersion" "${VERSION}"

!define MUI_ICON "..\assets\icon.ico"
!define MUI_UNICON "..\assets\icon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Bienvenido a ${APPNAME}"
!define MUI_WELCOMEPAGE_TEXT "Este asistente instalará ${APPNAME}, un programa sencillo para hacer tarjetas, invitaciones, carteles y listas en casa.$\r$\n$\r$\nPulsa Siguiente para continuar."
!define MUI_FINISHPAGE_RUN "$INSTDIR\LienzoCasero.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir ${APPNAME} ahora"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APPNAME} necesita Windows de 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Programa" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "/oname=LienzoCasero.exe" "${EXE}"
  File "/oname=icono.ico" "..\assets\icon.ico"
  ; Fuentes gratuitas que vienen con el programa
  SetOutPath "$INSTDIR\fuentes"
  ; Versiones antiguas (variables) que se han cambiado por otras
  Delete "$INSTDIR\fuentes\Montserrat.ttf"
  Delete "$INSTDIR\fuentes\PlayfairDisplay.ttf"
  Delete "$INSTDIR\fuentes\Fredoka.ttf"
  File "..\assets\fonts\*.ttf"
  File "..\assets\fonts\OFL.txt"
  File "..\assets\fonts\LICENSE-Apache.txt"
  File "..\assets\fonts\LEEME.md"
  ; Dibujos (Twemoji, licencia CC BY 4.0)
  SetOutPath "$INSTDIR\dibujos"
  File /r "..\assets\dibujos\*.*"
  SetOutPath "$INSTDIR"

  ; Accesos directos (para todos los usuarios del ordenador)
  SetShellVarContext all
  CreateShortCut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\LienzoCasero.exe" "" "$INSTDIR\LienzoCasero.exe" 0
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\LienzoCasero.exe" "" "$INSTDIR\LienzoCasero.exe" 0

  ; Abrir los archivos .lienzo con doble clic
  WriteRegStr HKLM "Software\Classes\.lienzo" "" "LienzoCasero.Diseno"
  WriteRegStr HKLM "Software\Classes\LienzoCasero.Diseno" "" "Diseño de Lienzo Casero"
  WriteRegStr HKLM "Software\Classes\LienzoCasero.Diseno\DefaultIcon" "" "$INSTDIR\icono.ico"
  WriteRegStr HKLM "Software\Classes\LienzoCasero.Diseno\shell\open\command" "" '"$INSTDIR\LienzoCasero.exe" "%1"'

  ; Datos para "Agregar o quitar programas"
  WriteRegStr HKLM "Software\${APPKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "Familia"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\LienzoCasero.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Desinstalar.exe"

  ; Avisar al Explorador de que hay un tipo de archivo nuevo
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$INSTDIR\LienzoCasero.exe"
  Delete "$INSTDIR\icono.ico"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir /r "$INSTDIR\fuentes"
  RMDir /r "$INSTDIR\dibujos"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Classes\LienzoCasero.Diseno"
  ReadRegStr $0 HKLM "Software\Classes\.lienzo" ""
  StrCmp $0 "LienzoCasero.Diseno" 0 +2
    DeleteRegKey HKLM "Software\Classes\.lienzo"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\${APPKEY}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  ; Los diseños y plantillas de cada persona (en %APPDATA%\LienzoCasero) no se borran
SectionEnd
