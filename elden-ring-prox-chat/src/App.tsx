import { useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Switch } from '@/components/ui/switch';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import {
  parseDllLine,
  parseAreaLine,
  parseCameraLine,
  parseBlockLine,
  LOCAL_PLAYER_SLOT,
  type DllAreaReading,
  type DllCameraReading,
  type DllBlockReading,
} from './dll-protocol';
import { SignalingClient } from './signaling-client';
import { WebRtcMesh, type SignalPayload } from './webrtc-mesh';

interface PeerRow {
  id: string;
  gain: number;
}

function Meter({ innerRef }: { innerRef: React.Ref<HTMLDivElement> }) {
  return (
    <div className="h-2.5 w-36 overflow-hidden rounded-full bg-muted">
      <div ref={innerRef} className="h-full w-0 bg-primary" />
    </div>
  );
}

export default function App() {
  const [serverUrl, setServerUrl] = useState('wss://er-proximity-chat.duckdns.org');
  const [roomSecret, setRoomSecret] = useState('');
  const [playerId, setPlayerId] = useState('');
  const [roomId, setRoomId] = useState('test-room');
  const [micDevices, setMicDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedMic, setSelectedMic] = useState('');
  const [muted, setMuted] = useState(false);
  const [status, setStatus] = useState('disconnected');
  const [isConnected, setIsConnected] = useState(false);
  const [peers, setPeers] = useState<PeerRow[]>([]);
  const [dllStatus, setDllStatus] = useState('Waiting for DLL readings on 127.0.0.1:9191…');
  const [manualX, setManualX] = useState('0');
  const [manualY, setManualY] = useState('0');
  const [manualZ, setManualZ] = useState('0');

  const signalingRef = useRef<SignalingClient | null>(null);
  const meshRef = useRef<WebRtcMesh | null>(null);
  const localStreamRef = useRef<MediaStream | null>(null);
  const isConnectingRef = useRef(false);
  const peerGainsRef = useRef(new Map<string, number>());
  // area/camera/block lines arrive separately from slot= lines (once per poll cycle,
  // not per slot) - remember the latest of each so they can be attached to the next
  // position send.
  const lastAreaRef = useRef<DllAreaReading | null>(null);
  const lastCameraRef = useRef<DllCameraReading | null>(null);
  const lastBlockRef = useRef<DllBlockReading | null>(null);

  const logRef = useRef<HTMLDivElement | null>(null);
  const localMeterRef = useRef<HTMLDivElement | null>(null);
  const peerMeterRefs = useRef(new Map<string, HTMLDivElement>());

  const appendLog = (line: string) => {
    const el = logRef.current;
    if (!el) return;
    el.textContent += line + '\n';
    el.scrollTop = el.scrollHeight;
  };

  const syncPeerRows = () => {
    setPeers([...peerGainsRef.current.entries()].map(([id, gain]) => ({ id, gain })));
  };

  // Device labels are blank until a mic permission has been granted at least once,
  // so this is called both on mount and again right after getUserMedia succeeds.
  const populateMicList = async () => {
    const devices = await navigator.mediaDevices.enumerateDevices();
    const audioInputs = devices.filter((d) => d.kind === 'audioinput');
    setMicDevices(audioInputs);
    setSelectedMic((prev) =>
      audioInputs.some((d) => d.deviceId === prev) ? prev : (audioInputs[0]?.deviceId ?? ''),
    );
  };

  useEffect(() => {
    populateMicList();
    navigator.mediaDevices.addEventListener('devicechange', populateMicList);
    return () => navigator.mediaDevices.removeEventListener('devicechange', populateMicList);
  }, []);

  // Meter animation loop - bypasses React state/re-renders since this needs to run
  // every frame; the bars are updated by writing directly to the DOM via refs.
  useEffect(() => {
    let frame: number;
    const tick = () => {
      const mesh = meshRef.current;
      if (mesh) {
        if (localMeterRef.current) {
          localMeterRef.current.style.width = `${Math.min(100, mesh.getLocalLevel() * 300)}%`;
        }
        for (const [id, el] of peerMeterRefs.current) {
          el.style.width = `${Math.min(100, mesh.getPeerLevel(id) * 300)}%`;
        }
      }
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, []);

  const cleanup = () => {
    meshRef.current?.closeAll();
    meshRef.current = null;
    signalingRef.current = null;
    localStreamRef.current?.getTracks().forEach((track) => track.stop());
    localStreamRef.current = null;
    peerGainsRef.current.clear();
    peerMeterRefs.current.clear();
    setPeers([]);
    setStatus('disconnected');
    setIsConnected(false);
  };

  const doConnect = async () => {
    const url = serverUrl.trim() || 'ws://localhost:8080';
    const secret = roomSecret.trim() || undefined;
    const resolvedPlayerId =
      playerId.trim() || `player-${Math.random().toString(36).slice(2, 6)}`;
    const room = roomId.trim() || 'test-room';
    setPlayerId(resolvedPlayerId);

    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: selectedMic ? { deviceId: { exact: selectedMic } } : true,
        video: false,
      });
      localStreamRef.current = stream;
      await populateMicList(); // labels are blank until permission is granted; refresh now that it is
    } catch (err) {
      console.warn('[mic] getUserMedia failed, continuing without a mic', err);
      localStreamRef.current = null;
    }
    if (localStreamRef.current) {
      for (const track of localStreamRef.current.getAudioTracks()) {
        track.enabled = !muted;
      }
    }

    const signaling = new SignalingClient({
      url,
      room,
      playerId: resolvedPlayerId,
      secret,
      onOpen: () => {
        setStatus(`connected as ${resolvedPlayerId} in room "${room}"`);
        setIsConnected(true);
      },
      onClose: cleanup,
      onError: () => setStatus('connection error'),
      onWelcome: (msg) => {
        for (const peerId of msg.peers) {
          meshRef.current?.registerExistingPeer(peerId);
          if (!peerGainsRef.current.has(peerId)) peerGainsRef.current.set(peerId, 1);
        }
        syncPeerRows();
      },
      onPeerJoined: (peerId) => {
        meshRef.current?.handlePeerJoined(peerId);
        peerGainsRef.current.set(peerId, 1);
        syncPeerRows();
      },
      onPeerLeft: (peerId) => {
        meshRef.current?.handlePeerLeft(peerId);
        peerGainsRef.current.delete(peerId);
        peerMeterRefs.current.delete(peerId);
        syncPeerRows();
      },
      onVolumes: (volumes) => {
        for (const [peerId, v] of Object.entries(volumes)) {
          meshRef.current?.setGain(peerId, v.gain);
          meshRef.current?.setDirection(peerId, v.relX, v.relY, v.relZ);
          peerGainsRef.current.set(peerId, v.gain);
        }
        syncPeerRows();
      },
      onSignal: (from, data) => {
        meshRef.current?.handleSignal(from, data as SignalPayload);
      },
    });
    signalingRef.current = signaling;
    meshRef.current = new WebRtcMesh(signaling, localStreamRef.current);
  };

  const handleConnectClick = async () => {
    if (signalingRef.current) {
      signalingRef.current.close();
      return;
    }
    if (isConnectingRef.current) return;
    isConnectingRef.current = true;
    try {
      await doConnect();
    } finally {
      isConnectingRef.current = false;
    }
  };

  const handleSendPosition = () => {
    if (!signalingRef.current) return;
    signalingRef.current.sendPosition(
      Number(manualX) || 0,
      Number(manualY) || 0,
      Number(manualZ) || 0,
    );
  };

  const handleMuteChange = (checked: boolean) => {
    setMuted(checked);
    const stream = localStreamRef.current;
    if (!stream) return;
    for (const track of stream.getAudioTracks()) {
      track.enabled = !checked;
    }
  };

  // Main-process TCP server status + DLL readings - see preload.ts/dll-bridge.d.ts.
  useEffect(() => {
    const unsubscribeStatus = window.dllBridge.onStatus((message) => {
      console.log(message);
      setDllStatus(message);
      appendLog(message);
    });
    const unsubscribeReading = window.dllBridge.onReading((line) => {
      console.log('[dll]', line);
      setDllStatus('Receiving live DLL data:');
      appendLog(line);

      const area = parseAreaLine(line);
      if (area) {
        lastAreaRef.current = area;
        return;
      }

      const camera = parseCameraLine(line);
      if (camera) {
        lastCameraRef.current = camera;
        return;
      }

      const block = parseBlockLine(line);
      if (block) {
        lastBlockRef.current = block;
        return;
      }

      const reading = parseDllLine(line);
      if (reading && reading.slot === LOCAL_PLAYER_SLOT && signalingRef.current) {
        signalingRef.current.sendPosition(
          reading.x,
          reading.y,
          reading.z,
          lastAreaRef.current?.mapId,
          lastAreaRef.current?.underground,
          lastCameraRef.current?.yawCos,
          lastCameraRef.current?.yawSin,
          lastBlockRef.current?.blockId,
          lastAreaRef.current?.mapX,
          lastAreaRef.current?.mapY,
        );
      }
    });
    return () => {
      unsubscribeStatus();
      unsubscribeReading();
    };
  }, []);

  return (
    <div className="mx-auto max-w-3xl space-y-4 p-8">
      <h1 className="font-display text-4xl font-semibold tracking-wide text-primary [text-shadow:0_0_20px_var(--primary)]">
        Elden Ring Prox Chat
      </h1>

      <Card>
        <CardContent className="flex flex-wrap items-center gap-3 pt-6">
          <Input
            className="w-56"
            placeholder="server url"
            value={serverUrl}
            onChange={(e) => setServerUrl(e.target.value)}
          />
          <Input
            className="w-48"
            type="password"
            placeholder="room secret (if required)"
            value={roomSecret}
            onChange={(e) => setRoomSecret(e.target.value)}
          />
          <Input
            className="w-32"
            placeholder="player id"
            value={playerId}
            onChange={(e) => setPlayerId(e.target.value)}
          />
          <Input
            className="w-32"
            placeholder="room"
            value={roomId}
            onChange={(e) => setRoomId(e.target.value)}
          />
          <Select value={selectedMic} onValueChange={setSelectedMic}>
            <SelectTrigger className="w-56">
              <SelectValue placeholder="microphone" />
            </SelectTrigger>
            <SelectContent>
              {micDevices.map((device) => (
                <SelectItem key={device.deviceId} value={device.deviceId}>
                  {device.label || `Microphone (${device.deviceId.slice(0, 8)})`}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <Button onClick={handleConnectClick}>{isConnected ? 'Disconnect' : 'Connect'}</Button>
          <Label htmlFor="mute-switch" className="ml-auto">
            <Switch id="mute-switch" checked={muted} onCheckedChange={handleMuteChange} />
            Mute mic
          </Label>
        </CardContent>
      </Card>

      <p className="text-sm text-muted-foreground">{status}</p>

      <div className="flex items-center gap-2 text-sm">
        <span>My mic:</span>
        <Meter innerRef={localMeterRef} />
      </div>

      {peers.length > 0 && (
        <ul className="space-y-1 font-mono text-sm">
          {peers.map((peer) => (
            <li key={peer.id} className="flex items-center gap-2">
              <span className="text-primary">
                {peer.id}: {peer.gain.toFixed(2)}
              </span>
              <Meter
                innerRef={(el) => {
                  if (el) peerMeterRefs.current.set(peer.id, el);
                  else peerMeterRefs.current.delete(peer.id);
                }}
              />
            </li>
          ))}
        </ul>
      )}

      <Card>
        <CardHeader>
          <CardTitle className="font-display text-base tracking-wide text-primary">
            Manual position (for testing without the game/DLL)
          </CardTitle>
        </CardHeader>
        <CardContent className="flex flex-wrap items-end gap-3">
          <Label className="flex-col items-start gap-1">
            x
            <Input
              className="w-20"
              type="number"
              value={manualX}
              onChange={(e) => setManualX(e.target.value)}
            />
          </Label>
          <Label className="flex-col items-start gap-1">
            y
            <Input
              className="w-20"
              type="number"
              value={manualY}
              onChange={(e) => setManualY(e.target.value)}
            />
          </Label>
          <Label className="flex-col items-start gap-1">
            z
            <Input
              className="w-20"
              type="number"
              value={manualZ}
              onChange={(e) => setManualZ(e.target.value)}
            />
          </Label>
          <Button variant="secondary" disabled={!isConnected} onClick={handleSendPosition}>
            Send position
          </Button>
        </CardContent>
      </Card>

      <p className="text-sm text-muted-foreground">{dllStatus}</p>
      <div
        ref={logRef}
        className="h-80 overflow-y-auto rounded-lg border border-border bg-black/60 p-4 font-mono text-sm whitespace-pre-wrap text-amber-200/80 shadow-[inset_0_0_30px_rgba(0,0,0,0.6)]"
      />
    </div>
  );
}
