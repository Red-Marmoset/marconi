; Copyright (c) 2026 Red Marmoset Studios Ltd.
; Marconi's additions to the Una engine's standard Windows Setup (the engine's Packaging\Installer.nsi), named by
; Project.json's package.installerInclude. Project.json's package.windows turns on the engine's own options: the "Just
; me" or "Everyone on this PC" choice, closing a running Marconi, the licence ticked, the vnc:// link, the source code
; link, an optional Start menu shortcut and an unticked desktop one. This file adds what is Marconi's alone:
;   - the AI tools component (MarconiMcp.exe, Apps.json, marconi.mcpb) and the page connecting it to the AI tools on
;     this computer (AiTools.nsh); /AITOOLS= does it in a silent install
;   - the Marconi Server installer as a component, and "Install Marconi Server now" on the finish page
;   - removing a person's saved connections, passwords and logs on uninstall (Marconi.exe --remove-my-data)
;   - Marconi's wording
; It is included near the top of Installer.nsi, so its sections, pages and functions are macros the engine inserts
; at their places (the list is at the top of Installer.nsi). makensis runs in the engine's Packaging folder: paths
; here are from this file's own folder (MARCONI_INSTALLER_DIR).

!define MARCONI_INSTALLER_DIR "${__FILEDIR__}"

!define WELCOME_TEXT "This will install ${APP_NAME}, the remote desktop client from ${PUBLISHER}, on your computer.$\r$\n$\r$\nIf ${APP_NAME} is running, Setup closes it and starts it again afterwards. AI tools using ${APP_NAME} lose it while Setup runs; Claude Code reconnects by itself, and other AI tools may need restarting. Your saved connections are kept when upgrading.$\r$\n$\r$\nClick Next to continue."
!define FINISH_TEXT "${APP_NAME} ${VERSION} has been installed on your computer.$\r$\n$\r$\nClick Finish to close Setup."
!define UNWELCOME_TEXT "This will remove ${APP_NAME} from your computer.$\r$\n$\r$\nYour saved connections, stored passwords and logs are kept unless you choose to remove them too, two pages on.$\r$\n$\r$\nIf ${APP_NAME} is running, it is closed first. Click Next to continue."
!define BRANDING_TEXT "${APP_NAME} ${VERSION}  |  ${PUBLISHER}"
!define MODE_PAGE_NOTE "Either way, each person's saved connections and passwords stay their own."
!define APP_DESCRIPTION "The ${APP_NAME} VNC client."
!define START_MENU_DESCRIPTION "Add ${APP_NAME}, and a link to its source code on GitHub, to the Start menu."

!include "StrFunc.nsh"
${StrStr}
${UnStrStr}

; The Marconi Server installer (Installer\MarconiServer.nsi, built first by Package.ps1) travels inside this one, so the
; client can put it on another computer's desktop (F8 > Install Marconi Server on the Other Computer) or run it here
; (Settings > Share this computer). It is installed beside Marconi.exe, where the client looks for it.
!define SERVER_SETUP "MarconiServer-${VERSION}-win64-setup.exe"
!define SERVER_SETUP_SOURCE "${MARCONI_INSTALLER_DIR}\..\Build\Installer\${SERVER_SETUP}"
!if /FileExists "${SERVER_SETUP_SOURCE}"
    !define HAVE_SERVER_SETUP
    ; A third checkbox on the finish page, "Install Marconi Server now", which MUI doesn't offer: FinishShow adds it
    ; below the other two and FinishLeave runs the server's installer.
    !define FINISH_SHOW_FUNCTION FinishShow
    !define FINISH_LEAVE_FUNCTION FinishLeave
    Var InstallServer ; the finish page's "Install Marconi Server now" checkbox, or "" when it isn't shown
!else
    !echo "No ${SERVER_SETUP_SOURCE}: this installer is built without the Marconi Server component. Package.ps1 builds it first."
!endif

; Ends any MarconiMcp.exe running from $INSTDIR, which AI tools such as Claude Code start in the background and keep
; running; it holds the folder's DLLs open, so they couldn't be replaced or removed. Only copies from this folder are
; ended, and the AI tools themselves stay open (Claude Code was seen starting a new copy by itself; others not checked).
; MarconiMcp has no window to close politely, so it is ended outright; it keeps nothing that would be lost. PowerShell
; comes with Windows; tasklist and taskkill can't filter by folder. Setup is a 32-bit program, and the 32-bit PowerShell
; it would start sees no path for a 64-bit process, so nothing matched: Sysnative is the 64-bit System32 as a 32-bit
; program sees it (Setup only runs on 64-bit Windows). Called again just before MarconiMcp.exe is replaced, since an AI
; tool may start a new copy in between.
!macro CloseRunningMcpFunction PREFIX
Function ${PREFIX}CloseRunningMcp
    nsExec::ExecToStack `"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "@(Get-Process MarconiMcp -ErrorAction SilentlyContinue | Where-Object { $$_.Path -and $$_.Path.StartsWith('$INSTDIR\', 'OrdinalIgnoreCase') } | Stop-Process -Force -PassThru | ForEach-Object { $$_.WaitForExit(10000) | Out-Null; 1 }).Count"`
    Pop $0
    Pop $1
    ${If} $0 == 0
    ${AndIf} $1 > 0
        DetailPrint "Closed the AI tools helper (MarconiMcp); Claude Code reconnects by itself, other AI tools may need restarting"
    ${EndIf}
FunctionEnd
!macroend
!insertmacro CloseRunningMcpFunction ""
!insertmacro CloseRunningMcpFunction "un."

; In the main section, before the previous version is removed and the files copied
!macro ExtraInstallBefore
    Call CloseRunningMcp
!macroend

; After the Start menu and desktop shortcut sections
!macro ExtraSections
    !ifdef HAVE_SERVER_SETUP
    Section "Marconi Server installer" SecServerSetup
        ; Only the installer, to set up other computers (or this one); it installs nothing until it is run.
        Delete "$INSTDIR\MarconiServer-*-setup.exe"
        SetOutPath "$INSTDIR"
        File "${SERVER_SETUP_SOURCE}"
    SectionEnd
    !endif

    Section "AI tools (MarconiMcp)" SecAiTools
        ; The program AI tools such as Claude Code start to use other computers through Marconi, the list of AI tools
        ; it looks for, and the Claude Desktop extension. The page after installing connects it to the tools it finds.
        ; The main component leaves these out (package.windows.componentFiles).
        SetOutPath "$INSTDIR"
        Call CloseRunningMcp
        File "${STAGE_DIR}\App\MarconiMcp.exe"
        File "${STAGE_DIR}\App\Apps.json"
        File "${STAGE_DIR}\App\marconi.mcpb"
    SectionEnd

    ; Unticked on an upgrade: an earlier install's MarconiMcp goes, after taking itself out of the AI tools it was in.
    Section "-AiToolsUnticked"
        ${IfNot} ${SectionIsSelected} ${SecAiTools}
            Call AiToolsRemoveUnticked
            Call CloseRunningMcp
            Delete "$INSTDIR\MarconiMcp.exe"
            Delete "$INSTDIR\Apps.json"
            Delete "$INSTDIR\marconi.mcpb"
        ${EndIf}
    SectionEnd
!macroend

; What needs the sections' numbers
!macro ExtraAfterSections
    ; The "Connect Marconi to your AI tools" page and its uninstall side.
    !include "${MARCONI_INSTALLER_DIR}\AiTools.nsh"
    ; For the uninstaller's saved-data section below (the engine inserts the installer's).
    !insertmacro GetConsoleUserFunction "un."

    !ifdef HAVE_SERVER_SETUP
    ; Adds "Install Marconi Server now", unticked, under MUI's two checkboxes (at 90u and 110u; see MUI2's
    ; Finish.nsh), when the Marconi Server installer was chosen.
    Function FinishShow
        StrCpy $InstallServer ""
        ${IfNot} ${SectionIsSelected} ${SecServerSetup}
            Return
        ${EndIf}
        ${NSD_CreateCheckbox} 120u 130u 195u 10u "Install Marconi Server now"
        Pop $InstallServer
        SetCtlColors $InstallServer "${MUI_TEXTCOLOR}" "${MUI_BGCOLOR}"
        ; As MUI does for its own checkboxes: in high contrast, SetCtlColors doesn't change a themed checkbox's text.
        ${If} ${IsHighContrastModeActive}
            System::Call 'UXTHEME::SetWindowTheme(p$InstallServer,w" ",w" ")'
        ${EndIf}
    FunctionEnd

    ; Starts the Marconi Server installer. It asks for administrator permission itself (it installs for the whole
    ; computer), so it is started directly rather than as the signed-in user. MUI starts Marconi straight after.
    Function FinishLeave
        ${If} $InstallServer == ""
            Return
        ${EndIf}
        ${NSD_GetState} $InstallServer $0
        ${If} $0 == ${BST_CHECKED}
        ${AndIf} ${FileExists} "$INSTDIR\${SERVER_SETUP}"
            ClearErrors
            ExecShell "open" "$INSTDIR\${SERVER_SETUP}"
            ${If} ${Errors}
                MessageBox MB_OK|MB_ICONEXCLAMATION "The Marconi Server installer didn't start. You can run it later from $INSTDIR."
            ${EndIf}
        ${EndIf}
    FunctionEnd
    !endif
