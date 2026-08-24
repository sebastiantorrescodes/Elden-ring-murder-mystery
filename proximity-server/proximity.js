'use strict';

// blockId (see the BlockId comment in WorldChrMan.h) is what gates proximity here -
// a much more granular area identifier, confirmed against live data, than mapId/
// underground ever was. Two players' positions are only comparable when their
// blockId matches; otherwise they're in different loaded instances and distance
// between them is meaningless. (The gain distance itself is computed from mapX/mapY
// now, not raw ChrIns coordinates - see hasMapPosition/mapDistance below - but the
// same "different block = incomparable" logic still applies to those.)
const MIN_RANGE = 5; // full volume at or below this distance
const MAX_RANGE = 30; // silent at or beyond this distance

// Applied instead of distance math when two players are confirmed to be in
// different blocks - raw distance between them is meaningless at that point (they
// may not even be in overlapping coordinate spaces), so cut audio entirely rather
// than guess at a "nearby but not in the same space" volume.
const DIFFERENT_AREA_GAIN = 0;

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

// mapX/mapY (see the Location comment in WorldChrMan.h) come from each client's own
// CsMenuManImp read, sent alongside raw x/y/z. This is the ONLY thing the gain
// distance is computed from now - no fallback to raw x/y/z, and no z/depth
// contribution (that was a brief experiment - see git history for the weighted-z
// version if it's ever wanted back). One consequence: a peer that hasn't reported
// map coordinates yet can't have a gain computed at all - see hasMapPosition below.
function hasMapPosition(peer) {
  return (
    typeof peer.mapX === 'number' && Number.isFinite(peer.mapX) &&
    typeof peer.mapY === 'number' && Number.isFinite(peer.mapY)
  );
}

function mapDistance(a, b) {
  const dx = a.mapX - b.mapX;
  const dy = a.mapY - b.mapY;
  return Math.sqrt(dx * dx + dy * dy);
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
  // No raw x/y/z fallback anymore (see hasMapPosition) - a peer without map
  // coordinates yet (just joined, older client, mock-client.js) has no distance
  // that can be computed, so silence rather than guess.
  if (!hasMapPosition(self) || !hasMapPosition(other)) {
    return 0;
  }
  return distanceGain(mapDistance(self, other));
}

// Rotates a peer's position into "relative to the listener, in the listener's own
// facing frame" coordinates, for feeding a Web Audio PannerNode directly (rolloff
// disabled on that node - direction only, volume stays computeGain's job). Falls
// back to a neutral straight-ahead direction whenever there isn't enough information
// to compute a real one, rather than a stale or undefined direction: missing facing
// data, different block (direction is as meaningless as raw distance there), or the
// two positions coinciding (no defined direction when distance is ~0).
//
// NOTE: which world axis "yaw=0" faces was a first attempt ported from the DLL's
// Camera struct. Left/right handedness has since been confirmed in-game and fixed
// (see the rightX/rightZ comment below); front/back has not been separately
// confirmed yet - if that ever comes out swapped too, negate forwardComponent below.
//
// Deliberately still uses raw x/y/z here rather than distance3D's mapX/mapY: yawCos/
// yawSin come from the DLL's raw-coordinate-frame Camera read, so forward/right only
// project correctly against dx/dz in that same raw frame - mixing in map coordinates
// (a different, unrelated coordinate space) would scramble the panning direction.
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
  // Confirmed in-game left/right came out swapped with the other rotation direction
  // (rightX = forwardZ, rightZ = -forwardX) - flipped here. Front/back weren't
  // reported as swapped, so forwardComponent below is untouched.
  const rightX = -forwardZ;
  const rightZ = forwardX;

  const forwardComponent = dx * forwardX + dz * forwardZ;
  const rightComponent = dx * rightX + dz * rightZ;

  // Web Audio's default listener faces -Z with +Y up, so "forward" maps to -Z here.
  return { x: rightComponent, y: dy, z: -forwardComponent };
}

module.exports = {
  mapDistance,
  distanceGain,
  computeGain,
  computeRelativeOffset,
  sameBlock,
  MIN_RANGE,
  MAX_RANGE,
  DIFFERENT_AREA_GAIN,
};
