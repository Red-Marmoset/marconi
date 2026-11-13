; Copyright (c) 2026 Red Marmoset Studios Ltd.
; The installer's "Connect Marconi to your AI tools" page, and removing what it added on uninstall. Included by
; Marconi.nsi. It finds the tools with the engine's DetectApps.nsh (Packaging\Include, which PackageProject.ps1
; adds to makensis's include folders) and registers with MarconiMcp.exe, which uses each tool's own commands.
;
; Registering runs as the signed-in user: installing just for them, Setup isn't elevated and waits for the answer;
; installing for everyone, Setup is elevated, so MarconiMcp is started as the signed-in user through
; ShellExecAsUser, which can't wait, and shows its own message if it fails (--show-errors). When Setup runs as a
; different account from the signed-in one (an administrator's password typed at the prompt), the page is skipped:
; those tools would be the other account's. Marconi's AI tools window does the same any time later.
;
; A silent install registers with the tools named by /AITOOLS=claude-code,codex,claude-desktop (or all).

!include "DetectApps.nsh"
!insertmacro DETECTAPP_FUNCTION_claude_code ""
!insertmacro DETECTAPP_FUNCTION_codex ""
!insertmacro DETECTAPP_FUNCTION_claude_desktop ""

!define MCP_EXE "MarconiMcp.exe"
!define MCP_EXTENSION "marconi.mcpb"

Var AiClaudeCode    ; the page's tick boxes, or 0 when that tool wasn't found
Var AiCodex
Var AiClaudeDesktop

; Registers with one tool. Stack in: the tool's id (claude-code, codex, claude-desktop).
Function AiRegister
    Exch $R0
    Push $R1
    Push $R2
    DetailPrint "Adding ${APP_NAME} to $R0"
    ${If} $AllUsers == 1
        ShellExecAsUser::ShellExecAsUser "" "$INSTDIR\${MCP_EXE}" "--register $R0 --show-errors"
    ${Else}
        nsExec::ExecToStack '"$INSTDIR\${MCP_EXE}" --register $R0 --quiet'
        Pop $R1 ; the exit code, or "error" if it couldn't start
        Pop $R2 ; what it printed
        ${If} $R1 != 0
            MessageBox MB_OK|MB_ICONEXCLAMATION "${APP_NAME} couldn't be added to $R0.$\r$\n$\r$\n$R2$\r$\nYou can try again later in ${APP_NAME}, under AI tools." /SD IDOK
        ${EndIf}
    ${EndIf}
    ; Remembered so the uninstaller can take it out again.
    ReadRegStr $R1 SHCTX "${UNINSTALL_KEY}" "AiTools"
    ${StrStr} $R2 " $R1 " " $R0 "
    ${If} $R2 == ""
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "AiTools" "$R1 $R0"
    ${EndIf}
    Pop $R2
    Pop $R1
    Pop $R0
FunctionEnd

Function AiToolsPageCreate
    ${If} $UserBlocked == 1
        Abort ; Setup is another account's: its AI tools aren't the signed-in user's
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\${MCP_EXE}"
        Abort
    ${EndIf}
    !insertmacro DetectApp claude_code $0
    !insertmacro DetectApp codex $1
    !insertmacro DetectApp claude_desktop $2
    ; Claude Desktop installs Marconi from its extension file, when this build has one.
    ${IfNot} ${FileExists} "$INSTDIR\${MCP_EXTENSION}"
        StrCpy $2 ""
    ${EndIf}
    ${If} $0 == ""
    ${AndIf} $1 == ""
    ${AndIf} $2 == ""
        Abort ; none of them is on this computer
    ${EndIf}

    !insertmacro MUI_HEADER_TEXT "Connect ${APP_NAME} to your AI tools" "Let AI tools on this computer use ${APP_NAME}."
    nsDialogs::Create 1018
    Pop $R0
    ${If} $R0 == error
        Abort
    ${EndIf}
    ${NSD_CreateLabel} 0 0 100% 48u "These AI tools are on this computer. The ones you tick can use ${APP_NAME} to connect to the computers you name, see their screens, and use their mouse, keyboard and clipboard. They still ask before each step unless you tell them not to.$\r$\n$\r$\nYou can change this later in ${APP_NAME}, under AI tools."
    Pop $R0
    StrCpy $AiClaudeCode 0
    StrCpy $AiCodex 0
    StrCpy $AiClaudeDesktop 0
    StrCpy $R1 56 ; where the next tick box goes, in dialog units
    ${If} $0 != ""
        ${NSD_CreateCheckbox} 8u $R1u 100% 12u "Claude Code"
        Pop $AiClaudeCode
        ${NSD_Check} $AiClaudeCode
        IntOp $R1 $R1 + 16
    ${EndIf}
    ${If} $1 != ""
        ${NSD_CreateCheckbox} 8u $R1u 100% 12u "Codex"
        Pop $AiCodex
        ${NSD_Check} $AiCodex
        IntOp $R1 $R1 + 16
    ${EndIf}
    ${If} $2 != ""
        ${NSD_CreateCheckbox} 8u $R1u 100% 12u "Claude Desktop (opens Claude to confirm)"
        Pop $AiClaudeDesktop
        ${NSD_Check} $AiClaudeDesktop
        IntOp $R1 $R1 + 16
    ${EndIf}
    nsDialogs::Show
FunctionEnd

Function AiToolsPageLeave
    ${If} $AiClaudeCode != 0
        ${NSD_GetState} $AiClaudeCode $0
        ${If} $0 == ${BST_CHECKED}
            Push "claude-code"
            Call AiRegister
        ${EndIf}
    ${EndIf}
    ${If} $AiCodex != 0
        ${NSD_GetState} $AiCodex $0
        ${If} $0 == ${BST_CHECKED}
            Push "codex"
            Call AiRegister
        ${EndIf}
    ${EndIf}
    ${If} $AiClaudeDesktop != 0
        ${NSD_GetState} $AiClaudeDesktop $0
        ${If} $0 == ${BST_CHECKED}
            Push "claude-desktop"
            Call AiRegister
        ${EndIf}
    ${EndIf}
FunctionEnd

; A silent install: the tools named by /AITOOLS=, each only if it's installed.
Function AiToolsSilent
    ${If} $UserBlocked == 1
        Return
    ${EndIf}
    ${GetParameters} $R0
    ClearErrors
    ${GetOptions} $R0 "/AITOOLS=" $R1
    ${If} ${Errors}
        Return
    ${EndIf}
    StrCpy $R1 ",$R1,"
    ${StrStr} $R2 $R1 ",all,"
    ${StrStr} $R3 $R1 ",claude-code,"
    ${If} $R2 != ""
    ${OrIf} $R3 != ""
        !insertmacro DetectApp claude_code $0
        ${If} $0 != ""
            Push "claude-code"
            Call AiRegister
        ${EndIf}
    ${EndIf}
    ${StrStr} $R3 $R1 ",codex,"
    ${If} $R2 != ""
    ${OrIf} $R3 != ""
        !insertmacro DetectApp codex $0
        ${If} $0 != ""
            Push "codex"
            Call AiRegister
        ${EndIf}
    ${EndIf}
    ${StrStr} $R3 $R1 ",claude-desktop,"
    ${If} $R2 != ""
    ${OrIf} $R3 != ""
        !insertmacro DetectApp claude_desktop $0
        ${If} $0 != ""
        ${AndIf} ${FileExists} "$INSTDIR\${MCP_EXTENSION}"
            Push "claude-desktop"
            Call AiRegister
        ${EndIf}
    ${EndIf}
FunctionEnd

; Uninstall: takes Marconi out of the tools Setup put it in, before MarconiMcp.exe is deleted. Runs as the account
; the uninstaller runs as (an install for everyone can only reach that account's tools). Claude Desktop's extension
; is removed in Claude Desktop itself.
Function un.AiToolsRemove
    ReadRegStr $R0 SHCTX "${UNINSTALL_KEY}" "AiTools"
    ${IfNot} ${FileExists} "$INSTDIR\${MCP_EXE}"
        Return
    ${EndIf}
    StrCpy $R0 " $R0 "
    ${UnStrStr} $R1 $R0 " claude-code "
    ${If} $R1 != ""
        DetailPrint "Removing ${APP_NAME} from Claude Code"
        nsExec::ExecToLog '"$INSTDIR\${MCP_EXE}" --unregister claude-code'
        Pop $R1
    ${EndIf}
    ${UnStrStr} $R1 $R0 " codex "
    ${If} $R1 != ""
        DetailPrint "Removing ${APP_NAME} from Codex"
        nsExec::ExecToLog '"$INSTDIR\${MCP_EXE}" --unregister codex'
        Pop $R1
    ${EndIf}
FunctionEnd
