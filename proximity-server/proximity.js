'use strict';

// blockId (see the BlockId comment in WorldChrMan.h) is what gates proximity here -
// a much more granular area identifier, confirmed against live data, than mapId/
// underground ever was. Two players' raw ChrIns x/y/z coordinates are only
// comparable when their blockId matches; otherwise they're in different loaded
// instances and raw distance between them is meaningless.
const MIN_RANGE = 5; // full volume at or below this distance
const MAX_RANGE = 30; // silent at or beyond this distance

// Applied instead of distance math when two players are confirmed to be in
// different blocks - a muffled "nearby but not in the same space" volume, rather
// than either a meaningless distance number or dead silence at an invisible line.
const DIFFERENT_AREA_GAIN = 0.25;

// blockId is optional on each peer - one that hasn't reported it (mock-client.js,
// the manual test controls, or a client predating this) is treated as "block
// unknown, trust the distance" rather than forced to DIFFERENT_AREA_GAIN, so
// distance-only testing still works.
function sameBlock(self, other) {
  const blockKnown =
    self.blockId !== undefined && self.blockId !== null && other.blockId !== undefined && other.blockId !== null;
  if (!blockKnown) {
    return true;
  }
  return self.blockId === other.blockId;
}

function distance3D(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

// Eased falloff (t normalized 0..1 across the range, gain = (1-t)^2) rather than a
// straight linear ramp - drops off faster up close and trails off more gently at
// range, closer to how real acoustics and perceived loudness actually behave than
// an even, constant-rate decline the whole way from MIN_RANGE to MAX_RANGE.
function distanceGain(distance) {
  if (distance <= MIN_RANGE) return 1;
  if (distance >= MAX_RANGE) return 0;
  const t = (distance - MIN_RANGE) / (MAX_RANGE - MIN_RANGE);
  return (1 - t) * (1 - t);
}

function computeGain(self, other) {
  if (!sameBlock(self, other)) {
    return DIFFERENT_AREA_GAIN;
  }
  return distanceGain(distance3D(self, other));
}

// Rotates a peer's position into "relative to the listener, in the listener's own
// facing frame" coordinates, for feeding a Web Audio PannerNode directly (rolloff
// disabled on that node - direction only, volume stays computeGain's job). Falls
// back to a neutral straight-ahead direction whenever there isn't enough information
// to compute a real one, rather than a stale or undefined direction: missing facing
// data, different block (direction is as meaningless as raw distance there), or the
// two positions coinciding (no defined direction when distance is ~0).
//
// NOTE: which world axis "yaw=0" faces, and the handedness of "right", are a first
// attempt ported from the DLL's Camera struct and not yet verified in-game - if
// left/right or front/back come out swapped once tested, flip the signs below.
const NEUTRAL_OFFSET = { x: 0, y: 0, z: -1 };
const MIN_HORIZONTAL_DISTANCE = 0.01;

function computeRelativeOffset(self, other) {
  const hasFacing =
    typeof self.yawCos === 'number' &&
    Number.isFinite(self.yawCos) &&
    typeof self.yawSin === 'number' &&
    Number.isFinite(self.yawSin);

  if (!sameBlock(self, other) || !hasFacing) {
    return NEUTRAL_OFFSET;
  }

  const dx = other.x - self.x;
  const dy = other.y - self.y;
  const dz = other.z - self.z;

  if (Math.hypot(dx, dz) < MIN_HORIZONTAL_DISTANCE) {
    return NEUTRAL_OFFSET;
  }

  const forwardX = self.yawSin;
  const forwardZ = self.yawCos;
  const rightX = forwardZ;
  const rightZ = -forwardX;

  const forwardComponent = dx * forwardX + dz * forwardZ;
  const rightComponent = dx * rightX + dz * rightZ;

  // Web Audio's default listener faces -Z with +Y up, so "forward" maps to -Z here.
  return { x: rightComponent, y: dy, z: -forwardComponent };
}

module.exports = {
  distance3D,
  distanceGain,
  computeGain,
  computeRelativeOffset,
  sameBlock,
  MIN_RANGE,
  MAX_RANGE,
  DIFFERENT_AREA_GAIN,
};
