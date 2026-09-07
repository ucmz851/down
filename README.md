<p align="center">
  <a href="https://github.com/ucmz851/down">
    <img src="https://github.com/ucmz851/down/releases/download/v0.0.2/down.png" alt="down logo" width="140" height="140" />
  </a>
</p>

<h1 align="center">down</h1>

<p align="center">
  <strong>High-performance streaming multi-connection download accelerator in C</strong>
</p>

<p align="center">
  <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/Language-C11-00599C.svg?style=flat-square&logo=c" alt="Language: C11" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square" alt="License: MIT" /></a>
  <a href="https://github.com/ucmz851/down/releases/latest"><img src="https://img.shields.io/github/v/release/ucmz851/down?style=flat-square&color=green" alt="Release" /></a>
  <a href="#test-suite"><img src="https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg?style=flat-square" alt="Tests" /></a>
  <a href="#installation"><img src="https://img.shields.io/badge/Binary-~120%20KB-blueviolet.svg?style=flat-square" alt="Binary Size" /></a>
</p>

**down** is a lightweight, blazing-fast segmented download accelerator implemented in modern C11 for Linux and macOS (Darwin / Apple Silicon & Intel). Engineered for multi-gigabit network saturation and high-speed NVMe storage, it combines lockless positional I/O with dynamic work-stealing scheduling, native HTTP/3 (QUIC) support, direct AWS S3 / Cloudflare R2 SigV4 authentication, an intuitive interactive wizard, and zero-rehash crash recovery.

---

## 🎬 Demo

<p align="center">
  <img src="https://github.com/ucmz851/down/releases/download/v0.0.2/demo.gif" alt="down interactive terminal demo" width="100%" />
</p>

<p align="center">
  <sub>Full session recording: <a href="assets/screenrecording-2026-09-07_11-37-24_trimmed.mp4"><code>assets/screenrecording-2026-09-07_11-37-24_trimmed.mp4</code></a></sub>
</p>

---

## 📦 Installation & Updates

**Install or Update to Latest Release**:
```bash
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash
```

**Self-Update In-Place (Built-in CLI)**:
```bash
down --update
```
*(Check if a newer version is available without installing: `down --check-update`)*

**Uninstall Completely**:
```bash
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash
```
*(Tip: pass `--purge` to also wipe history and configuration: `curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash -s -- --purge`)*

---

## 🚀 Quick Start Guide

Each command below is independent and ready to copy:

### 1. Launch Guided Interactive Wizard (Zero Flags Needed)
Simply type `down` in your terminal and press `[Enter]`:
```bash
down
```

### 2. Direct High-Speed Download (4 Parallel Connections)
```bash
down https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### 3. Parallel Multi-File Swarm
Download multiple files concurrently in parallel slots:
```bash
down -j 2 https://example.com/file1.iso https://example.com/file2.iso
```

### 4. Check Download History & Resumable Sessions
```bash
down --history
```

---

## ✨ Key Features

- **Lockless Positional I/O**: Writes directly to pre-allocated disk sectors via atomic `pwrite()` system calls. Eliminates temporary chunk files, post-download concatenation delays, and filesystem fragmentation on ext4, Btrfs, and XFS.
- **🧙 Guided Interactive Wizard**: Don't want to memorize flags? Simply run `down` with no arguments to launch an interactive setup wizard with beginner-friendly Quick Start and Advanced customization modes.
- **🐝 Parallel Multi-Download Swarms (`-j, --concurrent`)**: Download multiple files simultaneously with independent worker pools and a live, high-frequency stacked telemetry dashboard with aggregate bandwidth and total ETA.
- **🔄 Instant Zero-Rehash Crash Recovery**: Transfer state is mirrored into a compact memory-mapped control file (`<file>.down`). If internet drops or your PC loses power, resume instantly with 1 keystroke without re-reading or re-hashing gigabytes of data.
- **📜 Lightweight Session History Database**: Automatically tracks all download sessions in `$XDG_STATE_HOME/down/history.tsv` using atomic writes. Easily inspect past downloads with `down --history`.
- **🔀 Dynamic Work-Stealing Scheduler**: Continuously balances transfer loads. When unallocated chunks are depleted, idle workers dynamically bisect the remaining ranges of slower connections to eliminate "tail latency" stalls.
- **🌐 Modern Protocol Transport (HTTP/3 & QUIC)**: Native multiplexed transport over UDP via HTTP/3 with automatic protocol negotiation and seamless fallback to HTTP/2 and HTTP/1.1.
- **☁️ Native S3 & Cloudflare R2 Authentication**: Direct AWS Signature Version 4 (`AWS4-HMAC-SHA256`) signing for `s3://` URLs without requiring Python, AWS CLI, or external SDK runtimes.
- **🔒 Automated Cryptographic Verification**: Automatic hash calculation and verification supporting SHA-256, SHA-512, MD5, SHA-1, and BLAKE2 with automatic algorithm inference.
- **🪶 Ultra-Lightweight Binary**: Compiles to a self-contained, standalone executable (~120 KB stripped) depending only on standard system libraries (`libcurl`, `OpenSSL`).

