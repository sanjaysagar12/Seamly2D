import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Overridable so the dev server can proxy to a backend running in a different container (there's
// no 127.0.0.1:8000 to reach across a Docker network) -- defaults preserve the original same-host
// local-dev behavior untouched.
const backendUrl = process.env.BACKEND_URL || 'http://127.0.0.1:8000'
const backendWsUrl = backendUrl.replace(/^http/, 'ws')

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': backendUrl,
      '/files': backendUrl,
      '/ws': {
        target: backendWsUrl,
        ws: true,
      },
    },
  },
})
