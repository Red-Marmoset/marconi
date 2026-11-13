; Copyright (c) 2026 Red Marmoset Studios Ltd.
; NSIS script for the Marconi Server installer (Windows). Package.ps1 builds and stages the server and runs makensis with:
;   /DVERSION=0.1.0.27         MAJOR.MINOR.PATCH.REVISION, the same as the client's
;   /DSTAGE_DIR=<dir>          App\ holds MarconiServer.exe and the MSVC runtime DLLs; UninstallFiles.nsh deletes them;
;                              LicensePage.txt is the license with one line per paragraph
;   /DOUT_FILE=<path>          the setup executable to write
;   /DSIGN_SCRIPT=<path>       optional: a script that signs the file given as its only argument (needs NSIS 3.08+)
; Paths are absolute; makensis runs from this folder, so the relative ones below are relative to it.
;
; NOT YET RUN ON WINDOWS. It compiles with makensis on Linux (checked with a stand-in payload), but nothing here has
; been run: the pages, the firewall rule, the start-with-Windows entry, the settings folder's permissions and the
; uninstaller all need a try on a real Windows PC. SERVER_PLAN.md, Phase 2, lists what to check.
;
; The server installs for the whole computer (SERVER_PLAN.md, section 1, point 8): the installer asks for administrator
; permission when it starts, and puts the program in Program Files. Until the Windows service exists (Phase 3) the
; program is the tray app, started when someone signs in through a Run entry in the registry ("Start with Windows").
;
; "Start Marconi Server now" on the finish page needs the ShellExecAsUser plug-in, as the client's installer does.
;
; The access password is typed on its own page. It never goes on a command line (where any program could read it): Setup
; writes it to a private temporary file, MarconiServer.exe --set-password-file reads it, stores only its hash and removes
; the file. A silent install (/S) has no page and takes no password, so the tray icon asks for one the first time it
; runs (SERVER_PLAN.md, section 1, point 9).

!ifndef VERSION | STAGE_DIR | OUT_FILE
    !error "Run Package.ps1 rather than makensis directly (it defines VERSION, STAGE_DIR and OUT_FILE)."
!endif

!define APP_NAME "Marconi Server"
!define PUBLISHER "Red Marmoset Studios Ltd."
!define APP_EXE "MarconiServer.exe"
!define APP_ICON_FILE "MarconiServer.ico"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define FIREWALL_RULE "${APP_NAME}"
!define DEFAULT_PORT "1895"
!define DATA_FOLDER "Marconi Server"
!define APP_ICON "..\Modules\MarconiUI\Resources\Marconi.ico"
; Branding bitmaps from Tools/make_installer_art.py, shared with the client's installer.
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

; Authenticode signing, when Package.ps1 is configured for it (see Marconi.nsi).
!ifdef SIGN_SCRIPT
    !uninstfinalize '"${SIGN_SCRIPT}" "%1"' = 0
    !finalize '"${SIGN_SCRIPT}" "%1"' = 0
!endif

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "StrFunc.nsh"
${StrStr}
${UnStrStr}

Var PasswordBox
Var ConfirmBox
Var Password        ; what was typed on the password page ("" when none: silent install, or an upgrade keeping it)
Var HasSettings     ; 1 when an earlier install left its settings, so the password may stay as it is
Var ConsoleUser     ; the account signed in at this computer's screen: it runs the tray app and keeps the settings
Var WasRunning      ; 1 when Setup closed a running ${APP_NAME} (it is started again at the end)

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING

!define MUI_WELCOMEFINISHPAGE_BITMAP "${WIZARD_BITMAP}"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${WIZARD_BITMAP}"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${HEADER_BITMAP}"
!define MUI_HEADERIMAGE_UNBITMAP "${HEADER_BITMAP}"

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APP_NAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This will install ${APP_NAME} on this computer, so you can see and control it from another computer with Marconi.$\r$\n$\r$\nAnyone who knows the access password you choose can control this computer, so pick one you would not mind guarding.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE "${APP_NAME} is ready"
!define MUI_FINISHPAGE_TEXT "${APP_NAME} ${VERSION} has been installed on this computer.$\r$\n$\r$\nTo connect, open Marconi on another computer and type marconi://this computer's name. If Windows asks about the certificate, check that it matches the one shown by the ${APP_NAME} icon near the clock, then trust it.$\r$\n$\r$\nClick Finish to close Setup."
; Setup runs as administrator, and MUI's own run would start the program that way too: it would then keep its settings
; for the administrator account. LaunchAsUser starts it unelevated, as the signed-in user.
!define MUI_FINISHPAGE_RUN ""
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME} now"

