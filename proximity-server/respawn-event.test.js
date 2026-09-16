'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname,
  '../src/MurderMystery/events/m16_00_00_00.emevd.dcx.js'), 'utf8');

test('destination switches after Commence effect is gone, and rearms on map reload', () => {
  const start = source.indexOf('$Event(16009631,');
  const body = source.slice(source.indexOf('{', start) + 1, source.indexOf('\n});', start))
    .replace(/WaitFor\(([\s\S]*?)\);/g, 'yield () => ($1);');
  const flags = new Map([[16009582, true], [16009580, false]]);
  const destinations = [];
  const restart = Symbol();
  const create = vm.runInNewContext(`(function* () {${body}})`, {
    roundtableRespawnPoint: 11101950, ON: true, OFF: false,
    DisableNetworkSync() {}, PlayerInMap: () => true,
    CharacterBackreadStatus: () => true, CharacterHasSpEffect: () => false,
    EventFlag: id => flags.get(id), SetEventFlagID: (id, value) => flags.set(id, value),
    SetPlayerRespawnPoint: id => destinations.push(id),
    RestartEvent() { throw restart; },
  });
  let iterator, waiting;
  function reset() { iterator = create(); waiting = iterator.next(); }
  function tick() {
    try { while (!waiting.done && waiting.value()) waiting = iterator.next(); }
    catch (err) { if (err !== restart) throw err; reset(); }
  }
  reset(); tick();
  assert.deepEqual(destinations, [16001952]);
  flags.set(16009580, true);
  tick(); tick();
  assert.equal(destinations.at(-1), 11101950);
  assert.equal(flags.get(16009581), true);
  reset(); tick();
  assert.equal(destinations.at(-1), 11101950);
});
