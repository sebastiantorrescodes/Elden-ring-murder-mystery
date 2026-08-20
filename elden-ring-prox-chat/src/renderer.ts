import './index.css';
import { parseDllLine, LOCAL_PLAYER_SLOT } from './dll-protocol';
import { SignalingClient } from './signaling-client';
import { WebRtcMesh, type SignalPayload } from './webrtc-mesh';

console.log(
  '👋 This message is being logged by "renderer.ts", included via Vite',
);

const logEl = document.querySelector<HTMLPreElement>('#dll-log');
const statusEl = document.querySelector<HTMLDivElement>('#status');
const peerListEl = document.querySelector<HTMLUListElement>('#peer-list');
const serverUrlInput = document.querySelector<HTMLInputElement>('#server-url');
const roomSecretInput = document.querySelector<HTMLInputElement>('#room-secret');
const playerIdInput = document.querySelector<HTMLInputElement>('#player-id');
const roomIdInput = document.querySelector<HTMLInputElement>('#room-id');
const connectBtn = document.querySelector<HTMLButtonElement>('#connect-btn');
const muteToggle = document.querySelector<HTMLInputElement>('#mute-toggle');
const micSelect = document.querySelector<HTMLSelectElement>('#mic-select');
const manualXInput = document.querySelector<HTMLInputElement>('#manual-x');
const manualYInput = document.querySelector<HTMLInputElement>('#manual-y');
const manualZInput = document.querySelector<HTMLInputElement>('#manual-z');
const sendPositionBtn = document.querySelector<HTMLButtonElement>('#send-position-btn');
const localMeterFillEl = document.querySelector<HTMLDivElement>('#local-meter-fill');

let signaling: SignalingClient | null = null;
let mesh: WebRtcMesh | null = null;
let localStream: MediaStream | null = null;
const peerGains = new Map<string, number>();
const peerMeterEls = new Map<string, HTMLDivElement>();

function setStatus(text: string) {
  if (statusEl) statusEl.textContent = text;
}

function renderPeerList() {
  if (!peerListEl) return;
  peerListEl.innerHTML = '';
  peerMeterEls.clear();
  for (const [peerId, gain] of peerGains) {
    const li = document.createElement('li');
    li.className = 'peer-row';

    const label = document.createElement('span');
    label.textContent = `${peerId}: ${gain.toFixed(2)}`;
    li.appendChild(label);

    const meter = document.createElement('div');
    meter.className = 'meter';
    const meterFill = document.createElement('div');
    meterFill.className = 'meter-fill';
    meter.appendChild(meterFill);
    li.appendChild(meter);

    peerListEl.appendChild(li);
    peerMeterEls.set(peerId, meterFill);
  }
}

// Rough RMS level (0..1) scaled up for visibility, since typical speech RMS is low.
function updateMeters() {
  if (mesh) {
    if (localMeterFillEl) {
      localMeterFillEl.style.width = `${Math.min(100, mesh.getLocalLevel() * 300)}%`;
    }
    for (const [peerId, fillEl] of peerMeterEls) {
      fillEl.style.width = `${Math.min(100, mesh.getPeerLevel(peerId) * 300)}%`;
    }
  }
  requestAnimationFrame(updateMeters);
}
requestAnimationFrame(updateMeters);

// Device labels are blank until a mic permission has been granted at least once,
// so this is called both on load (deviceId still usable, label may be blank) and
// again right after getUserMedia succeeds (labels populate in).
async function populateMicList() {
  if (!micSelect) return;
  const previousValue = micSelect.value;
  const devices = await navigator.mediaDevices.enumerateDevices();
  const audioInputs = devices.filter((d) => d.kind === 'audioinput');

  micSelect.innerHTML = '';
  for (const device of audioInputs) {
    const option = document.createElement('option');
    option.value = device.deviceId;
    option.textContent = device.label || `Microphone (${device.deviceId.slice(0, 8)})`;
    micSelect.appendChild(option);
  }
  if (audioInputs.some((d) => d.deviceId === previousValue)) {
    micSelect.value = previousValue;
  }
}

navigator.mediaDevices.addEventListener('devicechange', populateMicList);
populateMicList();

if (sendPositionBtn) sendPositionBtn.disabled = true;

