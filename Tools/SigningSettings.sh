# Tools/SigningSettings.sh - Where the signing inputs come from, for Package.sh and Apps/Android/Package.sh to source.
#
# Project.json's "signing" section names the environment variable that holds each input (the certificate, the
# keystore, its passwords, ...); the Una engine's BuildTool reads the section and prints the names. Nothing here
# prints what a variable holds. The engine's Packaging/README.md describes the section.
#
# Expects ROOT (the Marconi checkout). find_buildtool sets BUILDTOOL, or leaves it empty when there is none.

find_buildtool() {
    local engine cfg
    engine="${UNA_ENGINE_PATH:-$ROOT/../engine}"
    BUILDTOOL=""
    for cfg in Release MinSizeRel Debug Bootstrap; do
        if [[ -x "$engine/Build/BuildTool/$cfg/BuildTool" ]]; then
            BUILDTOOL="$engine/Build/BuildTool/$cfg/BuildTool"
            return
        fi
    done
}

# signing_env <setting>: the name of the environment variable a setting is read from (android.keystore, ...).
signing_env() {
    "$BUILDTOOL" "$ROOT/Project.json" --signing-env "$1" || {
        echo "ERROR: $BUILDTOOL couldn't say which variable holds $1. Check Project.json's signing section, and that the engine is recent enough to have one (update it and bootstrap BuildTool again)." >&2
        return 1
    }
}

# signing_input <setting>: what a setting that isn't a secret is set to: the value Project.json fixes, else what
# its variable holds, else nothing. Passwords are never read this way; the tool that needs one reads its variable.
signing_input() {
    local value name
    value="$("$BUILDTOOL" "$ROOT/Project.json" --signing-value "$1")" || {
        echo "ERROR: $BUILDTOOL couldn't read $1 from Project.json's signing section (is the engine recent enough to have one?)." >&2
        return 1
    }
    if [[ -z "$value" ]]; then
        name="$(signing_env "$1")" || return 1
        value="${!name:-}"
    fi
    printf '%s' "$value"
}
