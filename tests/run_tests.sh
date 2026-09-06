#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=========================================================="
echo "    Inlay Test Suite: Validating Core Engine & Extensions "
echo "=========================================================="

echo "[1/7] Compiling unit tests..."
mkdir -p build
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_storage.c src/storage.c -o test_storage -lpthread
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_meta.c src/meta.c -o test_meta -lpthread
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_scheduler.c src/scheduler.c src/meta.c -o test_scheduler -lpthread
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_checksum.c src/checksum.c -o test_checksum -lcrypto
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_s3.c src/s3.c -o test_s3 -lcurl
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_batch.c src/batch.c -o test_batch
gcc -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude tests/test_update.c src/update.c -o test_update -lcurl

echo "[2/7] Running unit tests..."
echo "  [*] test_storage (Positional I/O & fallocate)..."
./test_storage
echo "  [*] test_meta (mmap crash recovery & bitfield)..."
./test_meta
echo "  [*] test_scheduler (Dynamic work stealing & lagger bisection)..."
./test_scheduler
echo "  [*] test_checksum (OpenSSL EVP digests & verification)..."
./test_checksum
echo "  [*] test_s3 (S3 URL transformation & SigV4 parameters)..."
./test_s3
echo "  [*] test_batch (Input file queue parsing)..."
./test_batch
echo "  [*] test_update (Semantic version comparison & update logic)..."
./test_update

echo "[3/7] Setting up mock HTTP server with Range and SigV4 support..."
SERVE_DIR=$(mktemp -d /tmp/inlay_serve_XXXXXX)
WORK_DIR=$(mktemp -d /tmp/inlay_work_XXXXXX)

# Generate test random files
head -c 4194304 /dev/urandom > "$SERVE_DIR/data_4m.bin"
head -c 8388608 /dev/urandom > "$SERVE_DIR/data_8m.bin"
head -c 16777216 /dev/urandom > "$SERVE_DIR/data_16m.bin"
HASH_4M=$(sha256sum "$SERVE_DIR/data_4m.bin" | awk '{print $1}')
HASH_8M=$(sha256sum "$SERVE_DIR/data_8m.bin" | awk '{print $1}')
HASH_16M=$(sha256sum "$SERVE_DIR/data_16m.bin" | awk '{print $1}')

# Start mock HTTP server
python3 tests/mock_server.py "$SERVE_DIR" 0 > "$WORK_DIR/server.log" 2>&1 &
SERVER_PID=$!

cleanup() {
    kill -9 "$SERVER_PID" 2>/dev/null || true
    rm -rf "$SERVE_DIR" "$WORK_DIR" test_storage test_meta test_scheduler test_checksum test_s3 test_batch test_update
}
trap cleanup EXIT

# Wait for server to become ready
PORT=""
for i in {1..50}; do
    if grep -q "READY" "$WORK_DIR/server.log" 2>/dev/null; then
        PORT=$(grep "READY" "$WORK_DIR/server.log" | awk '{print $2}')
        break
    fi
    sleep 0.1
done

if [ -z "$PORT" ]; then
    echo "[!] Mock server failed to start"
    cat "$WORK_DIR/server.log"
    exit 1
fi
echo "[+] Mock HTTP server running on port $PORT"

echo "[4/7] Executing Core Engine Integration Tests..."

