import { create } from 'zustand';
import type { StreamState, StreamStatus } from '@/types/stream';
import type {
  GatewayCommand,
  CommandResult,
  GatewayStatus,
  RecordStatus,
  ConnectionState,
} from '@/types/gateway-command';
import {
  WebPixelFormat,
  pixelFormatToName,
  isFormatSupported,
} from '@/types/web-frame-protocol';

interface StreamStore {
  streams: Record<string, StreamState>;
  connectionState: ConnectionState;
  gatewayUrl: string;
  lastCommandResult: CommandResult | null;

  // Actions
  updateStream: (streamId: string, partial: Partial<StreamState>) => void;
  addStream: (streamId: string) => void;
  removeStream: (streamId: string) => void;
  setConnectionState: (state: ConnectionState) => void;
  setGatewayUrl: (url: string) => void;
  sendCommand: (command: GatewayCommand) => void;
  handleCommandResult: (result: CommandResult) => void;
  handleGatewayStatus: (status: GatewayStatus) => void;
  handleRecordStatus: (status: RecordStatus) => void;
  setSendTextFn: (fn: ((data: string) => void) | null) => void;
}

let sendTextFn: ((data: string) => void) | null = null;

export const useStreamStore = create<StreamStore>((set, get) => ({
  streams: {},
  connectionState: 'disconnected',
  gatewayUrl: '',
  lastCommandResult: null,

  updateStream: (streamId, partial) =>
    set((state) => {
      const existing = state.streams[streamId];
      if (!existing) return state;
      return {
        streams: {
          ...state.streams,
          [streamId]: { ...existing, ...partial },
        },
      };
    }),

  addStream: (streamId) =>
    set((state) => {
      if (state.streams[streamId]) return state;
      const newStream: StreamState = {
        streamId,
        status: 'idle',
        width: 0,
        height: 0,
        pixelFormat: WebPixelFormat.Unknown,
        pixelFormatName: 'UNKNOWN',
        fps: 0,
        frameCount: 0,
        dropCount: 0,
        errorCount: 0,
        lastFramePayload: null,
        lastFrameTimestamp: 0,
        isFormatSupported: false,
        recording: false,
        recordFile: '',
        encodedFrames: 0,
        decodedFrames: 0,
        recordError: '',
      };
      return {
        streams: { ...state.streams, [streamId]: newStream },
      };
    }),

  removeStream: (streamId) =>
    set((state) => {
      const { [streamId]: _, ...rest } = state.streams;
      return { streams: rest };
    }),

  setConnectionState: (connectionState) => set({ connectionState }),

  setGatewayUrl: (gatewayUrl) => set({ gatewayUrl }),

  sendCommand: (command) => {
    if (sendTextFn) {
      sendTextFn(JSON.stringify(command));
    } else {
      console.warn('[stream-store] Cannot send command: WebSocket not connected');
    }
  },

  handleCommandResult: (result) => {
    set({ lastCommandResult: result });
    // Unsupported extension commands are logged here; feature-specific status
    // such as recording state is handled through dedicated status messages.
    if (result.status === 'not_supported') {
      console.info(`[stream-store] Command not supported: ${result.reason ?? 'unknown'}`);
    }
  },

  handleGatewayStatus: (status) => {
    const streamId = status.stream_id;
    const store = get();

    // Ensure stream exists
    if (!store.streams[streamId]) {
      store.addStream(streamId);
    }

    // Map Gateway status to StreamStatus
    let streamStatus: StreamStatus = 'idle';
    if (status.status === 'streaming') {
      streamStatus = 'streaming';
    } else if (status.status === 'unsupported') {
      streamStatus = 'unsupported_format';
    }

    // Map format string back to WebPixelFormat
    let pixelFormat = WebPixelFormat.Unknown;
    const formatLower = status.format.toLowerCase();
    if (formatLower === 'jpeg' || formatLower === 'mjpeg') {
      pixelFormat = WebPixelFormat.Jpeg;
    } else if (formatLower === 'rgb') {
      pixelFormat = WebPixelFormat.Rgb;
    } else if (formatLower === 'rgba') {
      pixelFormat = WebPixelFormat.Rgba;
    } else if (formatLower === 'nv12') {
      pixelFormat = WebPixelFormat.Nv12;
    } else if (formatLower === 'yuyv') {
      pixelFormat = WebPixelFormat.Yuyv;
    } else if (formatLower === 'uyvy') {
      pixelFormat = WebPixelFormat.Uyvy;
    }

    store.updateStream(streamId, {
      status: streamStatus,
      width: status.width,
      height: status.height,
      pixelFormat,
      pixelFormatName: pixelFormatToName(pixelFormat),
      isFormatSupported: isFormatSupported(pixelFormat),
      dropCount: status.dropped_frames,
    });
  },

  handleRecordStatus: (status) => {
    const streamId = status.stream_id || 'usb_camera_0';
    const store = get();

    if (!store.streams[streamId]) {
      store.addStream(streamId);
    }

    store.updateStream(streamId, {
      recording: Boolean(status.recording),
      recordFile: status.file ?? '',
      encodedFrames: status.encoded_frames ?? 0,
      decodedFrames: status.decoded_frames ?? 0,
      recordError: status.error ?? '',
      dropCount: status.dropped_frames ?? store.streams[streamId]?.dropCount ?? 0,
    });
  },

  setSendTextFn: (fn) => {
    sendTextFn = fn;
  },
}));