!macroend

; Between installing and the finish page
!macro ExtraPagesAfterInstall
    Page custom AiToolsPageCreate AiToolsPageLeave
!macroend

; A silent install has no AI tools page: /AITOOLS= names the tools
!macro ExtraInstSuccess
    ${If} ${Silent}
        Call AiToolsSilent
    ${EndIf}
!macroend

; Saved connections (registry), keychain passwords and logs belong to each user and are kept unless asked for, so
; reinstalling or upgrading doesn't lose them. This section comes before the program's files are removed, because it
; needs Marconi.exe. It is unticked by default and never runs in the silent uninstall an upgrade does.
!macro ExtraUnSectionsFirst
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
!macroend

; In the uninstall section, before the files are removed
!macro ExtraUninstall
    Call un.CloseRunningMcp
    ; Out of the AI tools first: it needs MarconiMcp.exe, which the files include.
    Call un.AiToolsRemove
    Delete "$INSTDIR\MarconiServer-*-setup.exe"
!macroend

!macro ExtraDescriptions
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAiTools} "Lets AI tools such as Claude Code, Codex and Claude Desktop use other computers through ${APP_NAME}. The next page asks which of them to connect."
    !ifdef HAVE_SERVER_SETUP
        !insertmacro MUI_DESCRIPTION_TEXT ${SecServerSetup} "The Marconi Server installer, for setting up other computers so you can control them with ${APP_NAME}. It installs nothing until you use it."
    !endif
!macroend

!macro ExtraUnDescriptions
    !insertmacro MUI_DESCRIPTION_TEXT ${SecUnData} "Also remove your saved connections, their passwords from Windows Credential Manager, your settings and ${APP_NAME}'s logs. Other users' data stays."
!macroend
