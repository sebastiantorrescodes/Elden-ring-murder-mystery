// Thin wrapper around the proximity-server WebSocket protocol (see proximity-server/server.js).
export type PeerId = string;

interface WelcomeMessage {
  type: 'welcome';
  id: PeerId;
  room: string;
  hostId: PeerId | null;
  peers: { id: PeerId; displayName: string; ready: boolean; micMuted: boolean }[];
}
interface PeerJoinedMessage {
  type: 'peer-joined';
  id: PeerId;
  displayName: string;
}
interface PeerLeftMessage {
  type: 'peer-left';
  id: PeerId;
}
// A peer's ready-up state changed (see server.js's 'ready' handler) - broadcast to
// the whole room including whoever sent it, so ready state stays fully
// server-authoritative rather than each client also tracking its own locally.
interface PeerReadyMessage {
  type: 'peer-ready';
  id: PeerId;
  ready: boolean;
}
// A peer muted/unmuted their own mic (see server.js's 'mic-muted' handler) - purely
// cosmetic, lets the rest of the room see a muted icon next to that peer's name.
interface PeerMicMutedMessage {
  type: 'peer-mic-muted';
  id: PeerId;
  micMuted: boolean;
}
// The host disconnected and the server promoted someone else (see server.js's
// close handler) - lets every remaining client pick up host-only UI/actions
// without needing to leave and rejoin.
interface HostChangedMessage {
  type: 'host-changed';
  hostId: PeerId;
}
// The round's shared deadline (host-only to send - see server.js's roomHosts
// check), broadcast to the whole room including the host, so every client
// counts down from the same absolute timestamp instead of drifting apart with
// independently-started local timers. null clears it (round ended, whether by
// End Game or by the countdown reaching zero).
interface RoundTimerMessage {
  type: 'round-timer';
  endsAt: number | null;
}
interface RoundStateMessage {
  type: 'round-state';
  state: 'LOADING' | 'STARTING' | 'ACTIVE' | 'ENDING' | 'WAITING';
  result?: 'InnocentWin' | 'MurdererWin' | 'Stalemate';
}
// No directional component - see server.js's broadcastVolumes.
export interface PeerVolume {
  gain: number;
}
interface VolumesMessage {
  type: 'volumes';
  volumes: Record<PeerId, PeerVolume>;
}
interface SignalMessage {
  type: 'signal';
  from: PeerId;
  data: unknown;
}
// Relayed from the room host to this peer only (see server.js's roomHosts check) -
// name matches an MMCommand enum value on the DLL side verbatim (e.g. "SetMurderer"),
// forwarded to window.dllBridge.sendCommand as-is. Unknown names are rejected DLL-side
// (HandleNetworkLine), not here - single source of truth for what's valid.
interface CommandMessage {
  type: 'command';
  name: string;
  params?: Record<string, string>;
}
type ServerMessage =
  | WelcomeMessage
  | PeerJoinedMessage
  | PeerLeftMessage
  | PeerReadyMessage
  | PeerMicMutedMessage
  | HostChangedMessage
  | RoundTimerMessage
  | RoundStateMessage
  | VolumesMessage
  | SignalMessage
  | CommandMessage;

export interface SignalingClientOptions {
  url: string;
  playerId: string;
  displayName: string;
  // Room code to join (from server.js's generateRoomCode) - omit to host a new room
  // instead, in which case the server generates one and returns it in onWelcome.
  room?: string;
  secret?: string; // only checked by the server when hosting - see server.js's ROOM_SECRET
  onOpen?: () => void;
  onClose?: () => void;
  onError?: () => void;
  onWelcome?: (msg: WelcomeMessage) => void;
  onPeerJoined?: (id: PeerId, displayName: string) => void;
  onPeerLeft?: (id: PeerId) => void;
  onPeerReady?: (id: PeerId, ready: boolean) => void;
  onPeerMicMuted?: (id: PeerId, micMuted: boolean) => void;
  onHostChanged?: (hostId: PeerId) => void;
  onRoundTimer?: (endsAt: number | null) => void;
  onRoundState?: (state: RoundStateMessage['state'], result?: RoundStateMessage['result']) => void;
  onVolumes?: (volumes: Record<PeerId, PeerVolume>) => void;
  onSignal?: (from: PeerId, data: unknown) => void;
  onCommand?: (name: string, params?: Record<string, string>) => void;
}

export class SignalingClient {
  private ws: WebSocket;

  constructor(private options: SignalingClientOptions) {
    this.ws = new WebSocket(options.url);

    this.ws.addEventListener('open', () => {
      if (options.room) {
        this.send({ type: 'join', room: options.room, id: options.playerId, displayName: options.displayName });
      } else {
        this.send({ type: 'host', id: options.playerId, displayName: options.displayName, secret: options.secret });
      }
      options.onOpen?.();
    });

    this.ws.addEventListener('message', (event) => {
      let msg: ServerMessage;
      try {
        msg = JSON.parse(event.data);
      } catch {
        return;
      }
      this.handleMessage(msg);
    });

    this.ws.addEventListener('close', () => options.onClose?.());
    this.ws.addEventListener('error', () => options.onError?.());
  }

