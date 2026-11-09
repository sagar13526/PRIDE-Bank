import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

// `npm run dev`   -> normal dev server (talks to the C++ backend on :8080)
// `npm run build` -> one self-contained dist/index.html (open it anywhere)
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  server: { port: 5173 },
});
