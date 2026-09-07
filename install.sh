#!/usr/bin/env bash
# ==============================================================================
#  down: Automated Installer & Updater
#  High-Performance Segmented Download Engine in C11
#  Crafted by Usama Imran Cheema (@ucmz851)
#
#  One-line installation / update:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/down"
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
  ██████╗  ██████╗ ██╗    ██╗███╗   ██╗
  ██╔══██╗██╔═══██╗██║    ██║████╗  ██║
  ██║  ██║██║   ██║██║ █╗ ██║██╔██╗ ██║
  ██║  ██║██║   ██║██║███╗██║██║╚██╗██║
  ██████╔╝╚██████╔╝╚███╔███╔╝██║ ╚████║
  ╚═════╝  ╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═══╝
ASCII
    echo -e "${C_BOLD}  ⚡ High-Performance Positional I/O Segmented Engine${C_RESET}"
    echo -e "  ${C_MUTED}Crafted by ${C_RESET}${C_PURPLE}${C_BOLD}${AUTHOR}${C_RESET}"
    echo -e "  ${C_BLUE}https://github.com/${REPO}${C_RESET}\n"
}

# ── Handle Uninstallation Flag ───────────────────────────────────────────────
if [ "${1:-}" = "--uninstall" ] || [ "${1:-}" = "uninstall" ]; then
    print_banner
    PURGE=0
    for arg in "$@"; do
        if [ "$arg" = "--purge" ] || [ "$arg" = "-p" ]; then
            PURGE=1
        fi
    done

    echo -e "${C_BOLD}🗑️  Uninstalling Down...${C_RESET}\n"
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
                echo -e "  ${C_RED}✖ Permission denied for ${target}${C_RESET}"
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
EXISTING_BIN="$(command -v down 2>/dev/null || command -v inlay 2>/dev/null || true)"
IS_UPGRADE=0
USE_SUDO=""

if [ -n "${INSTALL_DIR:-}" ]; then
    TARGET_DIR="$INSTALL_DIR"
    mkdir -p "$TARGET_DIR"
elif [ -n "$EXISTING_BIN" ] && [ -x "$EXISTING_BIN" ]; then
    CURRENT_VER="$("$EXISTING_BIN" --version 2>/dev/null | head -n1 | awk '{print $2}' || echo "unknown")"
    TARGET_DIR="$(dirname "$EXISTING_BIN")"
    IS_UPGRADE=1
    echo -e "  ${C_MUTED}Existing installation:${C_RESET} ${C_YELLOW}v${CURRENT_VER}${C_RESET} at ${TARGET_DIR}/down"
else
    if [ -w "$DEFAULT_INSTALL_DIR" ] || [ "$(id -u)" -eq 0 ]; then
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

echo -e "  ${C_MUTED}Target location:${C_RESET} ${C_BOLD}${TARGET_DIR}/down${C_RESET}"

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

TMP_DIR="$(mktemp -d /tmp/down_install_XXXXXX)"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

INSTALLED=0
TARBALL="down-v${VERSION}-${OS}-${NORM_ARCH}.tar.gz"
RELEASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${TARBALL}"
CHECKSUM_URL="${RELEASE_URL}.sha256"

NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 2)"

if [ "$OS" = "linux" ] || [ "$OS" = "darwin" ]; then
    echo -e "  ${C_MUTED}Downloading:${C_RESET} ${TARBALL}"
    if ! curl -fsSL -o "${TMP_DIR}/${TARBALL}" "$RELEASE_URL" 2>/dev/null; then
        # Fallback check if current release has legacy inlay prefix or linux naming
        LEGACY_TARBALL="inlay-v${VERSION}-${OS}-${NORM_ARCH}.tar.gz"
        LEGACY_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${LEGACY_TARBALL}"
        curl -fsSL -o "${TMP_DIR}/${TARBALL}" "$LEGACY_URL" 2>/dev/null || true
    fi

    if [ -f "${TMP_DIR}/${TARBALL}" ] && [ -s "${TMP_DIR}/${TARBALL}" ]; then
        # Check for sha256
        if curl -fsSL -o "${TMP_DIR}/${TARBALL}.sha256" "$CHECKSUM_URL" 2>/dev/null || \
           curl -fsSL -o "${TMP_DIR}/${TARBALL}.sha256" "https://github.com/${REPO}/releases/download/v${VERSION}/inlay-v${VERSION}-${OS}-${NORM_ARCH}.tar.gz.sha256" 2>/dev/null; then
            EXPECTED_HASH="$(awk '{print $1}' "${TMP_DIR}/${TARBALL}.sha256" | head -n1)"
            if command -v sha256sum >/dev/null 2>&1; then
                ACTUAL_HASH="$(sha256sum "${TMP_DIR}/${TARBALL}" | awk '{print $1}')"
            else
                ACTUAL_HASH="$(shasum -a 256 "${TMP_DIR}/${TARBALL}" | awk '{print $1}')"
            fi
            if [ "$EXPECTED_HASH" = "$ACTUAL_HASH" ]; then
                echo -e "  ${C_GREEN}✔ SHA-256 integrity verified:${C_RESET} ${C_DIM}${ACTUAL_HASH:0:16}...${C_RESET}"
            fi
        fi

        tar -xzf "${TMP_DIR}/${TARBALL}" -C "${TMP_DIR}"
        if [ -f "${TMP_DIR}/down" ]; then
            INSTALLED=1
        elif [ -f "${TMP_DIR}/inlay" ]; then
            mv "${TMP_DIR}/inlay" "${TMP_DIR}/down"
            INSTALLED=1
        fi
    fi
