; Copyright (c) 2026 Red Marmoset Studios Ltd.
; NSIS script for the Windows installer. Package.ps1 builds the app, stages its files and runs makensis with:
;   /DVERSION=0.1.0.27         MAJOR.MINOR.PATCH.REVISION, as stamped into Marconi.exe
;   /DSTAGE_DIR=<dir>          App\ holds Marconi.exe and the MSVC runtime DLLs; UninstallFiles.nsh deletes them;
;                              License.txt is the licence shown before installing, with each paragraph on one line
;   /DOUT_FILE=<path>          the setup executable to write
;   /DSIGN_SCRIPT=<path>       optional: a script that signs the file given as its only argument. Run on Uninstall.exe
;                              before it's embedded and on the setup executable once written (needs NSIS 3.08+).
; Paths are absolute; makensis runs from this folder, so the relative ones below are relative to it.
;
; "Start Marconi now" and "Open the source code on GitHub" on the finish page need the ShellExecAsUser plug-in in the
; NSIS installation's Plugins\x86-unicode folder (see Package.ps1, which checks for it).

!ifndef VERSION | STAGE_DIR | OUT_FILE
    !error "Run Package.ps1 rather than makensis directly (it defines VERSION, STAGE_DIR and OUT_FILE)."
!endif

!define APP_NAME "Marconi"
!define PUBLISHER "Red Marmoset Studios Ltd."
!define APP_EXE "Marconi.exe"
!define SOURCE_URL "https://github.com/Red-Marmoset/marconi"
!define SOURCE_SHORTCUT "${APP_NAME} source code.url"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define APP_ICON "..\Modules\MarconiUI\Resources\Marconi.ico"
; Branding bitmaps from Tools/make_installer_art.py; rerun it after changing the icon, colours or fonts.
!define WIZARD_BITMAP "Art\Wizard.bmp"
!define HEADER_BITMAP "Art\Header.bmp"

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}  |  ${PUBLISHER}"

; The setup executable's own version information (Explorer > Properties > Details).
VIProductVersion "${VERSION}"
VIFileVersion "${VERSION}"
VIAddVersionKey /LANG=0 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "${APP_NAME} ${VERSION} Setup"
VIAddVersionKey /LANG=0 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=0 "LegalCopyright" "Copyright (c) 2026 ${PUBLISHER}"

; Authenticode signing, when Package.ps1 is configured for it. The uninstaller is signed before it's embedded and the
; setup executable after it's written; makensis fails if either signing fails.
!ifdef SIGN_SCRIPT
    !uninstfinalize '"${SIGN_SCRIPT}" "%1"' = 0
    !finalize '"${SIGN_SCRIPT}" "%1"' = 0
!endif

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING

; The red panel on the welcome and finish pages and the logo in the header of the others.
!define MUI_WELCOMEFINISHPAGE_BITMAP "${WIZARD_BITMAP}"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${WIZARD_BITMAP}"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${HEADER_BITMAP}"
!define MUI_HEADERIMAGE_UNBITMAP "${HEADER_BITMAP}"

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APP_NAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This will install ${APP_NAME}, the VNC client from ${PUBLISHER}, on your computer.$\r$\n$\r$\nIf ${APP_NAME} is running, close it before continuing. Your saved connections are kept when upgrading.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE "${APP_NAME} is ready"
!define MUI_FINISHPAGE_TEXT "${APP_NAME} ${VERSION} has been installed on your computer.$\r$\n$\r$\nClick Finish to close Setup."
; Setup runs as administrator, and MUI's own run would start Marconi that way too: its settings and saved passwords
; would then belong to the administrator account rather than the signed-in user. LaunchAsUser starts it unelevated.
; MUI_FINISHPAGE_RUN only enables the checkbox; its value isn't used when MUI_FINISHPAGE_RUN_FUNCTION is set.
!define MUI_FINISHPAGE_RUN ""
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME} now"
; The second checkbox opens the source code in the user's browser, unelevated for the same reason.
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenSourceAsUser
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the source code on GitHub"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

; The MIT licence. The checkbox must be ticked before Next is enabled.
!define MUI_LICENSEPAGE_CHECKBOX

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\License.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_WELCOMEPAGE_TEXT "This will remove ${APP_NAME} from your computer.$\r$\n$\r$\nYour saved connections, stored passwords and logs are kept unless you choose to remove them too, two pages on.$\r$\n$\r$\nClose ${APP_NAME} before continuing, then click Next."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

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

