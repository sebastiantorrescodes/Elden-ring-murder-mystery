'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { readFileSync } = require('node:fs');
const vm = require('node:vm');

// Exercise the real message handler with an in-memory transport and inert timers.
function setup(env = {}) {
  const transport = new EventEmitter();
  const context = vm.createContext({
    require: (name) => name === 'ws'
      ? { WebSocketServer: class { constructor() { return transport; } } }
      : require(name),
    process: { env },
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
    ghostByPlayerId: new Map(),
  };
  context.state.roomRounds.set(roomId, round);
  messages.length = 0;
  return {
    round, messages, room: context.state.rooms.get(roomId), send, close: () => ws.emit('close'),
    event: (name, roundId = round.id) => send({ type: 'game-event', name, roundId }),
  };
}

test('F5 registers a drop and normal pickup promotes the solo Innocent once', () => {
  const { round, messages, event } = setup();
  event('TestFakeSheriffDeath');
  assert.equal(round.aliveByPlayerId.get('__test_fake_sheriff__'), false);
  assert.ok(messages.some(m => m.name === 'SetDeathRespawn' && m.params.destination === 'Roundtable'));
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

test('game disconnect immediately produces Stalemate with disconnect notice', () => {
  const { round, messages, send } = setup();
  send({ type: 'lobby-state', inRoundtable: false, gameConnected: false });
  assert.equal(round.state, 'ENDING');
  assert.ok(messages.some(m => m.name === 'RoundResult' && m.params.result === 'Stalemate' && m.params.disconnected === '1'));
});

test('leaving Roundtable while still connected does not end a round', () => {
  const { round, messages, send } = setup();
  send({ type: 'lobby-state', inRoundtable: false, gameConnected: true });
  assert.equal(round.state, 'ACTIVE');
  assert.ok(!messages.some(m => m.name === 'RoundResult'));
});

test('app socket close ends the round and notifies the remaining player', () => {
  const { round, room, close } = setup();
  const received = [];
  round.participantIds.push('other');
  room.set('other', { ws: { readyState: 1, OPEN: 1, send: s => received.push(JSON.parse(s)) } });
  close();
  assert.equal(round.state, 'ENDING');
  assert.ok(received.some(m => m.name === 'RoundResult' && m.params.result === 'Stalemate' && m.params.disconnected === '1'));
});

test('fixed point applies to actual Sheriff death and is broadcast to every participant', () => {
  const { round, messages, room, event } = setup({ MM_SHERIFF_DROP_POINT: '1' });
  round.roleByPlayerId.set('tester', 'Sheriff');
  round.participantIds.push('observer');
  round.roleByPlayerId.set('observer', 'Innocent');
  round.aliveByPlayerId.set('observer', true);
  const observerMessages = [];
  room.set('observer', {
    ws: { readyState: 1, OPEN: 1, send: s => observerMessages.push(JSON.parse(s)) },
  });
  event('PlayerDied');
  assert.equal(round.sheriffDropIndex, 1);
  for (const received of [messages, observerMessages]) {
    assert.equal(received.find(m => m.name === 'SheriffDrop').params.spawn, '1');
  }
});

test('fixed point applies to solo test drops', () => {
  const { round, event } = setup({ MM_SHERIFF_DROP_POINT: '13' });
  event('TestFakeSheriffDeath');
  assert.equal(round.sheriffDropIndex, 13);
});

test('zero restores random selection', () => {
  const { round, event } = setup({ MM_SHERIFF_DROP_POINT: '0' });
  event('TestFakeSheriffDeath');
  assert.ok(round.sheriffDropIndex >= 1 && round.sheriffDropIndex <= 13);
});

for (const value of ['-1', '14', '1.5', 'invalid']) {
  test(`invalid fixed drop setting ${value} fails explicitly`, () => {
    assert.throws(() => setup({ MM_SHERIFF_DROP_POINT: value }), /MM_SHERIFF_DROP_POINT/);
  });
}

for (const role of ['Innocent', 'Sheriff']) {
  test(`last ${role} death ends the round without spectator conversion or a kit drop`, () => {
    const { round, messages, event } = setup();
    round.roleByPlayerId.set('tester', role);
    round.aliveByPlayerId.set('__test_fake_sheriff__', false);
    event('PlayerDied');
    assert.equal(round.state, 'ENDING');
    assert.equal(round.roundEnded, true);
    assert.notEqual(round.ghostByPlayerId.get('tester'), true);
    assert.ok(messages.some(m => m.name === 'SetDeathRespawn' && m.params.destination === 'Roundtable'));
    assert.ok(!messages.some(m => m.name === 'EnterGhost' || m.name === 'SheriffDrop'));
    const count = messages.length;
    event('PlayerDied');
    assert.equal(messages.length, count);
  });
}

test('a non-final death still enters spectator mode and pre-arms the remaining real survivor', () => {
  const { round, messages, room, event } = setup();
  round.participantIds.push('__test_fake_sheriff__');
  const survivorMessages = [];
  room.set('__test_fake_sheriff__', {
    ws: { readyState: 1, OPEN: 1, send: s => survivorMessages.push(JSON.parse(s)) },
  });
  event('PlayerDied');
  assert.equal(round.state, 'ACTIVE');
  assert.equal(round.ghostByPlayerId.get('tester'), true);
  assert.ok(messages.some(m => m.name === 'EnterGhost'));
  assert.ok(survivorMessages.some(m => m.name === 'SetDeathRespawn' && m.params.destination === 'Roundtable'));
});
