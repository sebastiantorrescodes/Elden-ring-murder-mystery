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
