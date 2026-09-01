import { useEffect, useRef, useState } from 'react';
import { cn } from '@/lib/utils';
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
  ready: boolean;
}

// Only one server in use - not something worth exposing as an editable field.
// const SERVER_URL = 'wss://er-proximity-chat.duckdns.org'; // < og
const SERVER_URL = 'ws://localhost:8080';

// Murder Mystery needs exactly one Murderer and one Sheriff plus at least two
// Innocents to be worth playing, and stops making sense past 6 for a single room.
// MIN temporarily dropped to 1 for solo testing - restore to 4 before real games.
const MIN_GAME_PLAYERS = 1;
const MAX_GAME_PLAYERS = 6;

// Round length options, in minutes - the host picks one in Settings; the chosen
// value is what handleStartGameClick multiplies out to milliseconds and sends
// via sendRoundTimer.
const ROUND_DURATION_OPTIONS_MIN = [1, 5, 10] as const;
type RoundDurationMin = (typeof ROUND_DURATION_OPTIONS_MIN)[number];
const DEFAULT_ROUND_DURATION_MIN: RoundDurationMin = 5;

const PLAYER_ID_STORAGE_KEY = 'elden-ring-prox-chat:playerId';
const DISPLAY_NAME_STORAGE_KEY = 'elden-ring-prox-chat:displayName';
const MIC_STORAGE_KEY = 'elden-ring-prox-chat:selectedMic';
const OUTPUT_DEVICE_STORAGE_KEY = 'elden-ring-prox-chat:selectedOutputDevice';
const ROUND_DURATION_STORAGE_KEY = 'elden-ring-prox-chat:roundDurationMin';

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

function getStoredOutputDevice(): string {
  try {
    return localStorage.getItem(OUTPUT_DEVICE_STORAGE_KEY) ?? '';
  } catch {
    return '';
  }
}

function getStoredRoundDurationMin(): RoundDurationMin {
  try {
    const stored = Number(localStorage.getItem(ROUND_DURATION_STORAGE_KEY));
    if (ROUND_DURATION_OPTIONS_MIN.includes(stored as RoundDurationMin)) {
      return stored as RoundDurationMin;
    }
  } catch {
    // fall through to default
  }
  return DEFAULT_ROUND_DURATION_MIN;
}

