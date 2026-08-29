#!/usr/bin/env bash
# tests/integration_tests.sh
# 5 curl-based integration tests proving routing behavior (Linux version)

set -euo pipefail

PORT=28080
URL="http://127.0.0.1:$PORT"

NO_BUILD=0
for arg in "$@"; do
    if [ "$arg" = "--no-build" ]; then
        NO_BUILD=1
    fi
done

if [ "$NO_BUILD" -eq 0 ]; then
    echo "=== Building executable ==="
    make clean
    make all
fi

echo "=== Launching local server on port $PORT ==="
# Spawn the server in the background
./mdview -p "$PORT" &
SERVER_PID=$!

# Give the server a moment to bind and listen
sleep 1

FAILED=0

# Test 1: GET / (serve index.html)
echo -n "Test 1: GET / ... "
RESP=$(curl -s -i "$URL/")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "SoullessSages"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 2: GET /static/styles.css
echo -n "Test 2: GET /static/styles.css ... "
RESP=$(curl -s -i "$URL/static/styles.css")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "text/css"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 3: POST /render (Success)
echo -n "Test 3: POST /render (Success) ... "
RESP=$(curl -s -i -X POST -H "Content-Type: application/json" -d '{"md":"# Hello"}' "$URL/render")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "Hello"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 4: POST /serialize (Success)
echo -n "Test 4: POST /serialize (Success) ... "
RESP=$(curl -s -i -X POST -H "Content-Type: application/json" -d '{"html":"<p>Stub</p>"}' "$URL/serialize")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "Stub"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 5: GET /nonexistent (404)
echo -n "Test 5: GET /nonexistent (404) ... "
RESP=$(curl -s -i "$URL/nonexistent")
if echo "$RESP" | grep -q "404 Not Found"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 6: POST /render (413 Request Entity Too Large)
echo -n "Test 6: POST /render (413 Payload Too Large) ... "
RESP=$(curl -s -i -X POST -H "Content-Type: application/json" -H "Content-Length: 70000" -d "" "$URL/render" 2>/dev/null || true)
if echo "$RESP" | grep -q "413 Request Entity Too Large"; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 7: GET /file (Retrieve initial file payload)
echo -n "Test 7: GET /file ... "
RESP=$(curl -s -i "$URL/file")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "\"content\""; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

# Test 8: POST /save (Save content)
echo -n "Test 8: POST /save ... "
RESP=$(curl -s -i -X POST -H "Content-Type: application/json" -d '{"content":"# Saved Title"}' "$URL/save")
if echo "$RESP" | grep -q "200 OK" && echo "$RESP" | grep -q "\"status\":\"ok\""; then
    echo "PASSED"
else
    echo "FAILED"
    FAILED=$((FAILED+1))
fi

echo "=== Tearing down server ==="
curl -s -X POST "$URL/shutdown" >/dev/null 2>&1 || true
sleep 1
if kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill -9 "$SERVER_PID" || true
fi

if [ "$FAILED" -eq 0 ]; then
    echo "All integration tests passed successfully!"
    exit 0
else
    echo "$FAILED integration tests failed."
    exit 1
fi
