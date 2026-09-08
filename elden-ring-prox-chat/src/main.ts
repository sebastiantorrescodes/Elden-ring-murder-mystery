import { app, BrowserWindow, ipcMain, session } from 'electron';
import path from 'node:path';
import net from 'node:net';
import started from 'electron-squirrel-startup';

// Handle creating/removing shortcuts on Windows when installing/uninstalling.
if (started) {
  app.quit();
}

// Local multi-window testing (two mics, two distances) means multiple instances of
// this same app run at once. They'd otherwise all default to the same Chromium
// userData/GPU-cache directory and collide on it (Windows: "Access is denied" trying
// to write a cache file another process has locked). Give each process its own,
// keyed by PID, under the OS temp dir so nothing persists between runs.
// Must happen before app is ready - app.setPath has no effect after that.
app.setPath('userData', path.join(app.getPath('temp'), 'elden-ring-prox-chat', `pid-${process.pid}`));

// The DLL is the TCP client here - it connects out to us and streams one reading per line.
const DLL_TCP_HOST = '127.0.0.1';
const DLL_TCP_PORT = 9191;

// Reverse direction of the port above: MurderMystery.dll's MMTcpClient connects out to
// this port the same way ProximityChatMod's DLL connects to DLL_TCP_PORT, but
// this channel carries commands (Electron -> DLL) instead of telemetry (DLL ->
// Electron), so this side writes to the socket rather than only reading from it. Kept
// on its own port instead of multiplexed onto DLL_TCP_PORT since the two DLLs and
// their protocols are otherwise unrelated.
const MM_TCP_HOST = '127.0.0.1';
const MM_TCP_PORT = 9192;

let mainWindow: BrowserWindow | null = null;

// The single MurderMystery.dll connection, if one is currently connected. Commands
// (from the VPS via signaling-client.ts, or manual testing) are written here.
let mmSocket: net.Socket | null = null;

// Packaged builds have no attached console, so plain console.log here is invisible
// to anyone not running from a dev terminal - mirror it into the renderer's visible
// UI too, since that's the only thing a non-technical user (or a remote friend,
// diagnosed only via screenshots) can actually see.
function logStatus(message: string) {
  console.log(message);
  mainWindow?.webContents.send('dll-status', message);
}

const createWindow = () => {
  // Create the browser window.
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    resizable: false,
    maximizable: false,
    fullscreenable: false,
    // Packaged builds get their icon from packagerConfig.icon (forge.config.ts) via
    // the exe's own resource icon - this only matters for `npm start`, where the
    // process is the generic electron.exe and would otherwise show Electron's icon.
    icon: path.join(__dirname, '../../assets/mmicon.ico'),
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
    },
  });

  // and load the index.html of the app.
  if (MAIN_WINDOW_VITE_DEV_SERVER_URL) {
    mainWindow.loadURL(MAIN_WINDOW_VITE_DEV_SERVER_URL);
  } else {
    mainWindow.loadFile(
      path.join(__dirname, `../renderer/${MAIN_WINDOW_VITE_NAME}/index.html`),
    );
  }

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
};

const startDllServer = () => {
  const server = net.createServer((socket) => {
    logStatus(`[server] DLL connected from ${socket.remoteAddress}`);
    // Matches the DLL's own keepalive timing (TcpTestClient.cpp) - without this, a
    // peer that vanishes without a clean FIN/RST (crash, sleep/resume) can leave this
    // socket looking alive for a long time with no data flowing, since nothing here
    // is actively writing to notice a failed send.
    socket.setKeepAlive(true, 5000);

    let buffer = '';
    socket.on('data', (chunk) => {
      buffer += chunk.toString();

      // Split on newlines in case multiple readings arrive in one chunk,
      // or one reading arrives split across two chunks (both happen with TCP).
      const lines = buffer.split('\n');
      buffer = lines.pop() ?? '';

      for (const line of lines) {
        const trimmed = line.trim();
        if (trimmed.length > 0) {
          mainWindow?.webContents.send('dll-reading', trimmed);
        }
      }
    });

    socket.on('close', () => {
      logStatus('[server] DLL disconnected');
    });

    socket.on('error', (err) => {
      logStatus(`[server] socket error: ${err.message}`);
    });
  });

  server.on('error', (err: NodeJS.ErrnoException) => {
    if (err.code === 'EADDRINUSE') {
      // Expected when running multiple instances for local multi-mic testing - only one
      // window can own this port. Not fatal: this window just won't get live DLL data,
      // use the manual position controls in the UI instead.
      logStatus(
        `[server] port ${DLL_TCP_PORT} is already owned by another window - this window ` +
          'will not receive live DLL data. Use the manual position controls instead.',
      );
      return;
    }
    logStatus(`[server] server error: ${err.message}`);
  });

  server.listen(DLL_TCP_PORT, DLL_TCP_HOST, () => {
    logStatus(`[server] listening for DLL connections on ${DLL_TCP_HOST}:${DLL_TCP_PORT}`);
  });

  return server;
};

