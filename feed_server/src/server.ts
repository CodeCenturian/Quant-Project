/**
 * server.ts — LOBE WebSocket Bridge
 *
 * Architecture:
 *
 *   Browser clients
 *      │  WebSocket (port 8080)
 *      ▼
 *   This Node.js server
 *      │  stdin/stdout pipe (newline-delimited JSON)
 *      ▼
 *   C++ lobe_server process
 *
 * Flow:
 *   - Browser sends an order command  →  forwarded to C++ stdin
 *   - C++ emits a trade/ack/book JSON →  broadcast to all browser clients
 */

import * as http       from "http";
import * as fs         from "fs";
import * as path       from "path";
import { spawn, ChildProcessWithoutNullStreams } from "child_process";
import { WebSocketServer, WebSocket }            from "ws";

// ── Config ────────────────────────────────────────────────────────────────────
const PORT         = 8080;
const ENGINE_PATH  = path.resolve(__dirname, "../../build/lobe_server");
const PUBLIC_DIR   = path.resolve(__dirname, "../public");

// ── Spawn the C++ engine ──────────────────────────────────────────────────────
function spawnEngine(): ChildProcessWithoutNullStreams {
  console.log(`[engine] Spawning: ${ENGINE_PATH}`);
  const proc = spawn(ENGINE_PATH, [], { stdio: ["pipe", "pipe", "inherit"] });

  proc.on("error", (err) => {
    console.error("[engine] Failed to start:", err.message);
    console.error("[engine] Did you run: cd build && cmake .. && make ?");
    process.exit(1);
  });

  proc.on("exit", (code) => {
    console.log(`[engine] Process exited with code ${code}`);
    process.exit(code ?? 1);
  });

  return proc;
}

// ── HTTP server — serves the index.html UI ────────────────────────────────────
const httpServer = http.createServer((req, res) => {
  const filePath = path.join(PUBLIC_DIR, "index.html");
  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404);
      res.end("index.html not found");
      return;
    }
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
  });
});

// ── WebSocket server ─────────────────────────────────────────────────────────
const wss = new WebSocketServer({ server: httpServer });
const clients = new Set<WebSocket>();

function broadcast(msg: string): void {
  for (const client of clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(msg);
    }
  }
}

// ── Wire everything together ──────────────────────────────────────────────────
const engine = spawnEngine();

// C++ stdout → parse lines → broadcast to all connected browser clients
let buffer = "";
engine.stdout.on("data", (chunk: Buffer) => {
  buffer += chunk.toString();
  const lines = buffer.split("\n");
  buffer = lines.pop() ?? "";   // keep incomplete last line for next chunk

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    broadcast(trimmed);   // send raw JSON line to all browser tabs
  }
});

// Browser WebSocket → forward order commands to C++ stdin
wss.on("connection", (ws: WebSocket, req) => {
  const ip = req.socket.remoteAddress;
  console.log(`[ws] Client connected: ${ip}  (total: ${clients.size + 1})`);
  clients.add(ws);

  ws.on("message", (data) => {
    const msg = data.toString().trim();
    // Basic validation: must be a JSON object
    if (!msg.startsWith("{")) {
      ws.send(JSON.stringify({ type: "error", msg: "Expected JSON object" }));
      return;
    }
    // Forward to C++ engine stdin (add newline so engine reads a full line)
    engine.stdin.write(msg + "\n");
  });

  ws.on("close", () => {
    clients.delete(ws);
    console.log(`[ws] Client disconnected  (total: ${clients.size})`);
  });

  ws.on("error", (err) => console.error("[ws] Client error:", err.message));

  // Send a welcome message so the UI knows it's connected
  ws.send(JSON.stringify({ type: "connected", msg: "LOBE engine ready" }));
});

// ── Start listening ───────────────────────────────────────────────────────────
httpServer.listen(PORT, () => {
  console.log(`\n LOBE feed server running`);
  console.log(` Open:  http://localhost:${PORT}`);
  console.log(` WS:    ws://localhost:${PORT}\n`);
});
