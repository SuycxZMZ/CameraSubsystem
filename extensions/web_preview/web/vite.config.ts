import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    proxy: {
      '/ws': {
        target: 'ws://192.168.31.9:8080',
        ws: true,
      },
      '/status': {
        target: 'http://192.168.31.9:8080',
      },
    },
  },
})
