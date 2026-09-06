#!/usr/bin/env bash
# ==============================================================================
#  inlay: Automated Installer & Updater
#  High-Performance Segmented Download Engine in C11
#  Crafted by Usama Imran Cheema (@ucmz851)
#
#  One-line installation / update:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/install.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/inlay"
AUTHOR="Usama Imran Cheema (@ucmz851)"
DEFAULT_INSTALL_DIR="/usr/local/bin"
FALLBACK_INSTALL_DIR="${HOME}/.local/bin"

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

print_banner() {
    echo -e "${C_CYAN}"
    cat << 'ASCII'
  ██╗███╗   ██╗██╗      █████╗ ██╗   ██╗
  ██║████╗  ██║██║     ██╔══██╗╚██╗ ██╔╝
  ██║██╔██╗ ██║██║     ███████║ ╚████╔╝ 
  ██║██║╚██╗██║██║     ██╔══██║  ╚██╔╝  
  ██║██║ ╚████║███████╗██║  ██║   ██║   
  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝   ╚═╝   
ASCII
    echo -e "${C_BOLD}  ⚡ High-Performance Positional I/O Segmented Engine${C_RESET}"
    echo -e "  ${C_MUTED}Crafted by ${C_RESET}${C_PURPLE}${C_BOLD}${AUTHOR}${C_RESET}"
    echo -e "  ${C_BLUE}https://github.com/${REPO}${C_RESET}\n"
}

# ── Handle Uninstallation Flag ───────────────────────────────────────────────
if [ "${1:-}" = "--uninstall" ] || [ "${1:-}" = "uninstall" ]; then
    print_banner
    echo -e "${C_BOLD}🗑️  Uninstalling Inlay...${C_RESET}\n"
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
                echo -e "  ${C_RED}✖ Permission denied for ${target}${C_RESET}"
            fi
        fi
    done
    if [ "$REMOVED" -gt 0 ]; then
        echo -e "\n${C_GREEN}${C_BOLD}✔ Inlay has been completely removed from your system.${C_RESET}\n"
    else
        echo -e "\n${C_YELLOW}• No active Inlay installations were found on your system.${C_RESET}\n"
    fi
    exit 0
fi

print_banner

# ── Step 1: Detect Platform & Target Directory ───────────────────────────────
echo -e "${C_BLUE}${C_BOLD}◆ [1/4]${C_RESET} ${C_BOLD}Detecting system environment...${C_RESET}"

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$ARCH" in
    x86_64|amd64)
        NORM_ARCH="amd64"
        ;;
    aarch64|arm64)
        NORM_ARCH="arm64"
        ;;
    *)
        NORM_ARCH="$ARCH"
        ;;
esac

echo -e "  ${C_MUTED}OS:${C_RESET} ${OS}  ${C_MUTED}Architecture:${C_RESET} ${NORM_ARCH} (${ARCH})"

# Detect existing installation location
EXISTING_INLAY="$(command -v inlay 2>/dev/null || true)"
IS_UPGRADE=0
USE_SUDO=""

if [ -n "$EXISTING_INLAY" ] && [ -x "$EXISTING_INLAY" ]; then
    CURRENT_VER="$("$EXISTING_INLAY" --version 2>/dev/null | head -n1 | awk '{print $2}' || echo "unknown")"
    TARGET_DIR="$(dirname "$EXISTING_INLAY")"
    IS_UPGRADE=1
    echo -e "  ${C_MUTED}Existing installation:${C_RESET} ${C_YELLOW}v${CURRENT_VER}${C_RESET} at ${TARGET_DIR}/inlay"
else
    if [ -n "${INSTALL_DIR:-}" ]; then
        TARGET_DIR="$INSTALL_DIR"
        mkdir -p "$TARGET_DIR"
    elif [ -w "$DEFAULT_INSTALL_DIR" ] || [ "$(id -u)" -eq 0 ]; then
        TARGET_DIR="$DEFAULT_INSTALL_DIR"
    elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        TARGET_DIR="$DEFAULT_INSTALL_DIR"
        USE_SUDO="sudo"
    else
        TARGET_DIR="$FALLBACK_INSTALL_DIR"
        mkdir -p "$TARGET_DIR"
    fi
fi