; The account signed in at this computer's screen (it may differ from the account running this uninstaller after a
; UAC prompt), or "" if Windows won't say. Pushes the name.
Function un.GetConsoleUser
    System::Call 'kernel32::WTSGetActiveConsoleSessionId() i .r0'
    System::Call 'wtsapi32::WTSQuerySessionInformationW(p 0, i r0, i 5, *p .r1, *i .r2) i .r3'
    StrCpy $4 ""
    ${If} $3 != 0
        System::Call '*$1(&w256 .r4)'
        System::Call 'wtsapi32::WTSFreeMemory(p r1)'
    ${EndIf}
    Push $4
FunctionEnd

; Explorer launches Marconi on Setup's behalf, so it runs as the signed-in user. If Explorer isn't running, or is
; itself elevated, the plug-in falls back to a plain ShellExecute. The empty action means the default ("open").
Function LaunchAsUser
    SetOutPath "$INSTDIR"
    ShellExecAsUser::ShellExecAsUser "" "$INSTDIR\${APP_EXE}" ""
FunctionEnd

Function OpenSourceAsUser
    ShellExecAsUser::ShellExecAsUser "" "${SOURCE_URL}" ""
FunctionEnd

Section "${APP_NAME}" SecApp
    SectionIn RO

    ; Upgrading: remove the previous version first (keeping its user data), so files it had and this one doesn't
    ; aren't left behind. _?= runs its uninstaller in place and waits for it.
    ;
    ; That uninstaller runs as administrator, so it's only trusted inside Program Files, which standard users can't
    ; write to. An earlier install elsewhere (/D=<folder>) could have had its Uninstall.exe replaced by anyone who
    ; can write to that folder; in that case the old files are left alone and this version is installed over them.
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 != ""
        StrLen $1 "$PROGRAMFILES64\"
        StrCpy $2 "$0\" $1
        ${If} $2 != "$PROGRAMFILES64\"
            DetailPrint "The previous version at $0 is outside $PROGRAMFILES64, so its uninstaller won't be run. Installing over it."
        ${ElseIf} ${FileExists} "$0\Uninstall.exe"
            ExecWait '"$0\Uninstall.exe" /S _?=$0'
            Delete "$0\Uninstall.exe"
            RMDir "$0"
        ${EndIf}
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
    ; An internet shortcut, which opens the source code in the user's default browser.
    WriteINIStr "$SMPROGRAMS\${SOURCE_SHORTCUT}" "InternetShortcut" "URL" "${SOURCE_URL}"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    SetShellVarContext all
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The ${APP_NAME} VNC client."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add ${APP_NAME}, and a link to its source code on GitHub, to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add a ${APP_NAME} shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Saved connections (registry), keychain passwords and logs belong to each user and are kept unless asked for, so
; reinstalling or upgrading doesn't lose them. This section comes first because it needs Marconi.exe, which the
; next one deletes. It is unticked by default and never runs in the silent uninstall an upgrade does.
Section /o "un.Saved connections, passwords and logs" SecUnData
    ; The data lives in the user's own profile and keychain, so only Marconi running as that user can remove it.
    ; After a UAC prompt the uninstaller may be running as a different (administrator) account.
    Call un.GetConsoleUser
    Pop $1
    UserInfo::GetName
    Pop $2
    ${If} $1 != ""
    ${AndIf} $1 != $2
        MessageBox MB_OK|MB_ICONINFORMATION "This uninstaller is running as $2, so it can't remove the saved connections of $1.$\r$\n$\r$\nTo remove them, open ${APP_NAME} as $1 and choose Settings > Remove my saved connections, passwords and logs." /SD IDOK
    ${Else}
        DetailPrint "Removing the saved connections, passwords and logs of $2"
        ExecWait '"$INSTDIR\${APP_EXE}" --remove-my-data'
    ${EndIf}
SectionEnd

Section "un.${APP_NAME}" SecUnApp
    SectionIn RO
    SetShellVarContext all
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${SOURCE_SHORTCUT}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; Only the files the installer put there, in case the folder was shared with something else.
    !include "${STAGE_DIR}\UninstallFiles.nsh"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnApp} "Remove the ${APP_NAME} program files and shortcuts."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnData} "Also remove your saved connections, their passwords from Windows Credential Manager, your settings and ${APP_NAME}'s logs. Other users' data stays."
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
