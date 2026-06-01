#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# start.sh — build the C++ engine and launch the full LOBE stack
#
# What this does:
#   1. Build the C++ engine (lobe, lobe_server, lobe_bench)
#   2. Install Node.js dependencies if needed
#   3. Start the WebSocket feed server
#   4. Open the browser at http://localhost:8080
#
# Usage:
#   chmod +x start.sh
#   ./start.sh
# ─────────────────────────────────────────────────────────────────────────────

set -e  # exit on any error

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build"
FEED_DIR="$ROOT/feed_server"

echo ""
echo "═══════════════════════════════════════"
echo "  LOBE — Build & Start"
echo "═══════════════════════════════════════"

# ── Step 1: Build C++ ─────────────────────────────────────────────────────────
echo ""
echo "[ 1/3 ] Building C++ engine..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
make -j"$(nproc)" 2>&1 | grep -E "Built|error:|warning:" || true

if [ ! -f "$BUILD_DIR/lobe_server" ]; then
  echo "ERROR: build failed — lobe_server not found"
  exit 1
fi

echo "  ✓ lobe         (interactive CLI)"
echo "  ✓ lobe_server  (JSON bridge for Node.js)"
echo "  ✓ lobe_bench   (latency benchmark)"

# ── Step 2: Node.js deps ──────────────────────────────────────────────────────
echo ""
echo "[ 2/3 ] Installing Node.js dependencies..."
cd "$FEED_DIR"

if [ ! -d "node_modules" ]; then
  npm install --silent
  echo "  ✓ npm install done"
else
  echo "  ✓ node_modules already present"
fi

# Compile TypeScript
npm run build -- --silent 2>/dev/null || npx tsc
echo "  ✓ TypeScript compiled"

# ── Step 3: Start server ──────────────────────────────────────────────────────
echo ""
echo "[ 3/3 ] Starting feed server..."
echo ""
echo "  UI  →  http://localhost:8080"
echo "  WS  →  ws://localhost:8080"
echo ""
echo "  Press Ctrl+C to stop."
echo ""

# Try to open browser (works on WSL with Windows browser)
if command -v explorer.exe &> /dev/null; then
  explorer.exe "http://localhost:8080" 2>/dev/null &
elif command -v xdg-open &> /dev/null; then
  xdg-open "http://localhost:8080" 2>/dev/null &
fi

node dist/server.js
