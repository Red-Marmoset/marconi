; Copyright (c) 2026 Red Marmoset Studios Ltd.
; NSIS script for the Windows installer. Package.ps1 builds the app, stages its files and runs makensis with:
;   /DVERSION=0.1.0.27         MAJOR.MINOR.PATCH.REVISION, as stamped into Marconi.exe
;   /DSTAGE_DIR=<dir>          App\ holds Marconi.exe and the MSVC runtime DLLs; UninstallFiles.nsh deletes them
;   /DOUT_FILE=<path>          the setup executable to write
; Paths are absolute; makensis runs from this folder, so the relative ones below are relative to it.

!ifndef VERSION | STAGE_DIR | OUT_FILE
    !error "Run Package.ps1 rather than makensis directly (it defines VERSION, STAGE_DIR and OUT_FILE)."
!endif

!define APP_NAME "Marconi"
!define PUBLISHER "Red Marmoset Studios Ltd."
!define APP_EXE "Marconi.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define APP_ICON "..\Modules\MarconiUI\Resources\Marconi.ico"

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}"

; The setup executable's own version information (Explorer > Properties > Details).
VIProductVersion "${VERSION}"
VIFileVersion "${VERSION}"
VIAddVersionKey /LANG=0 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "${APP_NAME} ${VERSION} Setup"
VIAddVersionKey /LANG=0 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=0 "LegalCopyright" "Copyright (c) 2026 ${PUBLISHER}"

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Section "${APP_NAME}" SecApp
    SectionIn RO

    ; Upgrading: remove the previous version first (keeping its user data), so files it had and this one doesn't
    ; aren't left behind. _?= runs its uninstaller in place and waits for it.
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 != ""
    ${AndIf} ${FileExists} "$0\Uninstall.exe"
        ExecWait '"$0\Uninstall.exe" /S _?=$0'
        Delete "$0\Uninstall.exe"
        RMDir "$0"
    ${EndIf}

    SetOutPath "$INSTDIR"
    ; Marconi.exe and the MSVC runtime DLLs, deployed next to it (app-local) so no separate redistributable is needed.
    File "${STAGE_DIR}\App\*.*"
    File "/oname=LICENSE.txt" "..\LICENSE"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Settings > Apps and Programs and Features.
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Start menu shortcut" SecStartMenu
    SetShellVarContext all
    CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    SetShellVarContext all
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The ${APP_NAME} VNC client."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add ${APP_NAME} to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add a ${APP_NAME} shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Saved connections (registry), keychain passwords and logs belong to each user and are kept, so reinstalling or
; upgrading doesn't lose them.
Section "Uninstall"
    SetShellVarContext all
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; Only the files the installer put there, in case the folder was shared with something else.
    !include "${STAGE_DIR}\UninstallFiles.nsh"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
