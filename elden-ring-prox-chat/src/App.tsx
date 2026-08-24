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
import { Settings as SettingsIcon, ArrowLeft } from 'lucide-react';
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
  displayName: string;
  gain: number;
}

// DLL polls every 250ms (dllmain.cpp) - anything past a couple seconds of silence
// means something's actually wrong (game closed, DLL unloaded, TCP layer still
// reconnecting), not just poll-cycle jitter.
const DLL_STALE_THRESHOLD_MS = 2000;

// Only one server in use - not something worth exposing as an editable field.
const SERVER_URL = 'wss://er-proximity-chat.duckdns.org';

const PLAYER_ID_STORAGE_KEY = 'elden-ring-prox-chat:playerId';
const DISPLAY_NAME_STORAGE_KEY = 'elden-ring-prox-chat:displayName';

// Stable per-install identity used for peer routing (server.js's room Map key) - not
// something the user picks or sees; displayName (below) is what's actually shown.
// Persisted so reconnecting/restarting the app doesn't churn peer ids for no reason.
function getOrCreatePlayerId(): string {
  try {
    const existing = localStorage.getItem(PLAYER_ID_STORAGE_KEY);
    if (existing) return existing;
    const fresh = crypto.randomUUID();
    localStorage.setItem(PLAYER_ID_STORAGE_KEY, fresh);
    return fresh;
  } catch {
    // localStorage unavailable for some reason - still lets the app work, just
    // without a stable id across restarts.
    return crypto.randomUUID();
  }
}

function getStoredDisplayName(): string {
  try {
    return localStorage.getItem(DISPLAY_NAME_STORAGE_KEY) ?? '';
  } catch {
    return '';
  }
}

function Meter({ innerRef }: { innerRef: React.Ref<HTMLDivElement> }) {
  return (
    <div className="h-2.5 w-36 overflow-hidden rounded-full bg-muted">
      <div ref={innerRef} className="h-full w-0 bg-primary" />
    </div>
  );
}

type View = 'landing' | 'settings';

