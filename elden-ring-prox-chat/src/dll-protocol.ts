export interface DllReading {
  slot: number;
  x: number;
  y: number;
  z: number;
  hp: number;
  maxHp: number;
}

const LINE_PATTERN =
  /^slot=(\d+) x=([-\d.]+) y=([-\d.]+) z=([-\d.]+) hp=(-?\d+) maxHp=(-?\d+)$/;

export function parseDllLine(line: string): DllReading | null {
  const match = LINE_PATTERN.exec(line.trim());
  if (!match) return null;
  const [, slot, x, y, z, hp, maxHp] = match;
  return {
    slot: Number(slot),
    x: Number(x),
    y: Number(y),
    z: Number(z),
    hp: Number(hp),
    maxHp: Number(maxHp),
  };
}

// The dedicated local-player HP/coord pointers in WorldChrMan.cpp (php, pmhp, lxcoord, ...)
// use the same offset chain as playerOffset=0 in the per-player array - i.e. slot 0 is
// structurally always the local player, never another peer.
export const LOCAL_PLAYER_SLOT = 0;

// This client's own current map/area (see the Location comment in WorldChrMan.h) - a
// separate line from slot=..., sent once per poll cycle rather than per slot.
export interface DllAreaReading {
  mapId: number;
  underground: number;
}

const AREA_LINE_PATTERN = /^area mapId=(-?\d+) underground=(-?\d+)$/;

export function parseAreaLine(line: string): DllAreaReading | null {
  const match = AREA_LINE_PATTERN.exec(line.trim());
  if (!match) return null;
  const [, mapId, underground] = match;
  return {
    mapId: Number(mapId),
    underground: Number(underground),
  };
}

// This client's own current camera facing direction (see the Camera comment in
// WorldChrMan.h) - another separate, once-per-poll-cycle line, not per slot. Which
// world axis "yaw=0" points along is not yet verified in-game.
export interface DllCameraReading {
  yawCos: number;
  yawSin: number;
}

const CAMERA_LINE_PATTERN = /^camera yawCos=([-\d.]+) yawSin=([-\d.]+)$/;

export function parseCameraLine(line: string): DllCameraReading | null {
  const match = CAMERA_LINE_PATTERN.exec(line.trim());
  if (!match) return null;
  const [, yawCos, yawSin] = match;
  return {
    yawCos: Number(yawCos),
    yawSin: Number(yawSin),
  };
}
