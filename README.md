# Inlay: High-Performance Segmented Download Engine in C

`inlay` is an ultra-fast, multi-threaded download manager written in C11 designed for raw I/O throughput, low resource consumption, and resilience against network jitter and process crashes.

Inspired by tools like `aria2`, `inlay` is engineered with an emphasis on **lockless positional I/O**, **contiguous upfront block pre-allocation**, **dynamic work-stealing with lagger bisection**, and **zero-hash crash recovery via memory-mapped control files**.

---

## Architectural Highlights

```
                       +-----------------------------------+
                       |             inlay CLI             |
                       +-----------------+-----------------+
                                         |
                       +-----------------v-----------------+
                       |         HTTP/S Prober (libcurl)   |
                       +-----------------+-----------------+
                                         |
               +-------------------------+-------------------------+
               |                                                   |
    +----------v----------+                             +----------v----------+
    |   Upfront Storage   |                             |   Mmap Control File |
    |  posix_fallocate()  |                             |     <file>.inlay    |
    +----------+----------+                             +----------+----------+
               |                                                   |
               +-------------------------+-------------------------+
                                         |
                       +-----------------v-----------------+
                       |    Dynamic Work-Stealing Queue    |
                       |       & Lagger Bisection          |
                       +-----------------+-----------------+
                                         |
                +------------------------+------------------------+
                |                        |                        |
      +---------v---------+    +---------v---------+    +---------v---------+
      |  Worker Thread 0  |    |  Worker Thread 1  |    |  Worker Thread N  |
      |   Range: [0, M)   |    |  Range: [M, 2M)   |    |  Range: [K, End)  |
      +---------+---------+    +---------+---------+    +---------+---------+
                |                        |                        |
                +------------------------+------------------------+
                                         |
                               pwrite() (Lockless)
                                         |
                                         v
                         [ Shared Target File Descriptor ]
```

### Phase 1: Zero-Assembly Foundation & Positional I/O
- **Upfront Contiguous Block Allocation (`posix_fallocate`)**: Pre-allocates disk blocks before downloading begins. Eliminates filesystem fragmentation, avoids continuous file-extent expansion stalls in ext4/XFS/Btrfs, and guarantees upfront detection of insufficient disk space (`ENOSPC`).
- **Positional I/O (`pwrite`)**: Instead of downloading segments into separate files and performing costly sequential concatenation, all workers write concurrently into their exact byte offsets in the target file descriptor.
- **Probe Subsystem (`probe.c`)**: Performs `HEAD` capability discovery and falls back gracefully to `GET Range: bytes=0-0` for servers that disallow `HEAD`. Automatically extracts filenames, content length, and range support (`Accept-Ranges`).

### Phase 2: Static Multi-Worker Concurrency
- Divide total payload into $N$ static disjoint byte slices.
- Stream parallel segments simultaneously to the exact same shared file descriptor without any file-level mutex locks.
- Selectable with `--static` flag.

### Phase 3: Dynamic Work-Stealing Allocator
- **Uniform Chunking**: File is partitioned into uniform blocks (e.g., 512 KB, 1 MB).
- **Batch Allocation**: Fast workers claim contiguous chunk runs to reduce HTTP connection establishment overhead.
- **Lagger Bisection**: When unallocated chunks are exhausted, idle workers inspect active peers, find the slowest connection ("lagger") with remaining work, and bisect its assigned range at a chunk boundary. Work is stolen dynamically with zero redundant downloading or write collisions.

### Phase 4: Crash Recovery & `.inlay` Control File
- Binary metadata file `<target>.inlay` mapped directly into process virtual memory (`mmap`).
- Header records file size, chunk size, total chunks, completed count, and 64-bit URL hash.
- Compact bitfield tracks chunk completion (1 bit per chunk: 10 GB file with 512 KB chunks requires only 2.5 KB of metadata).
- Thread-safe atomic bit manipulation (`stdatomic.h`) with asynchronous dirty page syncing (`msync(MS_ASYNC)`).
- Instant pause and crash tolerance without needing hash re-verification. Upon 100% completion, `.inlay` is automatically unlinked.

### Phase 5: CLI Telemetry & POSIX Signal Trapping
- **Signals**: Intercepts `SIGINT` (Ctrl+C) and `SIGTERM` via `sigaction`. Notifies workers to abort transfers, commits memory-mapped state with synchronous `msync()`, and flushes storage with `fsync()`.
- **Atomic Telemetry**: Moving average speed estimation and time remaining (ETA) calculation.
- **ANSI Terminal Interface**: Dynamic in-place progress bar with color cues and non-interactive log fallback when stdout is redirected or piped.

---

## Building and Installing

### Prerequisites
- GCC or Clang (C11 support)
- `make`
- `libcurl` (`pkg-config --libs libcurl`)
- POSIX compliant Linux kernel (v2.6+ for `fallocate` / `posix_fallocate`)

### Build
```bash
make
```

### Run Test Suite
Runs both unit tests (storage, meta, scheduler) and end-to-end downloads against a multi-threaded Python range HTTP server:
```bash
make test
```

### Install
```bash
sudo make install
```

---

## Usage Examples

### 1. Basic Multi-Connection Download (Default: 4 Workers, Dynamic Work-Stealing)
```bash
inlay https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### 2. High-Concurrency Download with Custom Chunk Size
```bash
inlay -n 8 -s 1M -o ubuntu.iso https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### 3. Resume an Interrupted Download
```bash
inlay -c -o ubuntu.iso https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
```

