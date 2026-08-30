// Thin wrapper around the proximity-server WebSocket protocol (see proximity-server/server.js).
export type PeerId = string;

interface WelcomeMessage {
  type: 'welcome';
  id: PeerId;
  room: string;
  hostId: PeerId | null;
  peers: { id: PeerId; displayName: string }[];
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
// relX/Y/Z are the peer's position already rotated into the listener's own facing
// frame server-side (see proximity.js's computeRelativeOffset) - feed straight into
// a PannerNode's position, no further rotation needed on this end.
export interface PeerVolume {
  gain: number;
  relX: number;
  relY: number;
  relZ: number;
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

  // mapId/underground/yawCos/yawSin/blockId/mapX/mapY/hp are all optional: an older
  // client or the manual test controls that omit them are treated by the server as
  // "unknown" rather than being penalized (area/block: trust distance; facing: no
  // directional cue; hp: not muted). blockId is what the server actually gates
  // proximity on - mapId/underground/mapX/mapY are kept for other potential uses but
  // don't drive that decision (yet, for mapX/mapY - see the Location comment in
  // WorldChrMan.h). hp === 0 mutes this peer's mic for everyone, regardless of
  // distance - see proximity.js's computeGain.
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
  ) {
    this.send({ type: 'position', x, y, z, mapId, underground, yawCos, yawSin, blockId, mapX, mapY, hp });
  }

  sendSignal(to: PeerId, data: unknown) {
    this.send({ type: 'signal', to, data });
  }

  // Only takes effect server-side if this client is the room's host - see server.js.
  sendCommand(to: PeerId, name: string, params?: Record<string, string>) {
    this.send({ type: 'command', to, name, params });
  }

  close() {
    this.ws.close();
  }
}
