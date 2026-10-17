; Copyright (c) 2026 Red Marmoset Studios Ltd.
; NSIS script for the Windows installer. Package.ps1 builds the app, stages its files and runs makensis with:
;   /DVERSION=0.1.0.27         MAJOR.MINOR.PATCH.REVISION, as stamped into Marconi.exe
;   /DSTAGE_DIR=<dir>          App\ holds Marconi.exe and the MSVC runtime DLLs; UninstallFiles.nsh deletes them;
;                              LicensePage.txt is the license with one line per paragraph
;   /DOUT_FILE=<path>          the setup executable to write
;   /DSIGN_SCRIPT=<path>       optional: a script that signs the file given as its only argument. Run on Uninstall.exe
;                              before it's embedded and on the setup executable once written (needs NSIS 3.08+).
; Paths are absolute; makensis runs from this folder, so the relative ones below are relative to it.
;
; "Start Marconi now" and "Open the source code on GitHub" on the finish page need the ShellExecAsUser plug-in in the
; NSIS installation's Plugins\x86-unicode folder (see Package.ps1, which checks for it).
;
; Setup installs either just for the person running it (their own folder and registry, no administrator rights) or
; for everyone on the PC (Program Files, administrator rights). It starts without administrator rights and, when
; "Everyone" is chosen, starts itself again with them (/ALLUSERS /RELAUNCHED), carrying on from the page after the
; choice. The command line can choose too: /ALLUSERS or /CURRENTUSER, with /S for a silent install.

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
RequestExecutionLevel user

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
; No InstallDir: $INSTDIR is empty in .onInit unless /D=<folder> was given, and ApplyMode fills it in.
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
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "WinMessages.nsh"

Var AllUsers      ; 1: everyone on this PC (HKLM, Program Files); 0: just the current account (HKCU, its own folder)
Var IsAdmin       ; 1 when running with administrator rights
Var Relaunched    ; 1 in the copy started with administrator rights after "Everyone" was chosen
Var CmdLineDir    ; the folder from /D=, if any
Var AppliedMode   ; the mode $INSTDIR was last filled in for, so going back and forth keeps a folder the user chose
Var UserBlocked   ; 1 when running as an account other than the one signed in, where "Just me" would be wrong
Var ConsoleUser
Var MachineVersion
Var UserVersion
Var RadioUser
Var RadioAll

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
; When installing for everyone, Setup runs as administrator, and MUI's own run would start Marconi that way too: its
; settings and saved passwords would then belong to the administrator account rather than the signed-in user.
; LaunchAsUser starts it unelevated. MUI_FINISHPAGE_RUN only enables the checkbox; its value isn't used when
; MUI_FINISHPAGE_RUN_FUNCTION is set.
!define MUI_FINISHPAGE_RUN ""
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME} now"
; The second checkbox opens the source code in the user's browser, unelevated for the same reason.
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenSourceAsUser
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the source code on GitHub"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

; The MIT license. Its checkbox must be ticked before Next is enabled.
!define MUI_LICENSEPAGE_CHECKBOX

; The copy started with administrator rights begins at the page after the choice.
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipIfRelaunched
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipIfRelaunched
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ClearShield
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\LicensePage.txt"
Page custom ModePageCreate ModePageLeave
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ComponentsShow
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
!insertmacro GetConsoleUserFunction "un."

; Sets $IsAdmin. Under UAC an administrator's normal (unelevated) programs count as "User".
!macro CheckAdminFunction PREFIX
Function ${PREFIX}CheckAdmin
    UserInfo::GetAccountType
    Pop $0
    StrCpy $IsAdmin 0
    ${If} $0 == "Admin"
        StrCpy $IsAdmin 1
    ${EndIf}
FunctionEnd
!macroend
!insertmacro CheckAdminFunction ""
!insertmacro CheckAdminFunction "un."

