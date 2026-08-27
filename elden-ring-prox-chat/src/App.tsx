import { useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Settings as SettingsIcon, ArrowLeft, Check, Clipboard } from 'lucide-react';
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
import volcanoManorImage from '@/assets/ER_Volcano_Manor_Guest_Hall_Roof_Panorama.webp';

interface PeerRow {
  id: string;
  displayName: string;
  gain: number;
}

// Only one server in use - not something worth exposing as an editable field.
const SERVER_URL = 'wss://er-proximity-chat.duckdns.org';

const PLAYER_ID_STORAGE_KEY = 'elden-ring-prox-chat:playerId';
const DISPLAY_NAME_STORAGE_KEY = 'elden-ring-prox-chat:displayName';
const MIC_STORAGE_KEY = 'elden-ring-prox-chat:selectedMic';

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

function getStoredMic(): string {
  try {
    return localStorage.getItem(MIC_STORAGE_KEY) ?? '';
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
  const [hostId, setHostId] = useState<string | null>(null);
  const [micDevices, setMicDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedMic, setSelectedMic] = useState(() => getStoredMic());
  // True for a brief window right after clicking Save, so the settings view can show
  // a "Saved" confirmation before returning to the landing view.
  const [justSaved, setJustSaved] = useState(false);
  // True for a brief window right after clicking the room code's copy button, so it
  // can show a checkmark confirming the copy actually happened.
  const [roomCodeCopied, setRoomCodeCopied] = useState(false);
  const [status, setStatus] = useState('disconnected');
  const [isConnected, setIsConnected] = useState(false);
  const [peers, setPeers] = useState<PeerRow[]>([]);

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

  const localMeterRef = useRef<HTMLDivElement | null>(null);
  const peerMeterRefs = useRef(new Map<string, HTMLDivElement>());

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

  // Persist Settings fields as they're edited, not just at connect time, so the
  // choice sticks even if the app is closed from the settings view without ever
  // hosting/joining a room.
  useEffect(() => {
    try {
      localStorage.setItem(DISPLAY_NAME_STORAGE_KEY, displayName);
    } catch {
      // non-fatal - just won't be remembered next launch
    }
  }, [displayName]);

  useEffect(() => {
    if (!selectedMic) return; // avoid clobbering a stored value before devices finish loading
    try {
      localStorage.setItem(MIC_STORAGE_KEY, selectedMic);
    } catch {
      // non-fatal - just won't be remembered next launch
    }
  }, [selectedMic]);

  // displayName/selectedMic are already persisted live by the effects above - Save
  // just gives an explicit, visible confirmation; heading back is still manual (Back button).
  useEffect(() => {
    if (!justSaved) return;
    const timer = setTimeout(() => setJustSaved(false), 2000);
    return () => clearTimeout(timer);
  }, [justSaved]);

  const handleSaveSettings = () => {
    setJustSaved(true);
  };

  useEffect(() => {
    if (!roomCodeCopied) return;
    const timer = setTimeout(() => setRoomCodeCopied(false), 1500);
    return () => clearTimeout(timer);
  }, [roomCodeCopied]);

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
    setHostId(null);
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
        setHostId(msg.hostId);
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
    if (!activeRoomCode) return;
    navigator.clipboard.writeText(activeRoomCode).catch(() => {});
    setRoomCodeCopied(true);
  };

  // Main-process TCP server status + DLL readings - see preload.ts/dll-bridge.d.ts.
  useEffect(() => {
    const unsubscribeStatus = window.dllBridge.onStatus((message) => {
      console.log(message);
    });
    const unsubscribeReading = window.dllBridge.onReading((line) => {
      console.log('[dll]', line);

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
    <div className="relative flex min-h-screen items-center justify-center p-8">
    {!isConnected && view === 'landing' && (
      <Button
        variant="ghost"
        size="sm"
        className="absolute top-6 right-6"
        onClick={() => setView('settings')}
      >
        <SettingsIcon /> Settings
      </Button>
    )}
    {!isConnected && view === 'settings' && (
      <Button
        variant="ghost"
        size="sm"
        className="absolute top-6 left-6"
        onClick={() => setView('landing')}
      >
        <ArrowLeft /> Back
      </Button>
    )}
    {isConnected && (
      <>
        <div className="group absolute top-6 left-6 flex items-center gap-1">
          <h1 className="font-display text-2xl tracking-wide text-primary">
            Room: <span className="font-mono">{activeRoomCode}</span>
          </h1>
          <Button
            variant="ghost"
            size="icon"
            className="opacity-0 transition-opacity group-hover:opacity-100"
            onClick={handleCopyRoomCode}
          >
            {roomCodeCopied ? <Check /> : <Clipboard />}
          </Button>
        </div>
        <Button variant="destructive" className="absolute top-6 right-6" onClick={handleDisconnectClick}>
          Disconnect
        </Button>
        <div className="absolute top-24 right-0 flex w-64 flex-col items-center gap-2 pr-6">
          <h2 className="font-display text-lg tracking-wide text-primary">Choose Map</h2>
          <img
            src={volcanoManorImage}
            alt="Volcano Manor"
            className="h-80 w-full rounded-lg border border-border object-cover shadow-lg"
          />
          <span className="font-display text-sm tracking-wide text-primary">Volcano Manor</span>
        </div>
        <div className="absolute top-24 left-0 flex w-64 flex-col gap-3 pl-6">
          <h2 className="font-display text-lg tracking-wide text-primary">Players</h2>
          <ul className="flex flex-col gap-3 font-mono text-sm">
            <li className="flex items-center gap-2">
              <span>My mic:</span>
              {playerId === hostId && (
                <span className="rounded bg-primary/20 px-1.5 py-0.5 text-[10px] font-semibold text-primary">
                  Host
                </span>
              )}
              <Meter innerRef={localMeterRef} />
            </li>
            {peers.map((peer) => (
              <li key={peer.id} className="flex items-center gap-2">
                <span className="text-primary">
                  {peer.displayName}: {peer.gain.toFixed(2)}
                </span>
                {peer.id === hostId && (
                  <span className="rounded bg-primary/20 px-1.5 py-0.5 text-[10px] font-semibold text-primary">
                    Host
                  </span>
                )}
                <Meter
                  innerRef={(el) => {
                    if (el) peerMeterRefs.current.set(peer.id, el);
                    else peerMeterRefs.current.delete(peer.id);
                  }}
                />
              </li>
            ))}
          </ul>
        </div>
      </>
    )}
    <div className="mx-auto w-full max-w-3xl space-y-4">
      {view !== 'settings' && !isConnected && (
        <h1 className="text-center font-display font-semibold">
          <div className="text-6xl tracking-wide text-primary [text-shadow:0_0_20px_var(--primary)]">
            Elden Ring
          </div>
          <div className="text-3xl tracking-[0.2em] text-red-600 [text-shadow:0_0_20px_#dc2626]">
            Murder Mystery
          </div>
        </h1>
      )}

      {isConnected ? null : view === 'settings' ? (
        <div className="flex flex-col items-center gap-4 pt-6">
          <h2 className="font-display text-4xl tracking-wide text-primary">Settings</h2>
          <div className="grid gap-1.5">
            <Label htmlFor="settings-display-name">Display name</Label>
            <Input
              id="settings-display-name"
              className="w-72"
              placeholder="display name"
              value={displayName}
              onChange={(e) => setDisplayName(e.target.value)}
            />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="settings-mic">Microphone</Label>
            <Select value={selectedMic} onValueChange={setSelectedMic}>
              <SelectTrigger id="settings-mic" className="w-72">
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
          </div>

          <Button className="w-72" disabled={justSaved} onClick={handleSaveSettings}>
            {justSaved ? (
              <>
                <Check /> Saved
              </>
            ) : (
              'Save'
            )}
          </Button>
        </div>
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

          {status !== 'disconnected' && <p className="text-sm text-muted-foreground">{status}</p>}
        </div>
      )}
    </div>
    </div>
  );
}