function formatDuration(ms: number): string {
  const totalSeconds = Math.ceil(ms / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${seconds.toString().padStart(2, '0')}`;
}

function Meter({ innerRef }: { innerRef: React.Ref<HTMLDivElement> }) {
  return (
    <div className="h-2.5 w-36 overflow-hidden rounded-full bg-muted">
      <div ref={innerRef} className="h-full w-0 bg-primary" />
    </div>
  );
}

// 'main' is the landing form while disconnected, or the room UI once connected.
// 'settings' (display name/mic - open to everyone) and 'game-settings' (round
// length - host-only, see the isHost-gated button that opens it) both overlay
// 'main' and don't affect isConnected/the room's live audio/signaling state
// underneath them.
type View = 'main' | 'settings' | 'game-settings';

export default function App() {
  const [view, setView] = useState<View>('main');
  const [playerId] = useState(() => getOrCreatePlayerId());
  const [displayName, setDisplayName] = useState(() => getStoredDisplayName());
  // What's typed into the "join" field - distinct from activeRoomCode (the room
  // actually connected to, whether hosted or joined), so hosting doesn't require
  // clearing this out first and joining doesn't overwrite it mid-edit.
  const [roomCodeInput, setRoomCodeInput] = useState('');
  const [activeRoomCode, setActiveRoomCode] = useState('');
  const [hostId, setHostId] = useState<string | null>(null);
  const isHost = playerId === hostId;
  // True once the host has clicked Start Game, so the button swaps to End Game -
  // purely local UI feedback that the click registered, and a guard against
  // firing handleStartGameClick (and re-rolling/re-sending roles) more than once
  // per round.
  const [gameActive, setGameActive] = useState(false);
  // This player's own ready-up state. Set only from the server's echoed
  // 'peer-ready' (see onPeerReady below), not optimistically on click - keeps it
  // in sync with what the rest of the room actually sees, same reasoning as
  // peerReadyRef for everyone else.
  const [myReady, setMyReady] = useState(false);
  // Shared round deadline, echoed back from the server for every client (see
  // onRoundTimer below) - null means no round is currently timed. A separate
  // `nowTick` state (updated by the effect below) is what actually drives the
  // countdown re-rendering; roundEndsAt itself never changes except at round
  // start/end.
  const [roundEndsAt, setRoundEndsAt] = useState<number | null>(null);
  const [nowTick, setNowTick] = useState(() => Date.now());
  const [micDevices, setMicDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedMic, setSelectedMic] = useState(() => getStoredMic());
  const [outputDevices, setOutputDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedOutputDevice, setSelectedOutputDevice] = useState(() => getStoredOutputDevice());
  const [roundDurationMin, setRoundDurationMin] = useState(() => getStoredRoundDurationMin());
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
  const peerReadyRef = useRef(new Map<string, boolean>());
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
        ready: peerReadyRef.current.get(id) ?? false,
      })),
    );
  };

  // Device labels are blank until a mic permission has been granted at least once,
  // so this is called both on mount and again right after getUserMedia succeeds.
  // Output device labels unlock the same way, off that same mic permission - there's
  // no separate permission prompt for enumerating audiooutput devices in Chromium.
  const populateAudioDevices = async () => {
    const devices = await navigator.mediaDevices.enumerateDevices();
    const audioInputs = devices.filter((d) => d.kind === 'audioinput');
    const audioOutputs = devices.filter((d) => d.kind === 'audiooutput');
    setMicDevices(audioInputs);
    setOutputDevices(audioOutputs);
    setSelectedMic((prev) =>
      audioInputs.some((d) => d.deviceId === prev) ? prev : (audioInputs[0]?.deviceId ?? ''),
    );
    setSelectedOutputDevice((prev) =>
      audioOutputs.some((d) => d.deviceId === prev) ? prev : (audioOutputs[0]?.deviceId ?? ''),
    );
  };

  useEffect(() => {
    populateAudioDevices();
    navigator.mediaDevices.addEventListener('devicechange', populateAudioDevices);
    return () => navigator.mediaDevices.removeEventListener('devicechange', populateAudioDevices);
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

  useEffect(() => {
    if (!selectedOutputDevice) return; // avoid clobbering a stored value before devices finish loading
    try {
      localStorage.setItem(OUTPUT_DEVICE_STORAGE_KEY, selectedOutputDevice);
    } catch {
      // non-fatal - just won't be remembered next launch
    }
  }, [selectedOutputDevice]);

  useEffect(() => {
    try {
      localStorage.setItem(ROUND_DURATION_STORAGE_KEY, String(roundDurationMin));
    } catch {
      // non-fatal - just won't be remembered next launch
    }
  }, [roundDurationMin]);

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
    peerReadyRef.current.clear();
    peerMeterRefs.current.clear();
    setPeers([]);
    setActiveRoomCode('');
    setHostId(null);
    setGameActive(false);
    setMyReady(false);
    setRoundEndsAt(null);
    setStatus('disconnected');
    setIsConnected(false);
    setView('main');
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
      await populateAudioDevices(); // labels are blank until permission is granted; refresh now that it is
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
          peerReadyRef.current.set(peer.id, peer.ready);
          if (!peerGainsRef.current.has(peer.id)) peerGainsRef.current.set(peer.id, 1);
        }
        syncPeerRows();
      },
      onPeerJoined: (peerId, peerDisplayName) => {
        meshRef.current?.handlePeerJoined(peerId);
        peerDisplayNamesRef.current.set(peerId, peerDisplayName);
        peerGainsRef.current.set(peerId, 1);
        peerReadyRef.current.set(peerId, false); // fresh peer state is always unready - see makeInitialPeerState
        syncPeerRows();
      },
      onPeerLeft: (peerId) => {
        meshRef.current?.handlePeerLeft(peerId);
        peerGainsRef.current.delete(peerId);
        peerDisplayNamesRef.current.delete(peerId);
        peerReadyRef.current.delete(peerId);
        peerMeterRefs.current.delete(peerId);
        syncPeerRows();
      },
      onPeerReady: (peerId, ready) => {
        if (peerId === playerId) {
          setMyReady(ready);
          return;
        }
        peerReadyRef.current.set(peerId, ready);
        syncPeerRows();
      },
      onRoundTimer: (endsAt) => setRoundEndsAt(endsAt),
      onRoundState: (state, result) => {
        if (state === 'LOADING') {
          setGameActive(true);
          setStatus('waiting for all players to load...');
        } else if (state === 'STARTING') {
          setStatus('assigning roles...');
        } else if (state === 'ACTIVE') {
          setGameActive(true);
          setStatus('round active');
        } else if (state === 'ENDING') {
          setStatus(`round ended: ${result ?? 'Stalemate'}`);
        } else if (state === 'WAITING') {
          setGameActive(false);
          setStatus('returned to lobby');
        }
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
      // Relayed from the room host via server.js - forward straight to the local
      // MurderMystery.dll connection (see main.ts's sendMmCommand / preload.ts).
      onCommand: (name, params) => {
        void window.dllBridge.sendCommand(name, params);
      },
    });
    signalingRef.current = signaling;
    meshRef.current = new WebRtcMesh(signaling, localStreamRef.current, selectedOutputDevice);
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

  // Fisher-Yates - Math.random()-based .sort() comparators are a common shortcut but
  // don't actually produce a uniform shuffle, and fairness is the whole point of
  // randomly picking who's the Murderer.

  // unused supposedly

  // function shuffled<T>(items: T[]): T[] {
  //   const result = [...items];
  //   for (let i = result.length - 1; i > 0; i--) {
  //     const j = Math.floor(Math.random() * (i + 1));
  //     [result[i], result[j]] = [result[j], result[i]];
  //   }
  //   return result;
  // }

  // Toggles this player's own ready-up state - anyone in the room can do this,
  // not just the host (unlike Start/End Game below).
  const handleReadyToggle = () => {
    signalingRef.current?.sendReady(!myReady);
  };

  // Only the room host's "command" messages are honored server-side (see server.js) -
  // this is the one place role assignment happens, not something each client
  // decides for itself. The button is rendered for everyone but faded/inert for
  // non-hosts (see the JSX below).
  const handleStartGameClick = () => {
    if (!signalingRef.current || gameActive) return;

    const playerCount = 1 + peers.length;

    if (
      playerCount < MIN_GAME_PLAYERS ||
      playerCount > MAX_GAME_PLAYERS
    ) {
      setStatus(
        `need ${MIN_GAME_PLAYERS}-${MAX_GAME_PLAYERS} players ` +
        `to start (currently ${playerCount})`,
      );
      return;
    }

    if (
      !myReady ||
      peers.some((peer) => !peer.ready)
    ) {
      setStatus(
        'all players must be ready to start',
      );
      return;
    }

    signalingRef.current.sendStartRound(
      roundDurationMin * 60_000,
    );

    // Prevent repeated clicks while the server waits for every
    // participant to load. The actual timer remains null until
    // the server broadcasts it after Commence.
    setGameActive(true);
    setStatus('waiting for all players to load...');
  };

  // Sends everyone (including the host) back to the Roundtable - same
  // FLAG_RETURN_TO_HUB_REQUEST the DLL's F9 test hotkey sets - then flips the
  // button back so the host can start another round. Also used by the
  // round-timer effect below to auto-end when the countdown hits zero, so
  // clearing the timer here (rather than only on a manual click) covers both.
  const handleEndGameClick = () => {
    if (!signalingRef.current) return;

    signalingRef.current.sendEndRound();
    setStatus('ending round...');
  };

  // Countdown re-render tick - only runs while a round timer is active, and
  // stops itself the instant roundEndsAt clears (see the null case above).
  useEffect(() => {
    if (roundEndsAt === null) return;
    // nowTick may be stale by however long the app sat idle before this round
    // started - refresh it immediately rather than waiting up to 500ms for the
    // first interval tick, which would otherwise briefly show too much time left.
    setNowTick(Date.now());
    const interval = setInterval(() => setNowTick(Date.now()), 500);
    return () => clearInterval(interval);
  }, [roundEndsAt]);

  const remainingMs = roundEndsAt === null ? null : Math.max(0, roundEndsAt - nowTick);

  const handleCopyRoomCode = () => {
    if (!activeRoomCode) return;
    navigator.clipboard
      .writeText(activeRoomCode)
      .then(() => setRoomCodeCopied(true))
      .catch((err) => console.warn('[clipboard] copy failed', err));
  };

  // Main-process TCP server status + DLL readings - see preload.ts/dll-bridge.d.ts.
  useEffect(() => {
    const unsubscribeStatus = window.dllBridge.onStatus((message) => {
      console.log(message);
    });

    const unsubscribeMmEvent =
      window.dllBridge.onMmEvent((line) => {
        console.log('[mm-event]', line);

        const tokens = line.trim().split(/\s+/);
        const values = new Map<string, string>();

        for (const token of tokens) {
          const separator = token.indexOf('=');

          if (separator > 0) {
            values.set(
              token.slice(0, separator),
              token.slice(separator + 1),
            );
          }
        }

        const eventName = values.get('event');
        const roundId = values.get('round');

        if (
          (eventName === 'PlayerLoaded' ||
            eventName === 'PlayerDied' ||
            eventName === 'ItemPickedUp') &&
          roundId &&
          signalingRef.current
        ) {
          const parsedIndex =
            eventName === 'ItemPickedUp'
              ? Number(values.get('index'))
              : undefined;

          if (
            eventName === 'ItemPickedUp' &&
            (
              !Number.isInteger(parsedIndex) ||
              parsedIndex === undefined ||
              parsedIndex < 1 ||
              parsedIndex > 10
            )
          ) {
            console.warn(
              '[mm-event] invalid item pickup index',
              line,
            );

            return;
          }

          signalingRef.current.sendGameEvent(
            eventName,
            roundId,
            parsedIndex,
          );
        }
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
          reading.hp,
        );
      }
    });

    return () => {
      unsubscribeStatus();
      unsubscribeReading();
      unsubscribeMmEvent();
    };
  }, []);

  const allReady = myReady && peers.every((peer) => peer.ready);

  return (
    <div className="relative flex min-h-screen items-center justify-center p-8">
    {!isConnected && view === 'main' && (
      <Button
        variant="ghost"
        size="sm"
        className="absolute top-6 right-6"
        onClick={() => setView('settings')}
      >
        <SettingsIcon /> Settings
      </Button>
    )}
    {view !== 'main' && (
      <Button
        variant="ghost"
        size="sm"
        className="absolute top-6 left-6"
        onClick={() => setView('main')}
      >
        <ArrowLeft /> Back
      </Button>
    )}
    {isConnected && view === 'main' && (
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
        <div className="absolute top-6 right-6 flex items-center gap-2">
          {isHost && (
            <Button variant="ghost" size="sm" onClick={() => setView('game-settings')}>
              Game Settings
            </Button>
          )}
          <Button variant="destructive" onClick={handleDisconnectClick}>
            Disconnect
          </Button>
        </div>
        {remainingMs !== null && (
          <h1 className="absolute top-6 left-1/2 -translate-x-1/2 font-display text-2xl tracking-wide text-primary">
            <span className="font-mono">{formatDuration(remainingMs)}</span>
          </h1>
        )}
        <div className="absolute top-24 right-0 flex w-64 flex-col items-center gap-2 pr-6">
          <h2 className="font-display text-lg tracking-wide text-primary whitespace-nowrap">Choose Map</h2>
          <img
            src={volcanoManorImage}
            alt="Volcano Manor"
            className="h-80 w-full rounded-lg border border-border object-cover shadow-lg"
          />
          <span className="font-display text-sm tracking-wide text-primary">Volcano Manor</span>
        </div>
        <div className="absolute top-24 left-0 flex w-64 flex-col gap-3 pl-6">
          <h2 className="font-display text-lg tracking-wide text-primary whitespace-nowrap">Players</h2>
          <ul className="flex flex-col gap-3 font-mono text-sm">
            <li className="flex items-center gap-2 whitespace-nowrap">
              <span>My mic:</span>
              {isHost && (
                <span className="rounded bg-primary/20 px-1.5 py-0.5 text-[10px] font-semibold text-primary">
                  Host
                </span>
              )}
              <Meter innerRef={localMeterRef} />
              <button
                type="button"
                onClick={handleReadyToggle}
                className={cn(
                  'ml-4 rounded px-1.5 py-0.5 text-[10px] font-semibold transition-colors',
                  myReady
                    ? 'bg-green-500/20 text-green-400'
                    : 'bg-muted text-muted-foreground hover:bg-primary/10',
                )}
              >
                {myReady ? 'Ready' : 'Not Ready'}
              </button>
            </li>
            {peers.map((peer) => (
              <li key={peer.id} className="flex items-center gap-2 whitespace-nowrap">
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
                <span
                  className={cn(
                    'ml-8 rounded px-1.5 py-0.5 text-[10px] font-semibold',
                    peer.ready ? 'bg-green-500/20 text-green-400' : 'bg-muted text-muted-foreground',
                  )}
                >
                  {peer.ready ? 'Ready' : 'Not Ready'}
                </span>
              </li>
            ))}
          </ul>
        </div>
        {/*
          Not using the `disabled` attribute here - that adds pointer-events-none
          (see button.tsx), which would also block the title tooltip from showing
          on hover. A faded/blocked click is a no-op (handleStartGameClick has its
          own guards too), and the server enforces host-only either way (see
          server.js's roomHosts check).
        */}
        <Button
          variant="ghost"
          size="sm"
          className={cn(
            'absolute bottom-6 left-1/2 -translate-x-1/2 border border-primary bg-transparent text-primary hover:bg-primary/10',
            (!isHost || (!gameActive && !allReady)) && 'opacity-40 hover:bg-transparent',
          )}
          onClick={() => {
            if (!isHost) return;
            if (gameActive) handleEndGameClick();
            else handleStartGameClick();
          }}
          title={
            !isHost
              ? 'Only the host can start the game'
              : !gameActive && !allReady
                ? 'All players must be ready to start'
                : undefined
          }
        >
          {gameActive ? 'End Game' : 'Start Game'}
        </Button>
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

      {view === 'settings' ? (
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
          <div className="grid gap-1.5">
            <Label htmlFor="settings-output">Output device</Label>
            <Select value={selectedOutputDevice} onValueChange={setSelectedOutputDevice}>
              <SelectTrigger id="settings-output" className="w-72">
                <SelectValue placeholder="output device" />
              </SelectTrigger>
              <SelectContent>
                {outputDevices.map((device) => (
                  <SelectItem key={device.deviceId} value={device.deviceId}>
                    {device.label || `Speaker (${device.deviceId.slice(0, 8)})`}
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
      ) : view === 'game-settings' ? (
        <div className="flex flex-col items-center gap-4 pt-6">
          <h2 className="font-display text-4xl tracking-wide text-primary">Game Settings</h2>
          <div className="grid gap-1.5">
            <Label htmlFor="game-settings-round-duration">Round length</Label>
            <Select
              value={String(roundDurationMin)}
              onValueChange={(value) => setRoundDurationMin(Number(value) as RoundDurationMin)}
            >
              <SelectTrigger id="game-settings-round-duration" className="w-72">
                <SelectValue placeholder="round length" />
              </SelectTrigger>
              <SelectContent>
                {ROUND_DURATION_OPTIONS_MIN.map((minutes) => (
                  <SelectItem key={minutes} value={String(minutes)}>
                    {minutes} {minutes === 1 ? 'minute' : 'minutes'}
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
      ) : isConnected ? null : (
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