export default function App() {
  // Only meaningful while !isConnected - Host/Join (the only ways to become
  // connected) only exist on the landing view, so there's no path from settings
  // straight into a connected session that would leave this stale.
  const [view, setView] = useState<View>('landing');
  const [playerId] = useState(() => getOrCreatePlayerId());
  const [displayName, setDisplayName] = useState(() => getStoredDisplayName());
  // What's typed into the "join" field - distinct from activeRoomCode (the room
  // actually connected to, whether hosted or joined), so hosting doesn't require
  // clearing this out first and joining doesn't overwrite it mid-edit.
  const [roomCodeInput, setRoomCodeInput] = useState('');
  const [activeRoomCode, setActiveRoomCode] = useState('');
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
  const peerDisplayNamesRef = useRef(new Map<string, string>());
  // area/camera/block lines arrive separately from slot= lines (once per poll cycle,
  // not per slot) - remember the latest of each so they can be attached to the next
  // position send.
  const lastAreaRef = useRef<DllAreaReading | null>(null);
  const lastCameraRef = useRef<DllCameraReading | null>(null);
  const lastBlockRef = useRef<DllBlockReading | null>(null);
  // Timestamp of the last DLL reading of any kind - the TCP layer already
  // reconnects on its own (see TcpTestClient.cpp), but nothing previously told the
  // UI when data had actually stopped arriving; it just kept showing whatever the
  // last line was, forever. 0 means "never received one yet".
  const lastReadingAtRef = useRef(0);
  const wasStaleRef = useRef(false);

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
    setPeers(
      [...peerGainsRef.current.entries()].map(([id, gain]) => ({
        id,
        displayName: peerDisplayNamesRef.current.get(id) ?? id,
        gain,
      })),
    );
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
    peerDisplayNamesRef.current.clear();
    peerMeterRefs.current.clear();
    setPeers([]);
    setActiveRoomCode('');
    setStatus('disconnected');
    setIsConnected(false);
    setView('landing');
  };

  // Shared by both hosting and joining - the only difference is whether `room` is
  // passed (join an existing room code) or omitted (ask the server to create one).
  const connect = async (room: string | undefined) => {
    const name = displayName.trim() || 'Player';
    setDisplayName(name);
    try {
      localStorage.setItem(DISPLAY_NAME_STORAGE_KEY, name);
    } catch {
      // non-fatal - just won't be remembered next launch
    }

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
      url: SERVER_URL,
      playerId,
      displayName: name,
      room,
      onOpen: () => {
        setStatus(room ? `joining room ${room}…` : 'creating room…');
      },
      onClose: cleanup,
      onError: () => setStatus('connection error'),
      onWelcome: (msg) => {
        setActiveRoomCode(msg.room);
        setStatus(`connected as "${name}" in room ${msg.room}`);
        setIsConnected(true);
        for (const peer of msg.peers) {
          meshRef.current?.registerExistingPeer(peer.id);
          peerDisplayNamesRef.current.set(peer.id, peer.displayName);
          if (!peerGainsRef.current.has(peer.id)) peerGainsRef.current.set(peer.id, 1);
        }
        syncPeerRows();
      },
      onPeerJoined: (peerId, peerDisplayName) => {
        meshRef.current?.handlePeerJoined(peerId);
        peerDisplayNamesRef.current.set(peerId, peerDisplayName);
        peerGainsRef.current.set(peerId, 1);
        syncPeerRows();
      },
      onPeerLeft: (peerId) => {
        meshRef.current?.handlePeerLeft(peerId);
        peerGainsRef.current.delete(peerId);
        peerDisplayNamesRef.current.delete(peerId);
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

  const handleHostClick = async () => {
    if (isConnectingRef.current || signalingRef.current) return;
    isConnectingRef.current = true;
    try {
      await connect(undefined);
    } finally {
      isConnectingRef.current = false;
    }
  };

  const handleJoinClick = async () => {
    const code = roomCodeInput.trim();
    if (!code || isConnectingRef.current || signalingRef.current) return;
    isConnectingRef.current = true;
    try {
      await connect(code);
    } finally {
      isConnectingRef.current = false;
    }
  };

  const handleDisconnectClick = () => {
    signalingRef.current?.close();
  };

  const handleCopyRoomCode = () => {
    if (activeRoomCode) {
      navigator.clipboard.writeText(activeRoomCode).catch(() => {});
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
      lastReadingAtRef.current = Date.now();
      if (wasStaleRef.current) {
        wasStaleRef.current = false;
        appendLog('[dll] data resumed');
      }
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

    // TCP-level reconnect already happens on its own (TcpTestClient.cpp), but until
    // it does, this is what actually surfaces "the DLL has gone quiet" in the UI -
    // otherwise dllStatus just sits on the last thing that happened, indistinguishable
    // from data still flowing normally.
    const staleCheckInterval = setInterval(() => {
      if (lastReadingAtRef.current === 0) return; // never received anything yet
      const idleMs = Date.now() - lastReadingAtRef.current;
      if (idleMs > DLL_STALE_THRESHOLD_MS) {
        wasStaleRef.current = true;
        setDllStatus(`No DLL data received in ${Math.round(idleMs / 1000)}s - is the game/DLL still running?`);
      }
    }, 1000);

    return () => {
      unsubscribeStatus();
      unsubscribeReading();
      clearInterval(staleCheckInterval);
    };
  }, []);

  return (
    <div className="mx-auto max-w-3xl space-y-4 p-8">
      <h1 className="text-center font-display font-semibold">
        <div className="text-6xl tracking-wide text-primary [text-shadow:0_0_20px_var(--primary)]">
          Elden Ring
        </div>
        <div className="text-3xl tracking-[0.2em] text-red-600 [text-shadow:0_0_20px_#dc2626]">
          Murder Mystery
        </div>
      </h1>

      {isConnected ? (
        <>
          <Card>
            <CardContent className="flex flex-wrap items-center gap-3 pt-6">
              <span className="text-sm">
                Room code: <span className="font-mono text-primary">{activeRoomCode}</span>
              </span>
              <Button variant="secondary" size="sm" onClick={handleCopyRoomCode}>
                Copy
              </Button>
              <Label htmlFor="mute-switch" className="ml-auto">
                <Switch id="mute-switch" checked={muted} onCheckedChange={handleMuteChange} />
                Mute mic
              </Label>
              <Button variant="destructive" onClick={handleDisconnectClick}>
                Disconnect
              </Button>
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
                    {peer.displayName}: {peer.gain.toFixed(2)}
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
              <Button variant="secondary" onClick={handleSendPosition}>
                Send position
              </Button>
            </CardContent>
          </Card>

          <p className="text-sm text-muted-foreground">{dllStatus}</p>
          <div
            ref={logRef}
            className="h-80 overflow-y-auto rounded-lg border border-border bg-black/60 p-4 font-mono text-sm whitespace-pre-wrap text-amber-200/80 shadow-[inset_0_0_30px_rgba(0,0,0,0.6)]"
          />
        </>
      ) : view === 'settings' ? (
        <Card>
          <CardHeader>
            <CardTitle className="font-display text-base tracking-wide text-primary">Settings</CardTitle>
          </CardHeader>
          <CardContent className="flex flex-col items-start gap-4">
            <Label className="flex-col items-start gap-1">
              Display name
              <Input
                className="w-56"
                placeholder="display name"
                value={displayName}
                onChange={(e) => setDisplayName(e.target.value)}
              />
            </Label>
            <Label className="flex-col items-start gap-1">
              Microphone
              <Select value={selectedMic} onValueChange={setSelectedMic}>
                <SelectTrigger className="w-72">
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
            </Label>
            <Button variant="secondary" onClick={() => setView('landing')}>
              <ArrowLeft /> Back
            </Button>
          </CardContent>
        </Card>
      ) : (
        <div className="flex flex-col items-center gap-4 pt-6">
          <p className="text-sm text-muted-foreground">
            Playing as <span className="text-primary">{displayName || 'no name set'}</span>
          </p>

          <Button size="lg" className="w-56" onClick={handleHostClick}>
            Host Room
          </Button>

          <span className="text-sm text-muted-foreground">or</span>

          <div className="flex items-center gap-3">
            <Input
              className="w-32"
              placeholder="room code"
              value={roomCodeInput}
              onChange={(e) => setRoomCodeInput(e.target.value.toUpperCase())}
            />
            <Button variant="secondary" disabled={!roomCodeInput.trim()} onClick={handleJoinClick}>
              Join Room
            </Button>
          </div>

          <Button variant="ghost" size="sm" onClick={() => setView('settings')}>
            <SettingsIcon /> Settings
          </Button>

          <p className="text-sm text-muted-foreground">{status}</p>
        </div>
      )}
    </div>
  );
}