---

## 📊 Feature Comparison

| Feature | down | aria2c | curl | wget | axel |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Language** | **C11** | C++ | C | C | C |
| **Binary Size (Stripped)** | **~120 KB** | ~4.5 MB | ~3.2 MB | ~1.8 MB | ~130 KB |
| **I/O Model** | **Lockless `pwrite()`** | Lockless `pwrite()` | Sequential | Sequential | Multi-thread |
| **Disk Pre-allocation** | **`posix_fallocate()`** | Optional | None | None | None |
| **Dynamic Work-Stealing** | **Yes** | Yes | No | No | No (Static only) |
| **Interactive Setup Wizard** | **Yes (Type `down`)** | No | No | No | No |
| **Multi-Download Swarm (`-j`)**| **Yes (Live Dashboard)**| Yes | No | No | No |
| **Crash Recovery** | **Zero-Rehash `mmap`** | State file | Range `-C -` | Range `-c` | State file |
| **Session History Log** | **Built-in (`--history`)**| No | No | No | No |
| **HTTP/3 (QUIC)** | **Native (`--http3`)** | No | Via flags | No | No |
| **AWS S3 / R2 SigV4** | **Native (`s3://`)** | No | Manual headers | No | No |
| **Checksum Verification** | **Auto-detected** | Metalink only | Manual | Manual | No |
| **Dependencies** | **libcurl, OpenSSL** | C++ runtime, XML, SSL | OpenSSL, zlib | GnuTLS / OpenSSL | OpenSSL |

---

## 📦 Installation

### Method 1: Automated Script (Recommended)

Install or update to the latest version with one command:

```bash
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash
```

The installer automatically detects your operating system (Linux or macOS) and architecture (Apple Silicon `arm64` or Intel `x86_64`), fetches the official release, verifies its SHA-256 checksum, and deploys `down` into `~/.local/bin` (or `/usr/local/bin` if privileged).

> [!TIP]
> **Built-in Self-Updater**: Once installed, you can update `down` in-place anytime directly from your terminal by running:
> ```bash
> down --update
> ```
> Or check if a newer version is available without installing: `down --check-update`.

### Method 2: Pre-Compiled Release Binaries