const startMmCommandServer = () => {
  const server = net.createServer((socket) => {
    logStatus(`[mm-server] MurderMystery.dll connected from ${socket.remoteAddress}`);
    // Same reasoning as startDllServer's setKeepAlive: notice a DLL that vanished
    // without a clean close (game crash, force-quit) instead of leaving a stale
    // socket that silently swallows every write.
    socket.setKeepAlive(true, 5000);
    mmSocket = socket;

    let mmBuffer = '';

    socket.on('data', (chunk) => {
      mmBuffer += chunk.toString();

      const lines = mmBuffer.split('\n');
      mmBuffer = lines.pop() ?? '';

      for (const line of lines) {
        const trimmed = line.trim();

        if (trimmed.length > 0) {
          logStatus(`[mm-server] received event: ${trimmed}`);
          mainWindow?.webContents.send('mm-event', trimmed);
        }
      }
    });

    socket.on('close', () => {
      logStatus('[mm-server] MurderMystery.dll disconnected');
      if (mmSocket === socket) {
        mmSocket = null;
      }
    });

    socket.on('error', (err) => {
      logStatus(`[mm-server] socket error: ${err.message}`);
    });
  });

  server.on('error', (err: NodeJS.ErrnoException) => {
    if (err.code === 'EADDRINUSE') {
      // Same multi-instance local testing case as startDllServer above.
      logStatus(
        `[mm-server] port ${MM_TCP_PORT} is already owned by another window - this window ` +
          'will not be able to send MurderMystery commands.',
      );
      return;
    }
    logStatus(`[mm-server] server error: ${err.message}`);
  });

  server.listen(MM_TCP_PORT, MM_TCP_HOST, () => {
    logStatus(`[mm-server] listening for MurderMystery.dll connections on ${MM_TCP_HOST}:${MM_TCP_PORT}`);
  });

  return server;
};

// commandName matches an MMCommand-ish name on the DLL side (e.g. "SetMurderer",
// "StartGame") - HandleNetworkLine there strips the "cmd=" prefix, switches on the
// rest, and reads any "key=value" params (e.g. StartGame's role=) off the same line.
function sendMmCommand(commandName: string, params?: Record<string, string>): boolean {
  if (!mmSocket || mmSocket.destroyed) {
    logStatus(`[mm-server] cannot send "${commandName}" - no MurderMystery.dll connected`);
    return false;
  }
  const paramText = params
    ? Object.entries(params)
        .map(([key, value]) => ` ${key}=${value}`)
        .join('')
    : '';
  mmSocket.write(`cmd=${commandName}${paramText}\n`);
  logStatus(`[mm-server] sent command: ${commandName}${paramText}`);
  return true;
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', () => {
  // The renderer needs mic access for WebRTC and clipboard-write for the room code
  // copy button; explicitly scope the grant to those rather than relying on
  // Electron's default handler for everything.
  session.defaultSession.setPermissionRequestHandler((_webContents, permission, callback) => {
    callback(permission === 'media' || permission === 'clipboard-sanitized-write');
  });

  createWindow();
  startDllServer();
  startMmCommandServer();
});

// Renderer-invocable for now (manual testing); the VPS-driven path (once wired,
// via signaling-client.ts) will call sendMmCommand directly from the main process
// instead of going through this IPC handler.
ipcMain.handle('mm-send-command', (_event, commandName: string, params?: Record<string, string>) =>
  sendMmCommand(commandName, params),
);

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  // On OS X it's common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and import them here.
