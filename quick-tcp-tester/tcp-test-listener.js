// Minimal test listener — proves the DLL can reach a local process before
// any Electron/Node app logic gets built on top of it.
//
// Run with: node tcp_test_listener.js
// Then launch Elden Ring with your DLL pointed at 127.0.0.1:9191

const net = require('net');

const PORT = 9191;

const server = net.createServer((socket) => {
  console.log('[connected] DLL connected from', socket.remoteAddress);

  let buffer = '';

  socket.on('data', (chunk) => {
    buffer += chunk.toString();

    // Split on newlines in case multiple readings arrive in one chunk,
    // or one reading arrives split across two chunks (both happen with TCP).
    let lines = buffer.split('\n');
    buffer = lines.pop(); // keep the last, possibly incomplete line

    for (const line of lines) {
      const trimmed = line.trim();
      if (trimmed.length > 0) {
        console.log('[reading]', trimmed);
      }
    }
  });

  socket.on('close', () => {
    console.log('[disconnected] DLL connection closed');
  });

  socket.on('error', (err) => {
    console.error('[socket error]', err.message);
  });
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Listening for DLL connections on 127.0.0.1:${PORT}`);
  console.log('Waiting for data...\n');
});