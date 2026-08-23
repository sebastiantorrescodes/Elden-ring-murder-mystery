// Thin wrapper around the proximity-server WebSocket protocol (see proximity-server/server.js).
export type PeerId = string;

interface WelcomeMessage {
  type: 'welcome';
  id: PeerId;
  room: string;
  peers: PeerId[];
}
interface PeerJoinedMessage {
  type: 'peer-joined';
  id: PeerId;
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
type ServerMessage =
  | WelcomeMessage
  | PeerJoinedMessage
  | PeerLeftMessage
  | VolumesMessage
  | SignalMessage;

export interface SignalingClientOptions {
  url: string;
  room: string;
  playerId: string;
  secret?: string; // required by the server when it's started with ROOM_SECRET set
  onOpen?: () => void;
  onClose?: () => void;
  onError?: () => void;
  onWelcome?: (msg: WelcomeMessage) => void;
  onPeerJoined?: (id: PeerId) => void;
  onPeerLeft?: (id: PeerId) => void;
  onVolumes?: (volumes: Record<PeerId, PeerVolume>) => void;
  onSignal?: (from: PeerId, data: unknown) => void;
}

export class SignalingClient {
  private ws: WebSocket;

  constructor(private options: SignalingClientOptions) {
    this.ws = new WebSocket(options.url);

    this.ws.addEventListener('open', () => {
      this.send({ type: 'join', room: options.room, id: options.playerId, secret: options.secret });
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
        this.options.onPeerJoined?.(msg.id);
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
    }
  }

  private send(message: unknown) {
    if (this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message));
    }
  }

  // mapId/underground/yawCos/yawSin/blockId are all optional: an older client or the
  // manual test controls that omit them are treated by the server as "unknown" rather
  // than being penalized (area/block: trust distance; facing: no directional cue).
  // blockId is what the server actually gates proximity on - mapId/underground are
  // kept for other potential uses but no longer drive that decision.
  sendPosition(
    x: number,
    y: number,
    z: number,
    mapId?: number,
    underground?: number,
    yawCos?: number,
    yawSin?: number,
    blockId?: number,
  ) {
    this.send({ type: 'position', x, y, z, mapId, underground, yawCos, yawSin, blockId });
  }

  sendSignal(to: PeerId, data: unknown) {
    this.send({ type: 'signal', to, data });
  }

  close() {
    this.ws.close();
  }
}
