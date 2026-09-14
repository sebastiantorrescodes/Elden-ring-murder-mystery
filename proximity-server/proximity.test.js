'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { computeGain } = require('./proximity');

const living = { roundAlive: true, mapX: 0, mapY: 0, mapRelY: 0 };

test('living players retain proximity chat in both directions', () => {
  for (const [distance, expected] of [[0, 1], [17.5, 0.25], [30, 0]]) {
    const other = { ...living, mapX: distance };
    assert.equal(computeGain(living, other), expected);
    assert.equal(computeGain(other, living), expected);
  }
});

test('dead and living players cannot hear each other, even after respawn', () => {
  const dead = { ...living, roundAlive: false, hp: 100 };
  assert.equal(computeGain(living, dead), 0);
  assert.equal(computeGain(dead, living), 0);
});

test('dead players can talk regardless of distance or respawn telemetry', () => {
  const dead = { ...living, roundAlive: false };
  for (const other of [
    { ...dead, mapX: 1000, hp: 100 },
    { roundAlive: false },
  ]) {
    assert.equal(computeGain(dead, other), 1);
    assert.equal(computeGain(other, dead), 1);
  }
});

test('peers outside the round cannot hear or speak to dead players', () => {
  const lobby = { ...living, roundAlive: undefined };
  const dead = { ...living, roundAlive: false };
  assert.equal(computeGain(lobby, dead), 0);
  assert.equal(computeGain(dead, lobby), 0);
});

test('clearing round life state restores normal proximity chat', () => {
  const first = { ...living, roundAlive: undefined };
  const second = { ...first, mapX: 17.5 };
  assert.equal(computeGain(first, second), 0.25);
  assert.equal(computeGain(second, first), 0.25);
  assert.equal(computeGain(first, {}), 0);
});