### 4. Static Range Mode (Phase 2)
```bash
inlay -n 4 --static -o output.bin https://example.com/largefile.bin
```

### 5. Quiet / Headless / Scripted Mode
```bash
inlay -q -n 4 -o output.bin https://example.com/largefile.bin
```

---

## CLI Options

### Target & Storage
| Flag | Description | Default |
|------|-------------|---------|
| `-o, --output <PATH>` | Target destination file name or full path | Remote filename |
| `-d, --dir <DIR>` | Target destination folder (auto-created if nonexistent) | Current directory |
| `-c, --continue` | Resume download from `.inlay` control file | Off |
| `-C, --checksum <SPEC>` | Verify cryptographic hash after completion (`<algo>:<hex>` or raw hex) | None |
| `-i, --input-file <FILE>` | Batch download queue: read URLs line-by-line from file | None |
| `--no-fallocate` | Disable upfront disk block pre-allocation | Off |

### Concurrency & Performance
| Flag | Description | Default |
|------|-------------|---------|
| `-n, --connections <N>` | Number of concurrent worker threads (1-64) | `4` |
| `-s, --chunk-size <SIZE>` | Uniform chunk size (e.g. `256K`, `512K`, `1M`, `4M`) | `512K` |
| `--static` | Use static byte partitioning instead of work-stealing | Off (Dynamic) |
| `--force-single` | Force single-stream sequential download | Off |

### Protocols & Network
| Flag | Description | Default |
|------|-------------|---------|
| `--http3` | Enable HTTP/3 (QUIC) with automatic protocol fallback | Off |
| `--http3-only` | Force HTTP/3 (QUIC) without protocol fallback | Off |
| `-t, --timeout <SECS>` | Connection / transfer timeout per request | `30` |
| `-r, --rate-limit <SPEED>` | Maximum bandwidth rate limit (e.g. `500K`, `10M`, `1G`) | Unlimited |
| `--retry <N>` | Max connection retries upon failure | `3` |
| `--retry-delay <SECS>` | Seconds to wait between retries | `2` |
| `-H, --header <HEADER>` | Custom HTTP request header (repeatable) | None |
| `-U, --user-agent <STR>` | Custom HTTP User-Agent string | `inlay/1.0.0` |
| `-k, --insecure` | Skip TLS certificate verification | Off |
| `-4, --ipv4` | Resolve IPv4 addresses only | Off |
| `-6, --ipv6` | Resolve IPv6 addresses only | Off |

### Cloud Storage & AWS SigV4 (AWS S3 & Cloudflare R2)
| Flag | Description | Default |
|------|-------------|---------|
| `--aws-sigv4 [PROVIDER]` | Enable AWS SigV4 signing (`aws:amz:<region>:<service>`) | Auto |
| `--aws-access-key <KEY>` | AWS / S3 access key ID (or env `AWS_ACCESS_KEY_ID`) | From env |
| `--aws-secret-key <KEY>` | AWS / S3 secret access key (or env `AWS_SECRET_ACCESS_KEY`) | From env |
| `--aws-region <REGION>` | AWS region (or env `AWS_REGION`, auto for Cloudflare R2) | `us-east-1` |
| `--aws-token <TOKEN>` | AWS temporary session token (or env `AWS_SESSION_TOKEN`) | None |
| `--s3-endpoint <URL>` | Custom S3 endpoint (e.g. Cloudflare R2, MinIO, Ceph) | None |

### Display & Logging
| Flag | Description | Default |
|------|-------------|---------|
| `-q, --quiet` | Disable live ANSI terminal progress bar | Off |
| `-v, --verbose` | Detailed libcurl & scheduler diagnostic output | Off |
| `--no-color` | Disable ANSI color codes in output | Off |
| `-V, --version` | Display version and build information | |
| `-h, --help` | Display command-line usage summary | |

---

## Advanced Usage

### 1. Checksum Validation (SHA-256, SHA-512, MD5, SHA-1, BLAKE2)
Verify payload authenticity immediately upon completion before unlinking state:
```bash
# Explicit algorithm
inlay -C sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/file.iso

# Auto-detected by hash length (64 hex characters -> SHA-256)
inlay -C e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/file.iso
```

### 2. Batch URL Downloads (`-i, --input-file`)
Download multiple files sequentially from an input file:
```bash
inlay -i urls.txt -d ~/Downloads -n 8
```
`urls.txt` supports comments, blank lines, and per-URL override options:
```text
# Distribution images
https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso
  out=ubuntu.iso
  checksum=sha256:b590e8a71584e27f...

# Object storage assets
s3://ml-models/llama-3-8b.bin
```

### 3. HTTP/3 (QUIC) Transports
Utilize UDP-based multiplexed QUIC connections with zero head-of-line blocking:
```bash
inlay --http3 https://cloudflare-quic.com/test.iso
```

### 4. Direct S3 & Cloudflare R2 SigV4 Downloads
Download directly from private Amazon S3 buckets or Cloudflare R2 without pre-signing URLs:
```bash
# AWS S3 URL syntax (auto-resolves AWS_ACCESS_KEY_ID & AWS_SECRET_ACCESS_KEY from environment)
inlay s3://my-private-bucket/datasets/images.tar.gz

# Cloudflare R2 bucket with custom endpoint
inlay --s3-endpoint https://<account_id>.r2.cloudflarestorage.com s3://models-bucket/weights.safetensors
```

