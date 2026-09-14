'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname,
  '../src/MurderMystery/events/m16_00_00_00.emevd.dcx.js'), 'utf8');

// Run the actual event's presentation/claim prefix as a coroutine. WaitFor
// conditions are reevaluated each tick, as they are by the game's event engine.
function pickup() {
  const start = source.indexOf('$Event(16009960,');
  const body = source.slice(source.indexOf('{', start) + 1,
    source.indexOf('// INNOCENT', start))
    .replace(/WaitFor\(([\s\S]*?)\);/g, 'yield () => ($1);');
  const flags = new Map([[1, false], [2, false]]);
  const state = { visible: false, interact: false, claims: 0 };
  const restart = Symbol('restart');
  const context = {
    activeFlag: 1, permissionFlag: 2, assetEntityId: 3, OFF: false,
    DisableNetworkSync() {},
    DeleteAssetfollowingSFX() { state.visible = false; },
    CreateAssetfollowingSFX() { state.visible = true; },
    EventFlag: (id) => flags.get(id),
    SetEventFlagID: (id, value) => flags.set(id, value),
    SetNetworkconnectedEventFlagID: (id, value) => flags.set(id, value),
    CharacterHasSpEffect: (_, id) => id === 500002,
    ActionButtonInArea: () => state.interact,
    RestartEvent() { throw restart; },
  };
  const create = vm.runInNewContext(`(function* () {${body}\n})`, context);
  let iterator;
  let waiting;
  function reset() { iterator = create(); waiting = iterator.next(); }
  reset();
  function tick() {
    try {
      while (!waiting.done && waiting.value()) waiting = iterator.next();
      if (waiting.done) { state.claims++; reset(); }
    } catch (error) {
      if (error !== restart) throw error;
      reset();
    }
  }
  return { flags, state, tick };
}

test('a stale shared active flag never displays or awards an unauthorized pickup', () => {
  const { flags, state, tick } = pickup();
  flags.set(1, true);
  state.interact = true;
  tick();
  assert.equal(state.visible, false);
  assert.equal(state.claims, 0);
});

test('a newly authorized pickup appears and consumes its permission on claim', () => {
  const { flags, state, tick } = pickup();
  flags.set(1, true);
  flags.set(2, true);
  tick();
  assert.equal(state.visible, true);
  state.interact = true;
  tick();
  assert.equal(state.claims, 1);
  assert.equal(flags.get(2), false);
  assert.equal(flags.get(1), false);
  flags.set(1, true); // late replicated state from the old pickup
  tick();
  assert.equal(state.visible, false);
  assert.equal(state.claims, 1);
});

for (const [name, flag] of [['round cleanup', 2], ['another player claiming', 1]]) {
  test(`${name} removes the visual without awarding an item`, () => {
    const { flags, state, tick } = pickup();
    flags.set(1, true);
    flags.set(2, true);
    tick();
    assert.equal(state.visible, true);
    flags.set(flag, false);
    tick();
    assert.equal(state.visible, false);
    assert.equal(state.claims, 0);
    assert.equal(flags.get(2), false);
  });
}

test('constructor clears all ten local permissions before initializing pickup slots', () => {
  const constructor = source.slice(source.indexOf('$Event(0,'), source.indexOf('$Event(50,'));
  const firstPickup = constructor.indexOf('$InitializeEvent(0, 16009960,');
  for (let i = 0; i < 10; i++) {
    const clear = constructor.indexOf(`SetEventFlagID(${16009761 + i}, OFF);`);
    assert.ok(clear >= 0 && clear < firstPickup);
    assert.ok(constructor.includes(
      `$InitializeEvent(${i}, 16009960, ${16009861 + i}, ${17001800 + i}, ${16009761 + i});`));
  }
});
