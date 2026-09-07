#!/usr/bin/env bash
# ==============================================================================
#  down: Automated Uninstaller
#  High-Performance Segmented Download Engine in C11
#  Crafted by Usama Imran Cheema (@ucmz851)
#
#  One-line removal:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/down"
AUTHOR="Usama Imran Cheema (@ucmz851)"

# ── Color Palette & Styling ───────────────────────────────────────────────────
if [ -t 1 ] || [ "${FORCE_COLOR:-0}" = "1" ]; then
    C_RESET="\033[0m"
    C_BOLD="\033[1m"
    C_DIM="\033[2m"
    C_CYAN="\033[38;2;56;189;248m"
    C_PURPLE="\033[38;2;168;85;247m"
    C_BLUE="\033[38;2;99;102;241m"
    C_GREEN="\033[38;2;52;211;153m"
    C_YELLOW="\033[38;2;251;191;36m"
    C_RED="\033[38;2;248;113;113m"
    C_MUTED="\033[38;2;148;163;184m"
else
    C_RESET=""
    C_BOLD=""
    C_DIM=""
    C_CYAN=""
    C_PURPLE=""
    C_BLUE=""
    C_GREEN=""
    C_YELLOW=""
    C_RED=""
    C_MUTED=""
fi

echo -e "${C_CYAN}"
cat << 'ASCII'
  ██████╗  ██████╗ ██╗    ██╗███╗   ██╗
  ██╔══██╗██╔═══██╗██║    ██║████╗  ██║
  ██║  ██║██║   ██║██║ █╗ ██║██╔██╗ ██║
  ██║  ██║██║   ██║██║███╗██║██║╚██╗██║
  ██████╔╝╚██████╔╝╚███╔███╔╝██║ ╚████║
  ╚═════╝  ╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═══╝
ASCII
echo -e "${C_BOLD}  🗑️  Down Automated Uninstaller${C_RESET}"
echo -e "  ${C_MUTED}Crafted by ${C_RESET}${C_PURPLE}${C_BOLD}${AUTHOR}${C_RESET}"
echo -e "  ${C_BLUE}https://github.com/${REPO}${C_RESET}\n"

PURGE=0
for arg in "$@"; do
    if [ "$arg" = "--purge" ] || [ "$arg" = "-p" ]; then
        PURGE=1
    fi
done

# Search for candidate installation paths
CANDIDATES=(
    "$(command -v down 2>/dev/null || true)"
    "/usr/local/bin/down"
    "${HOME}/.local/bin/down"
    "/usr/bin/down"
    "$(command -v down-dev 2>/dev/null || true)"
    "${HOME}/.local/bin/down-dev"
    "$(command -v inlay 2>/dev/null || true)"
    "/usr/local/bin/inlay"
    "${HOME}/.local/bin/inlay"
    "/usr/bin/inlay"
)

REMOVED=0
SEEN_TARGETS=" "

echo -e "${C_BLUE}${C_BOLD}◆ Scanning system for Down installations...${C_RESET}"

for target in "${CANDIDATES[@]}"; do
    [ -z "$target" ] && continue
    case "$SEEN_TARGETS" in
        *" ${target} "*) continue ;;
    esac
    SEEN_TARGETS="${SEEN_TARGETS}${target} "

    if [ -f "$target" ] || [ -L "$target" ]; then
        echo -e "  ${C_MUTED}Found binary at:${C_RESET} ${C_BOLD}${target}${C_RESET}"
        if [ -w "$target" ] || [ -w "$(dirname "$target")" ]; then
            rm -f "$target"
            echo -e "  ${C_GREEN}✔ Successfully removed ${target}${C_RESET}"
            REMOVED=$((REMOVED + 1))
        elif command -v sudo >/dev/null 2>&1; then
            echo -e "  ${C_YELLOW}Privileged access needed. Requesting sudo...${C_RESET}"
            sudo rm -f "$target"
            echo -e "  ${C_GREEN}✔ Successfully removed ${target} (via sudo)${C_RESET}"
            REMOVED=$((REMOVED + 1))
        else
            echo -e "  ${C_RED}✖ Permission denied for ${target} (sudo not available)${C_RESET}"
        fi
    fi
done

STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/down"
CONFIG_DIR="${XDG_CONFIG_HOME:-${HOME}/.config}/down"
MAC_APP_DIR="${HOME}/Library/Application Support/down"
if [ "$PURGE" -eq 1 ]; then
    if [ -d "$STATE_DIR" ]; then
        rm -rf "$STATE_DIR"
        echo -e "  ${C_GREEN}✔ Purged history and session state at ${STATE_DIR}${C_RESET}"
    fi
    if [ -d "$MAC_APP_DIR" ]; then
        rm -rf "$MAC_APP_DIR"
        echo -e "  ${C_GREEN}✔ Purged Application Support data at ${MAC_APP_DIR}${C_RESET}"
    fi
    if [ -d "$CONFIG_DIR" ]; then
        rm -rf "$CONFIG_DIR"
        echo -e "  ${C_GREEN}✔ Purged configuration directory at ${CONFIG_DIR}${C_RESET}"
    fi
    [ -f "${HOME}/.downrc" ] && rm -f "${HOME}/.downrc" && echo -e "  ${C_GREEN}✔ Removed ${HOME}/.downrc${C_RESET}"
elif [ -d "$STATE_DIR" ] || [ -d "$MAC_APP_DIR" ]; then
    echo -e "  ${C_MUTED}Note: Download history preserved. (Pass --purge to remove)${C_RESET}"
fi

if [ "$REMOVED" -gt 0 ]; then
    echo -e "\n${C_GREEN}${C_BOLD}✔ Down has been completely removed from your system.${C_RESET}\n"
else
    echo -e "\n${C_YELLOW}• No active Down installations were found on your system.${C_RESET}\n"
fi
