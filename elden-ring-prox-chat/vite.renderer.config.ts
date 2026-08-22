import path from 'node:path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// https://vitejs.dev/config
export default defineConfig(async () => {
  // @tailwindcss/vite is ESM-only; this config file loads as CommonJS (no "type":
  // "module" in package.json), so a static import fails at config-load time - a
  // dynamic import works fine even from a CJS context.
  const { default: tailwindcss } = await import('@tailwindcss/vite');

  return {
    plugins: [react(), tailwindcss()],
    resolve: {
      alias: {
        '@': path.resolve(__dirname, './src'),
      },
    },
  };
});