function cleanup() {
  mesh?.closeAll();
  mesh = null;
  signaling = null;
  localStream?.getTracks().forEach((track) => track.stop());
  localStream = null;
  peerGains.clear();
  renderPeerList();
  setStatus('disconnected');
  if (connectBtn) connectBtn.textContent = 'Connect';
  if (sendPositionBtn) sendPositionBtn.disabled = true;
}

let isConnecting = false;

async function connect() {
  // signaling isn't assigned until after the getUserMedia await below, so a second
  // click during that gap (e.g. while the mic permission popup is pending) would
  // otherwise slip past a signaling-only guard and start a second parallel session
  // under the same player id - two RTCPeerConnections fighting each other.
  if (signaling || isConnecting) return;
  isConnecting = true;

  try {
    await doConnect();
  } finally {
    isConnecting = false;
  }
}

async function doConnect() {
  const url = serverUrlInput?.value.trim() || 'ws://localhost:8080';
  const secret = roomSecretInput?.value.trim() || undefined;
  const playerId =
    playerIdInput?.value.trim() || `player-${Math.random().toString(36).slice(2, 6)}`;
  const room = roomIdInput?.value.trim() || 'test-room';
  if (playerIdInput) playerIdInput.value = playerId;

  const deviceId = micSelect?.value || undefined;
  try {
    localStream = await navigator.mediaDevices.getUserMedia({
      audio: deviceId ? { deviceId: { exact: deviceId } } : true,
      video: false,
    });
    await populateMicList(); // labels are blank until permission is granted; refresh now that it is
  } catch (err) {
    console.warn('[mic] getUserMedia failed, continuing without a mic', err);
    localStream = null;
  }
  if (localStream && muteToggle) {
    for (const track of localStream.getAudioTracks()) {
      track.enabled = !muteToggle.checked;
    }
  }

  signaling = new SignalingClient({
    url,
    room,
    playerId,
    secret,
    onOpen: () => {
      setStatus(`connected as ${playerId} in room "${room}"`);
      if (connectBtn) connectBtn.textContent = 'Disconnect';
      if (sendPositionBtn) sendPositionBtn.disabled = false;
    },
    onClose: cleanup,
    onError: () => setStatus('connection error'),
    onWelcome: (msg) => {
      for (const peerId of msg.peers) {
        mesh?.registerExistingPeer(peerId);
        if (!peerGains.has(peerId)) peerGains.set(peerId, 1);
      }
      renderPeerList();
    },
    onPeerJoined: (peerId) => {
      mesh?.handlePeerJoined(peerId);
      peerGains.set(peerId, 1);
      renderPeerList();
    },
    onPeerLeft: (peerId) => {
      mesh?.handlePeerLeft(peerId);
      peerGains.delete(peerId);
      renderPeerList();
    },
    onVolumes: (volumes) => {
      for (const [peerId, gain] of Object.entries(volumes)) {
        mesh?.setGain(peerId, gain);
        peerGains.set(peerId, gain);
      }
      renderPeerList();
    },
    onSignal: (from, data) => {
      mesh?.handleSignal(from, data as SignalPayload);
    },
  });

  mesh = new WebRtcMesh(signaling, localStream);
}

connectBtn?.addEventListener('click', () => {
  if (signaling) {
    signaling.close();
  } else {
    connect();
  }
});

sendPositionBtn?.addEventListener('click', () => {
  if (!signaling) return;
  const x = Number(manualXInput?.value ?? 0);
  const y = Number(manualYInput?.value ?? 0);
  const z = Number(manualZInput?.value ?? 0);
  signaling.sendPosition(x, y, z);
});

muteToggle?.addEventListener('change', () => {
  if (!localStream) return;
  for (const track of localStream.getAudioTracks()) {
    track.enabled = !muteToggle.checked;
  }
});

// DLL readings: log every line, and forward the local player's (slot 0) position
// to the signaling server so it can recompute proximity volumes for the room.
window.dllBridge.onReading((line) => {
  console.log('[dll]', line);
  if (logEl) {
    logEl.textContent += line + '\n';
    logEl.scrollTop = logEl.scrollHeight;
  }

  const reading = parseDllLine(line);
  if (reading && reading.slot === LOCAL_PLAYER_SLOT && signaling) {
    signaling.sendPosition(reading.x, reading.y, reading.z);
  }
});
