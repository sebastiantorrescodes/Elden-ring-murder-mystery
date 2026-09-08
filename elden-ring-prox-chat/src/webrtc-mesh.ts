import type { PeerId } from './signaling-client';
import type { SignalingClient } from './signaling-client';

const ICE_SERVERS: RTCIceServer[] = [{ urls: 'stun:stun.l.google.com:19302' }];

interface PeerEntry {
  connection: RTCPeerConnection;
  pannerNode: PannerNode;
  gainNode: GainNode;
  analyser: AnalyserNode;
  audioSource: MediaStreamAudioSourceNode | null;
  pullElement: HTMLAudioElement | null;
}

// Rough 0..1 loudness (RMS of the time-domain waveform) for a simple visual level
// meter - lets you confirm audio is actually flowing without trusting speakers/ears.
function computeLevel(analyser: AnalyserNode): number {
  const data = new Uint8Array(analyser.fftSize);
  analyser.getByteTimeDomainData(data);
  let sumSquares = 0;
  for (let i = 0; i < data.length; i++) {
    const normalized = (data[i] - 128) / 128;
    sumSquares += normalized * normalized;
  }
  return Math.sqrt(sumSquares / data.length);
}

export type SignalPayload =
  | { kind: 'offer'; sdp: string }
  | { kind: 'answer'; sdp: string }
  | { kind: 'ice-candidate'; candidate: RTCIceCandidateInit };

// One RTCPeerConnection per other player in the room (mesh, not an SFU - see the
// earlier proximity-server discussion for why that's fine at coop party sizes).
// Offer/answer/ICE ride over the signaling server's generic 'signal' relay.
export class WebRtcMesh {
  private peers = new Map<PeerId, PeerEntry>();
  private audioCtx = new AudioContext();
  private localAnalyser: AnalyserNode | null = null;

  constructor(
    private signaling: SignalingClient,
    private localStream: MediaStream | null,
    outputDeviceId?: string,
  ) {
    this.audioCtx.resume().catch(() => undefined);

    if (outputDeviceId) {
      this.setOutputDevice(outputDeviceId);
    }

    // The listener stays fixed at the origin, facing -Z with +Y up (Web Audio's own
    // default) for the whole session - the server already rotates each peer's
    // position into this listener's own facing frame (see computeRelativeOffset),
    // so there's nothing here that needs to track the local player's real position
    // or orientation directly.
    const { listener } = this.audioCtx;
    listener.positionX.value = 0;
    listener.positionY.value = 0;
    listener.positionZ.value = 0;
    listener.forwardX.value = 0;
    listener.forwardY.value = 0;
    listener.forwardZ.value = -1;
    listener.upX.value = 0;
    listener.upY.value = 1;
    listener.upZ.value = 0;

    // Tapped straight off the mic, before it's ever sent anywhere - never connected
    // onward to destination, so this can't cause local monitoring/feedback.
    if (localStream) {
      const localSource = this.audioCtx.createMediaStreamSource(localStream);
      this.localAnalyser = this.audioCtx.createAnalyser();
      this.localAnalyser.fftSize = 256;
      localSource.connect(this.localAnalyser);
    }
  }

  // 0..1, for a "my mic" level meter - confirms input is actually reaching the
  // browser at all, independent of any peer connection.
  getLocalLevel(): number {
    return this.localAnalyser ? computeLevel(this.localAnalyser) : 0;
  }

  // 0..1, tapped after the gain stage - reflects what you'd actually hear from
  // this peer, including proximity attenuation.
  getPeerLevel(peerId: PeerId): number {
    const peer = this.peers.get(peerId);
    return peer ? computeLevel(peer.analyser) : 0;
  }

  // Peers already in the room when we join: don't offer, just prepare the
  // connection and wait for their offer (see handlePeerJoined) - avoids both
  // sides racing to offer the same pair.
  registerExistingPeer(peerId: PeerId) {
    this.getOrCreatePeer(peerId);
  }

  // Someone joined after us: we're the existing member, so we initiate.
  async handlePeerJoined(peerId: PeerId) {
    const peer = this.getOrCreatePeer(peerId);
    const offer = await peer.connection.createOffer();
    await peer.connection.setLocalDescription(offer);
    this.signaling.sendSignal(peerId, { kind: 'offer', sdp: offer.sdp });
  }

