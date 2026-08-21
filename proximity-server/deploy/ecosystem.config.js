// pm2 process definition. Deploy note: keep ROOM_SECRET as a placeholder here in
// git - edit it to the real generated value only in the copy that lives on the VPS,
// never commit the real secret.
const path = require('node:path');

module.exports = {
  apps: [
    {
      name: 'proximity-server',
      script: 'server.js',
      cwd: path.join(__dirname, '..'),
      autorestart: true,
      env: {
        PORT: 8080,
        ROOM_SECRET: 'REPLACE_ME',
      },
    },
  ],
};
