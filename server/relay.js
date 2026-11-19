import { randomUUID } from "node:crypto";
import { WebSocket, WebSocketServer } from "ws";
import { roomStore } from "./rooms.js";

const roomCode = /^[a-f0-9]{12}$/;
const poseBounds = [[-32, 928], [-40, 130], [-32, 928], [-3.15, 3.15], [0, 1e8], [0, 1.01]];

function validPose(pose) {
  return Array.isArray(pose) && pose.length === poseBounds.length &&
    pose.every((value, i) => Number.isFinite(value) && value >= poseBounds[i][0] && value <= poseBounds[i][1]);
}

function allowedOrigin(request) {
  const origin = request.headers.origin;
  if (!origin) return false;
  try {
    const host = new URL(origin).host;
    return host === request.headers.host || host === request.headers["x-forwarded-host"];
  } catch {
    return false;
  }
}

export function attachRelay(server, redisUrl = process.env.REDIS_URL, { maxConnections = 256 } = {}) {
  const wss = new WebSocketServer({ noServer: true, maxPayload: 256, perMessageDeflate: false });
  const rooms = new Map();
  let storePromise;
  const send = (socket, message) => {
    if (socket.readyState === WebSocket.OPEN && socket.bufferedAmount < 64 * 1024)
      socket.send(JSON.stringify(message));
  };
  const deliver = (room, message) => {
    for (const [id, socket] of rooms.get(room) ?? [])
      if (id !== message.id) send(socket, message);
  };
  const store = () => storePromise ??= roomStore(redisUrl, deliver).catch((error) => {
    storePromise = undefined;
    throw error;
  });

  server.on("upgrade", (request, socket, head) => {
    const url = new URL(request.url, "http://localhost");
    const room = url.searchParams.get("room");
    if (url.pathname !== "/api/ws" || !roomCode.test(room ?? "") || !allowedOrigin(request)) {
      socket.end("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
      return;
    }
    if (wss.clients.size >= maxConnections) {
      socket.end("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
      return;
    }
    wss.handleUpgrade(request, socket, head, (ws) => void connect(ws, room));
  });

  async function connect(socket, room) {
    const id = randomUUID();
    socket.on("error", () => socket.terminate());
    try {
      const membership = await store();
      if (socket.readyState !== WebSocket.OPEN) return;
      if (!await membership.reserve(room, id)) {
        socket.close(4003, "Room is full");
        return;
      }
      if (socket.readyState !== WebSocket.OPEN) {
        await membership.release(room, id);
        return;
      }
      const peers = rooms.get(room) ?? new Map();
      rooms.set(room, peers);
      peers.set(id, socket);

      let answered = true;
      let lastPose = -Infinity;
      let publishing = false;
      socket.on("pong", () => { answered = true; });
      const ping = setInterval(async () => {
        if (!answered) return socket.terminate();
        answered = false;
        socket.ping();
        try {
          if (!await membership.refresh(room, id)) socket.close(1013, "Room lease expired");
        } catch {
          socket.close(1013, "Relay unavailable");
        }
      }, 5000);
      ping.unref();
      socket.once("close", async () => {
        clearInterval(ping);
        peers.delete(id);
        if (!peers.size) rooms.delete(room);
        try {
          await membership.release(room, id);
          await membership.publish(room, { type: "left", id });
        } catch {
          // The Redis lease and client snapshot timeout clear stale presence.
        }
      });
      socket.on("message", async (data, binary) => {
        const now = performance.now();
        if (binary || publishing || now-lastPose < 38) return;
        lastPose = now;
        let pose;
        try { pose = JSON.parse(data.toString()); } catch { return; }
        if (!validPose(pose)) return;
        publishing = true;
        try {
          await membership.publish(room, { type: "pose", id, pose });
        } catch {
          socket.close(1013, "Relay unavailable");
        } finally {
          publishing = false;
        }
      });
      send(socket, { type: "joined" });
    } catch {
      socket.close(1013, "Relay unavailable");
    }
  }
  return wss;
}
