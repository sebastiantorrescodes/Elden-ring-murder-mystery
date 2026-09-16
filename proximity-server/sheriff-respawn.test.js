'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname,
  '../src/MurderMystery/events/sheriff-startup-cleanup.js'), 'utf8');

for (let point = 1; point <= 13; point++) {
  test(`respawn startup preserves active Sheriff point ${point} and its shield`, () => {
    const activeFlag = 16009599 + point;
    const flags = new Map([[activeFlag, true]]);
    const deleted = [];
    let startup;
    vm.runInNewContext(source, {
      Default: 0, ON: true, OFF: false,
      $Event(id, _, fn) { assert.equal(id, 16009957); startup = fn; },
      DisableNetworkSync() {},
      SetEventFlagID(id, value) { flags.set(id, value); },
      DeleteAssetfollowingSFX(id) { deleted.push(id); },
    });
    startup();
    startup(); // repeated local map initialization must also be harmless
    assert.equal(flags.get(activeFlag), true);
    assert.deepEqual(deleted, []);
    assert.equal(flags.get(16009592), true);
    for (const id of [16009961,16009962,16009963,16009964,16009965,
      16009966,16009967,16009968,16009969,16009971]) {
      assert.equal(flags.get(id), false);
    }
  });
}