; Fills in the folder and the registry and shortcut locations for $AllUsers. A folder from /D= is kept, and so is one
; chosen on the folder page unless the mode has changed since.
Function ApplyMode
    ${If} $AllUsers == 1
        SetShellVarContext all
    ${Else}
        SetShellVarContext current
    ${EndIf}
    ${If} $CmdLineDir != ""
    ${OrIf} $AppliedMode == $AllUsers
        Return
    ${EndIf}
    StrCpy $AppliedMode $AllUsers
    ; SHCTX is HKLM or HKCU, following SetShellVarContext. An earlier install's folder is reused.
    ReadRegStr $INSTDIR SHCTX "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $INSTDIR == ""
        ${If} $AllUsers == 1
            StrCpy $INSTDIR "$PROGRAMFILES64\${APP_NAME}"
        ${Else}
            StrCpy $INSTDIR "$LOCALAPPDATA\Programs\${APP_NAME}"
        ${EndIf}
    ${EndIf}
FunctionEnd

; Starts this setup again with administrator rights to install for everyone, then closes this copy. Returns with the
; error flag set if it couldn't (most often because the UAC prompt was refused).
Function RelaunchElevated
    StrCpy $0 "/ALLUSERS /RELAUNCHED"
    ${If} ${Silent}
        StrCpy $0 "$0 /S"
    ${EndIf}
    ${If} $CmdLineDir != ""
        StrCpy $0 "$0 /D=$CmdLineDir" ; /D= must come last and isn't quoted
    ${EndIf}
    ClearErrors
    ExecShell "runas" "$EXEPATH" "$0"
    ${IfNot} ${Errors}
        Quit
    ${EndIf}