  handlePeerLeft(peerId: PeerId) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    peer.connection.close();
    peer.audioSource?.disconnect();
    peer.pannerNode.disconnect();
    peer.gainNode.disconnect();
    if (peer.pullElement) {
      peer.pullElement.pause();
      peer.pullElement.srcObject = null;
    }
    this.peers.delete(peerId);
  }

  async handleSignal(from: PeerId, data: SignalPayload) {
    const peer = this.getOrCreatePeer(from);
    if (data.kind === 'offer') {
      await peer.connection.setRemoteDescription({ type: 'offer', sdp: data.sdp });
      const answer = await peer.connection.createAnswer();
      await peer.connection.setLocalDescription(answer);
      this.signaling.sendSignal(from, { kind: 'answer', sdp: answer.sdp });
    } else if (data.kind === 'answer') {
      await peer.connection.setRemoteDescription({ type: 'answer', sdp: data.sdp });
    } else if (data.kind === 'ice-candidate') {
      try {
        await peer.connection.addIceCandidate(data.candidate);
      } catch {
        // late/invalid candidates aren't fatal - the connection can still succeed without them
      }
    }
  }

  // Applied from the server's 'volumes' message - the server owns the proximity
  // math, this just renders whatever gain it decided onto the peer's audio graph.
  setGain(peerId: PeerId, value: number) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    const now = this.audioCtx.currentTime;
    peer.gainNode.gain.cancelScheduledValues(now);
    peer.gainNode.gain.linearRampToValueAtTime(value, now + 0.15);
  }

  // relX/Y/Z come from the server's 'volumes' message, already rotated into this
  // listener's own facing frame (see PeerVolume in signaling-client.ts) - just
  // hand them to the panner as-is, no rotation needed on this end.
  setDirection(peerId: PeerId, relX: number, relY: number, relZ: number) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    const now = this.audioCtx.currentTime;
    peer.pannerNode.positionX.cancelScheduledValues(now);
    peer.pannerNode.positionY.cancelScheduledValues(now);
    peer.pannerNode.positionZ.cancelScheduledValues(now);
    peer.pannerNode.positionX.linearRampToValueAtTime(relX, now + 0.15);
    peer.pannerNode.positionY.linearRampToValueAtTime(relY, now + 0.15);
    peer.pannerNode.positionZ.linearRampToValueAtTime(relZ, now + 0.15);
  }

  // Routes all peer audio (which all converges on this shared audioCtx.destination -
  // see getOrCreatePeer) to a specific output device. This is the modern Audio Output
  // Devices API (AudioContext.prototype.setSinkId) - lib.dom.d.ts doesn't have it yet
  // as of TS 5.9 (only HTMLMediaElement.setSinkId is typed there), hence the cast, but
  // it's been in Chromium since M110 and this app ships on a much newer Electron/Chromium.
  async setOutputDevice(deviceId: string): Promise<void> {
    const ctx = this.audioCtx as AudioContext & { setSinkId?: (sinkId: string) => Promise<void> };
    if (!ctx.setSinkId) return;
    try {
      await ctx.setSinkId(deviceId);
    } catch {
      // output device selection just won't take effect
    }
  }

  closeAll() {
    for (const peerId of [...this.peers.keys()]) {
      this.handlePeerLeft(peerId);
    }
  }

  private getOrCreatePeer(peerId: PeerId): PeerEntry {
    const existing = this.peers.get(peerId);
    if (existing) return existing;

    const connection = new RTCPeerConnection({ iceServers: ICE_SERVERS });

    // Direction only - distance-based attenuation is disabled (rolloffFactor: 0) so
    // this never double-attenuates on top of the server's own gain value below.
    // Starts pointed straight ahead until the first 'volumes' message arrives.
    const pannerNode = this.audioCtx.createPanner();
    pannerNode.panningModel = 'HRTF';
    pannerNode.distanceModel = 'linear';
    pannerNode.rolloffFactor = 0;
    pannerNode.positionX.value = 0;
    pannerNode.positionY.value = 0;
    pannerNode.positionZ.value = -1;

    const gainNode = this.audioCtx.createGain();
    gainNode.gain.value = 1; // full volume until the server's first volumes message says otherwise
    const analyser = this.audioCtx.createAnalyser();
    analyser.fftSize = 256;
    pannerNode.connect(gainNode);
    gainNode.connect(analyser);
    analyser.connect(this.audioCtx.destination);

    const entry: PeerEntry = { connection, pannerNode, gainNode, analyser, audioSource: null, pullElement: null };
    this.peers.set(peerId, entry);

    if (this.localStream) {
      for (const track of this.localStream.getTracks()) {
        connection.addTrack(track, this.localStream);
      }
    }

    connection.addEventListener('icecandidate', (event) => {
      if (event.candidate) {
        this.signaling.sendSignal(peerId, {
          kind: 'ice-candidate',
          candidate: event.candidate.toJSON(),
        });
      }
    });

    connection.addEventListener('track', (event) => {
      const [remoteStream] = event.streams;
      entry.audioSource?.disconnect(); // renegotiation can refire this - don't leak the old node
      const audioSource = this.audioCtx.createMediaStreamSource(remoteStream);
      audioSource.connect(pannerNode);
      entry.audioSource = audioSource;

      // Chromium quirk: a remote WebRTC MediaStreamTrack can sit there fully
      // "connected" without ever actually decoding into a MediaStreamAudioSourceNode
      // unless something else is also consuming the stream. A hidden, muted <audio>
      // element forces the decode; it stays muted, all real (gain-controlled) output
      // goes through the Web Audio graph above.
      if (!entry.pullElement) {
        const pullElement = new Audio();
        pullElement.muted = true;
        pullElement.srcObject = remoteStream;
        pullElement.play().catch(() => undefined);
        entry.pullElement = pullElement;
      }
    });

    return entry;
  }
}