!define MUI_LICENSEPAGE_CHECKBOX

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\LicensePage.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page custom PasswordPageCreate PasswordPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_WELCOMEPAGE_TEXT "This will remove ${APP_NAME} from this computer.$\r$\n$\r$\nIts settings, certificate, password and logs are kept unless you choose to remove them too, two pages on.$\r$\n$\r$\nIf ${APP_NAME} is running, it is closed first. Click Next to continue."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; The account signed in at this computer's screen (it may differ from the account running Setup after a UAC prompt),
; or "" if Windows won't say. Pushes the name.
!macro GetConsoleUserFunction PREFIX
Function ${PREFIX}GetConsoleUser
    System::Call 'kernel32::WTSGetActiveConsoleSessionId() i .r0'
    System::Call 'wtsapi32::WTSQuerySessionInformationW(p 0, i r0, i 5, *p .r1, *i .r2) i .r3'
    StrCpy $4 ""
    ${If} $3 != 0
        System::Call '*$1(&w256 .r4)'
        System::Call 'wtsapi32::WTSFreeMemory(p r1)'
    ${EndIf}
    Push $4
FunctionEnd
!macroend
!insertmacro GetConsoleUserFunction ""

; Closes any running ${APP_EXE} so its files can be replaced or removed: first politely, then, after ten seconds,
; forcibly. Sets $WasRunning when there was one. taskkill and tasklist come with Windows.
; NSIS can't expand a define named by another (${${X}}), so this picks the installer's or uninstaller's StrStr.
!macro CloseRunningAppStrStr PREFIX OUT IN FIND
    !if "${PREFIX}" == "un."
        ${UnStrStr} ${OUT} ${IN} "${FIND}"
    !else
        ${StrStr} ${OUT} ${IN} "${FIND}"
    !endif
!macroend

!macro CloseRunningAppFunction PREFIX
Function ${PREFIX}CloseRunningApp
    StrCpy $WasRunning 0
    nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${APP_EXE}" /NH'
    Pop $0
    Pop $1
    ${If} $0 != 0
        Return ; tasklist isn't available: carry on as before
    ${EndIf}
    !insertmacro CloseRunningAppStrStr "${PREFIX}" $2 $1 "${APP_EXE}"
    ${If} $2 == ""
        Return
    ${EndIf}
    StrCpy $WasRunning 1
    DetailPrint "Closing ${APP_NAME}"
    nsExec::ExecToStack 'taskkill /IM ${APP_EXE}'
    Pop $0
    Pop $1
    StrCpy $3 0
    ${Do}
        Sleep 500
        nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${APP_EXE}" /NH'
        Pop $0
        Pop $1
        !insertmacro CloseRunningAppStrStr "${PREFIX}" $2 $1 "${APP_EXE}"
        ${If} $2 == ""
            Return
        ${EndIf}
        IntOp $3 $3 + 1
    ${LoopUntil} $3 >= 20
    DetailPrint "${APP_NAME} didn't close; ending it"
    nsExec::ExecToStack 'taskkill /F /IM ${APP_EXE}'
    Pop $0
    Pop $1
    Sleep 1000
FunctionEnd
!macroend
!insertmacro CloseRunningAppFunction ""
!insertmacro CloseRunningAppFunction "un."

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs 64-bit Windows." /SD IDOK
        Abort
    ${EndIf}
    SetRegView 64
    ; Everything is for the whole computer: the shared start menu and the shared settings folder (ProgramData).
    SetShellVarContext all
    StrCpy $Password ""
    ${If} ${FileExists} "$APPDATA\${DATA_FOLDER}\settings.ini"
        StrCpy $HasSettings 1
    ${Else}
        StrCpy $HasSettings 0
    ${EndIf}