Download standalone pre-built binaries directly from [GitHub Releases](https://github.com/ucmz851/down/releases):

```bash
# Linux x86_64 (amd64):
tar -xzf down-v0.0.2-linux-amd64.tar.gz
install -m 755 down ~/.local/bin/down

# macOS Apple Silicon (arm64 / M-series):
tar -xzf down-v0.0.2-darwin-arm64.tar.gz
install -m 755 down /usr/local/bin/down

# macOS Intel (x86_64):
tar -xzf down-v0.0.2-darwin-amd64.tar.gz
install -m 755 down /usr/local/bin/down
```

### Method 3: Build from Source

#### Prerequisites
- C11-compliant compiler (`gcc` or Apple `clang`)
- `make` and `pkg-config`
- `libcurl` (development headers)
- `OpenSSL` (`libcrypto` development headers)
- Linux kernel 2.6.14+ (`posix_fallocate`) or macOS 10.10+ Darwin (`fcntl F_PREALLOCATE`)

```bash
# macOS (Homebrew):
brew install curl openssl pkg-config

# Debian / Ubuntu / Mint / Pop!_OS:
sudo apt install build-essential libcurl4-openssl-dev libssl-dev pkg-config

# Arch Linux / Manjaro:
sudo pacman -S base-devel curl openssl

# Fedora / RHEL:
sudo dnf install gcc make libcurl-devel openssl-devel pkgconfig

# Clone and compile:
git clone https://github.com/ucmz851/down.git
cd down
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make test
sudo make install
```

---

## 📖 User Guide & Examples

### 1. 🧙 Interactive Setup Wizard

Running `down` without arguments in your terminal launches the interactive wizard:

```bash
down
```

```text
  down — Interactive Setup Wizard
  High-performance streaming download accelerator
─────────────────────────────────────────────────────────────────

? Enter download URL(s): https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso

Configuration Mode:
  [1] Quick Start (Recommended)
      → Download immediately with optimized defaults to current directory
  [2] Advanced Setup
      → Customize destination, connection count, chunk size, speed limit, checksum
  [3] View Download History

? Select mode [1/2/3] (default: 1): 
```

- **Quick Start (`1` or Enter)**: Zero friction. Paste the URL $\rightarrow$ hit Enter $\rightarrow$ the download begins immediately in your current folder with 4 parallel work-stealing connections.
- **Advanced Setup (`2`)**: Customize destination folder (with `~` expansion), output filename, parallel connections (1–64), chunk sizing, speed throttling, and checksum validation.
- **Single or Multiple URLs**: Paste a single link or multiple links separated by spaces to launch parallel download slots automatically.

---

### 2. 🔄 Automatic Crash Recovery (Power Loss / Wi-Fi Drop)

If your download gets interrupted by `Ctrl+C`, network failure, or a sudden power outage:
- The unfinished file and its compact `.down` state tracker remain safely on disk.
- Simply type `down` again. It automatically detects the unfinished download and prompts you to resume:

```text
  down — Interactive Setup Wizard
  High-performance streaming download accelerator
─────────────────────────────────────────────────────────────────

  Found an interrupted / resumable download:
    File        : ubuntu-24.04-desktop-amd64.iso
    Progress    : 45.2% (2.62 GB / 5.80 GB completed)
    Source URL  : https://releases.ubuntu.com/...
    Saved Path  : ./ubuntu-24.04-desktop-amd64.iso

What would you like to do?
  [1] Resume 'ubuntu-24.04-desktop-amd64.iso' (Recommended)
  [2] Start a new download (Quick Start)
  [3] Advanced setup for new download
  [4] View all download history
  [5] Discard this resume state

? Select option [1-5] (default: 1): 
```

Press **`[Enter]`**, and the transfer instantly resumes from the exact byte where it stopped, without re-reading or re-hashing earlier data!

If multiple downloads were interrupted, option `[1]` lets you **resume all of them in parallel slots** at the same time!

---

### 3. 📜 Download History & Session Management

down maintains a lightweight, zero-dependency history log in `$XDG_STATE_HOME/down/history.tsv`:

```bash
# View complete history table of completed, resumable, and interrupted downloads:
down --history

# Clear the history database:
down --clear-history
```

```text
  down — Download History & Sessions
──────────────────────────────────────────────────────────────────────────────────────────
  Date/Time            Status        Progress          File / Source
──────────────────────────────────────────────────────────────────────────────────────────
  2026-09-07 10:26     RESUMABLE     7.00 MB (0.1%)    omarchy-4.0.2.iso (.down state)
                                     ↳ https://iso.omarchy.org/omarchy-4.0.2.iso
  2026-09-07 10:26     RESUMABLE     5.00 MB (0.2%)    linuxmint-22.3-cinnamon-64bit.iso (.down state)
                                     ↳ https://mirrors.cicku.me/linuxmint/...
  2026-09-07 09:26     COMPLETED     4.20 GB (100%)    ubuntu-24.04-desktop-amd64.iso
                                     ↳ https://releases.ubuntu.com/...
──────────────────────────────────────────────────────────────────────────────────────────
```

---

### 4. 🎯 Direct CLI Downloads

```bash
# Download a file (filename inferred automatically from URL headers):
down https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso

# Save with a custom filename to a specific directory:
down -o ubuntu.iso -d ~/Downloads https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso

# Resume an interrupted transfer directly via command line:
down -c https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

---

### 5. 🐝 Parallel Multi-Download Swarm (`-j, --concurrent`)

Download multiple files concurrently with independent worker pools and a live stacked dashboard:

```bash
# Download 3 URLs with 2 concurrent active download slots:
down -j 2 https://example.com/file1.iso https://example.com/file2.iso https://example.com/file3.iso
```

The live dashboard displays per-file progress bars, connection counts, individual transfer speeds, and an aggregate speed + swarm ETA line.

---

### 6. 🚀 Concurrency & Chunk Size Tuning

```bash
# Squeeze maximum throughput with 16 parallel connections and 1 MB chunk sizing:
down -n 16 -s 1M https://example.com/dataset.tar.gz

# Use static range partitioning instead of dynamic work-stealing:
down -n 8 --static https://example.com/archive.zip

# Force single-stream sequential download (e.g. for streams or unseekable servers):
down --force-single https://example.com/stream.bin
```

---

### 7. 🌐 HTTP/3 (QUIC) Transport

```bash
# Attempt HTTP/3 over UDP with automatic protocol fallback to HTTP/2 and HTTP/1.1:
down --http3 https://cloudflare-quic.com/test.iso

# Force HTTP/3 exclusively:
down --http3-only https://cloudflare-quic.com/test.iso
```

---

### 8. ☁️ AWS S3 & Cloudflare R2 Authentication

down natively computes AWS SigV4 signatures without external dependencies:

```bash
# Standard AWS credentials from environment variables:
export AWS_ACCESS_KEY_ID="AKIA..."
export AWS_SECRET_ACCESS_KEY="..."
export AWS_REGION="us-east-1"

down s3://my-bucket/models/weights.safetensors

# Direct download from Cloudflare R2 / MinIO / Ceph with custom endpoint:
down --s3-endpoint https://<account_id>.r2.cloudflarestorage.com \
      --aws-access-key "R2_ACCESS_KEY" \
      --aws-secret-key "R2_SECRET_KEY" \
      s3://models/weights.safetensors
```

---

### 9. 🔒 Checksum Verification

Verify file integrity automatically upon completion:

```bash
# Specify algorithm explicitly:
down -C sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso

# Algorithm auto-detected from hex length (64 chars -> SHA-256):
down -C e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso
```

---

### 10. 📑 Batch URL Queue File (`-i, --input-file`)

```bash
down -i urls.txt -d ~/Downloads -n 8
```

Example `urls.txt` syntax:
```text
# Linux ISOs
https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
  out=ubuntu-24.04.iso
  checksum=sha256:b590e8a71584e27f47492c10b27...

# Cloud storage datasets
s3://my-datasets/cifar100.bin
https://example.com/weights.safetensors   custom_name.safetensors
```

---

### 11. ⏱️ Speed Throttling & Custom Headers

```bash
# Limit bandwidth consumption to 25 MB/s:
down -r 25M https://example.com/large-archive.tar.gz

# Send custom HTTP headers:
down -H "Authorization: Bearer <token>" -H "X-API-Key: key123" https://api.example.com/data.zip
```

---

## ⚙️ Configuration File

down supports persistent configuration so you don't need to pass favorite flags every time.

### File Locations (Checked in order):
1. `--config <path>` (explicit command-line path)
2. `$XDG_CONFIG_HOME/down/config` (or `~/.config/down/config`)
3. `~/.downrc`
4. `/etc/down/config` (system-wide defaults)

To bypass all configuration files, pass `--no-config`.

### Example `~/.config/down/config`:
```ini
# Concurrency & Chunk Size
connections = 8
chunk-size = 1M

# Destination Directory (supports ~ expansion)
dir = ~/Downloads

# Network & Protocol Preferences
http3 = true
timeout = 45
retry = 3
rate-limit = 50M

# Cloud Storage Defaults
aws-region = us-east-1
# s3-endpoint = https://<account_id>.r2.cloudflarestorage.com
```

---

## 🛠️ Command-Line Reference

```text
Usage: down [OPTIONS] <URL> [<URL2> ...]

High-performance segmented download engine with zero-assembly positional I/O.

Arguments:
  <URL>                      HTTP, HTTPS, or S3 (s3://) resource URLs to download

Modes & Wizards:
  -I, --interactive          Launch interactive guided setup wizard
      --history              Display past download history and resumable sessions
      --clear-history        Delete download history database

Target & Batch:
  -o, --output <PATH>        Destination file name or path (default: auto-detected)
  -d, --dir <DIRECTORY>      Destination folder (auto-created if nonexistent)
  -i, --input-file <FILE>    Batch download: read list of URLs from file
  -c, --continue             Resume interrupted download from .down control file
  -C, --checksum <SPEC>      Verify hash after download (<algo>:<hex> or raw hex)
      --no-fallocate         Disable upfront contiguous disk block pre-allocation

Concurrency & Performance:
  -j, --concurrent <N>       Number of concurrent downloads (1-16, default: 2)
  -n, --connections <N>      Number of parallel connections per file (1-64, default: 4)
  -s, --chunk-size <SIZE>    Uniform chunk size (e.g. 256K, 512K, 1M, 4M, default: 512K)
      --static               Use static range partitioning instead of work-stealing
      --force-single         Force single-stream sequential download

Protocols & Network:
      --http3                Enable HTTP/3 (QUIC) with automatic protocol fallback
      --http3-only           Force HTTP/3 (QUIC) without protocol fallback
  -t, --timeout <SECS>       Connection/transfer timeout in seconds (default: 30)
  -r, --rate-limit <SPEED>   Maximum bandwidth rate limit (e.g. 500K, 10M, 1G)
      --retry <N>            Maximum connection retries upon failure (default: 3)
      --retry-delay <SECS>   Seconds to wait between retries (default: 2)
  -H, --header <HEADER>      Custom HTTP header (repeatable: -H "Authorization: ...")
  -U, --user-agent <STRING>  Custom HTTP User-Agent string
  -k, --insecure             Allow insecure HTTPS connections (skip TLS check)
  -4, --ipv4                 Resolve IPv4 addresses only
  -6, --ipv6                 Resolve IPv6 addresses only

Cloud Storage & AWS SigV4:
      --aws-sigv4 [PARAM]    Enable AWS SigV4 request signing (default: aws:amz:<region>:s3)
      --aws-access-key <KEY> AWS/S3 access key ID (or env AWS_ACCESS_KEY_ID)
      --aws-secret-key <KEY> AWS/S3 secret access key (or env AWS_SECRET_ACCESS_KEY)
      --aws-region <REGION>  AWS/S3 region (default: us-east-1, auto for Cloudflare R2)
      --aws-token <TOKEN>    AWS temporary session token (or env AWS_SESSION_TOKEN)
      --s3-endpoint <URL>    Custom S3/R2 endpoint (e.g. Cloudflare R2, MinIO, Ceph)

Display & Logging:
  -q, --quiet                Suppress live ANSI progress bar and interactive output
  -v, --verbose              Enable detailed diagnostic and curl debug logs
      --no-color             Disable ANSI color codes in output
  -V, --version              Print version information and exit
  -h, --help                 Print this help screen and exit

Configuration:
      --config <FILE>        Load options from specified configuration file
      --no-config            Ignore default configuration files

Updates & Maintenance:
      --update               Check for and install latest release from GitHub
      --check-update         Check if a newer version is available without installing
```

---

## 🏗️ Technical Architecture

### 1. Positional Storage Subsystem
Unlike traditional download tools that download chunks into dozens of `.part` scratch files and merge them upon completion, down relies on high-performance POSIX positional file primitives:
- Contiguous physical disk blocks are allocated upfront (`posix_fallocate()` on Linux, `fcntl(F_PREALLOCATE)` on macOS APFS/HFS+), eliminating extent fragmentation and guaranteeing sufficient disk space before any network transfer starts.
- Worker threads issue atomic `pwrite(2)` system calls directly to designated byte offsets in a shared file descriptor, completely avoiding file-handle mutex contention.

### 2. Work-Stealing Scheduling
Connection throughput fluctuates dynamically over real-world routes. down solves this with an atomic work-stealing scheduler:
- The byte range is divided into uniform chunks (default: 512 KB).
- Workers lease runs of contiguous chunks to preserve sequential disk locality.
- When unassigned chunks are exhausted, idle workers dynamically bisect the remaining byte range of slower tail connections, launching concurrent Range requests to process the upper half.

### 3. Memory-Mapped Crash Recovery
When a download begins, down memory-maps (`mmap`) a compact control file (`<output>.down`):
- A header records the target URL, resource size, chunk size, and timestamp.
- An atomic bitfield tracks completion state per chunk (1 bit per chunk: a 10 GB file needs only ~2.5 KB of metadata).
- If terminated, `-c` (or the interactive wizard) re-maps the state file and resumes missing chunks without disk scanning.
- Upon verified completion, the `.down` file is cleanly unlinked.

---

## 🧪 Test Suite

The test suite validates core primitives, network concurrency, and crash recovery against a live mock HTTP server:

```bash
make test
```

Tests include:
- Positional I/O & `posix_fallocate` pre-allocation
- Memory-mapped bitfield persistence & atomic recovery
- Dynamic work-stealing & tail-worker range bisection
- Multi-algorithm cryptographic hash validation (SHA-256, SHA-512, MD5, SHA-1, BLAKE2)
- AWS SigV4 authorization & URL parsing
- Batch input file queue processing
- Interactive wizard menu flow & input parsing
- Parallel swarm concurrency with stacked telemetry

---

## 🗑️ Uninstallation

To remove down from your system:

```bash
# Automated uninstallation:
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash

# Or completely purge binaries, configuration, and history database:
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash -s -- --purge
```

---

## 🙏 Acknowledgments & Credits

**down** is built on the shoulders of giants in the open-source community. Sincere thanks and appreciation to:

- **[curl / libcurl](https://curl.se/)** (Daniel Stenberg & contributors) — For the world-class, battle-tested network transfer foundation powering down's HTTP/1.1, HTTP/2, and HTTP/3 (QUIC) multiplexing.
- **[OpenSSL](https://www.openssl.org/)** (The OpenSSL Project) — For the robust cryptographic engine powering real-time checksum verification (SHA-256, SHA-512, MD5, SHA-1, BLAKE2) and AWS SigV4 HMAC-SHA256 request signing.
- **The Linux Kernel, macOS Darwin & POSIX Community** — For providing the rock-solid positional I/O primitives (`posix_fallocate`, `fcntl F_PREALLOCATE`, `pwrite`, `mmap`, `msync`) that make zero-copy lockless segmented downloads possible.

---

## 👤 Author

Crafted by **Usama Imran Cheema** ([@ucmz851](https://github.com/ucmz851)).

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