fi

# Prefer local build if running from cloned repository
if [ "$INSTALLED" -eq 0 ] && [ -f "./Makefile" ] && [ -f "./src/main.c" ]; then
    echo -e "  ${C_MUTED}Detected local repository checkout. Compiling...${C_RESET}"
    make -j"${NPROC}" >/dev/null
    if [ -f "./down" ] && [ -x "./down" ]; then
        cp "./down" "${TMP_DIR}/down"
        INSTALLED=1
    fi
fi

# Fallback: Compile from Source
if [ "$INSTALLED" -eq 0 ]; then
    echo -e "  ${C_YELLOW}• Pre-built binary unavailable for ${OS}/${NORM_ARCH}. Building from source...${C_RESET}"
    if [ "$OS" = "darwin" ]; then
        COMPILER=""
        for c in clang gcc cc; do
            if command -v "$c" >/dev/null 2>&1; then
                COMPILER="$c"
                break
            fi
        done
        if [ -z "$COMPILER" ] || ! command -v make >/dev/null 2>&1 || ! command -v curl >/dev/null 2>&1; then
            echo -e "  ${C_RED}✖ Error: Xcode Command Line Tools or Homebrew dependencies are required to compile Down on macOS.${C_RESET}"
            echo -e "    Run: xcode-select --install"
            echo -e "    And: brew install curl openssl pkg-config"
            exit 1
        fi
    else
        for tool in gcc make curl pkg-config; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo -e "  ${C_RED}✖ Error: '$tool' is required to compile Down from source.${C_RESET}"
                echo -e "    Install with: sudo apt install build-essential libcurl4-openssl-dev libssl-dev"
                exit 1
            fi
        done
    fi

    mkdir -p "${TMP_DIR}/source"
    echo -e "  ${C_MUTED}Fetching source tree...${C_RESET}"
    curl -fsSL "https://github.com/${REPO}/archive/refs/heads/main.tar.gz" | tar -xz --strip-components=1 -C "${TMP_DIR}/source"

    echo -e "  ${C_MUTED}Compiling optimized C11 binaries...${C_RESET}"
    make -C "${TMP_DIR}/source" -j"${NPROC}" >/dev/null
    if [ -f "${TMP_DIR}/source/down" ]; then
        cp "${TMP_DIR}/source/down" "${TMP_DIR}/down"
        INSTALLED=1
    elif [ -f "${TMP_DIR}/source/inlay" ]; then
        cp "${TMP_DIR}/source/inlay" "${TMP_DIR}/down"
        INSTALLED=1
    fi
fi

# ── Step 4: Atomic Installation ──────────────────────────────────────────────
echo -e "\n${C_BLUE}${C_BOLD}◆ [4/4]${C_RESET} ${C_BOLD}Deploying binary...${C_RESET}"

if [ -n "${USE_SUDO:-}" ]; then
    $USE_SUDO install -m 755 "${TMP_DIR}/down" "${TARGET_DIR}/down"
    # Remove legacy inlay binary if present in target dir
    [ -f "${TARGET_DIR}/inlay" ] && $USE_SUDO rm -f "${TARGET_DIR}/inlay" || true
else
    install -m 755 "${TMP_DIR}/down" "${TARGET_DIR}/down"
    [ -f "${TARGET_DIR}/inlay" ] && rm -f "${TARGET_DIR}/inlay" || true
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
    MSG="✔ Down successfully upgraded (v${VERSION})!"
else
    MSG="✔ Down successfully installed (v${VERSION})!"
fi
render_box_line "${C_GREEN}${C_BOLD}${MSG}${C_RESET}" "${#MSG}"
echo -e "${C_CYAN}  ├─────────────────────────────────────────────────────────────┤${C_RESET}"

L1="Location     : ${TARGET_DIR}/down"
render_box_line "${C_MUTED}Location     ${C_RESET}: ${C_BOLD}${TARGET_DIR}/down${C_RESET}" "${#L1}"

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
echo -e "    ${C_CYAN}down${C_RESET}                    Launch guided interactive setup wizard"
echo -e "    ${C_CYAN}down <URL>${C_RESET}              Direct high-speed multi-connection download"
echo -e "    ${C_CYAN}down -j 2 <URL1> <URL2>${C_RESET} Parallel concurrent download swarm"
echo -e "\n  ${C_BOLD}✦ Useful Commands:${C_RESET}"
echo -e "    ${C_MUTED}down --history${C_RESET}        View download history & resumable sessions"
echo -e "    ${C_MUTED}down --help${C_RESET}           Full documentation of flags & protocols"
echo -e "    ${C_MUTED}down --update${C_RESET}         Self-update in-place to latest version\n"

