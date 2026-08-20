'use strict';

// NOTE: the DLL streams "PlayerLocalXCoord/Y/Z" (see WorldChrMan.h) - local/chunk-relative
// floats, not confirmed to be a single global frame. Two real players' raw coordinates may
// not be directly comparable across map chunks. This math is verified correct for same-frame
// coordinates, which is all the mock client below produces; treat real in-game distances as
// unverified until checked against known player positions in the same area.
const MIN_RANGE = 5; // full volume at or below this distance
const MAX_RANGE = 30; // silent at or beyond this distance

function distance3D(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function computeGain(distance) {
  if (distance <= MIN_RANGE) return 1;
  if (distance >= MAX_RANGE) return 0;
  return 1 - (distance - MIN_RANGE) / (MAX_RANGE - MIN_RANGE);
}

module.exports = { distance3D, computeGain, MIN_RANGE, MAX_RANGE };
