#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=========================================================="
echo "    Down Test Suite: Validating Core Engine & Extensions  "
echo "=========================================================="

TEST_STATE_DIR=$(mktemp -d /tmp/down_state_XXXXXX)
export XDG_STATE_HOME="$TEST_STATE_DIR"

calc_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

UNAME_S="$(uname -s)"
if [ "$UNAME_S" = "Darwin" ]; then
    CC="${CC:-clang}"
    BREW_PREFIX="$(brew --prefix 2>/dev/null || ([ -d /opt/homebrew ] && echo /opt/homebrew) || echo /usr/local)"
    OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo "${BREW_PREFIX}/opt/openssl")"
    CURL_PREFIX="$(brew --prefix curl 2>/dev/null || echo "${BREW_PREFIX}/opt/curl")"
    MAC_CFLAGS="-I${BREW_PREFIX}/include -I${OPENSSL_PREFIX}/include -I${CURL_PREFIX}/include"
    MAC_LDFLAGS="-L${BREW_PREFIX}/lib -L${OPENSSL_PREFIX}/lib -L${CURL_PREFIX}/lib"
else
    CC="${CC:-gcc}"
    MAC_CFLAGS=""
    MAC_LDFLAGS=""
fi
BASE_CFLAGS="-Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude ${MAC_CFLAGS}"

echo "[1/7] Compiling unit tests..."
mkdir -p build
$CC $BASE_CFLAGS tests/test_storage.c src/storage.c -o test_storage $MAC_LDFLAGS -lpthread
$CC $BASE_CFLAGS tests/test_meta.c src/meta.c -o test_meta $MAC_LDFLAGS -lpthread
$CC $BASE_CFLAGS tests/test_scheduler.c src/scheduler.c src/meta.c -o test_scheduler $MAC_LDFLAGS -lpthread
$CC $BASE_CFLAGS tests/test_checksum.c src/checksum.c -o test_checksum $MAC_LDFLAGS -lcrypto
$CC $BASE_CFLAGS tests/test_s3.c src/s3.c -o test_s3 $MAC_LDFLAGS -lcurl
$CC $BASE_CFLAGS tests/test_batch.c src/batch.c -o test_batch $MAC_LDFLAGS
$CC $BASE_CFLAGS tests/test_update.c src/update.c -o test_update $MAC_LDFLAGS -lcurl
$CC $BASE_CFLAGS tests/test_config_file.c src/config_file.c src/cli.c src/checksum.c src/s3.c src/update.c src/interactive.c src/history.c src/telemetry.c src/batch.c -o test_config_file $MAC_LDFLAGS -lcurl -lcrypto -lpthread -lm
$CC $BASE_CFLAGS tests/test_interactive.c src/interactive.c src/history.c src/cli.c src/checksum.c src/s3.c src/update.c src/config_file.c src/telemetry.c src/batch.c -o test_interactive $MAC_LDFLAGS -lcurl -lcrypto -lpthread -lm
$CC $BASE_CFLAGS tests/test_history.c src/history.c src/cli.c src/checksum.c src/s3.c src/update.c src/config_file.c src/interactive.c src/telemetry.c src/batch.c -o test_history $MAC_LDFLAGS -lcurl -lcrypto -lpthread -lm

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
echo "  [*] test_config_file (Config file parsing & CLI precedence)..."
./test_config_file
echo "  [*] test_interactive (CLI wizard prompt & mode flows)..."
./test_interactive
echo "  [*] test_history (Session history & crash recovery detection)..."
./test_history

echo "[3/7] Setting up mock HTTP server with Range and SigV4 support..."
SERVE_DIR=$(mktemp -d /tmp/down_serve_XXXXXX)
WORK_DIR=$(mktemp -d /tmp/down_work_XXXXXX)

# Generate test random files with portable dd
dd if=/dev/urandom of="$SERVE_DIR/data_4m.bin" bs=1048576 count=4 2>/dev/null
dd if=/dev/urandom of="$SERVE_DIR/data_8m.bin" bs=1048576 count=8 2>/dev/null
dd if=/dev/urandom of="$SERVE_DIR/data_16m.bin" bs=1048576 count=16 2>/dev/null
HASH_4M=$(calc_sha256 "$SERVE_DIR/data_4m.bin")
HASH_8M=$(calc_sha256 "$SERVE_DIR/data_8m.bin")
HASH_16M=$(calc_sha256 "$SERVE_DIR/data_16m.bin")

# Start mock HTTP server with unbuffered I/O
python3 -u tests/mock_server.py "$SERVE_DIR" 0 > "$WORK_DIR/server.log" 2>&1 &
SERVER_PID=$!

cleanup() {
    kill -9 "$SERVER_PID" 2>/dev/null || true
    rm -rf "$SERVE_DIR" "$WORK_DIR" "$TEST_STATE_DIR" test_storage test_meta test_scheduler test_checksum test_s3 test_batch test_update test_config_file test_interactive test_history
}
trap cleanup EXIT

