#!/usr/bin/env bash
# ==============================================================================
#  inlay: Automated Uninstaller
#  High-Performance Segmented Download Engine in C11
#  Crafted by Usama Imran Cheema (@ucmz851)
#
#  One-line removal:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/uninstall.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/inlay"
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
  ██╗███╗   ██╗██╗      █████╗ ██╗   ██╗
  ██║████╗  ██║██║     ██╔══██╗╚██╗ ██╔╝
  ██║██╔██╗ ██║██║     ███████║ ╚████╔╝ 
  ██║██║╚██╗██║██║     ██╔══██║  ╚██╔╝  
  ██║██║ ╚████║███████╗██║  ██║   ██║   
  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝   ╚═╝   
ASCII
echo -e "${C_BOLD}  🗑️  Inlay Automated Uninstaller${C_RESET}"
echo -e "  ${C_MUTED}Crafted by ${C_RESET}${C_PURPLE}${C_BOLD}${AUTHOR}${C_RESET}"
echo -e "  ${C_BLUE}https://github.com/${REPO}${C_RESET}\n"

# Search for candidate installation paths
CANDIDATES=(
    "$(command -v inlay 2>/dev/null || true)"
    "/usr/local/bin/inlay"
    "${HOME}/.local/bin/inlay"
    "/usr/bin/inlay"
)

REMOVED=0
declare -A SEEN

echo -e "${C_BLUE}${C_BOLD}◆ Scanning system for Inlay installations...${C_RESET}"

for target in "${CANDIDATES[@]}"; do
    [ -z "$target" ] && continue
    [ -n "${SEEN[$target]:-}" ] && continue
    SEEN["$target"]=1

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

if [ "$REMOVED" -gt 0 ]; then
    echo -e "\n${C_GREEN}${C_BOLD}✔ Inlay has been completely removed from your system.${C_RESET}\n"
else
    echo -e "\n${C_YELLOW}• No active Inlay installations were found on your system.${C_RESET}\n"
fi
