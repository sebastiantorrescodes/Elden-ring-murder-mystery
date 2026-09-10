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
const MIN_GAME_PLAYERS = 1;
const MAX_GAME_PLAYERS = 6;
const SPAWN_COUNT = 11;

// temp
const ENABLE_TWO_PLAYER_FAKE_MURDERER = true;
const TWO_PLAYER_FAKE_MURDERER_ID = '__test_fake_murderer__';

const MIN_ROUND_DURATION_MS = 60_000;
const MAX_ROUND_DURATION_MS = 10 * 60_000;
const WIN_RESULT_DELAY_MS = 3_000;
const RESULT_DISPLAY_MS = 6_000;
// Time after EndGame is sent for each client to complete its
// Roundtable warp before the server permits another round.
const HUB_RETURN_GRACE_MS = 5_000;

const ITEM_SPAWN_INTERVAL_MS = 31_313;
const ITEM_SPAWN_POINT_COUNT = 10;
const SHERIFF_DROP_POINT_COUNT = 13;

// Prefixes every server log with a sortable UTC timestamp so lines can be
// correlated with client-side logs / crash reports without guessing at
// server-local time.
function log(...args) {
  console.log(`[${new Date().toISOString()}]`, ...args);
}

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function isValidIdString(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= MAX_ID_LENGTH;
}

const MAX_COMMAND_PARAMS = 8;

