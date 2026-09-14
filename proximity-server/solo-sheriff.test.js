'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { readFileSync } = require('node:fs');
const vm = require('node:vm');

// Exercise the real message handler with an in-memory transport and inert timers.
function setup() {
  const transport = new EventEmitter();
  const context = vm.createContext({
    require: (name) => name === 'ws'
      ? { WebSocketServer: class { constructor() { return transport; } } }
      : require(name),
    process: { env: {} },
    console: { log() {} },
    setTimeout: () => 1, clearTimeout() {},
    setInterval: () => 1, clearInterval() {},
  });
  vm.runInContext(readFileSync(require.resolve('./server'), 'utf8') +
    '\nglobalThis.state = { rooms, roomRounds };', context);
  const ws = new EventEmitter();
  const messages = [];
  ws.readyState = 1;
  ws.OPEN = 1;
  ws.send = (data) => messages.push(JSON.parse(data));
  transport.emit('connection', ws);
  const send = (msg) => ws.emit('message', JSON.stringify(msg));
  send({ type: 'host', id: 'tester' });
  const roomId = messages.find((msg) => msg.type === 'welcome').room;
  const round = {
    id: 'test-round', state: 'ACTIVE', roundEnded: false,
    useSoloRoleTest: true, participantIds: ['tester'], sheriffDropIndex: null,
    roleByPlayerId: new Map([
      ['tester', 'Innocent'], ['__test_fake_sheriff__', 'Sheriff'],
      ['__test_fake_murderer__', 'Murderer'],
    ]),
    aliveByPlayerId: new Map([
      ['tester', true], ['__test_fake_sheriff__', true],
      ['__test_fake_murderer__', true],
    ]),
  };
  context.state.roomRounds.set(roomId, round);
  messages.length = 0;
  return {
    round, messages, room: context.state.rooms.get(roomId),
    event: (name, roundId = round.id) => send({ type: 'game-event', name, roundId }),
  };
}

test('F5 registers a drop and normal pickup promotes the solo Innocent once', () => {
  const { round, messages, event } = setup();
  event('TestFakeSheriffDeath');
  assert.equal(round.aliveByPlayerId.get('__test_fake_sheriff__'), false);
  assert.equal(round.state, 'ACTIVE');
  assert.ok(round.sheriffDropIndex >= 1 && round.sheriffDropIndex <= 13);
  const drop = messages.find((msg) => msg.name === 'SheriffDrop');
  assert.equal(drop.params.spawn, String(round.sheriffDropIndex));
  event('TestFakeSheriffDeath');
  assert.equal(messages.filter((msg) => msg.name === 'SheriffDrop').length, 1);
  event('SheriffKitPickedUp');
  assert.equal(round.roleByPlayerId.get('tester'), 'Sheriff');
  assert.equal(round.sheriffDropIndex, null);
  assert.ok(messages.some((msg) => msg.name === 'SheriffDropClear'));
  assert.ok(messages.some((msg) => msg.name === 'SetRole' && msg.params.role === 'Sheriff'));
  event('SheriffKitPickedUp');
  assert.equal(messages.filter((msg) => msg.name === 'SetRole').length, 1);
});

test('unregistered F1 pickup does not grant a role', () => {
  const { round, messages, event } = setup();
  event('SheriffKitPickedUp');
  assert.equal(round.roleByPlayerId.get('tester'), 'Innocent');
  assert.equal(messages.length, 0);
});

for (const [name, change] of [
  ['normal round', ({ round }) => { round.useSoloRoleTest = false; }],
  ['multiplayer room', ({ room }) => { room.set('other', {}); }],
  ['multiple participants', ({ round }) => { round.participantIds.push('other'); }],
  ['loading', ({ round }) => { round.state = 'LOADING'; }],
  ['ended', ({ round }) => { round.roundEnded = true; }],
  ['dead player', ({ round }) => { round.aliveByPlayerId.set('tester', false); }],
  ['wrong role', ({ round }) => { round.roleByPlayerId.set('tester', 'Sheriff'); }],
  ['existing drop', ({ round }) => { round.sheriffDropIndex = 11; }],
]) {
  test(`F5 rejects ${name}`, () => {
    const state = setup();
    change(state);
    state.event('TestFakeSheriffDeath');
    assert.equal(state.messages.length, 0);
    assert.equal(state.round.aliveByPlayerId.get('__test_fake_sheriff__'), true);
  });
}

test('F5 rejects a stale round ID', () => {
  const { event, messages } = setup();
  event('TestFakeSheriffDeath', 'previous-round');
  assert.equal(messages.length, 0);
});
