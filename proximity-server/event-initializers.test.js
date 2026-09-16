'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname,
  '../src/MurderMystery/events/m16_00_00_00.emevd.dcx.js'), 'utf8');

for (const [id, slots] of [[16009620, 13], [16009631, 1]]) {
  test(`event ${id} constructor calls supply every parameter`, () => {
    const definition = source.match(new RegExp(`\\$Event\\(${id}, Restart, function\\(([^)]*)\\)`));
    assert.ok(definition);
    const parameterCount = definition[1].split(',').length;
    const calls = [...source.matchAll(new RegExp(`\\$InitializeEvent\\((\\d+), ${id}, ([^)]*)\\);`, 'g'))];
    assert.equal(calls.length, slots);
    calls.forEach(call => assert.equal(call[2].split(',').length, parameterCount));
    if (id === 16009631) assert.equal(calls[0][2], '11101950');
  });
}
