#!/usr/bin/env bash
# ==============================================================================
#  inlay: Automated Uninstaller
#  One-line removal:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/uninstall.sh | bash
# ==============================================================================

set -euo pipefail

# ANSI Colors
BOLD="\033[1m"
GREEN="\033[1;32m"
CYAN="\033[1;36m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
DIM="\033[38;5;244m"
RESET="\033[0m"

echo -e "${CYAN}"
cat << 'ASCII'
  ___       __             
 |_ _| _ _  | | __ _  _  _ 
  | | | ' \ | |/ _` || || |
 |___||_||_||_|\__,_| \_, |
                      |__/ 
ASCII
echo -e "${BOLD}Inlay Uninstaller${RESET}\n"

# Search for candidate installation paths
CANDIDATES=(
    "$(command -v inlay 2>/dev/null || true)"
    "/usr/local/bin/inlay"
    "${HOME}/.local/bin/inlay"
    "/usr/bin/inlay"
)

REMOVED=0
declare -A SEEN

for target in "${CANDIDATES[@]}"; do
    [ -z "$target" ] && continue
    [ -n "${SEEN[$target]:-}" ] && continue
    SEEN["$target"]=1

    if [ -f "$target" ] || [ -L "$target" ]; then
        echo -e "[*] Found Inlay at: ${BOLD}${target}${RESET}"
        if [ -w "$target" ] || [ -w "$(dirname "$target")" ]; then
            rm -f "$target"
            echo -e "${GREEN}[✓] Removed ${target}${RESET}"
            REMOVED=$((REMOVED + 1))
        elif command -v sudo >/dev/null 2>&1; then
            echo -e "[*] Root privileges required to remove ${target}..."
            sudo rm -f "$target"
            echo -e "${GREEN}[✓] Removed ${target} (via sudo)${RESET}"
            REMOVED=$((REMOVED + 1))
        else
            echo -e "${RED}[!] Cannot remove ${target}: permission denied (sudo not available).${RESET}"
        fi
    fi
done

if [ "$REMOVED" -gt 0 ]; then
    echo -e "\n${GREEN}${BOLD}[✓] Inlay has been completely removed from your system.${RESET}\n"
else
    echo -e "${YELLOW}[*] No active Inlay installations were found on your system.${RESET}\n"
fi