if [ ! -w "$TARGET_DIR" ]; then
    if command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
        USE_SUDO="sudo"
    else
        TARGET_DIR="$FALLBACK_INSTALL_DIR"
        mkdir -p "$TARGET_DIR"
    fi
fi

echo -e "  ${C_MUTED}Target location:${C_RESET} ${C_BOLD}${TARGET_DIR}/inlay${C_RESET}"

# ── Step 2: Resolve Latest Release ───────────────────────────────────────────
echo -e "\n${C_BLUE}${C_BOLD}◆ [2/4]${C_RESET} ${C_BOLD}Resolving latest release from GitHub...${C_RESET}"

LATEST_TAG="$(curl -sIL -o /dev/null -w "%{url_effective}\n" "https://github.com/${REPO}/releases/latest" 2>/dev/null | awk -F'/' '{print $NF}' || true)"
if [ -z "$LATEST_TAG" ] || [ "$LATEST_TAG" = "latest" ]; then
    LATEST_TAG="$(curl -fsSL --connect-timeout 5 "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null | grep -o '"tag_name": *"[^"]*"' | head -n1 | cut -d '"' -f 4 || true)"
fi

if [ -n "$LATEST_TAG" ]; then
    VERSION="${LATEST_TAG#v}"
else
    VERSION="0.0.1"
fi

echo -e "  ${C_MUTED}Release:${C_RESET} ${C_GREEN}${C_BOLD}v${VERSION}${C_RESET} (latest release)"

# ── Step 3: Fetch Binary & Verify Integrity ──────────────────────────────────
echo -e "\n${C_BLUE}${C_BOLD}◆ [3/4]${C_RESET} ${C_BOLD}Fetching release package & verifying integrity...${C_RESET}"

TMP_DIR="$(mktemp -d /tmp/inlay_install_XXXXXX)"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

INSTALLED=0
TARBALL="inlay-v${VERSION}-linux-${NORM_ARCH}.tar.gz"
RELEASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${TARBALL}"
CHECKSUM_URL="${RELEASE_URL}.sha256"

if [ "$OS" = "linux" ] && [ "$NORM_ARCH" = "amd64" ]; then
    echo -e "  ${C_MUTED}Downloading:${C_RESET} ${TARBALL}"
    if curl -fsSL -o "${TMP_DIR}/${TARBALL}" "$RELEASE_URL" 2>/dev/null; then
        # Check for sha256
        if curl -fsSL -o "${TMP_DIR}/${TARBALL}.sha256" "$CHECKSUM_URL" 2>/dev/null; then
            EXPECTED_HASH="$(awk '{print $1}' "${TMP_DIR}/${TARBALL}.sha256" | head -n1)"
            ACTUAL_HASH="$(sha256sum "${TMP_DIR}/${TARBALL}" | awk '{print $1}')"
            if [ "$EXPECTED_HASH" = "$ACTUAL_HASH" ]; then
                echo -e "  ${C_GREEN}✔ SHA-256 integrity verified:${C_RESET} ${C_DIM}${ACTUAL_HASH:0:16}...${C_RESET}"
            else
                echo -e "  ${C_YELLOW}⚠ Checksum mismatch, falling back to source build.${C_RESET}"
            fi
        fi

        tar -xzf "${TMP_DIR}/${TARBALL}" -C "${TMP_DIR}"
        if [ -f "${TMP_DIR}/inlay" ]; then
            INSTALLED=1
        fi
    fi
fi

# Fallback: Compile from Source
if [ "$INSTALLED" -eq 0 ]; then
    echo -e "  ${C_YELLOW}• Pre-built binary unavailable for ${OS}/${ARCH}. Building from source...${C_RESET}"
    for tool in gcc make curl pkg-config; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo -e "  ${C_RED}✖ Error: '$tool' is required to compile Inlay from source.${C_RESET}"
            echo -e "    Install with: sudo apt install build-essential libcurl4-openssl-dev libssl-dev"
            exit 1
        fi
    done

    mkdir -p "${TMP_DIR}/source"
    echo -e "  ${C_MUTED}Fetching source tree...${C_RESET}"
    curl -fsSL "https://github.com/${REPO}/archive/refs/heads/main.tar.gz" | tar -xz --strip-components=1 -C "${TMP_DIR}/source"

    echo -e "  ${C_MUTED}Compiling optimized C11 binaries...${C_RESET}"
    make -C "${TMP_DIR}/source" -j"$(nproc 2>/dev/null || echo 2)" >/dev/null
    cp "${TMP_DIR}/source/inlay" "${TMP_DIR}/inlay"
    INSTALLED=1
