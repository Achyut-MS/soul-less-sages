#!/bin/bash
cd "$(dirname "$0")" || exit 1

echo "Building project..."
make
if [ $? -ne 0 ]; then
  exit 1
fi

echo "Opening browser..."
if command -v xdg-open >/dev/null; then
  xdg-open http://localhost:8080 &
elif command -v open >/dev/null; then
  open http://localhost:8080 & # For macOS
fi

echo "Starting server (Press Ctrl+C to stop)..."
./mdview "$@"