FunctionEnd

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs 64-bit Windows." /SD IDOK
        Abort
    ${EndIf}
    SetRegView 64
    StrCpy $CmdLineDir $INSTDIR
    StrCpy $AppliedMode ""
    Call CheckAdmin

    ${GetParameters} $R0
    StrCpy $Relaunched 0
    ClearErrors
    ${GetOptions} $R0 "/RELAUNCHED" $1
    ${IfNot} ${Errors}
        StrCpy $Relaunched 1
    ${EndIf}

    ; Earlier installs of each kind, shown on the choice page.
    ReadRegStr $MachineVersion HKLM "${UNINSTALL_KEY}" "DisplayVersion"
    ReadRegStr $UserVersion HKCU "${UNINSTALL_KEY}" "DisplayVersion"

    ; After a UAC prompt for another account (a standard user typing an administrator's password), HKCU and the
    ; user's folders are that administrator's, so "Just me" would install for the wrong person.
    Call GetConsoleUser
    Pop $ConsoleUser
    UserInfo::GetName
    Pop $1
    StrCpy $UserBlocked 0
    ${If} $IsAdmin == 1
    ${AndIf} $ConsoleUser != ""
    ${AndIf} $ConsoleUser != $1
        StrCpy $UserBlocked 1
    ${EndIf}

    ; The command line decides; otherwise the kind already installed; otherwise "Just me". Silent installs run as
    ; administrator default to everyone, as they did before Setup offered the choice.
    ClearErrors
    ${GetOptions} $R0 "/ALLUSERS" $1
    ${IfNot} ${Errors}
        StrCpy $AllUsers 1
    ${Else}
        ClearErrors
        ${GetOptions} $R0 "/CURRENTUSER" $1
        ${IfNot} ${Errors}
            StrCpy $AllUsers 0
        ${ElseIf} $UserVersion != ""
            StrCpy $AllUsers 0
        ${ElseIf} $MachineVersion != ""
            StrCpy $AllUsers 1
        ${ElseIf} ${Silent}
        ${AndIf} $IsAdmin == 1
            StrCpy $AllUsers 1
        ${Else}
            StrCpy $AllUsers 0
        ${EndIf}
    ${EndIf}
    ${If} $UserBlocked == 1
        StrCpy $AllUsers 1
    ${EndIf}

    ${If} $AllUsers == 1
    ${AndIf} $IsAdmin != 1
        ${If} $Relaunched == 1
            MessageBox MB_OK|MB_ICONSTOP "Installing ${APP_NAME} for everyone needs administrator permission, which Setup didn't get." /SD IDOK
            Abort
        ${EndIf}
        ${If} ${Silent}
            Call RelaunchElevated
            Abort ; the prompt was refused
        ${EndIf}
    ${EndIf}
    Call ApplyMode
FunctionEnd

Function SkipIfRelaunched
    ${If} $Relaunched == 1
        Abort
    ${EndIf}
FunctionEnd

; The shield on Next means "this asks for administrator permission"; only the choice page sets it.
Function ClearShield
    GetDlgItem $0 $HWNDPARENT 1
    SendMessage $0 ${BCM_SETSHIELD} 0 0
FunctionEnd

Function UpdateShield
    GetDlgItem $1 $HWNDPARENT 1
    ${NSD_GetState} $RadioAll $0
    ${If} $0 == ${BST_CHECKED}
    ${AndIf} $IsAdmin != 1
        SendMessage $1 ${BCM_SETSHIELD} 0 1
    ${Else}
        SendMessage $1 ${BCM_SETSHIELD} 0 0
    ${EndIf}
FunctionEnd

Function ModeClicked
    Pop $0
    Call UpdateShield
FunctionEnd

Function ModePageCreate
    ${If} $Relaunched == 1
        Abort
    ${EndIf}
    !insertmacro MUI_HEADER_TEXT "Choose who can use ${APP_NAME}" "Install ${APP_NAME} just for you, or for everyone who uses this PC."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    SetShellVarContext current
    StrCpy $1 "Just me"
    ${If} $UserVersion != ""
        StrCpy $1 "$1 (version $UserVersion is installed this way)"
    ${EndIf}
    ${NSD_CreateRadioButton} 0 0 100% 12u "$1"
    Pop $RadioUser
    ${If} $UserBlocked == 1
        StrCpy $2 "Not available, because Setup is running as a different account from $ConsoleUser."
    ${Else}
        StrCpy $2 "Installed in your own folder, $LOCALAPPDATA\Programs. No administrator permission needed."
    ${EndIf}
    ${NSD_CreateLabel} 12u 14u -12u 20u "$2"
    Pop $0

    StrCpy $1 "Everyone on this PC"
    ${If} $MachineVersion != ""
        StrCpy $1 "$1 (version $MachineVersion is installed this way)"
    ${EndIf}
    ${NSD_CreateRadioButton} 0 44u 100% 12u "$1"
    Pop $RadioAll
    StrCpy $2 "Installed in $PROGRAMFILES64, for every account on this PC."
    ${If} $IsAdmin != 1
        StrCpy $2 "$2 Needs administrator permission."
    ${EndIf}
    ${NSD_CreateLabel} 12u 58u -12u 20u "$2"
    Pop $0

    ${NSD_CreateLabel} 0 96u 100% 30u "Either way, each person's saved connections and passwords stay their own."
    Pop $0

    ${If} $UserBlocked == 1
        EnableWindow $RadioUser 0
    ${EndIf}
    ${If} $AllUsers == 1
        ${NSD_Check} $RadioAll
    ${Else}
        ${NSD_Check} $RadioUser
    ${EndIf}
    ${NSD_OnClick} $RadioUser ModeClicked
    ${NSD_OnClick} $RadioAll ModeClicked
    Call UpdateShield
    Call ApplyMode ; back to the chosen mode's shell folders after SetShellVarContext current above
    nsDialogs::Show
FunctionEnd

Function ModePageLeave
    ${NSD_GetState} $RadioAll $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $AllUsers 1
        ${If} $IsAdmin != 1
            Call RelaunchElevated
            MessageBox MB_OK|MB_ICONEXCLAMATION "Installing for everyone needs administrator permission. To install without it, choose Just me."
            Abort
        ${EndIf}
    ${Else}
        StrCpy $AllUsers 0
    ${EndIf}
    Call ClearShield
    Call ApplyMode
FunctionEnd

Function ComponentsShow
    Call ClearShield
    ; In the copy started with administrator rights the earlier pages belong to the copy that closed.
    ${If} $Relaunched == 1
        GetDlgItem $0 $HWNDPARENT 3
        EnableWindow $0 0
    ${EndIf}
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
    Call ApplyMode

    ; Upgrading: remove the previous version of the same kind first (keeping its user data), so files it had and
    ; this one doesn't aren't left behind. _?= runs its uninstaller in place and waits for it.
    ;
    ; With administrator rights, the old uninstaller is only trusted inside Program Files, which standard users can't
    ; write to. Elsewhere (a "Just me" folder, or /D=<folder>) anyone who can write to that folder could have replaced
    ; Uninstall.exe; in that case the old files are left alone and this version is installed over them.
    ReadRegStr $0 SHCTX "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 != ""
        StrLen $1 "$PROGRAMFILES64\"
        StrCpy $2 "$0\" $1
        ${If} $IsAdmin == 1
        ${AndIf} $2 != "$PROGRAMFILES64\"
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

    ; Settings > Apps and Programs and Features, for everyone (HKLM) or just this account (HKCU).
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr SHCTX "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "EstimatedSize" "$0"

    ; Open vnc:// links with Marconi, unless another program already handles them.
    ReadRegStr $0 SHCTX "Software\Classes\vnc\shell\open\command" ""
    ${If} $0 == ""
    ${OrIf} $0 == '"$INSTDIR\${APP_EXE}" "%1"'
        WriteRegStr SHCTX "Software\Classes\vnc" "" "URL:VNC connection"
        WriteRegStr SHCTX "Software\Classes\vnc" "URL Protocol" ""
        WriteRegStr SHCTX "Software\Classes\vnc\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
        WriteRegStr SHCTX "Software\Classes\vnc\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
    ${EndIf}
SectionEnd

; The shortcuts follow SetShellVarContext (ApplyMode): everyone's Start menu and desktop, or just this account's.
Section "Start menu shortcut" SecStartMenu
    CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    ; An internet shortcut, which opens the source code in the default browser.
    WriteINIStr "$SMPROGRAMS\${SOURCE_SHORTCUT}" "InternetShortcut" "URL" "${SOURCE_URL}"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The ${APP_NAME} VNC client."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add ${APP_NAME}, and a link to its source code on GitHub, to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add a ${APP_NAME} shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; The uninstaller works out which kind of install it belongs to from the registry entry naming its folder, and asks
; for administrator rights only for an install for everyone, by starting the installed Uninstall.exe again with them.
Function un.onInit
    SetRegView 64
    Call un.CheckAdmin
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    ReadRegStr $1 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 == $INSTDIR
        StrCpy $AllUsers 1
    ${ElseIf} $1 == $INSTDIR
        StrCpy $AllUsers 0
    ${Else}
        ; No entry names this folder (it was removed by hand): go by where it is.
        StrLen $2 "$PROGRAMFILES64\"
        StrCpy $3 "$INSTDIR\" $2
        StrCpy $AllUsers 0
        ${If} $3 == "$PROGRAMFILES64\"
            StrCpy $AllUsers 1
        ${EndIf}
    ${EndIf}

    ${If} $AllUsers == 1
    ${AndIf} $IsAdmin != 1
        ${GetParameters} $R0
        ClearErrors
        ${GetOptions} $R0 "/RELAUNCHED" $2
        ${IfNot} ${Errors}
            MessageBox MB_OK|MB_ICONSTOP "Removing ${APP_NAME} for everyone needs administrator permission, which the uninstaller didn't get." /SD IDOK
            Quit
        ${EndIf}
        StrCpy $2 "/RELAUNCHED"
        ${If} ${Silent}
            StrCpy $2 "$2 /S"
        ${EndIf}
        ClearErrors
        ExecShell "runas" "$INSTDIR\Uninstall.exe" "$2"
        ${If} ${Errors}
            MessageBox MB_OK|MB_ICONEXCLAMATION "${APP_NAME} is installed for everyone on this PC, so removing it needs administrator permission." /SD IDOK
        ${EndIf}
        Quit
    ${EndIf}

    ${If} $AllUsers == 1
        SetShellVarContext all
    ${Else}
        SetShellVarContext current
    ${EndIf}
FunctionEnd

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
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${SOURCE_SHORTCUT}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; Only the files the installer put there, in case the folder was shared with something else.
    !include "${STAGE_DIR}\UninstallFiles.nsh"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; The vnc:// link handler, if it is still ours.
    ReadRegStr $0 SHCTX "Software\Classes\vnc\shell\open\command" ""
    ${If} $0 == '"$INSTDIR\${APP_EXE}" "%1"'
        DeleteRegKey SHCTX "Software\Classes\vnc"
    ${EndIf}
    DeleteRegKey SHCTX "${UNINSTALL_KEY}"
SectionEnd

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnApp} "Remove the ${APP_NAME} program files and shortcuts."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnData} "Also remove your saved connections, their passwords from Windows Credential Manager, your settings and ${APP_NAME}'s logs. Other users' data stays."
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
