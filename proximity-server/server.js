'use strict';

const { WebSocketServer } = require('ws');
const { distance3D, computeGain } = require('./proximity');

const PORT = process.env.PORT || 8080;
// Set on the VPS so randoms who guess a room name can't join it. Unset (local dev) = no check.
const ROOM_SECRET = process.env.ROOM_SECRET || null;
const MAX_ID_LENGTH = 64;

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function isValidIdString(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= MAX_ID_LENGTH;
}

// roomId -> Map<peerId, { ws, x, y, z }>
const rooms = new Map();

function getRoom(roomId) {
  let room = rooms.get(roomId);
  if (!room) {
    room = new Map();
    rooms.set(roomId, room);
  }
  return room;
}

function send(ws, message) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function broadcast(room, message, exceptWs) {
  for (const peer of room.values()) {
    if (peer.ws !== exceptWs) {
      send(peer.ws, message);
    }
  }
}

// Recomputes every pairwise distance in the room and sends each peer its own
// volume map for the others. The proximity "game logic" lives here so every
// listener agrees on the same gain values instead of each client computing
// its own (and potentially disagreeing, or being tampered with client-side).
function broadcastVolumes(room) {
  const peers = [...room.entries()];
  for (const [selfId, self] of peers) {
    if (self.x === undefined) continue; // hasn't sent a position yet
    const volumes = {};
    for (const [otherId, other] of peers) {
      if (otherId === selfId || other.x === undefined) continue;
      volumes[otherId] = computeGain(distance3D(self, other));
    }
    send(self.ws, { type: 'volumes', volumes });
  }
}

// maxPayload caps a single message well above any real SDP/ICE payload we'd ever
// send, but far below what's useful for a memory-abuse attempt - ws closes the
// connection with code 1009 if a client sends something bigger.
const wss = new WebSocketServer({ port: PORT, maxPayload: 16 * 1024 });

wss.on('connection', (ws) => {
  let roomId = null;
  let peerId = null;

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      return;
    }
    if (typeof msg !== 'object' || msg === null) return;

    if (msg.type === 'join') {
      if (ROOM_SECRET && msg.secret !== ROOM_SECRET) {
        console.log('[join] rejected: missing/incorrect secret');
        ws.close(4001, 'invalid secret');
        return;
      }
      if (msg.room !== undefined && !isValidIdString(msg.room)) return;
      if (msg.id !== undefined && !isValidIdString(msg.id)) return;

      roomId = msg.room || 'default';
      peerId = msg.id || `peer-${Math.random().toString(36).slice(2, 8)}`;
      const room = getRoom(roomId);
      room.set(peerId, { ws, x: undefined, y: undefined, z: undefined });
      send(ws, {
        type: 'welcome',
        id: peerId,
        room: roomId,
        peers: [...room.keys()].filter((id) => id !== peerId),
      });
      broadcast(room, { type: 'peer-joined', id: peerId }, ws);
      console.log(`[join] ${peerId} joined room "${roomId}" (${room.size} total)`);
      return;
    }

    if (!roomId || !peerId) return; // must join before doing anything else
    const room = getRoom(roomId);

    if (msg.type === 'position') {
      if (!isFiniteNumber(msg.x) || !isFiniteNumber(msg.y) || !isFiniteNumber(msg.z)) return;
      const peer = room.get(peerId);
      if (!peer) return;
      peer.x = msg.x;
      peer.y = msg.y;
      peer.z = msg.z;
      broadcastVolumes(room);
      return;
    }

    if (msg.type === 'signal') {
      // WebRTC offer/answer/ICE relay - passed through untouched to the target peer.
      if (!isValidIdString(msg.to) || msg.data === undefined) return;
      const target = room.get(msg.to);
      if (target) {
        send(target.ws, { type: 'signal', from: peerId, data: msg.data });
      }
      return;
    }
  });

  ws.on('close', () => {
    if (!roomId || !peerId) return;
    const room = rooms.get(roomId);
    if (!room) return;
    room.delete(peerId);
    console.log(`[leave] ${peerId} left room "${roomId}" (${room.size} remaining)`);
    if (room.size === 0) {
      rooms.delete(roomId);
    } else {
      broadcast(room, { type: 'peer-left', id: peerId });
    }
  });
});

console.log(`Proximity signaling server listening on ws://localhost:${PORT}`);
