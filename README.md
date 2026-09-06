<div align="center">

# ⚡ Inlay
### The Modern, Blazing-Fast Segmented Download Engine in C11
**Zero-Assembly Positional I/O • Dynamic Work-Stealing • HTTP/3 QUIC • AWS SigV4 • Zero-Hash Crash Recovery**

[![Language](https://img.shields.io/badge/Language-C11-00599C.svg?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](https://opensource.org/licenses/MIT)
[![Build & Tests](https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg?style=flat-square)](#test-suite)
[![Protocols](https://img.shields.io/badge/Protocols-HTTP%2F3%20(QUIC)%20%7C%20HTTP%2F2%20%7C%20S3-blueviolet.svg?style=flat-square)](#next-gen-protocols)
[![Footprint](https://img.shields.io/badge/Binary-~80%20KB-orange.svg?style=flat-square)](#why-inlay)

<br/>

```text
── inlay 0.0.1 ─────────────────────────────────────────────────────────
 Target   : llama-3-70b-instruct.Q4_K_M.gguf
 Size     : 42.60 GB (45741690880 bytes)
 Source   : s3://ai-weights-us/models/llama-3-70b-instruct.Q4_K_M.gguf
 Engine   : Dynamic Work-Stealing (16 workers, 2 MB chunk size)
 Protocol : HTTP/3 (QUIC) with fallback
 Auth     : AWS SigV4 (Region: us-east-1, Service: s3)
 Storage  : Contiguous blocks pre-allocated (posix_fallocate)
─────────────────────────────────────────────────────────────────────────

  78.4% ▕████████████▌░░░░▏ 33.40 GB/42.60 GB  412.80 MB/s  ETA 00:22  (16 conn)
```

</div>

---

## 🚀 Why Inlay?

Tired of 20-year-old C++ download managers, slow single-stream `curl` transfers, or installing 300MB Python packages just to download private S3 weights?

`inlay` is engineered from scratch in clean **C11** for modern NVMe storage, multi-gigabit networks, and cloud-native workflows. It eliminates filesystem fragmentation, saturates network interfaces, and brings native HTTP/3 and direct AWS S3 / Cloudflare R2 request signing to a single, ultra-lightweight **~80 KB static binary**.

### 🥊 Feature Showdown: Inlay vs The Alternatives

| Feature | ⚡ **Inlay** | `aria2c` | `curl` | `wget` | `axel` |
|:---|:---:|:---:|:---:|:---:|:---:|
| **Language & Codebase** | **Modern C11** | Heavy C++98/11 | Clean C | Legacy C | Legacy C |
| **Compiled Binary Size** | **~80 KB** | ~4.5 MB | ~3.2 MB | ~1.8 MB | ~130 KB |
| **HTTP/3 (QUIC Multiplexing)** | **Native (`--http3`)** | ❌ No | ⚠️ Via flags | ❌ No | ❌ No |
| **Native AWS S3 / Cloudflare R2** | **Native (`s3://` SigV4)** | ❌ No | ⚠️ Raw headers | ❌ No | ❌ No |
| **Multi-Worker Segmented I/O** | **Lockless `pwrite`** | Lockless `pwrite` | ❌ Single-stream | ❌ Single-stream | Multi-thread |
| **Work-Stealing & Lagger Bisection** | **Yes (Dynamic)** | Yes | ❌ N/A | ❌ N/A | ❌ (Static only) |
| **Upfront Disk Pre-allocation** | **`posix_fallocate()`** | `falloc` | ❌ None | ❌ None | ❌ None |
| **Instant Crash Resume** | **`mmap` Atomic Bitfield** | `.aria2` file | ❌ Manual Range | ❌ Manual `-c` | State file |
| **Zero-Hash Instant Reconnect** | **Instant (Zero re-hash)** | Instant | ❌ N/A | ❌ N/A | Partial |
| **Terminal Telemetry** | **28.5 Hz Unicode Sub-blocks** | 1-line text | Progress meter | Basic meter | Basic bars |
| **Digest Verification** | **Auto-detect SHA/MD5/BLAKE** | Metalink only | ❌ Manual | ❌ Manual | ❌ No |
| **Dependencies** | **libcurl + OpenSSL** | C++ runtime, XML, SSL | None / OpenSSL | OpenSSL | None |

---

## 💎 The Four Superpowers of Inlay

### 1. 🏎️ The Tail-Latency Killer: Dynamic Work-Stealing & Lagger Bisection
Traditional segmented downloaders (like `axel`) split a file into $N$ equal chunks. If one TCP connection throttles or gets stuck on a bad route, **your entire download stalls at 99%** waiting for that one sluggish connection while all other workers sit completely idle.

`inlay` uses an active work-stealing allocator:
* Workers lease contiguous chunk runs to minimize connection overhead.
* When unassigned chunks are depleted, idle workers inspect active peers, identify the slowest connection ("the lagger"), and **bisect its remaining chunk range in real-time**.
* Work is split atomically without redundant byte transfers or write collisions.

```
Worker 0 (Fast):  [████████████████████████] Done -> Steals upper 50% of Worker 2
Worker 1 (Fast):  [████████████████████████] Done -> Steals upper 50% of Worker 3
Worker 2 (Slow):  [████░░░░░░░░░░░░░░░░░░░░] -> Bisected! [████████] (Worker 0 takes the rest)
Worker 3 (Lagging): [██░░░░░░░░░░░░░░░░░░░░] -> Bisected! [████] (Worker 1 takes the rest)
```

### 2. ⚡ Zero-Assembly Positional I/O (`posix_fallocate` + `pwrite`)
Naive tools download segments into temporary files (`part0`, `part1`, ...) and then sequentially concatenate them when done, causing massive disk I/O thrashing on NVMe SSDs.

`inlay` eliminates this entirely:
1. **Contiguous Allocation**: `posix_fallocate()` claims physical disk extents before the first byte arrives. Prevents ext4/XFS extent fragmentation and instantly detects `ENOSPC` (out of disk space).
2. **Lockless `pwrite()`**: All worker threads write directly into their designated byte offsets in the shared file descriptor. **Zero file-level mutex locks. Zero concatenation stalls.**

### 3. 🌐 Cloud-Native S3 & Cloudflare R2 Direct SigV4
Why install the multi-hundred-megabyte AWS CLI or write a Python `boto3` script just to download a dataset or model weights?
* Simply pass `s3://my-bucket/path/file.bin`!
* `inlay` automatically performs AWS Signature Version 4 (`AWS4-HMAC-SHA256`) request signing on every segmented ranged request.
* Works seamlessly with **Amazon S3**, **Cloudflare R2**, **MinIO**, **Backblaze B2**, and **Ceph**.
* Credentials automatically resolve from environment variables (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, `AWS_REGION`, `AWS_SESSION_TOKEN`) or dedicated flags.

### 4. 🛡️ Bulletproof Crash Recovery (`.inlay` mmap Control File)
If your network drops, power cuts, or you hit `Ctrl+C`:
* State is maintained in a memory-mapped binary header (`<file>.inlay`) backed by an atomic bitfield (1 bit per chunk: a 10 GB file with 512 KB chunks requires only 2.5 KB of metadata).
* **Resume is instantaneous with `-c`**: No need to spend minutes re-hashing gigabytes of data on disk.
* Upon 100% completion, `.inlay` is automatically and cleanly unlinked.

---

## 📊 Beautiful 28.5 Hz ANSI Terminal Telemetry

Engineered for precision and aesthetic clarity without terminal flicker:
* **Fractional Unicode Sub-blocks**: Smooth sub-character progress resolution (`█`, `▉`, `▊`, `▋`, `▌`, `▍`, `▎`, `▏`).
* **Instantaneous Throughput**: 1-second sliding time-window rate estimator with byte/sec, KB/s, MB/s, and GB/s auto-scaling.
* **Non-Interactive Detection**: Automatically switches to quiet mode when piped or redirected into files/CI logs.

---

## 📦 Quick Installation

### 1. Instant One-Line Install (Recommended)
Automatically detects your architecture, fetches the official release binary (or builds from source), and installs `inlay`:

```bash
curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/install.sh | bash
```

### 2. Manual Source Build
```bash
git clone https://github.com/ucmz851/inlay.git
cd inlay
make
make test
sudo make install
```

### 3. Direct Binary Download (v0.0.1)
Standalone release binaries are available on the [GitHub Releases](https://github.com/ucmz851/inlay/releases) page:
* 🐧 **Linux (x86_64 / amd64)**: [`inlay-v0.0.1-linux-amd64.tar.gz`](https://github.com/ucmz851/inlay/releases/download/v0.0.1/inlay-v0.0.1-linux-amd64.tar.gz)

### 🔄 Updating Inlay
Keep your `inlay` installation up to date effortlessly:

```bash
# Self-update in-place (queries GitHub Releases, prompts, and upgrades automatically)
inlay --update

# Or check if a newer release is available without installing
inlay --check-update

# Or simply rerun the one-line install script (auto-detects existing install & upgrades)
curl -fsSL https://raw.githubusercontent.com/ucmz851/inlay/main/install.sh | bash
```

### Build Requirements (for source compilation)
* A C11-compliant compiler (`gcc` or `clang`)
* `libcurl` (HTTP/3 support recommended)
* `OpenSSL` (`libcrypto`)
* Linux kernel 2.6+ (for `posix_fallocate`)

---

## 💡 Practical Examples & Recipes

### 1. Maximize Bandwidth on High-Speed Links
Download with 16 parallel connections and 1 MB chunk dynamic work-stealing:
```bash
inlay -n 16 -s 1M https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### 2. Next-Gen HTTP/3 (QUIC) Downloads
Harness UDP multiplexing with zero head-of-line blocking on lossy Wi-Fi or cellular networks:
```bash
inlay --http3 https://cloudflare-quic.com/test.iso
```

### 3. Direct AWS S3 Private Download
Download directly from private S3 buckets without pre-signed URLs:
```bash
export AWS_ACCESS_KEY_ID="AKIA..."
export AWS_SECRET_ACCESS_KEY="..."
export AWS_REGION="us-east-1"

inlay s3://my-deeplearning-weights/llama-3-8b.gguf
```

### 4. Direct Cloudflare R2 / MinIO Bucket Download
```bash
inlay --s3-endpoint https://<account_id>.r2.cloudflarestorage.com \
      --aws-access-key "R2_KEY" \
      --aws-secret-key "R2_SECRET" \
      s3://models/weights.safetensors
```

### 5. Automated Cryptographic Integrity Verification
Validate SHA-256, SHA-512, MD5, SHA-1, or BLAKE2 immediately upon download. `inlay` automatically infers algorithm type if prefix is omitted:
```bash
# Explicit algorithm
inlay -C sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso

# Auto-detected by 64-character hex length (SHA-256)
inlay -C e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.iso
```

### 6. Batch URL Queue File (`-i, --input-file`)
Queue hundreds of URLs from an input file:
```bash
inlay -i urls.txt -d ~/Downloads -n 8
```
`urls.txt` supports comments, blank lines, and optional aria2-style overrides:
```text
# Distribution images
https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
  out=ubuntu-24.04.iso
  checksum=sha256:b590e8a71584e27f47492c10b27...

# Cloud assets
s3://my-datasets/cifar100.bin
https://example.com/weights.safetensors   custom_weights.safetensors
```

### 7. Bandwidth Throttling & Custom Headers
Cap downloads to preserve network headroom for other services:
```bash
inlay -r 15M -H "Authorization: Bearer SECRET_TOKEN" https://api.example.com/data.zip
```

---

## 🛠️ CLI Reference

```
Usage: inlay [OPTIONS] [<URL>]

High-performance segmented download engine with zero-assembly positional I/O.

Arguments:
  <URL>                      HTTP, HTTPS, or S3 (s3://) resource URL to download

Target & Batch:
  -o, --output <PATH>        Destination file name or path (default: auto-detected)
  -d, --dir <DIRECTORY>      Destination folder (auto-created if nonexistent)
  -i, --input-file <FILE>    Batch download: read list of URLs from file
  -c, --continue             Resume interrupted download from .inlay control file
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
  -k, --insecure             Allow insecure HTTPS connections (skip TLS certificate check)
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

Updates & Maintenance:
      --update               Check for and install latest release from GitHub
      --check-update         Check if a newer version is available without installing
```

---

## 🧪 Architecture & Test Suite

Inlay includes a comprehensive test harness validating unit functionality and real-world network concurrency:

```bash
make test
```

```
==========================================================
    Inlay Test Suite: Validating Core Engine & Extensions 
==========================================================
[1/7] Compiling unit tests...
[2/7] Running unit tests...
  [*] test_storage (Positional I/O & fallocate)... [PASS]
  [*] test_meta (mmap crash recovery & bitfield)... [PASS]
  [*] test_scheduler (Dynamic work stealing & lagger bisection)... [PASS]
  [*] test_checksum (OpenSSL EVP digests & verification)... [PASS]
  [*] test_s3 (S3 URL transformation & SigV4 parameters)... [PASS]
  [*] test_batch (Input file queue parsing)... [PASS]
[3/7] Setting up mock HTTP server with Range and SigV4 support...
[4/7] Executing Core Engine Integration Tests...
  --- Test A: Single Stream Download [PASS]
  --- Test B: Static Multi-Worker Concurrency (4 workers, --static) [PASS]
  --- Test C: Dynamic Work-Stealing Allocator (8 workers, 256K chunks) [PASS]
  --- Test D: Interruption, Pause & Crash Recovery Resume [PASS]
[5/7] Executing Checksum Validation Integration Tests...
  --- Test E1: Successful SHA-256 Checksum Validation [PASS]
  --- Test E2: Checksum Mismatch Detection [PASS]
[6/7] Executing Batch URL Queue Integration Tests...
  --- Test F: Batch Download via Input File (-i) [PASS]
[7/7] Executing AWS SigV4 & HTTP/3 Integration Tests...
  --- Test G: AWS SigV4 Request Authentication [PASS]
  --- Test H: HTTP/3 (QUIC) Flag Configuration [PASS]

==========================================================
    ALL INLAY TESTS PASSED SUCCESSFULLY! (100% GREEN)     
==========================================================
```

---

## 📜 License

Inlay is distributed under the permissive [MIT License](LICENSE). Contributions, bug reports, and feature suggestions are welcome!