// command messages' params (e.g. StartGame's { role: "Murderer" }) - plain
// string->string only, capped in both key/value length and entry count so this
// can't be used to smuggle an oversized payload to a peer.
function isValidParams(params) {
  if (typeof params !== 'object' || params === null || Array.isArray(params)) return false;
  const entries = Object.entries(params);
  if (entries.length > MAX_COMMAND_PARAMS) return false;
  return entries.every(
    ([key, value]) =>
      typeof key === 'string' &&
      key.length > 0 &&
      key.length <= MAX_ID_LENGTH &&
      typeof value === 'string' &&
      value.length <= MAX_ID_LENGTH,
  );
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

// roomCode -> Map<peerId, { ws, displayName, x, y, z, mapId, underground, yawCos, yawSin, blockId, mapX, mapY, oriDeg, mapRelY, mapRelX, mapRelZ, mapRelRad }>
const rooms = new Map();
// roomCode -> peerId of whoever hosted it. If the host disconnects and the room
// isn't empty, the close handler below promotes the longest-remaining peer rather
// than leaving the room permanently host-less.
const roomHosts = new Map();
// roomCode -> active coordinated round
const roomRounds = new Map();
// roomCode -> Set<peerId> kicked from that room - checked on 'join' so a kicked
// peer can't just immediately reconnect. Keyed by the same client-supplied peerId
// the rest of the protocol trusts (see getOrCreatePlayerId on the client), so it's
// no stronger than that trust boundary already is (clearing localStorage evades
// it) - same caveat as the host-only checks above, kept simple until needed.
const roomBans = new Map();

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
    ready: false,
    inRoundtable: false,
    gameConnected: false,
    micMuted: false,
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
    hp: undefined,
    oriDeg: undefined,
    mapRelY: undefined,
    mapRelX: undefined,
    mapRelZ: undefined,
    mapRelRad: undefined,
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

function shuffled(items) {
  const result = [...items];

  for (let i = result.length - 1; i > 0; i--) {
    const j = crypto.randomInt(i + 1);
    [result[i], result[j]] =
      [result[j], result[i]];
  }

  return result;
}

function sendCommand(
  room,
  targetId,
  name,
  params,
) {
  const target = room.get(targetId);

  if (!target) return false;

  send(target.ws, {
    type: 'command',
    name,
    params,
  });

  return true;
}

function broadcastRoundState(room, state, result) {
  broadcast(room, {
    type: 'round-state',
    state,
    ...(result ? { result } : {}),
  });
}

function resetRoomToWaiting(roomId, expectedRoundId) {
  const room = rooms.get(roomId);
  const round = roomRounds.get(roomId);

  if (
    !room ||
    !round ||
    round.id !== expectedRoundId ||
    round.state === 'RESETTING'
  ) {
    return;
  }

  round.state = 'RESETTING';

  // Prevent players from readying up while one or more clients
  // may still be completing the return warp.
  for (const [id, peer] of room) {
    peer.ready = false;

    broadcast(room, {
      type: 'peer-ready',
      id,
      ready: false,
    });
  }

  broadcast(room, {
    type: 'round-timer',
    endsAt: null,
  });

  broadcastRoundState(
    room,
    'RESETTING',
  );

  // Living players still in Volcano Manor need a warp.
  // Dead players are already in Roundtable.
  for (const id of round.participantIds) {
    sendCommand(
      room,
      id,
      'EndGame',
      {
        round: round.id,
        returnToHub: '1',
      },
    );
  }

  console.log(
    `[round] resetting room="${roomId}" ` +
    `round="${expectedRoundId}"`,
  );

  round.resetTimer = setTimeout(
    () => {
      const currentRoom =
        rooms.get(roomId);

      const currentRound =
        roomRounds.get(roomId);

      if (
        !currentRoom ||
        !currentRound ||
        currentRound.id !== expectedRoundId
      ) {
        return;
      }

      currentRound.resetTimer = null;

      broadcastRoundState(
        currentRoom,
        'WAITING',
      );

      roomRounds.delete(roomId);

      console.log(
        `[round] reset complete room="${roomId}" ` +
        `round="${expectedRoundId}"`,
      );
    },
    HUB_RETURN_GRACE_MS,
  );
}

function finishRound(roomId, result, reason) {
  const room = rooms.get(roomId);
  const round = roomRounds.get(roomId);

  if (!room || !round || round.roundEnded) return;

  round.roundEnded = true;
  round.state = 'ENDING';
  round.sheriffDropIndex = null;

  // Remove the kit as soon as the round result is decided, rather than leaving
  // it interactable during the result-banner delay.
  for (const id of round.participantIds) {
    sendCommand(room, id, 'SheriffDropClear', { round: round.id });
  }

  if (round.activeTimer) {
    clearTimeout(round.activeTimer);
    round.activeTimer = null;
  }

  if (round.itemSpawnTimer) {
    clearInterval(round.itemSpawnTimer);
    round.itemSpawnTimer = null;
  }

  if (round.resetTimer) {
    clearTimeout(round.resetTimer);
    round.resetTimer = null;
  }

  // Stop the gameplay timer immediately once a result has been
  // selected, even if its banner is intentionally delayed.
  broadcast(
    room,
    {
      type: 'round-timer',
      endsAt: null,
    },
  );

  // Tell clients gameplay has ended, but do not reveal the result
  // until the banner is actually sent.
  broadcastRoundState(
    room,
    'ENDING',
  );

  log(
    `[round] ending room="${roomId}" round="${round.id}" ` +
    `result=${result} reason=${reason}`,
  );

  const showResult = () => {
    // Make sure this callback still belongs to the same round.
    const currentRound =
      roomRounds.get(roomId);

    if (
      !currentRound ||
      currentRound.id !== round.id
    ) {
      return;
    }

    for (const id of round.participantIds) {
      sendCommand(
        room,
        id,
        'RoundResult',
        {
          round: round.id,
          result,
        },
      );
    }

    broadcastRoundState(
      room,
      'ENDING',
      result,
    );

    log(
      `[round] result banner sent ` +
      `room="${roomId}" round="${round.id}" ` +
      `result=${result}`,
    );

    // Keep the banner visible for ten seconds before returning
    // everyone to Roundtable.
    round.endingTimer = setTimeout(
      () => {
        round.endingTimer = null;

        resetRoomToWaiting(
          roomId,
          round.id,
        );
      },
      RESULT_DISPLAY_MS,
    );
  };

  // Kill-based wins wait five seconds. Timer expiration and a
  // host-requested ending show Stalemate immediately.
  if (result === 'Stalemate') {
    showResult();
  } else {
    round.resultTimer = setTimeout(
      showResult,
      WIN_RESULT_DELAY_MS,
    );
  }
}

function spawnRoundItem(
  roomId,
  expectedRoundId,
) {
  const room = rooms.get(roomId);
  const round = roomRounds.get(roomId);

  if (
    !room ||
    !round ||
    round.id !== expectedRoundId ||
    round.state !== 'ACTIVE' ||
    round.roundEnded
  ) {
    return;
  }

  const availableIndexes = [];

  for (
    let index = 1;
    index <= ITEM_SPAWN_POINT_COUNT;
    index++
  ) {
    if (!round.occupiedItemPoints.has(index)) {
      availableIndexes.push(index);
    }
  }

  // Every location currently contains an unclaimed pickup.
  if (availableIndexes.length === 0) {
    log(
      `[items] no available spawn points ` +
      `room="${roomId}" round="${round.id}"`,
    );
    return;
  }

  const index =
    availableIndexes[
      crypto.randomInt(availableIndexes.length)
    ];

  // Reserve it before sending commands so another interval
  // cannot choose the same point.
  round.occupiedItemPoints.add(index);

  // The EMEVD pickup uses a shared network-connected active
  // flag, while each client creates its own local presentation.
  // Send the request to every living participant so every
  // loaded client receives the same pickup presentation.
  for (const id of round.participantIds) {
    if (
      round.aliveByPlayerId.get(id) === true &&
      room.has(id)
    ) {
      sendCommand(
        room,
        id,
        'SpawnItem',
        {
          round: round.id,
          index: String(index),
        },
      );
    }
  }

  log(
    `[items] spawned index=${index} ` +
    `room="${roomId}" round="${round.id}"`,
  );
}

function spawnSheriffDrop(room, round, reason) {
  const spawnIndex =
    1 + crypto.randomInt(SHERIFF_DROP_POINT_COUNT);

  round.sheriffDropIndex = spawnIndex;

  for (const id of round.participantIds) {
    sendCommand(room, id, 'SheriffDrop', {
      round: round.id,
      spawn: String(spawnIndex),
    });
  }

  log(
    `[sheriff] drop=${spawnIndex} reason="${reason}" ` +
    `round="${round.id}"`,
  );
}

function evaluateRoundState(roomId) {
  const round = roomRounds.get(roomId);

  if (!round || round.state !== 'ACTIVE' || round.roundEnded) return;

  const murdererId = [...round.roleByPlayerId.entries()]
    .find(([, role]) => role === 'Murderer')?.[0];

  if (!murdererId) {
    finishRound(roomId, 'Stalemate', 'missing murderer');
    return;
  }

  if (round.aliveByPlayerId.get(murdererId) === false) {
    finishRound(roomId, 'InnocentWin', 'murderer died');
    return;
  }

  const anyNonMurdererAlive = round.participantIds.some(
    (id) =>
      id !== murdererId &&
      round.aliveByPlayerId.get(id) === true,
  );

  if (!anyNonMurdererAlive) {
    finishRound(roomId, 'MurdererWin', 'all non-murderers died');
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
        log('[host] rejected: missing/incorrect secret');
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
      log(`[host] ${peerId} ("${displayName}") created room "${roomId}"`);
      return;
    }

    if (msg.type === 'join') {
      if (msg.room !== undefined && !isValidIdString(msg.room)) return;
      if (msg.id !== undefined && !isValidIdString(msg.id)) return;

      const targetRoomId = msg.room;
      const room = targetRoomId ? rooms.get(targetRoomId) : undefined;
      if (!room) {
        log(`[join] rejected: room "${targetRoomId}" not found`);
        ws.close(4003, 'room not found');
        return;
      }

      const requestedId = msg.id || `peer-${Math.random().toString(36).slice(2, 8)}`;

      if (roomBans.get(targetRoomId)?.has(requestedId)) {
        log(`[join] rejected: id "${requestedId}" was kicked from room "${targetRoomId}"`);
        ws.close(4005, 'kicked from room');
        return;
      }

      // Reject rather than silently overwrite - two peers sharing an id would
      // otherwise have position/volume/signaling for that id randomly cross-routed
      // between their two sockets depending on which joined last.
      if (room.has(requestedId)) {
        log(`[join] rejected: id "${requestedId}" already in use in room "${targetRoomId}"`);
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
          .map(([id, peer]) => ({ id, displayName: peer.displayName, ready: peer.ready, micMuted: peer.micMuted })),
      });
      broadcast(room, { type: 'peer-joined', id: peerId, displayName }, ws);
      log(`[join] ${peerId} ("${displayName}") joined room "${roomId}" (${room.size} total)`);
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
      // hp === 0 mutes this peer for everyone in the room (see computeGain) -
      // undefined (older client, manual test controls) is left un-mutable.
      peer.hp = isFiniteNumber(msg.hp) ? msg.hp : undefined;
      // Character facing in degrees, from a different subsystem (CsMenuManImp) than
      // mapRelRad below - stored but not consumed by computeGain/computeRelativeOffset.
      peer.oriDeg = isFiniteNumber(msg.oriDeg) ? msg.oriDeg : undefined;
      // Map-relative position (see the MapRelativeCoords comment in WorldChrMan.h) -
      // unlike raw x/y/z above, these shouldn't reset when crossing a local map-tile
      // boundary in the open world. computeGain's mapDistance uses mapRelY instead of
      // raw y for the elevation term, and computeRelativeOffset uses mapRelX/mapRelZ/
      // mapRelRad instead of raw x/z and yawCos/yawSin for the panning direction term
      // - see hasMapPosition and computeRelativeOffset in proximity.js. mapRelRad is
      // this peer's own character facing (not their camera's) - preferred over
      // yawCos/yawSin because a third-person camera can be spun to face a different
      // direction than the character actually does, and preferred over oriDeg
      // because it shares the same underlying struct/coordinate frame as
      // mapRelX/mapRelZ (see the MapRelativeCoords comment in WorldChrMan.h).
      peer.mapRelY = isFiniteNumber(msg.mapRelY) ? msg.mapRelY : undefined;
      peer.mapRelX = isFiniteNumber(msg.mapRelX) ? msg.mapRelX : undefined;
      peer.mapRelZ = isFiniteNumber(msg.mapRelZ) ? msg.mapRelZ : undefined;
      peer.mapRelRad = isFiniteNumber(msg.mapRelRad) ? msg.mapRelRad : undefined;
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

    if (msg.type === 'ready') {
      if (typeof msg.ready !== 'boolean') return;
      const peer = room.get(peerId);
      if (!peer) return;
      // A client may only enter Ready while its DLL/EMEVD has confirmed that
      // its local player is actually loaded in Roundtable Hold.
      peer.ready =
        msg.ready &&
        peer.inRoundtable === true &&
        peer.gameConnected === true;
      // Broadcast to the whole room including the sender - keeps ready state
      // fully server-authoritative (same reasoning as broadcastVolumes) instead
      // of each client also optimistically updating its own local copy.
      broadcast(room, { type: 'peer-ready', id: peerId, ready: peer.ready });
      return;
    }

    if (msg.type === 'lobby-state') {
      if (
        typeof msg.inRoundtable !== 'boolean' ||
        typeof msg.gameConnected !== 'boolean'
      ) return;
      const peer = room.get(peerId);
      if (!peer) return;

      peer.inRoundtable = msg.inRoundtable;
      peer.gameConnected = msg.gameConnected;

      // Leaving Roundtable always revokes readiness. This also covers a manual
      // warp or disconnect from the map before the host starts the round.
      if ((!peer.inRoundtable || !peer.gameConnected) && peer.ready) {
        peer.ready = false;
        broadcast(room, { type: 'peer-ready', id: peerId, ready: false });
      }
      return;
    }

    if (msg.type === 'mic-muted') {
      if (typeof msg.micMuted !== 'boolean') return;
      const peer = room.get(peerId);
      if (!peer) return;
      peer.micMuted = msg.micMuted;
      // Same reasoning as 'ready' above - purely cosmetic (lets other players see
      // this peer muted themselves), but still server-authoritative rather than
      // trusted client-side state.
      broadcast(room, { type: 'peer-mic-muted', id: peerId, micMuted: msg.micMuted });
      return;
    }

    if (msg.type === 'start-round') {
      if (roomHosts.get(roomId) !== peerId) {
        log(
          `[start-round] rejected: "${peerId}" is not host`,
        );
        return;
      }

      if (roomRounds.has(roomId)) {
        log(
          `[start-round] rejected: room "${roomId}" already has a round`,
        );
        return;
      }

      if (
        !isFiniteNumber(msg.durationMs) ||
        msg.durationMs < MIN_ROUND_DURATION_MS ||
        msg.durationMs > MAX_ROUND_DURATION_MS
      ) {
        log(
          `[start-round] rejected: invalid duration`,
        );
        return;
      }

      const participantIds =
        [...room.keys()];

      if (
        participantIds.length < MIN_GAME_PLAYERS ||
        participantIds.length > MAX_GAME_PLAYERS ||
        participantIds.length > SPAWN_COUNT
      ) {
        log(
          `[start-round] rejected: invalid player count ` +
          participantIds.length,
        );
        return;
      }

      const everyoneReady =
        participantIds.every(
          (id) =>
            room.get(id)?.ready === true &&
            room.get(id)?.inRoundtable === true &&
            room.get(id)?.gameConnected === true,
        );

      if (!everyoneReady) {
        log(
          `[start-round] rejected: not everyone is ready`,
        );
        return;
      }

      const roundId =
        crypto.randomBytes(12).toString('hex');

      const spawnIndexes =
        shuffled(
          Array.from(
            { length: SPAWN_COUNT },
            (_, index) => index + 1,
          ),
        );

      const roleOrder =
        shuffled(participantIds);

      const spawnByPlayerId = new Map();
      const roleByPlayerId = new Map();

      participantIds.forEach((id, index) => {
        spawnByPlayerId.set(
          id,
          spawnIndexes[index],
        );
      });

      const useFakeMurderer =
        ENABLE_TWO_PLAYER_FAKE_MURDERER &&
        participantIds.length === 2;

      if (useFakeMurderer) {
        // The fake murderer exists only in server-side round state.
        roleByPlayerId.set(
          TWO_PLAYER_FAKE_MURDERER_ID,
          'Murderer',
        );

        // Randomly choose which real player starts as Sheriff.
        roleByPlayerId.set(
          roleOrder[0],
          'Sheriff',
        );

        roleByPlayerId.set(
          roleOrder[1],
          'Innocent',
        );

        log(
          `[test] fake murderer enabled; ` +
          `sheriff="${roleOrder[0]}" innocent="${roleOrder[1]}"`,
        );
      } else {
        roleByPlayerId.set(
          roleOrder[0],
          'Murderer',
        );

        if (roleOrder.length >= 2) {
          roleByPlayerId.set(
            roleOrder[1],
            'Sheriff',
          );
        }

        for (let i = 2; i < roleOrder.length; i++) {
          roleByPlayerId.set(
            roleOrder[i],
            'Innocent',
          );
        }
      }

      const round = {
        id: roundId,
        state: 'LOADING',
        participantIds,
        spawnByPlayerId,
        roleByPlayerId,
        loadedPlayerIds: new Set(),
        durationMs: msg.durationMs,
        commenced: false,
        roundEnded: false,
        aliveByPlayerId: new Map([
          ...participantIds.map((id) => [id, true]),
          ...(useFakeMurderer
            ? [[TWO_PLAYER_FAKE_MURDERER_ID, true]]
            : []),
        ]),
        activeTimer: null,
        resultTimer: null,
        resetTimer: null,
        endingTimer: null,
        itemSpawnTimer: null,
        occupiedItemPoints: new Set(),
        sheriffDropIndex: null,
        lastSheriffHit: null,
      };

      roomRounds.set(
        roomId,
        round,
      );

      broadcastRoundState(room, 'LOADING');

      for (const id of participantIds) {
        sendCommand(
          room,
          id,
          'StartGame',
          {
            round: roundId,
            spawn: String(
              spawnByPlayerId.get(id),
            ),
          },
        );
      }

      log(
        `[start-round] room="${roomId}" ` +
        `round="${roundId}" ` +
        `players=${participantIds.length}`,
      );

      return;
    }

    if (msg.type === 'game-event') {
      if (
        (msg.name !== 'PlayerLoaded' &&
          msg.name !== 'PlayerDied' &&
          msg.name !== 'ItemPickedUp' &&
          msg.name !== 'SheriffKitPickedUp' &&
          msg.name !== 'SheriffHit') ||
        !isValidIdString(msg.roundId)
      ) {
        return;
      }

      const round =
        roomRounds.get(roomId);

      if (
        !round ||
        round.id !== msg.roundId ||
        !round.participantIds.includes(peerId)
      ) {
        log(
          `[game-event] ignored stale/invalid event ` +
          `from "${peerId}"`,
        );
        return;
      }

      if (msg.name === 'ItemPickedUp') {
        if (
          round.state !== 'ACTIVE' ||
          round.roundEnded ||
          !Number.isInteger(msg.index) ||
          msg.index < 1 ||
          msg.index > ITEM_SPAWN_POINT_COUNT
        ) {
          return;
        }

        // Several clients may observe the shared flag turning OFF
        // and report it. Deleting from a Set is safely idempotent.
        if (round.occupiedItemPoints.delete(msg.index)) {
          log(
            `[items] picked up index=${msg.index} ` +
            `room="${roomId}" round="${round.id}"`,
          );
        }

        return;
      }

      if (msg.name === 'SheriffKitPickedUp') {
        if (
          round.state !== 'ACTIVE' ||
          round.roundEnded ||
          round.sheriffDropIndex === null ||
          round.aliveByPlayerId.get(peerId) !== true ||
          round.roleByPlayerId.get(peerId) !== 'Innocent'
        ) {
          return;
        }

        // First valid claim wins. Clear this before sending anything so two
        // near-simultaneous pickup messages cannot create two Sheriffs.
        round.sheriffDropIndex = null;
        round.roleByPlayerId.set(peerId, 'Sheriff');

        for (const id of round.participantIds) {
          sendCommand(room, id, 'SheriffDropClear', { round: round.id });
        }

        sendCommand(room, peerId, 'SetRole', {
          round: round.id,
          role: 'Sheriff',
        });

        log(`[sheriff] kit claimed by "${peerId}" round="${round.id}"`);
        return;
      }

      if (msg.name === 'SheriffHit') {
        if (
          round.state !== 'ACTIVE' ||
          round.roundEnded ||
          round.roleByPlayerId.get(peerId) !== 'Sheriff' ||
          round.aliveByPlayerId.get(peerId) !== true
        ) {
          return;
        }

        round.lastSheriffHit = {
          sheriffId: peerId,
          receivedAt: Date.now(),
        };

        log(
          `[sheriff] confirmed hit from "${peerId}" ` +
          `round="${round.id}"`,
        );

        return;
      }

      if (msg.name === 'PlayerDied') {
        if (round.state !== 'ACTIVE' || round.roundEnded) return;
        if (round.aliveByPlayerId.get(peerId) === false) return;

        const victimId = peerId;
        const victimRole =
          round.roleByPlayerId.get(victimId);

        round.aliveByPlayerId.set(victimId, false);

        log(
          `[game-event] PlayerDied "${victimId}" ` +
          `role=${victimRole || 'Unknown'} ` +
          `round="${round.id}"`,
        );

        if (victimRole === 'Sheriff') {
          spawnSheriffDrop(
            room,
            round,
            `Sheriff ${victimId} died`,
          );

          evaluateRoundState(roomId);
          return;
        }

        if (victimRole !== 'Innocent') {
          evaluateRoundState(roomId);
          return;
        }

        // SheriffHit and PlayerDied originate from different clients.
        // Briefly delay attribution so either network message can arrive first.
        const expectedRoundId = round.id;

        setTimeout(() => {
          const currentRoom =
            rooms.get(roomId);

          const currentRound =
            roomRounds.get(roomId);

          if (
            !currentRoom ||
            !currentRound ||
            currentRound.id !== expectedRoundId ||
            currentRound.state !== 'ACTIVE' ||
            currentRound.roundEnded
          ) {
            return;
          }

          const hit =
            currentRound.lastSheriffHit;

          const sheriffAttributed =
            hit !== null &&
            Date.now() - hit.receivedAt <= 2000;

          if (sheriffAttributed) {
            const sheriffId =
              hit.sheriffId;

            currentRound.lastSheriffHit = null;

            if (
              currentRound.roleByPlayerId.get(sheriffId) ===
                'Sheriff' &&
              currentRound.aliveByPlayerId.get(sheriffId) ===
                true
            ) {
              currentRound.roleByPlayerId.set(
                sheriffId,
                'Innocent',
              );

              const demotionSent =
                sendCommand(
                  currentRoom,
                  sheriffId,
                  'SetRole',
                  {
                    round: currentRound.id,
                    role: 'Innocent',
                  },
                );

              log(
                `[sheriff] wrong-kill demotion ` +
                `target="${sheriffId}" ` +
                `victim="${victimId}" ` +
                `sent=${demotionSent} ` +
                `round="${currentRound.id}"`,
              );

              spawnSheriffDrop(
                currentRoom,
                currentRound,
                `Sheriff ${sheriffId} killed ` +
                `Innocent ${victimId}`,
              );
            }
          } else {
            log(
              `[sheriff] Innocent "${victimId}" died ` +
              `without a recent confirmed Sheriff hit`,
            );
          }

          evaluateRoundState(roomId);
        }, 500);

        return;
      }

      if (round.commenced || round.state !== 'LOADING') {
        return;
      }

      round.loadedPlayerIds.add(
        peerId,
      );

      log(
        `[game-event] PlayerLoaded "${peerId}" ` +
        `${round.loadedPlayerIds.size}/` +
        `${round.participantIds.length}`,
      );

      if (
        round.loadedPlayerIds.size !==
        round.participantIds.length
      ) {
        return;
      }

      /// Mark the synchronization barrier complete before sending
      // anything, preventing duplicate events from commencing twice.
      round.commenced = true;
      round.state = 'STARTING';
      broadcastRoundState(room, 'STARTING');

      // Everyone is loaded. Assign roles first.
      for (const id of round.participantIds) {
        sendCommand(
          room,
          id,
          'SetRole',
          {
            round: round.id,
            role: round.roleByPlayerId.get(id),
          },
        );
      }

      // Then request the Commence banner.
      for (const id of round.participantIds) {
        sendCommand(
          room,
          id,
          'Commence',
          {
            round: round.id,
          },
        );
      }

      // Start the shared timer only after role and Commence
      // commands have been sent.
      const endsAt =
        Date.now() + round.durationMs;

      round.state = 'ACTIVE';
      round.endsAt = endsAt;
      round.activeTimer = setTimeout(
        () => finishRound(roomId, 'Stalemate', 'timer expired'),
        round.durationMs,
      );

      // The first item appears 30 seconds after Commence.
      // Subsequent attempts happen every 30 seconds.
      const itemRoundId =
        round.id;

      round.itemSpawnTimer = setInterval(
        () => spawnRoundItem(
          roomId,
          itemRoundId,
        ),
        ITEM_SPAWN_INTERVAL_MS,
      );

      broadcast(
        room,
        {
          type: 'round-timer',
          endsAt,
        },
      );

      broadcastRoundState(room, 'ACTIVE');

      log(
        `[round] commenced room="${roomId}" ` +
        `round="${round.id}" endsAt=${endsAt}`,
      );

      return;
    }

    if (msg.type === 'end-round') {
      if (roomHosts.get(roomId) !== peerId) {
        log(`[end-round] rejected: "${peerId}" is not host`);
        return;
      }

      finishRound(roomId, 'Stalemate', 'host ended round');
      return;
    }

    if (msg.type === 'round-timer') {
      // Same trust boundary as 'command' below - only the host starts/clears the
      // round timer, and it's broadcast (not per-target) since every player needs
      // to count down from the same deadline.
      if (roomHosts.get(roomId) !== peerId) {
        log(`[round-timer] rejected: "${peerId}" is not host of room "${roomId}"`);
        return;
      }
      if (msg.endsAt !== null && !isFiniteNumber(msg.endsAt)) return;
      broadcast(room, { type: 'round-timer', endsAt: msg.endsAt });
      return;
    }

    if (msg.type === 'command') {
      // Only the room's host may direct another peer's game state (e.g. assigning
      // Murderer/Sheriff/Innocent) - without this, any connected peer could be an
      // open remote-control channel for any other peer. Same trust boundary as
      // ROOM_SECRET gating who can create a room in the first place.
      if (roomHosts.get(roomId) !== peerId) {
        log(`[command] rejected: "${peerId}" is not host of room "${roomId}"`);
        return;
      }
      if (!isValidIdString(msg.to) || typeof msg.name !== 'string' || msg.name.length === 0 || msg.name.length > MAX_ID_LENGTH) {
        return;
      }
      if (msg.params !== undefined && !isValidParams(msg.params)) return;
      const target = room.get(msg.to);
      if (target) {
        send(target.ws, { type: 'command', name: msg.name, params: msg.params });
      }
      return;
    }

    if (msg.type === 'kick') {
      // Same trust boundary as 'command' above - only the host can remove another
      // peer from the room.
      if (roomHosts.get(roomId) !== peerId) {
        log(`[kick] rejected: "${peerId}" is not host of room "${roomId}"`);
        return;
      }
      if (!isValidIdString(msg.id) || msg.id === peerId) return;
      const target = room.get(msg.id);
      if (!target) return;
      log(`[kick] host "${peerId}" kicked "${msg.id}" from room "${roomId}"`);
      let banned = roomBans.get(roomId);
      if (!banned) {
        banned = new Set();
        roomBans.set(roomId, banned);
      }
      banned.add(msg.id);
      // Closing the target's own socket fires its own 'close' handler below,
      // which already does the full leave cleanup (room removal, peer-left
      // broadcast, ending an in-progress round if they were a participant) -
      // no separate cleanup needed here.
      target.ws.close(4004, 'kicked by host');
      return;
    }
  });

  ws.on('close', () => {
    if (!roomId || !peerId) return;
    const room = rooms.get(roomId);
    if (!room) return;
    const activeRound = roomRounds.get(roomId);
    const participantDisconnected =
      activeRound?.participantIds.includes(peerId) &&
      !activeRound.roundEnded;

    room.delete(peerId);
    log(`[leave] ${peerId} left room "${roomId}" (${room.size} remaining)`);
    if (room.size === 0) {
      rooms.delete(roomId);
      roomHosts.delete(roomId);
      roomRounds.delete(roomId);
      roomBans.delete(roomId);
    } else {
      broadcast(room, { type: 'peer-left', id: peerId });
      // The host disconnected but the room isn't empty - promote whoever's been
      // here longest (Map iteration order is insertion/join order) rather than
      // leaving the room permanently host-less. See makeInitialPeerState/roomHosts
      // above for why this wasn't done from the start (kept simple until needed).
      if (roomHosts.get(roomId) === peerId) {
        const newHostId = room.keys().next().value;
        roomHosts.set(roomId, newHostId);
        broadcast(room, { type: 'host-changed', hostId: newHostId });
        log(`[host] "${peerId}" (host) left room "${roomId}" - promoted "${newHostId}"`);
      }
      if (participantDisconnected) {
        finishRound(roomId, 'Stalemate', 'participant disconnected');
      }
    }
  });
});

log(`Proximity signaling server listening on ws://localhost:${PORT}`);
