'use strict';

const MIN_RANGE = 5; // full volume at or below this distance
const MAX_RANGE = 30; // silent at or beyond this distance

// mapX/mapY (see the Location comment in WorldChrMan.h) come from each client's own
// CsMenuManImp read, sent alongside raw x/y/z. mapX/mapY plus mapRelY (elevation -
// see the MapRelativeCoords comment in WorldChrMan.h) drive the gain distance - no
// raw x/y/z, and no blockId gating either (mapX/mapY alone proved reliable enough in
// testing that the "confirmed different instance" blockId check was dropped - see
// git history for sameBlock/DIFFERENT_AREA_GAIN if it's ever wanted back). mapRelY
// replaced raw y here specifically because raw y is only stable within the current
// local map-tile grid cell and was observed flipping sign (which peer reads "higher")
// across a tile-boundary crossing with no actual elevation change - mapRelY comes
// from a different per-player struct (same base pointer, different offset - ported
// from tremwil/minusone12's map-relative-teleport Cheat Engine script) that needs no
// rebase to stay comparable across tiles in the open world. One consequence: a peer
// that hasn't reported map coordinates (or mapRelY) yet can't have a gain computed at
// all - see hasMapPosition below.
function hasMapPosition(peer) {
  return (
    typeof peer.mapX === 'number' && Number.isFinite(peer.mapX) &&
    typeof peer.mapY === 'number' && Number.isFinite(peer.mapY) &&
    typeof peer.mapRelY === 'number' && Number.isFinite(peer.mapRelY)
  );
}

// Weights elevation above mapX/mapY in the Euclidean sum (2.0 means a unit of
// elevation contributes ELEVATION_WEIGHT^2 = 4x as much to the squared distance as a
// unit of mapX/mapY, since the weight applies before squaring). Was 0.5, which made a
// typical one-floor gap (~9-10 units, back when this was tuned against raw y)
// collapse to ~4.9 weighted distance - under MIN_RANGE (5), so two players a floor
// apart still got full volume with no falloff at all. mapRelY uses the same
// underlying unit scale as the old raw y, so this should still roughly hold, but
// hasn't been re-verified against mapRelY specifically yet.
const ELEVATION_WEIGHT = 2.0;

function mapDistance(a, b) {
  const dx = a.mapX - b.mapX;
  const dy = a.mapY - b.mapY;
  const dElevation = (a.mapRelY - b.mapRelY) * ELEVATION_WEIGHT;
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
// data, missing map-relative position, or the two positions coinciding (no defined
// direction when distance is ~0).
//
// NOTE: which world axis "angle=0" faces was a first attempt, never independently
// confirmed in-game. An earlier version of this function flipped rightX/rightZ's
// sign after an in-game check reported left/right backwards with the untouched
// rotation - that flip has since been reverted back to the plain
// 90°-rotation-of-forward math below, per explicit request, without re-testing. If
// left/right (or front/back, via forwardComponent) ever comes out backwards again,
// that's the first place to look.
//
// dx/dz come from mapRelX/mapRelZ (see the MapRelativeCoords comment in
// WorldChrMan.h) rather than raw x/z: raw x/z was observed resetting discontinuously
// when crossing a local map-tile boundary, which scrambled rightComponent/
// forwardComponent's sign out of nowhere (perceived as left/right suddenly
// swapping) even with no real facing/position change. mapRelX/mapRelZ come from the
// same underlying per-player struct as raw x/y/z (same WorldChrMan base pointer,
// just a different offset - see tryReadMapRelative), not from distance3D's mapX/mapY
// (a different, unrelated CsMenuManImp-sourced coordinate space).
//
// forwardX/forwardZ are derived from mapRelRad (this peer's own character facing)
// instead of the Camera struct's yawCos/yawSin (the camera's facing): in a
// third-person game the camera can be orbited to face a completely different
// direction than the character actually does (e.g. spun around to look at your own
// character's face while still walking forward), which would otherwise make the
// whole soundstage rotate with the camera instead of staying tied to where players
// actually are relative to each other. mapRelRad comes from the same struct as
// mapRelX/mapRelZ, so it projects against them in the same coordinate frame. dy is
// still raw y (a separate, smaller concern - see mapDistance for why elevation
// specifically needed mapRelY).
const NEUTRAL_OFFSET = { x: 0, y: 0, z: -1 };
const MIN_HORIZONTAL_DISTANCE = 0.01;

function computeRelativeOffset(self, other) {
  const hasFacing =
    typeof self.mapRelRad === 'number' &&
    Number.isFinite(self.mapRelRad);

  const hasStableHorizontalPosition =
    typeof self.mapRelX === 'number' && Number.isFinite(self.mapRelX) &&
    typeof self.mapRelZ === 'number' && Number.isFinite(self.mapRelZ) &&
    typeof other.mapRelX === 'number' && Number.isFinite(other.mapRelX) &&
    typeof other.mapRelZ === 'number' && Number.isFinite(other.mapRelZ);

  if (!hasFacing || !hasStableHorizontalPosition) {
    return NEUTRAL_OFFSET;
  }

  const dx = other.mapRelX - self.mapRelX;
  const dy = other.y - self.y;
  const dz = other.mapRelZ - self.mapRelZ;

  if (Math.hypot(dx, dz) < MIN_HORIZONTAL_DISTANCE) {
    return NEUTRAL_OFFSET;
  }

  const forwardX = Math.sin(self.mapRelRad);
  const forwardZ = Math.cos(self.mapRelRad);
  // Plain 90° rotation of the forward vector to get "right" - see the NOTE above.
  const rightX = forwardZ;
  const rightZ = -forwardX;

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
