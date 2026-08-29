'use strict';

const MIN_RANGE = 5; // full volume at or below this distance
const MAX_RANGE = 30; // silent at or beyond this distance

// mapX/mapY (see the Location comment in WorldChrMan.h) come from each client's own
// CsMenuManImp read, sent alongside raw x/y/z. mapX/mapY plus raw y (elevation -
// Location has no third axis of its own, so the vertical component is borrowed
// straight from the raw x/y/z reading below; y rather than z since the raw frame is
// y-up - see computeRelativeOffset's panner mapping) drive the gain distance - no raw
// x/z, and no blockId gating either (mapX/mapY alone proved reliable enough in
// testing that the "confirmed different instance" blockId check was dropped - see
// git history for sameBlock/DIFFERENT_AREA_GAIN if it's ever wanted back). One
// consequence: a peer that hasn't reported map coordinates yet can't have a gain
// computed at all - see hasMapPosition below.
function hasMapPosition(peer) {
  return (
    typeof peer.mapX === 'number' && Number.isFinite(peer.mapX) &&
    typeof peer.mapY === 'number' && Number.isFinite(peer.mapY)
  );
}

// Weights elevation above mapX/mapY in the Euclidean sum (2.0 means a unit of
// elevation contributes ELEVATION_WEIGHT^2 = 4x as much to the squared distance as a
// unit of mapX/mapY, since the weight applies before squaring). Was 0.5, which made a
// typical one-floor gap (~9-10 raw y units) collapse to ~4.9 weighted distance - under
// MIN_RANGE (5), so two players a floor apart still got full volume with no falloff
// at all.
const ELEVATION_WEIGHT = 2.0;

function mapDistance(a, b) {
  const dx = a.mapX - b.mapX;
  const dy = a.mapY - b.mapY;
  const dElevation = (a.y - b.y) * ELEVATION_WEIGHT;
  return Math.sqrt(dx * dx + dy * dy + dElevation * dElevation);
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
  // A dead player (hp reported as exactly 0 - not undefined/unknown) is muted for
  // everyone in the room immediately, regardless of distance.
  if (other.hp === 0) {
    return 0;
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
// data, or the two positions coinciding (no defined direction when distance is ~0).
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

  if (!hasFacing) {
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
  MIN_RANGE,
  MAX_RANGE,
};
