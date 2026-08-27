'use strict';

const crypto = require('node:crypto');
const { WebSocketServer } = require('ws');
const { computeGain, computeRelativeOffset } = require('./proximity');

const PORT = process.env.PORT || 8080;
// Set on the VPS so randoms can't spin up rooms on your server. Unset (local dev) =
// no check. Only gates hosting now - joining an existing room only requires knowing
// its (server-generated, effectively unguessable) room code, not this secret too.
const ROOM_SECRET = process.env.ROOM_SECRET || null;
const MAX_ID_LENGTH = 64;

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function isValidIdString(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= MAX_ID_LENGTH;
}

function resolveDisplayName(value) {
  if (typeof value === 'string') {
    const trimmed = value.trim();
    if (trimmed.length > 0 && trimmed.length <= MAX_ID_LENGTH) {
      return trimmed;
    }
  }
  return 'Player';
}

// roomCode -> Map<peerId, { ws, displayName, x, y, z, mapId, underground, yawCos, yawSin, blockId, mapX, mapY }>
const rooms = new Map();
// roomCode -> peerId of whoever hosted it. Not re-assigned if the host leaves - the
// room just has no host peer anymore ("Host" tag disappears client-side) rather than
// promoting someone; keeps this simple until host migration is actually needed.
const roomHosts = new Map();

// Excludes visually-ambiguous characters (0/O, 1/I/L) - this code gets read off one
// screen and typed into another, so a typo should just mean "room not found," not a
// character that's genuinely unclear which key to press.
const ROOM_CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
const ROOM_CODE_LENGTH = 6;

function generateRoomCode() {
  let code;
  do {
    code = '';
    for (let i = 0; i < ROOM_CODE_LENGTH; i++) {
      code += ROOM_CODE_ALPHABET[crypto.randomInt(ROOM_CODE_ALPHABET.length)];
    }
  } while (rooms.has(code)); // astronomically unlikely to collide, but cheap to guard
  return code;
}

function createRoom() {
  const code = generateRoomCode();
  rooms.set(code, new Map());
  return code;
}