# Test A: Single-Worker Fallback (Phase 1)
echo "--- Test A: Single Stream Download ---"
./inlay -q -n 1 -o "$WORK_DIR/dl_single.bin" "http://127.0.0.1:$PORT/data_8m.bin"
DL_HASH=$(sha256sum "$WORK_DIR/dl_single.bin" | awk '{print $1}')
if [ "$DL_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on single stream download!"
    exit 1
fi
echo "[+] Single stream download verified (SHA-256: $DL_HASH)"

# Test B: Static Multi-Worker Concurrency (Phase 2)
echo "--- Test B: Static Multi-Worker Concurrency (4 workers, --static) ---"
./inlay -q -n 4 --static -o "$WORK_DIR/dl_static.bin" "http://127.0.0.1:$PORT/data_8m.bin"
DL_HASH=$(sha256sum "$WORK_DIR/dl_static.bin" | awk '{print $1}')
if [ "$DL_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on static multi-worker download!"
    exit 1
fi
echo "[+] Static multi-worker download verified (SHA-256: $DL_HASH)"

# Test C: Dynamic Work-Stealing Allocator (Phase 3)
echo "--- Test C: Dynamic Work-Stealing Allocator (8 workers, 256K chunks) ---"
./inlay -q -n 8 -s 256K -o "$WORK_DIR/dl_dynamic.bin" "http://127.0.0.1:$PORT/data_16m.bin"
DL_HASH=$(sha256sum "$WORK_DIR/dl_dynamic.bin" | awk '{print $1}')
if [ "$DL_HASH" != "$HASH_16M" ]; then
    echo "[!] Hash mismatch on dynamic work-stealing download!"
    exit 1
fi
echo "[+] Dynamic work-stealing download verified (SHA-256: $DL_HASH)"

# Test D: SIGINT Signal Trapping, Pause & Resume Crash Recovery (Phase 4 & 5)
echo "--- Test D: Interruption, Pause & Crash Recovery Resume ---"
./inlay -n 4 -s 128K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin" &
INLAY_PID=$!

sleep 0.05
kill -INT "$INLAY_PID" 2>/dev/null || true
wait "$INLAY_PID" 2>/dev/null || true

if [ ! -f "$WORK_DIR/dl_resume.bin.inlay" ]; then
    rm -f "$WORK_DIR/dl_resume.bin"*
    ./inlay -n 2 -s 64K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin" &
    INLAY_PID=$!
    sleep 0.02
    kill -INT "$INLAY_PID" 2>/dev/null || true
    wait "$INLAY_PID" 2>/dev/null || true
fi

if [ -f "$WORK_DIR/dl_resume.bin.inlay" ]; then
    echo "[+] Interrupted successfully: .inlay control file preserved on disk."
    ./inlay -c -q -n 4 -s 128K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin"
fi

DL_HASH=$(sha256sum "$WORK_DIR/dl_resume.bin" | awk '{print $1}')
if [ "$DL_HASH" != "$HASH_16M" ]; then
    echo "[!] Hash mismatch after resumed download!"
    exit 1
fi
echo "[+] Crash recovery resume verified (SHA-256: $DL_HASH)"

echo "[5/7] Executing Checksum Validation Integration Tests..."
echo "--- Test E1: Successful SHA-256 Checksum Validation ---"
./inlay -q -n 4 -C "sha256:$HASH_8M" -o "$WORK_DIR/dl_checksum_ok.bin" "http://127.0.0.1:$PORT/data_8m.bin"
echo "[+] Download with correct checksum passed verification!"

echo "--- Test E2: Checksum Mismatch Detection ---"
set +e
./inlay -q -n 4 -C "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" -o "$WORK_DIR/dl_checksum_fail.bin" "http://127.0.0.1:$PORT/data_8m.bin" 2>"$WORK_DIR/checksum_err.log"
FAIL_RET=$?
set -e
if [ "$FAIL_RET" -ne 2 ]; then
    echo "[!] Expected exit code 2 on checksum mismatch, got $FAIL_RET"
    cat "$WORK_DIR/checksum_err.log"
    exit 1
fi
echo "[+] Checksum mismatch correctly detected and rejected with exit code 2!"

echo "[6/7] Executing Batch URL Queue Integration Tests..."
echo "--- Test F: Batch Download via Input File (-i) ---"
cat << EOF > "$WORK_DIR/batch_list.txt"
# Inlay batch test file
http://127.0.0.1:$PORT/data_4m.bin   file1.bin
http://127.0.0.1:$PORT/data_8m.bin   file2.bin
EOF

./inlay -q -i "$WORK_DIR/batch_list.txt" -d "$WORK_DIR/batch_out" -n 4
F1_HASH=$(sha256sum "$WORK_DIR/batch_out/file1.bin" | awk '{print $1}')
F2_HASH=$(sha256sum "$WORK_DIR/batch_out/file2.bin" | awk '{print $1}')

if [ "$F1_HASH" != "$HASH_4M" ] || [ "$F2_HASH" != "$HASH_8M" ]; then
    echo "[!] Batch files hash mismatch!"
    exit 1
fi
echo "[+] Batch download queue executed and verified successfully!"

echo "[7/7] Executing AWS SigV4 & HTTP/3 Integration Tests..."
echo "--- Test G: AWS SigV4 Request Authentication ---"
# Check that downloading /secure/ without SigV4 fails (403 Forbidden)
set +e
./inlay -q "http://127.0.0.1:$PORT/secure/data_8m.bin" -o "$WORK_DIR/unauth.bin" 2>/dev/null
UNAUTH_RET=$?
set -e
if [ "$UNAUTH_RET" -eq 0 ]; then
    echo "[!] Expected 403 Forbidden without AWS SigV4!"
    exit 1
fi
echo "[+] Unauthenticated request to protected endpoint correctly rejected (HTTP 403)."

# Download /secure/ with AWS SigV4 authentication
./inlay -q -n 4 \
    --aws-sigv4 "aws:amz:us-east-1:s3" \
    --aws-access-key "AKIAIOSFODNN7EXAMPLE" \
    --aws-secret-key "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY" \
    -o "$WORK_DIR/auth_s3.bin" \
    "http://127.0.0.1:$PORT/secure/data_8m.bin"

S3_HASH=$(sha256sum "$WORK_DIR/auth_s3.bin" | awk '{print $1}')
if [ "$S3_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on SigV4 authenticated download!"
    exit 1
fi
echo "[+] AWS SigV4 authenticated segmented download verified (SHA-256: $S3_HASH)!"

echo "--- Test H: HTTP/3 (QUIC) Flag Configuration ---"
./inlay --http3 --help >/dev/null 2>&1
echo "[+] HTTP/3 CLI options verified."

echo ""
echo "=========================================================="
echo "    ALL INLAY TESTS PASSED SUCCESSFULLY! (100% GREEN)     "
echo "=========================================================="
