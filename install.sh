#!/usr/bin/env bash
# ==============================================================================
#  inlay: Automated Installer
#  One-line installation:
#    curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/install.sh | bash
# ==============================================================================

set -euo pipefail

REPO="ucmz851/inlay"
VERSION="0.0.1"
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
echo -e "${BOLD}High-Performance Segmented Download Engine (v${VERSION})${RESET}"
echo -e "${DIM}https://github.com/${REPO}${RESET}\n"

# 1. Detect Operating System
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

if [ "$OS" != "linux" ]; then
    echo -e "${YELLOW}[!] Warning: Pre-built binaries are optimized for Linux. Attempting source compilation...${RESET}"
    FORCE_BUILD=1
else
    FORCE_BUILD=0
fi

# 2. Normalize Architecture
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

# 3. Determine Installation Directory
if [ -w "$DEFAULT_INSTALL_DIR" ]; then
    TARGET_DIR="$DEFAULT_INSTALL_DIR"
elif command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
    TARGET_DIR="$DEFAULT_INSTALL_DIR"
    USE_SUDO="sudo"
else
    TARGET_DIR="$FALLBACK_INSTALL_DIR"
    mkdir -p "$TARGET_DIR"
fi

TMP_DIR="$(mktemp -d /tmp/inlay_install_XXXXXX)"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

INSTALLED=0

# 4. Attempt to Download Pre-compiled Release Binary
if [ "$FORCE_BUILD" -eq 0 ]; then
    TARBALL="inlay-v${VERSION}-linux-${NORM_ARCH}.tar.gz"
    RELEASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${TARBALL}"

    echo -e "[*] Downloading pre-compiled binary: ${BOLD}${TARBALL}${RESET}..."
    if curl -fsSL -o "${TMP_DIR}/${TARBALL}" "$RELEASE_URL" 2>/dev/null; then
        echo -e "[*] Extracting ${TARBALL}..."
        tar -xzf "${TMP_DIR}/${TARBALL}" -C "${TMP_DIR}"

        if [ -f "${TMP_DIR}/inlay" ]; then
            echo -e "[*] Installing inlay to ${BOLD}${TARGET_DIR}/inlay${RESET}..."
            if [ -n "${USE_SUDO:-}" ]; then
                $USE_SUDO install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
            else
                install -m 755 "${TMP_DIR}/inlay" "${TARGET_DIR}/inlay"
            fi
            INSTALLED=1
        fi
    else
        echo -e "${YELLOW}[!] Pre-built binary not found for ${OS}-${NORM_ARCH} on GitHub Releases.${RESET}"
        echo -e "[*] Falling back to automated compilation from source..."
    fi
fi

# 5. Fallback: Build from Source if pre-compiled download failed
if [ "$INSTALLED" -eq 0 ]; then
    echo -e "[*] Compiling inlay from source repository..."

    # Check build tools
    for tool in gcc make curl pkg-config; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo -e "${RED}[!] Error: '$tool' is required to compile inlay from source.${RESET}"
            echo -e "    Please install it using your package manager (e.g. apt install build-essential libcurl4-openssl-dev libssl-dev)."
            exit 1
        fi
    done

    # Check for git
    if command -v git >/dev/null 2>&1; then
        git clone --depth 1 "https://github.com/${REPO}.git" "${TMP_DIR}/source"
    else
        mkdir -p "${TMP_DIR}/source"
        curl -fsSL "https://github.com/${REPO}/archive/refs/heads/main.tar.gz" | tar -xz --strip-components=1 -C "${TMP_DIR}/source"
    fi

    echo -e "[*] Building with make -C ..."
    make -C "${TMP_DIR}/source" -j"$(nproc 2>/dev/null || echo 2)"

    echo -e "[*] Installing binary to ${BOLD}${TARGET_DIR}/inlay${RESET}..."
    if [ -n "${USE_SUDO:-}" ]; then
        $USE_SUDO install -m 755 "${TMP_DIR}/source/inlay" "${TARGET_DIR}/inlay"
    else
        install -m 755 "${TMP_DIR}/source/inlay" "${TARGET_DIR}/inlay"
    fi
    INSTALLED=1
fi

# 6. Verify Installation
if [ "$INSTALLED" -eq 1 ]; then
    echo -e "\n${GREEN}${BOLD}[✓] Inlay successfully installed!${RESET}"
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