FunctionEnd

; The password page: a password and its confirmation. Only the first install has to set one; an upgrade can leave both
; empty to keep the password the server already has.
Function PasswordPageCreate
    !insertmacro MUI_HEADER_TEXT "Access password" "Choose the password Marconi asks for when it connects to this computer."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 24u "Anyone who knows this password can see and control this computer from another computer. It is kept scrambled on this computer, never as you typed it."
    Pop $0
    ${NSD_CreateLabel} 0 30u 100% 10u "Password"
    Pop $0
    ${NSD_CreatePassword} 0 42u 100% 12u ""
    Pop $PasswordBox
    ${NSD_CreateLabel} 0 60u 100% 10u "Type it again"
    Pop $0
    ${NSD_CreatePassword} 0 72u 100% 12u ""
    Pop $ConfirmBox
    ${If} $HasSettings == 1
        ${NSD_CreateLabel} 0 92u 100% 20u "${APP_NAME} is already set up here. Leave both boxes empty to keep its current password."
        Pop $0
    ${Else}
        ${NSD_CreateLabel} 0 92u 100% 20u "You can change it later from the ${APP_NAME} icon near the clock."
        Pop $0
    ${EndIf}
    nsDialogs::Show
FunctionEnd

Function PasswordPageLeave
    ${NSD_GetText} $PasswordBox $0
    ${NSD_GetText} $ConfirmBox $1
    ${If} $0 == ""
    ${AndIf} $1 == ""
        ${If} $HasSettings == 1
            StrCpy $Password ""
            Return
        ${EndIf}
        MessageBox MB_OK|MB_ICONEXCLAMATION "Please choose an access password."
        Abort
    ${EndIf}
    ${If} $0 != $1
        MessageBox MB_OK|MB_ICONEXCLAMATION "The two passwords are not the same. Please type them again."
        Abort
    ${EndIf}
    StrCpy $Password $0
FunctionEnd

; Explorer launches the program on Setup's behalf, so it runs as the signed-in user. If Explorer isn't running, or is
; itself elevated, the plug-in falls back to a plain ShellExecute.
Function LaunchAsUser
    SetOutPath "$INSTDIR"
    ShellExecAsUser::ShellExecAsUser "" "$INSTDIR\${APP_EXE}" ""
FunctionEnd

; A silent install has no finish page, so a ${APP_NAME} that Setup closed is started again here.
Function .onInstSuccess
    ${If} ${Silent}
    ${AndIf} $WasRunning == 1
        Call LaunchAsUser
    ${EndIf}
FunctionEnd