fi

# ── Step 4: Atomic Installation ──────────────────────────────────────────────
echo -e "\n${C_BLUE}${C_BOLD}◆ [4/4]${C_RESET} ${C_BOLD}Deploying binary...${C_RESET}"

if [ -n "${USE_SUDO:-}" ]; then
    $USE_SUDO install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
else
    install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
fi

# ── Summary Box ──────────────────────────────────────────────────────────────
render_box_line() {
    local text="$1"
    local raw_len="$2"
    local pad=$(( 59 - raw_len ))
    local spaces=""
    if [ "$pad" -gt 0 ]; then
        spaces="$(printf "%*s" "$pad" "")"
    fi
    echo -e "${C_CYAN}  │${C_RESET}  ${text}${spaces}${C_CYAN}│${C_RESET}"
}

echo -e "\n${C_CYAN}  ╭─────────────────────────────────────────────────────────────╮${C_RESET}"
if [ "$IS_UPGRADE" -eq 1 ]; then
    MSG="✔ Inlay successfully upgraded (v${VERSION})!"
else
    MSG="✔ Inlay successfully installed (v${VERSION})!"
fi
render_box_line "${C_GREEN}${C_BOLD}${MSG}${C_RESET}" "${#MSG}"
echo -e "${C_CYAN}  ├─────────────────────────────────────────────────────────────┤${C_RESET}"

L1="Location     : ${TARGET_DIR}/inlay"
render_box_line "${C_MUTED}Location     ${C_RESET}: ${C_BOLD}${TARGET_DIR}/inlay${C_RESET}" "${#L1}"

L2="Version      : v${VERSION} (latest release)"
render_box_line "${C_MUTED}Version      ${C_RESET}: v${VERSION} (latest release)" "${#L2}"

L3="Crafted By   : ${AUTHOR}"
render_box_line "${C_MUTED}Crafted By   ${C_RESET}: ${C_PURPLE}${C_BOLD}${AUTHOR}${C_RESET}" "${#L3}"

L4="Platform     : ${OS}/${NORM_ARCH}"
render_box_line "${C_MUTED}Platform     ${C_RESET}: ${OS}/${NORM_ARCH}" "${#L4}"

L5="Engine       : C11 Lockless Positional I/O (Zero-Copy)"
render_box_line "${C_MUTED}Engine       ${C_RESET}: C11 Lockless Positional I/O (Zero-Copy)" "${#L5}"

L6="Protocols    : HTTP/1.1, HTTP/2, HTTP/3 (QUIC), S3 SigV4"
render_box_line "${C_MUTED}Protocols    ${C_RESET}: HTTP/1.1, HTTP/2, HTTP/3 (QUIC), S3 SigV4" "${#L6}"

echo -e "${C_CYAN}  ╰─────────────────────────────────────────────────────────────╯${C_RESET}\n"

# Verify PATH
if ! echo "$PATH" | tr ':' '\n' | grep -qx "$TARGET_DIR"; then
    echo -e "  ${C_YELLOW}⚠ Notice:${C_RESET} '${TARGET_DIR}' is not currently in your PATH."
    echo -e "  Add it to your shell profile (~/.bashrc or ~/.zshrc):"
    echo -e "  ${C_BOLD}export PATH=\"${TARGET_DIR}:\$PATH\"${C_RESET}\n"
fi

echo -e "  ${C_BOLD}✦ Quick Start:${C_RESET}"
echo -e "    inlay https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso"
echo -e "\n  ${C_BOLD}✦ Useful Commands:${C_RESET}"
echo -e "    ${C_MUTED}inlay --help${C_RESET}          Full documentation of flags & protocols"
echo -e "    ${C_MUTED}inlay --check-update${C_RESET}  Check for new releases on GitHub"
echo -e "    ${C_MUTED}inlay --update${C_RESET}        Self-update in-place to latest version\n"
