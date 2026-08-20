import { app, BrowserWindow, session } from 'electron';
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

// Same host/port and newline-delimited text protocol as quick-tcp-tester/tcp-test-listener.js.
// The DLL is the TCP client here - it connects out to us and streams one reading per line.
const DLL_TCP_HOST = '127.0.0.1';
const DLL_TCP_PORT = 9191;

let mainWindow: BrowserWindow | null = null;

const createWindow = () => {
  // Create the browser window.
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
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

  // Open the DevTools.
  mainWindow.webContents.openDevTools();
};

const startDllServer = () => {
  const server = net.createServer((socket) => {
    console.log('[dll] connected from', socket.remoteAddress);

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
      console.log('[dll] disconnected');
    });

    socket.on('error', (err) => {
      console.error('[dll] socket error', err.message);
    });
  });

  server.on('error', (err: NodeJS.ErrnoException) => {
    if (err.code === 'EADDRINUSE') {
      // Expected when running multiple instances for local multi-mic testing - only one
      // window can own this port. Not fatal: this window just won't get live DLL data,
      // use the manual position controls in the UI instead.
      console.log(
        `[dll] port ${DLL_TCP_PORT} is already owned by another window - this window ` +
          'will not receive live DLL data. Use the manual position controls instead.',
      );
      return;
    }
    console.error('[dll] server error', err.message);
  });

  server.listen(DLL_TCP_PORT, DLL_TCP_HOST, () => {
    console.log(`[dll] listening for DLL connections on ${DLL_TCP_HOST}:${DLL_TCP_PORT}`);
  });

  return server;
};

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', () => {
  // The renderer needs mic access for WebRTC; explicitly scope the grant to
  // 'media' rather than relying on Electron's default handler for everything.
  session.defaultSession.setPermissionRequestHandler((_webContents, permission, callback) => {
    callback(permission === 'media');
  });

  createWindow();
  startDllServer();
});

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
