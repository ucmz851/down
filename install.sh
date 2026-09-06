#!/usr/bin/env bash
# ==============================================================================
#  inlay: Automated Installer & Updater
#  One-line installation / update:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/install.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/inlay"
DEFAULT_INSTALL_DIR="/usr/local/bin"
FALLBACK_INSTALL_DIR="${HOME}/.local/bin"

# ANSI Colors
BOLD="\033[1m"
GREEN="\033[1;32m"
CYAN="\033[1;36m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
DIM="\033[38;5;244m"
RESET="\033[0m"

echo -e "${CYAN}"
cat << 'EOF'
  ___       __             
 |_ _| _ _  | | __ _  _  _ 
  | | | ' \ | |/ _` || || |
 |___||_||_||_|\__,_| \_, |
                      |__/ 
EOF
echo -e "${BOLD}High-Performance Segmented Download Engine${RESET}"
echo -e "${DIM}https://github.com/${REPO}${RESET}\n"

# 1. Detect latest version from GitHub releases redirect or API
LATEST_TAG="$(curl -sIL -o /dev/null -w "%{url_effective}\n" "https://github.com/${REPO}/releases/latest" 2>/dev/null | awk -F'/' '{print $NF}' || true)"
if [ -z "$LATEST_TAG" ] || [ "$LATEST_TAG" = "latest" ]; then
    LATEST_TAG="$(curl -fsSL --connect-timeout 5 "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null | grep -o '"tag_name": *"[^"]*"' | head -n1 | cut -d '"' -f 4 || true)"
fi
if [ -n "$LATEST_TAG" ]; then
    VERSION="${LATEST_TAG#v}"
else
    VERSION="0.0.1"
fi

# 2. Check for existing installation
EXISTING_INLAY="$(command -v inlay 2>/dev/null || true)"
IS_UPGRADE=0

if [ -n "$EXISTING_INLAY" ] && [ -x "$EXISTING_INLAY" ]; then
    CURRENT_VER="$("$EXISTING_INLAY" --version 2>/dev/null | head -n1 | awk '{print $2}' || true)"
    echo -e "[*] Existing installation detected: ${BOLD}v${CURRENT_VER}${RESET} (${EXISTING_INLAY})"
    IS_UPGRADE=1
    TARGET_DIR="$(dirname "$EXISTING_INLAY")"
else
    # 3. Determine Installation Directory for new installs
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

# Check permissions on target directory
USE_SUDO=""
if [ ! -w "$TARGET_DIR" ]; then
    if command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
        USE_SUDO="sudo"
    else
        # Fall back to user local bin if cannot write
        TARGET_DIR="$FALLBACK_INSTALL_DIR"
        mkdir -p "$TARGET_DIR"
    fi
fi

# 4. Detect OS and Architecture
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

if [ "$OS" != "linux" ]; then
    echo -e "${YELLOW}[!] Warning: Pre-built binaries are optimized for Linux. Attempting source compilation...${RESET}"
    FORCE_BUILD=1
else
    FORCE_BUILD=0
fi

case "$ARCH" in
    x86_64|amd64)
        NORM_ARCH="amd64"
        ;;
    aarch64|arm64)
        NORM_ARCH="arm64"
        ;;
    *)
        echo -e "${YELLOW}[*] Non-standard architecture (${ARCH}), falling back to local source build.${RESET}"
        FORCE_BUILD=1
        ;;
esac

TMP_DIR="$(mktemp -d /tmp/inlay_install_XXXXXX)"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

INSTALLED=0

# 5. Download pre-compiled release binary
if [ "$FORCE_BUILD" -eq 0 ]; then
    TARBALL="inlay-v${VERSION}-linux-${NORM_ARCH}.tar.gz"
    RELEASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${TARBALL}"

    echo -e "[*] Fetching release asset: ${BOLD}${TARBALL}${RESET} (v${VERSION})..."
    if curl -fsSL -o "${TMP_DIR}/${TARBALL}" "$RELEASE_URL" 2>/dev/null; then
        tar -xzf "${TMP_DIR}/${TARBALL}" -C "${TMP_DIR}"

        if [ -f "${TMP_DIR}/inlay" ]; then
            echo -e "[*] Installing inlay into ${BOLD}${TARGET_DIR}/inlay${RESET}..."
            if [ -n "${USE_SUDO:-}" ]; then
                $USE_SUDO install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
            else
                install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
            fi
            INSTALLED=1
        fi
    else
        echo -e "${YELLOW}[!] Pre-built binary not found on GitHub Releases for ${OS}-${NORM_ARCH}.${RESET}"
        echo -e "[*] Falling back to automated compilation from source..."
    fi
fi

# 6. Fallback: Build from Source
if [ "$INSTALLED" -eq 0 ]; then
    echo -e "[*] Compiling inlay from source repository..."

    for tool in gcc make curl pkg-config; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo -e "${RED}[!] Error: '$tool' is required to compile inlay from source.${RESET}"
            echo -e "    Please install it using your package manager (e.g. apt install build-essential libcurl4-openssl-dev libssl-dev)."
            exit 1
        fi
    done

    if command -v git >/dev/null 2>&1; then
        git clone --depth 1 "https://github.com/${REPO}.git" "${TMP_DIR}/source"
    else
        mkdir -p "${TMP_DIR}/source"
        curl -fsSL "https://github.com/${REPO}/archive/refs/heads/main.tar.gz" | tar -xz --strip-components=1 -C "${TMP_DIR}/source"
    fi

    echo -e "[*] Building with make..."
    make -C "${TMP_DIR}/source" -j"$(nproc 2>/dev/null || echo 2)"

    echo -e "[*] Installing binary to ${BOLD}${TARGET_DIR}/inlay${RESET}..."
    if [ -n "${USE_SUDO:-}" ]; then
        $USE_SUDO install -m 755 "${TMP_DIR}/source/inlay" "${TARGET_DIR}/inlay"
    else
        install -m 755 "${TMP_DIR}/source/inlay" "${TARGET_DIR}/inlay"
    fi
    INSTALLED=1
fi

# 7. Verification & Summary
if [ "$INSTALLED" -eq 1 ]; then
    if [ "$IS_UPGRADE" -eq 1 ]; then
        echo -e "\n${GREEN}${BOLD}[✓] Inlay successfully updated to v${VERSION}!${RESET}"
    else
        echo -e "\n${GREEN}${BOLD}[✓] Inlay successfully installed (v${VERSION})!${RESET}"
    fi

    "${TARGET_DIR}/inlay" --version

    # Check PATH
    if ! echo "$PATH" | tr ':' '\n' | grep -qx "$TARGET_DIR"; then
        echo -e "\n${YELLOW}[!] Note: '${TARGET_DIR}' is not in your current PATH.${RESET}"
        echo -e "    Add it to your shell configuration:"
        echo -e "    ${BOLD}export PATH=\"${TARGET_DIR}:\$PATH\"${RESET}"
    fi

    echo -e "\n${BOLD}Quick Start:${RESET}"
    echo -e "  inlay https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso"
    echo -e "  inlay --help\n"
else
    echo -e "${RED}[!] Installation failed.${RESET}"
    exit 1
fi