  private handleMessage(msg: ServerMessage) {
    switch (msg.type) {
      case 'welcome':
        this.options.onWelcome?.(msg);
        break;
      case 'peer-joined':
        this.options.onPeerJoined?.(msg.id, msg.displayName);
        break;
      case 'peer-left':
        this.options.onPeerLeft?.(msg.id);
        break;
      case 'peer-ready':
        this.options.onPeerReady?.(msg.id, msg.ready);
        break;
      case 'peer-mic-muted':
        this.options.onPeerMicMuted?.(msg.id, msg.micMuted);
        break;
      case 'host-changed':
        this.options.onHostChanged?.(msg.hostId);
        break;
      case 'round-timer':
        this.options.onRoundTimer?.(msg.endsAt);
        break;
      case 'round-state':
        this.options.onRoundState?.(msg.state, msg.result);
        break;
      case 'volumes':
        this.options.onVolumes?.(msg.volumes);
        break;
      case 'signal':
        this.options.onSignal?.(msg.from, msg.data);
        break;
      case 'command':
        this.options.onCommand?.(msg.name, msg.params);
        break;
    }
  }

  private send(message: unknown) {
    if (this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message));
    }
  }

  // mapId/underground/yawCos/yawSin/blockId/mapX/mapY/hp/oriDeg/mapRelY/mapRelX/
  // mapRelZ/mapRelRad are all optional: an older client or the manual test controls
  // that omit them are treated by the server as "unknown" rather than being
  // penalized (area/block: trust distance; facing: no directional cue).
  // blockId is what the server actually gates proximity on -
  // mapId/underground/mapX/mapY are kept for other potential uses but don't drive
  // that decision (yet, for mapX/mapY - see the Location comment in
  // WorldChrMan.h). hp === 0 mutes this peer's mic for everyone, regardless of
  // distance - see proximity.js's computeGain. mapRelY (see the MapRelativeCoords
  // comment in WorldChrMan.h) is what computeGain's mapDistance uses for elevation
  // instead of raw y, since it shouldn't reset when crossing a local map-tile
  // boundary the way raw y does. mapRelX/mapRelZ/mapRelRad aren't currently
  // consumed server-side (directional/panning audio, which used to use them, was
  // removed) - still sent in case direction is ever revisited.
  // WorldChrMan.h). HP is telemetry only; the server's authoritative round life
  // state controls whether a listener may hear a Ghost. mapRelY/mapRelX/mapRelZ (see the
  // MapRelativeCoords comment in WorldChrMan.h) are what computeGain's mapDistance
  // (y) and computeRelativeOffset's panning direction (x/z) use instead of raw
  // y/x/z, since they shouldn't reset when crossing a local map-tile boundary the
  // way raw x/y/z does. mapRelRad is this client's own character facing (not the
  // camera's, unlike yawCos/yawSin) - computeRelativeOffset now uses it instead of
  // yawCos/yawSin for the forward/right basis, since a third-person camera can be
  // spun to face a different direction than the character actually does.
  sendPosition(
    x: number,
    y: number,
    z: number,
    mapId?: number,
    underground?: number,
    yawCos?: number,
    yawSin?: number,
    blockId?: number,
    mapX?: number,
    mapY?: number,
    hp?: number,
    oriDeg?: number,
    mapRelY?: number,
    mapRelX?: number,
    mapRelZ?: number,
    mapRelRad?: number,
  ) {
    this.send({
      type: 'position',
      x, y, z,
      mapId, underground,
      yawCos, yawSin,
      blockId,
      mapX, mapY,
      hp,
      oriDeg,
      mapRelY, mapRelX, mapRelZ, mapRelRad,
    });
  }

  sendSignal(to: PeerId, data: unknown) {
    this.send({ type: 'signal', to, data });
  }

  // Only takes effect server-side if this client is the room's host - see server.js.
  sendCommand(to: PeerId, name: string, params?: Record<string, string>) {
    this.send({ type: 'command', to, name, params });
  }

  // Only takes effect server-side if this client is the room's host - see server.js.
  // The target's socket is closed server-side, so they see it as an ordinary
  // disconnect (onClose) rather than a distinct "you were kicked" message.
  sendKick(id: PeerId) {
    this.send({ type: 'kick', id });
  }

  sendReady(ready: boolean) {
    this.send({ type: 'ready', ready });
  }

  sendLobbyState(inRoundtable: boolean, gameConnected: boolean) {
    this.send({ type: 'lobby-state', inRoundtable, gameConnected });
  }

  sendMicMuted(micMuted: boolean) {
    this.send({ type: 'mic-muted', micMuted });
  }

  // Host asks the server to begin a coordinated round.
  sendStartRound(durationMs: number) {
    this.send({
      type: 'start-round',
      durationMs,
    });
  }

  // Reports a local DLL event to the round coordinator.
  sendGameEvent(
    name:
    | 'PlayerLoaded'
    | 'PlayerDied'
    | 'ItemPickedUp'
    | 'SheriffKitPickedUp'
    | 'SheriffHit'
    | 'TestFakeSheriffDeath',
    roundId: string,
    index?: number,
    cause?: 'Sheriff' | 'Other',
  ) {
    this.send({
      type: 'game-event',
      name,
      roundId,
      ...(index !== undefined
        ? { index }
        : {}),
      ...(cause !== undefined
        ? { cause }
        : {}),
    });
  }

  sendEndRound() {
    this.send({ type: 'end-round' });
  }

  // Only takes effect server-side if this client is the room's host - see server.js.
  sendRoundTimer(endsAt: number | null) {
    this.send({ type: 'round-timer', endsAt });
  }

  close() {
    this.ws.close();
  }
}