Section "${APP_NAME}" SecApp
    SectionIn RO
    SetShellVarContext all
    Call CloseRunningApp

    ; Upgrading: remove the previous version first (its settings stay), so files it had and this one doesn't aren't
    ; left behind. Its uninstaller runs from Program Files, which only administrators can write to. _?= runs it in
    ; place and waits for it.
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
    ; MarconiServer.exe and the MSVC runtime DLLs, deployed next to it (app-local) so no separate redistributable is needed.
    File "${STAGE_DIR}\App\*.*"
    File "/oname=${APP_ICON_FILE}" "${APP_ICON}"
    File "/oname=LICENSE.txt" "..\LICENSE"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Settings > Apps and Programs and Features.
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_ICON_FILE}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" "$0"

    ; The settings folder, in ProgramData: the settings, the certificate and key and the logs. Administrators and the
    ; system get everything; the account signed in at the screen (which runs the tray app until the Windows service
    ; comes) gets to change things, so its password and port changes can be saved. Nobody else can read the folder.
    CreateDirectory "$APPDATA\${DATA_FOLDER}"
    Call GetConsoleUser
    Pop $ConsoleUser
    nsExec::ExecToLog 'icacls "$APPDATA\${DATA_FOLDER}" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F"'
    Pop $0
    ${If} $0 != 0
        DetailPrint "Could not restrict who can use the settings folder (icacls exit code $0)."
    ${EndIf}
    ${If} $ConsoleUser != ""
        nsExec::ExecToLog 'icacls "$APPDATA\${DATA_FOLDER}" /grant:r "$ConsoleUser:(OI)(CI)M"'
        Pop $0
        ${If} $0 != 0
            DetailPrint "Could not let $ConsoleUser change the settings (icacls exit code $0). ${APP_NAME} will then keep its settings in that account's own folder instead."
        ${EndIf}
    ${EndIf}

    ; The access password: through a private temporary file, never a command line. The server reads it, keeps only its
    ; hash and removes the file; Delete here is for when it couldn't.
    ${If} $Password != ""
        InitPluginsDir
        ClearErrors
        FileOpen $0 "$PLUGINSDIR\access.txt" w
        ${IfNot} ${Errors}
            FileWriteWord $0 "65279" ; the byte order mark: the file is UTF-16 little-endian
            FileWriteUTF16LE $0 "$Password"
            FileClose $0
            ExecWait '"$INSTDIR\${APP_EXE}" --set-password-file "$PLUGINSDIR\access.txt"' $1
            Delete "$PLUGINSDIR\access.txt"
            ${If} $1 != 0
                MessageBox MB_OK|MB_ICONEXCLAMATION "${APP_NAME} could not store the password (code $1). It will ask for one when it first runs." /SD IDOK
            ${EndIf}
        ${Else}
            MessageBox MB_OK|MB_ICONEXCLAMATION "Setup could not write the password to a temporary file. ${APP_NAME} will ask for one when it first runs." /SD IDOK
        ${EndIf}
        StrCpy $Password ""
    ${EndIf}
SectionEnd

Section "Allow through Windows Firewall" SecFirewall
    ; Without this, other computers can't reach the server. The rule lets this program receive connections on the
    ; port Marconi uses (${DEFAULT_PORT}); if the port is changed later, the rule needs changing too.
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FIREWALL_RULE}"'
    Pop $0
    nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${FIREWALL_RULE}" dir=in action=allow protocol=TCP localport=${DEFAULT_PORT} program="$INSTDIR\${APP_EXE}" enable=yes'
    Pop $0
    ${If} $0 != 0
        DetailPrint "Could not add the firewall rule (netsh exit code $0). Other computers may not be able to connect until port ${DEFAULT_PORT} is allowed."
    ${EndIf}
SectionEnd

Section "Start with Windows" SecAutoStart
    ; Starts the tray app for whoever signs in. (The Windows service of a later version replaces this.)
    WriteRegStr HKLM "${RUN_KEY}" "${APP_NAME}" '"$INSTDIR\${APP_EXE}"'
SectionEnd

Section "Start menu shortcut" SecStartMenu
    SetShellVarContext all
    CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_ICON_FILE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The ${APP_NAME} program and its access password."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecFirewall} "Let other computers reach ${APP_NAME} through Windows Firewall (port ${DEFAULT_PORT})."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAutoStart} "Start ${APP_NAME} when someone signs in to Windows."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add ${APP_NAME} to the Start menu."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.onInit
    SetRegView 64
    SetShellVarContext all
FunctionEnd

; The settings, certificate and key, password and logs are kept unless asked for, so reinstalling or upgrading doesn't
; lose them. This section never runs in the silent uninstall an upgrade does.
Section /o "un.Settings, password and logs" SecUnData
    RMDir /r "$APPDATA\${DATA_FOLDER}"
SectionEnd

Section "un.${APP_NAME}" SecUnApp
    SectionIn RO
    Call un.CloseRunningApp
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"

    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FIREWALL_RULE}"'
    Pop $0
    DeleteRegValue HKLM "${RUN_KEY}" "${APP_NAME}"

    ; Only the files the installer put there, in case the folder was shared with something else.
    !include "${STAGE_DIR}\UninstallFiles.nsh"
    Delete "$INSTDIR\${APP_ICON_FILE}"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnApp} "Remove the ${APP_NAME} program, its shortcut and its firewall rule."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnData} "Also remove ${APP_NAME}'s settings, its certificate, the access password and the logs, kept in the ProgramData folder. Settings kept in a user's own folder stay."
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
