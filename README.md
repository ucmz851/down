# Down

[![Language: C11](https://img.shields.io/badge/Language-C11-00599C.svg?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square)](LICENSE)
[![Release](https://img.shields.io/github/v/release/ucmz851/down?style=flat-square&color=green)](https://github.com/ucmz851/down/releases/latest)
[![Tests](https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg?style=flat-square)](#test-suite)
[![Binary Size](https://img.shields.io/badge/Binary-~65%20KB-blueviolet.svg?style=flat-square)](#installation)

**Down** is a lightweight, high-throughput segmented download accelerator implemented in C11 for Linux systems. Designed for multi-gigabit network saturation and NVMe storage, it combines lockless parallel positional I/O with dynamic work-stealing scheduling, native HTTP/3 (QUIC) support, direct AWS S3 / Cloudflare R2 SigV4 authentication, and instantaneous zero-rehash crash recovery.

---

## Key Features

- **Lockless Positional I/O**: Direct `pwrite()` writes to contiguous physical disk allocations pre-allocated via `posix_fallocate()`. Avoids temporary segment files, post-download concatenation delays, and ext4/XFS filesystem fragmentation.
- **Dynamic Work-Stealing Scheduler**: Continuously balances transfer loads across connection pools. When unassigned chunks are exhausted, idle workers dynamically bisect the remaining byte ranges of slower tail connections to eliminate end-of-transfer stalling.
- **Modern Protocol Transport**: First-class support for HTTP/1.1, HTTP/2, and HTTP/3 (QUIC) multiplexed transport over UDP with automatic protocol negotiation and fallback.
- **Direct Cloud Object Storage**: Native AWS Signature Version 4 (`AWS4-HMAC-SHA256`) signing for `s3://` URLs, enabling authenticated segmented downloads from Amazon S3, Cloudflare R2, MinIO, and Ceph without requiring external CLIs or SDK runtimes.
- **Zero-Rehash Crash Recovery**: Transfer state is mirrored in a compact memory-mapped control file (`<filename>.down`) backed by atomic bitfields. Interrupted downloads resume immediately with `-c` without re-reading or hashing existing file blocks.
- **Cryptographic Verification**: Automated post-download hash validation supporting SHA-256, SHA-512, MD5, SHA-1, and BLAKE2 with automatic algorithm inference from hex string lengths.
- **Batch Processing**: Process multi-URL queue files with custom per-item output paths and checksum specifications (`-i, --input-file`).
- **Minimal Footprint**: Standalone, statically or dynamically linked binary (~65 KB stripped) depending only on standard system libraries (`libcurl`, `OpenSSL`).

---

## Comparison

| Feature | Down | aria2c | curl | wget | axel |
|:---|:---:|:---:|:---:|:---:|:---:|
| **Language** | **C11** | C++ | C | C | C |
| **Binary Size** | **~65 KB** | ~4.5 MB | ~3.2 MB | ~1.8 MB | ~130 KB |
| **I/O Model** | **Lockless `pwrite()`** | Lockless `pwrite()` | Sequential | Sequential | Multi-thread |
| **Disk Pre-allocation** | **`posix_fallocate()`** | Optional | None | None | None |
| **Dynamic Work-Stealing** | **Yes** | Yes | N/A | N/A | No (Static only) |
| **HTTP/3 (QUIC)** | **Native (`--http3`)** | No | Via flags | No | No |
| **S3 / R2 SigV4 Signing** | **Native (`s3://`)** | No | Manual headers | No | No |
| **Crash Recovery** | **`mmap` Bitfield** | State file | Range `-C -` | Range `-c` | State file |
| **Checksum Verification** | **Auto-detected** | Metalink only | Manual | Manual | No |
| **Dependencies** | **libcurl, OpenSSL** | OpenSSL, XML, C++ runtime | OpenSSL, zlib | GnuTLS / OpenSSL | OpenSSL |

---

## Installation

### Method 1: Automated Script (Recommended)

To install or update to the latest release:

```bash
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash
```

The script automatically detects the host architecture, fetches the official release binary, validates its SHA-256 checksum, and installs the binary into `~/.local/bin` (or `/usr/local/bin` if privileged).

### Method 2: Pre-Compiled Binary

Standalone release binaries are available directly on the [GitHub Releases](https://github.com/ucmz851/down/releases) page:

- **Linux (x86_64 / amd64)**: [`down-v0.0.1-linux-amd64.tar.gz`](https://github.com/ucmz851/down/releases/download/v0.0.1/down-v0.0.1-linux-amd64.tar.gz)

```bash
tar -xzf down-v0.0.1-linux-amd64.tar.gz
install -m 755 down ~/.local/bin/down
```

### Method 3: Build from Source

#### Prerequisites
- C11-compliant compiler (`gcc` or `clang`)
- `make`
- `libcurl` (development headers, HTTP/3 enabled if QUIC support is required)
- `OpenSSL` (`libcrypto` development headers)
- Linux kernel 2.6.14+ (for `posix_fallocate`)

```bash
# Debian / Ubuntu dependencies
sudo apt install build-essential libcurl4-openssl-dev libssl-dev pkg-config

# Clone and compile
git clone https://github.com/ucmz851/down.git
cd down
make -j$(nproc)
make test
sudo make install
```

---

## Updating & Maintenance

Down provides native self-update and verification capabilities:

```bash
# Check if a newer version is available without making changes
down --check-update

# Automatically download, verify, and apply the latest release in-place
down --update

# Or rerun the installation script to upgrade
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash
```

---

## Configuration

Down supports persistent configuration files so default flags, concurrency limits, directories, and protocol preferences do not need to be specified on every invocation.

### Discovery Locations

Down automatically searches for configuration files in the following order:
1. `--config <path>` (explicit command-line path)
2. `$XDG_CONFIG_HOME/down/config` (or `~/.config/down/config`)
3. `~/.downrc`
4. `/etc/down/config` (system-wide defaults)

To bypass all configuration files for an isolated transfer, pass `--no-config`.

### Example Configuration (`~/.config/down/config`)

```ini
# Concurrency & Chunk Sizing
connections = 8
chunk-size = 1M

# Destination Directory (supports ~ expansion)
dir = ~/Downloads

# Network & Protocols
http3 = true
timeout = 45
retry = 3
rate-limit = 50M

# Cloud Storage Defaults
aws-region = us-east-1
# s3-endpoint = https://<account_id>.r2.cloudflarestorage.com

# Display
# no-color = false
```

### Precedence Hierarchy
Command-line flags always take highest precedence, overriding both environment variables and configuration file settings:  
`CLI Flags` > `Environment Variables` > `User Config File` > `Hardcoded Defaults`

---

## Uninstallation

Down is a self-contained executable that operates without background daemons, system services, or global configurations:

```bash
# Automated uninstallation script:
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/uninstall.sh | bash

# Or via the installer script:
curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash -s -- --uninstall

# Or if built from source:
sudo make uninstall

# Or direct removal:
rm -f "$(command -v down)"
```

---

## Usage & Examples

### Interactive Wizard Mode (Beginner-Friendly)

If you run `down` without arguments in an interactive terminal, or pass `-I` / `--interactive`, Down launches a clean, guided interactive wizard:

```bash
# Simply type down and press Enter:
down

# Or explicitly launch the wizard:
down -I
```

```text
  ⚡ DOWN — High-Performance Download Manager
  Interactive Setup Wizard
─────────────────────────────────────────────────────────────────

? Enter download URL(s): https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso

Configuration Mode:
  [1] Quick Start (Recommended)
      → Download immediately with optimized defaults to current directory
  [2] Advanced Setup
      → Customize destination, connection count, chunk size, speed limit, checksum
  [3] View Download History

? Select mode [1/2/3] (default: 1): 1
```

- **Single or Multiple URLs**: Paste a single link or multiple links separated by spaces to download multiple files in parallel slots.
- **Automatic Power-Outage / Crash Recovery**: If one or more downloads are interrupted by a power failure or connection drop, typing `down` and pressing Enter automatically detects unfinished downloads and lets you resume all of them concurrently in parallel slots!
- **Quick Start (`1` or Enter)**: Zero friction. Paste URL(s) $\rightarrow$ press Enter $\rightarrow$ download starts immediately in the current folder with 4 parallel work-stealing workers.
- **Advanced Setup (`2`)**: Interactively configure destination directory (with `~` expansion), custom filename, connection count, chunk size, speed throttling, and cryptographic checksum.
- **View History (`3`)**: Displays interactive summary of recent completed and interrupted downloads.

### Download History & Session Management

Down automatically keeps a lightweight, zero-dependency history log in `$XDG_STATE_HOME/down/history.tsv`:

```bash
# View complete history of all downloads (completed, in-progress, resumable)
down --history

# Clear the history database
down --clear-history
```

```text
  ⚡ DOWN — Download History & Sessions
──────────────────────────────────────────────────────────────────────────────────────────
  Date/Time            Status        Progress          File / Source
──────────────────────────────────────────────────────────────────────────────────────────
  2026-09-07 09:26     COMPLETED     4.20 GB (100%)    ubuntu-24.04-desktop-amd64.iso
                                     ↳ https://releases.ubuntu.com/...
  2026-09-07 09:28     RESUMABLE     2.80 GB (66.7%)   debian-12.iso (.down state)
                                     ↳ https://cdimage.debian.org/...
──────────────────────────────────────────────────────────────────────────────────────────
```

### Basic Downloads (CLI Flags)

```bash
# Download a file (filename inferred from URL)
down https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso

# Specify custom destination filename and directory
down -o ubuntu.iso -d ~/Downloads https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### Concurrency & Segment Tuning

```bash
# Download using 16 parallel connections and 1 MB chunk sizing
down -n 16 -s 1M https://example.com/dataset.tar.gz

# Use static range partitioning instead of dynamic work-stealing
down -n 8 --static https://example.com/archive.zip

# Fall back to single-stream sequential download
down --force-single https://example.com/stream.bin
```

### Resuming Interrupted Downloads

If a transfer is interrupted by network failure or manual termination (`Ctrl+C`), resume it instantly:

```bash
down -c https://example.com/large-archive.tar.gz
```

### HTTP/3 (QUIC) Downloads

```bash
# Attempt HTTP/3 with automatic protocol fallback to HTTP/2 and HTTP/1.1
down --http3 https://cloudflare-quic.com/test.iso

# Force HTTP/3 exclusively (requires HTTP/3-capable libcurl)
down --http3-only https://cloudflare-quic.com/test.iso
```

### AWS S3 & Cloudflare R2 Authentication

Down automatically signs requests with AWS SigV4 when accessing `s3://` URLs:

```bash
# Credentials loaded from standard AWS environment variables:
export AWS_ACCESS_KEY_ID="AKIA..."
export AWS_SECRET_ACCESS_KEY="..."
export AWS_REGION="us-east-1"

down s3://my-dataset-bucket/models/weights.bin

# Direct download from Cloudflare R2 / MinIO / Ceph with custom endpoint:
down --s3-endpoint https://<account_id>.r2.cloudflarestorage.com \
      --aws-access-key "R2_ACCESS_KEY" \
      --aws-secret-key "R2_SECRET_KEY" \
      s3://models/weights.safetensors
```

### Cryptographic Checksum Validation

```bash
# Explicit algorithm prefix
down -C sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso

# Auto-detected by hex digest length (64 hex chars -> SHA-256)
down -C e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso
```

### Parallel Multi-Download Swarm (`-j, --concurrent`)

Download multiple files simultaneously with independent worker pools and a live stacked telemetry dashboard:

```bash
# Download 3 URLs with 2 concurrent file slots and 4 connections per file:
down -j 2 https://example.com/file1.iso https://example.com/file2.iso https://example.com/file3.iso
```

### Batch URL Queue (`-i, --input-file`)

```bash
down -i urls.txt -d ~/Downloads -n 8
```

Example `urls.txt` syntax:

```text
# Distribution images
https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
  out=ubuntu-24.04.iso
  checksum=sha256:b590e8a71584e27f47492c10b27...

# Cloud storage datasets
s3://my-datasets/cifar100.bin
https://example.com/weights.safetensors   custom_name.safetensors
```

### Bandwidth Throttling & Custom Headers

```bash
# Limit aggregate download bandwidth to 25 MB/s
down -r 25M https://example.com/large-archive.tar.gz

# Pass custom authentication or session headers
down -H "Authorization: Bearer <token>" -H "X-Custom-Header: value" https://api.example.com/export.zip
```

---

## Command-Line Reference

```
Usage: down [OPTIONS] [<URL>]

Arguments:
  <URL>                      HTTP, HTTPS, or S3 (s3://) resource URL to download

Target & Batch:
  -o, --output <PATH>        Destination file name or path (default: auto-detected)
  -d, --dir <DIRECTORY>      Destination folder (auto-created if nonexistent)
  -i, --input-file <FILE>    Batch download: read list of URLs from file
  -c, --continue             Resume interrupted download from .down control file
  -C, --checksum <SPEC>      Verify hash after download (<algo>:<hex> or raw hex)
      --no-fallocate         Disable upfront contiguous disk block pre-allocation

Concurrency & Performance:
  -n, --connections <N>      Number of parallel connections (1-64, default: 4)
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
      --aws-region <REGION>  AWS/S3 region (default: us-east-1)
      --aws-token <TOKEN>    AWS temporary session token (or env AWS_SESSION_TOKEN)
      --s3-endpoint <URL>    Custom S3/R2 endpoint (e.g. Cloudflare R2, MinIO, Ceph)

Display & Logging:
  -q, --quiet                Suppress live progress indicators and interactive output
  -v, --verbose              Enable diagnostic transfer logs
      --no-color             Disable ANSI formatting in output
  -V, --version              Print version information and exit
  -h, --help                 Print help documentation and exit

Updates & Maintenance:
      --update               Check for and install latest release from GitHub
      --check-update         Check if a newer version is available without installing
```

---

## Technical Architecture

### 1. Positional Storage Subsystem
Unlike traditional download tools that write parts into separate scratch files and concatenate them sequentially upon completion, Down relies on Linux positional file primitives:
- `posix_fallocate()` allocates disk sectors contiguously prior to initiating worker connections, eliminating extent fragmentation on ext4/XFS filesystems and failing fast if storage capacity is insufficient.
- Worker threads issue atomic `pwrite(2)` system calls directly against their designated byte offsets into a single shared file descriptor, removing thread-level mutex contention on the file handle.

### 2. Work-Stealing Scheduling
Connection throughput fluctuates dynamically over TCP/UDP routes. Down employs a work-stealing scheduler:
- The overall byte range is divided into uniform chunks (default: 512 KB).
- Workers lease runs of contiguous chunks from an atomic queue to preserve sequential disk locality.
- Once unallocated chunks are depleted, idle workers inspect active peers. If an active connection has fallen behind ("tail latency"), the idle worker bisects the remaining byte range of the slower connection, initiating an independent HTTP Range request to process the upper half concurrently.

### 3. State Persistence & Crash Recovery
When a download is initialized, Down memory-maps (`mmap`) a compact control file (`<output>.down`):
- A header records the target URL, total resource size, chunk size, and file timestamps.
- A bitfield tracks completion state at chunk-level granularity (1 bit per chunk: a 10 GB transfer requires only ~2.5 KB of metadata).
- If terminated abruptly, `-c` re-maps the state file and resumes missing chunks without requiring disk re-validation.
- Upon 100% verified completion, the `.down` file is automatically unlinked.

---

## Test Suite

The codebase includes automated unit and integration tests covering positional I/O, bitfield state recovery, dynamic work-stealing, checksum algorithms, S3 URL transformation, batch queues, and AWS SigV4 mock servers:

```bash
make test
```

---

## Author

Crafted by **Usama Imran Cheema** ([@ucmz851](https://github.com/ucmz851)).

---

## License

This project is licensed under the terms of the [MIT License](LICENSE).