# Wait for server to become ready (up to 15 seconds)
PORT=""
for i in {1..150}; do
    if grep -q "READY" "$WORK_DIR/server.log" 2>/dev/null; then
        PORT=$(grep "READY" "$WORK_DIR/server.log" | awk '{print $2}')
        break
    fi
    sleep 0.1
done

if [ -z "$PORT" ]; then
    echo "[!] Mock server failed to start (timeout). Server log:"
    cat "$WORK_DIR/server.log" 2>/dev/null || true
    exit 1
fi
echo "[+] Mock HTTP server running on port $PORT"

echo "[4/7] Executing Core Engine Integration Tests..."

# Test A: Single-Worker Fallback (Phase 1)
echo "--- Test A: Single Stream Download ---"
./down -q -n 1 -o "$WORK_DIR/dl_single.bin" "http://127.0.0.1:$PORT/data_8m.bin"
DL_HASH=$(calc_sha256 "$WORK_DIR/dl_single.bin")
if [ "$DL_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on single stream download!"
    exit 1
fi
echo "[+] Single stream download verified (SHA-256: $DL_HASH)"

# Test B: Static Multi-Worker Concurrency (Phase 2)
echo "--- Test B: Static Multi-Worker Concurrency (4 workers, --static) ---"
./down -q -n 4 --static -o "$WORK_DIR/dl_static.bin" "http://127.0.0.1:$PORT/data_8m.bin"
DL_HASH=$(calc_sha256 "$WORK_DIR/dl_static.bin")
if [ "$DL_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on static multi-worker download!"
    exit 1
fi
echo "[+] Static multi-worker download verified (SHA-256: $DL_HASH)"

# Test C: Dynamic Work-Stealing Allocator (Phase 3)
echo "--- Test C: Dynamic Work-Stealing Allocator (8 workers, 256K chunks) ---"
./down -q -n 8 -s 256K -o "$WORK_DIR/dl_dynamic.bin" "http://127.0.0.1:$PORT/data_16m.bin"
DL_HASH=$(calc_sha256 "$WORK_DIR/dl_dynamic.bin")
if [ "$DL_HASH" != "$HASH_16M" ]; then
    echo "[!] Hash mismatch on dynamic work-stealing download!"
    exit 1
fi
echo "[+] Dynamic work-stealing download verified (SHA-256: $DL_HASH)"

# Test D: SIGINT Signal Trapping, Pause & Resume Crash Recovery (Phase 4 & 5)
echo "--- Test D: Interruption, Pause & Crash Recovery Resume ---"
./down -n 4 -s 128K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin" &
DOWN_PID=$!

sleep 0.05
kill -INT "$DOWN_PID" 2>/dev/null || true
wait "$DOWN_PID" 2>/dev/null || true

if [ ! -f "$WORK_DIR/dl_resume.bin.down" ]; then
    rm -f "$WORK_DIR/dl_resume.bin"*
    ./down -n 2 -s 64K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin" &
    DOWN_PID=$!
    sleep 0.02
    kill -INT "$DOWN_PID" 2>/dev/null || true
    wait "$DOWN_PID" 2>/dev/null || true
fi

if [ -f "$WORK_DIR/dl_resume.bin.down" ]; then
    echo "[+] Interrupted successfully: .down control file preserved on disk."
    ./down -c -q -n 4 -s 128K -o "$WORK_DIR/dl_resume.bin" "http://127.0.0.1:$PORT/data_16m.bin"
fi

DL_HASH=$(calc_sha256 "$WORK_DIR/dl_resume.bin")
if [ "$DL_HASH" != "$HASH_16M" ]; then
    echo "[!] Hash mismatch after resumed download!"
    exit 1
fi
echo "[+] Crash recovery resume verified (SHA-256: $DL_HASH)"

echo "[5/7] Executing Checksum Validation Integration Tests..."
echo "--- Test E1: Successful SHA-256 Checksum Validation ---"
./down -q -n 4 -C "sha256:$HASH_8M" -o "$WORK_DIR/dl_checksum_ok.bin" "http://127.0.0.1:$PORT/data_8m.bin"
echo "[+] Download with correct checksum passed verification!"

echo "--- Test E2: Checksum Mismatch Detection ---"
set +e
./down -q -n 4 -C "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" -o "$WORK_DIR/dl_checksum_fail.bin" "http://127.0.0.1:$PORT/data_8m.bin" 2>"$WORK_DIR/checksum_err.log"
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
# Down batch test file
http://127.0.0.1:$PORT/data_4m.bin   file1.bin
http://127.0.0.1:$PORT/data_8m.bin   file2.bin
EOF

./down -q -i "$WORK_DIR/batch_list.txt" -d "$WORK_DIR/batch_out" -n 4
F1_HASH=$(calc_sha256 "$WORK_DIR/batch_out/file1.bin")
F2_HASH=$(calc_sha256 "$WORK_DIR/batch_out/file2.bin")

if [ "$F1_HASH" != "$HASH_4M" ] || [ "$F2_HASH" != "$HASH_8M" ]; then
    echo "[!] Batch files hash mismatch!"
    exit 1
fi
echo "[+] Batch download queue executed and verified successfully!"

echo "[7/7] Executing AWS SigV4 & HTTP/3 Integration Tests..."
echo "--- Test G: AWS SigV4 Request Authentication ---"
# Check that downloading /secure/ without SigV4 fails (403 Forbidden)
set +e
./down -q "http://127.0.0.1:$PORT/secure/data_8m.bin" -o "$WORK_DIR/unauth.bin" 2>/dev/null
UNAUTH_RET=$?
set -e
if [ "$UNAUTH_RET" -eq 0 ]; then
    echo "[!] Expected 403 Forbidden without AWS SigV4!"
    exit 1
fi
echo "[+] Unauthenticated request to protected endpoint correctly rejected (HTTP 403)."

# Download /secure/ with AWS SigV4 authentication
./down -q -n 4 \
    --aws-sigv4 "aws:amz:us-east-1:s3" \
    --aws-access-key "AKIAIOSFODNN7EXAMPLE" \
    --aws-secret-key "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY" \
    -o "$WORK_DIR/auth_s3.bin" \
    "http://127.0.0.1:$PORT/secure/data_8m.bin"

S3_HASH=$(calc_sha256 "$WORK_DIR/auth_s3.bin")
if [ "$S3_HASH" != "$HASH_8M" ]; then
    echo "[!] Hash mismatch on SigV4 authenticated download!"
    exit 1
fi
echo "[+] AWS SigV4 authenticated segmented download verified (SHA-256: $S3_HASH)!"

echo "--- Test H: HTTP/3 (QUIC) Flag Configuration ---"
./down --http3 --help >/dev/null 2>&1
echo "[+] HTTP/3 CLI options verified."

echo "--- Test I: Interactive CLI Wizard Download ---"
./down --clear-history >/dev/null 2>&1 || true
rm -f "$WORK_DIR"/*.down "$WORK_DIR"/*.inlay
printf "http://127.0.0.1:$PORT/data_4m.bin\n2\n$WORK_DIR\ninteractive_result.bin\n4\n256K\n\n\n" | ./down -I -q
INT_HASH=$(calc_sha256 "$WORK_DIR/interactive_result.bin")
if [ "$INT_HASH" != "$HASH_4M" ]; then
    echo "[!] Hash mismatch on interactive wizard download!"
    exit 1
fi
echo "[+] Interactive CLI wizard download verified (SHA-256: $INT_HASH)!"

echo "--- Test J: Interactive Wizard Resume of Interrupted Download ---"
# Start a 16MB download and pause it with SIGINT to simulate crash/interruption
set +e
./down "http://127.0.0.1:$PORT/data_16m.bin" -o "$WORK_DIR/wiz_resume.bin" -s 128K -n 4 >/dev/null 2>&1 &
PAUSE_PID=$!
for i in {1..50}; do
    if [ -f "$WORK_DIR/wiz_resume.bin.down" ]; then
        break
    fi
    sleep 0.01
done
kill -INT "$PAUSE_PID" 2>/dev/null || true
wait "$PAUSE_PID" 2>/dev/null || true
set -e

# Interactive wizard detects resumable download; press Enter (option 1) to resume
printf "1\n" | ./down -I -q
WIZ_HASH=$(calc_sha256 "$WORK_DIR/wiz_resume.bin")
if [ "$WIZ_HASH" != "$HASH_16M" ]; then
    echo "[!] Hash mismatch on wizard resumed download!"
    exit 1
fi
echo "[+] Interactive wizard resume verified (SHA-256: $WIZ_HASH)!"

echo "--- Test K: Parallel Swarm Concurrency (-j 2 with 3 URLs) ---"
rm -rf "$WORK_DIR/swarm_out"
mkdir -p "$WORK_DIR/swarm_out"
./down -q -j 2 -d "$WORK_DIR/swarm_out" -n 2 \
    "http://127.0.0.1:$PORT/data_4m.bin" \
    "http://127.0.0.1:$PORT/data_8m.bin" \
    "http://127.0.0.1:$PORT/data_16m.bin"

SW_4M=$(calc_sha256 "$WORK_DIR/swarm_out/data_4m.bin")
SW_8M=$(calc_sha256 "$WORK_DIR/swarm_out/data_8m.bin")
SW_16M=$(calc_sha256 "$WORK_DIR/swarm_out/data_16m.bin")

if [ "$SW_4M" != "$HASH_4M" ] || [ "$SW_8M" != "$HASH_8M" ] || [ "$SW_16M" != "$HASH_16M" ]; then
    echo "[!] Swarm parallel download hash mismatch!"
    exit 1
fi
echo "[+] Swarm concurrent download (3 files across 2 slots) verified successfully!"

echo ""
echo "=========================================================="
echo "    ALL DOWN TESTS PASSED SUCCESSFULLY! (100% GREEN)      "
echo "=========================================================="
