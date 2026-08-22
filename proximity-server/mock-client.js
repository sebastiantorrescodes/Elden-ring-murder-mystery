'use strict';

// Fake "player" for local testing - connects to the signaling server, joins a room,
// and streams a slow random walk of positions, matching the DLL's ~250ms poll cadence.
// Run two or more of these in separate terminals to see the server's proximity gain
// math respond as their simulated positions drift toward/away from each other, with
// no game or DLL required:
//   node mock-client.js alice
//   node mock-client.js bob

const WebSocket = require('ws');

const [, , idArg, ...rest] = process.argv;
if (!idArg) {
  console.error('Usage: node mock-client.js <playerId> [--server ws://localhost:8080] [--room test-room]');
  process.exit(1);
}

function argValue(flag, fallback) {
  const i = rest.indexOf(flag);
  return i !== -1 ? rest[i + 1] : fallback;
}

const SERVER_URL = argValue('--server', 'ws://localhost:8080');
const ROOM = argValue('--room', 'test-room');
const PLAYER_ID = idArg;

const ws = new WebSocket(SERVER_URL);

let x = Math.random() * 20 - 10;
let y = 0;
let z = Math.random() * 20 - 10;

ws.on('open', () => {
  console.log(`[${PLAYER_ID}] connected to ${SERVER_URL}, joining room "${ROOM}"`);
  ws.send(JSON.stringify({ type: 'join', room: ROOM, id: PLAYER_ID }));

  setInterval(() => {
    x += (Math.random() - 0.5) * 2;
    z += (Math.random() - 0.5) * 2;
    ws.send(JSON.stringify({ type: 'position', x, y, z }));
  }, 250);
});

ws.on('message', (raw) => {
  const msg = JSON.parse(raw.toString());
  if (msg.type === 'welcome') {
    console.log(`[${PLAYER_ID}] joined as ${msg.id}, existing peers: ${msg.peers.join(', ') || '(none)'}`);
  } else if (msg.type === 'peer-joined') {
    console.log(`[${PLAYER_ID}] peer joined: ${msg.id}`);
  } else if (msg.type === 'peer-left') {
    console.log(`[${PLAYER_ID}] peer left: ${msg.id}`);
  } else if (msg.type === 'volumes') {
    // volumes[id] is now { gain, relX, relY, relZ } - this mock client doesn't send
    // yawCos/yawSin, so relX/Y/Z will always be the server's neutral fallback direction.
    const entries = Object.entries(msg.volumes).map(([id, v]) => `${id}=${v.gain.toFixed(2)}`);
    console.log(`[${PLAYER_ID}] volumes: ${entries.join(', ') || '(none)'}`);
  }
});

ws.on('close', () => console.log(`[${PLAYER_ID}] disconnected`));
ws.on('error', (err) => console.error(`[${PLAYER_ID}] error:`, err.message));