function makeInitialPeerState(ws, displayName) {
  return {
    ws,
    displayName,
    x: undefined,
    y: undefined,
    z: undefined,
    mapId: undefined,
    underground: undefined,
    yawCos: undefined,
    yawSin: undefined,
    blockId: undefined,
    mapX: undefined,
    mapY: undefined,
  };
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

// Recomputes every pairwise distance/direction in the room and sends each peer its
// own volume+direction map for the others. The proximity "game logic" lives here so
// every listener agrees on the same values instead of each client computing its own
// (and potentially disagreeing, or being tampered with client-side). The relative
// offset is already rotated into each listener's own facing frame server-side (see
// computeRelativeOffset), so the client can feed it straight into a PannerNode
// without needing to do any of that rotation itself.
function broadcastVolumes(room) {
  const peers = [...room.entries()];
  for (const [selfId, self] of peers) {
    if (self.x === undefined) continue; // hasn't sent a position yet
    const volumes = {};
    for (const [otherId, other] of peers) {
      if (otherId === selfId || other.x === undefined) continue;
      const offset = computeRelativeOffset(self, other);
      volumes[otherId] = {
        gain: computeGain(self, other),
        relX: offset.x,
        relY: offset.y,
        relZ: offset.z,
      };
    }
    send(self.ws, { type: 'volumes', volumes });
  }
}

// maxPayload caps a single message well above any real SDP/ICE payload we'd ever
// send, but far below what's useful for a memory-abuse attempt - ws closes the
// connection with code 1009 if a client sends something bigger.
const wss = new WebSocketServer({ port: PORT, maxPayload: 16 * 1024 });

// A connection that dies without a clean close (lost internet, sleep, crash) would
// otherwise sit in its room forever - still counted in broadcastVolumes and still a
// valid signaling target. Ping every connection on an interval; if it didn't answer
// the *previous* ping by the time the next one is due, terminate it. terminate()
// fires the same 'close' handler below, so cleanup doesn't need to be duplicated.
const HEARTBEAT_INTERVAL_MS = 30000;

const heartbeatInterval = setInterval(() => {
  for (const ws of wss.clients) {
    if (ws.isAlive === false) {
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.ping();
  }
}, HEARTBEAT_INTERVAL_MS);

wss.on('close', () => clearInterval(heartbeatInterval));

wss.on('connection', (ws) => {
  let roomId = null;
  let peerId = null;

  ws.isAlive = true;
  ws.on('pong', () => {
    ws.isAlive = true;
  });

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      return;
    }
    if (typeof msg !== 'object' || msg === null) return;

    if (msg.type === 'host') {
      if (ROOM_SECRET && msg.secret !== ROOM_SECRET) {
        console.log('[host] rejected: missing/incorrect secret');
        ws.close(4001, 'invalid secret');
        return;
      }
      if (msg.id !== undefined && !isValidIdString(msg.id)) return;

      peerId = msg.id || `peer-${Math.random().toString(36).slice(2, 8)}`;
      const displayName = resolveDisplayName(msg.displayName);

      roomId = createRoom();
      const room = rooms.get(roomId);
      room.set(peerId, makeInitialPeerState(ws, displayName));
      roomHosts.set(roomId, peerId);

      send(ws, { type: 'welcome', id: peerId, room: roomId, hostId: peerId, peers: [] });
      console.log(`[host] ${peerId} ("${displayName}") created room "${roomId}"`);
      return;
    }

    if (msg.type === 'join') {
      if (msg.room !== undefined && !isValidIdString(msg.room)) return;
      if (msg.id !== undefined && !isValidIdString(msg.id)) return;

      const targetRoomId = msg.room;
      const room = targetRoomId ? rooms.get(targetRoomId) : undefined;
      if (!room) {
        console.log(`[join] rejected: room "${targetRoomId}" not found`);
        ws.close(4003, 'room not found');
        return;
      }

      const requestedId = msg.id || `peer-${Math.random().toString(36).slice(2, 8)}`;
      // Reject rather than silently overwrite - two peers sharing an id would
      // otherwise have position/volume/signaling for that id randomly cross-routed
      // between their two sockets depending on which joined last.
      if (room.has(requestedId)) {
        console.log(`[join] rejected: id "${requestedId}" already in use in room "${targetRoomId}"`);
        ws.close(4002, 'id already in use');
        return;
      }

      const displayName = resolveDisplayName(msg.displayName);
      roomId = targetRoomId;
      peerId = requestedId;
      room.set(peerId, makeInitialPeerState(ws, displayName));

      send(ws, {
        type: 'welcome',
        id: peerId,
        room: roomId,
        hostId: roomHosts.get(roomId) || null,
        peers: [...room.entries()]
          .filter(([id]) => id !== peerId)
          .map(([id, peer]) => ({ id, displayName: peer.displayName })),
      });
      broadcast(room, { type: 'peer-joined', id: peerId, displayName }, ws);
      console.log(`[join] ${peerId} ("${displayName}") joined room "${roomId}" (${room.size} total)`);
      return;
    }

    if (!roomId || !peerId) return; // must host or join before doing anything else
    const room = rooms.get(roomId);
    if (!room) return;

    if (msg.type === 'position') {
      if (!isFiniteNumber(msg.x) || !isFiniteNumber(msg.y) || !isFiniteNumber(msg.z)) return;
      const peer = room.get(peerId);
      if (!peer) return;
      peer.x = msg.x;
      peer.y = msg.y;
      peer.z = msg.z;
      // Optional - a client that doesn't send these (mock-client.js, manual test
      // controls, or an older build) is treated by computeGain as "block unknown"
      // and by computeRelativeOffset as "facing unknown" (neutral direction).
      // mapId/underground are stored but no longer drive the proximity decision -
      // blockId is what computeGain/computeRelativeOffset actually gate on now.
      peer.mapId = isFiniteNumber(msg.mapId) ? msg.mapId : undefined;
      peer.underground = isFiniteNumber(msg.underground) ? msg.underground : undefined;
      peer.yawCos = isFiniteNumber(msg.yawCos) ? msg.yawCos : undefined;
      peer.yawSin = isFiniteNumber(msg.yawSin) ? msg.yawSin : undefined;
      peer.blockId = isFiniteNumber(msg.blockId) ? msg.blockId : undefined;
      peer.mapX = isFiniteNumber(msg.mapX) ? msg.mapX : undefined;
      peer.mapY = isFiniteNumber(msg.mapY) ? msg.mapY : undefined;
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
      roomHosts.delete(roomId);
    } else {
      broadcast(room, { type: 'peer-left', id: peerId });
    }
  });
});

console.log(`Proximity signaling server listening on ws://localhost:${PORT}`);
