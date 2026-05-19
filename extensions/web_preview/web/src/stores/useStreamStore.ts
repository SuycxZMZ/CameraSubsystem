import { create } from 'zustand';
import type { StreamState, StreamStatus } from '@/types/stream';
import type {
  GatewayCommand,
  CommandResult,
  GatewayStatus,
  RecordStatus,
  ConnectionState,
  DetectionResponse,
} from '@/types/gateway-command';
import {
  WebPixelFormat,
  pixelFormatToName,
  isFormatSupported,
} from '@/types/web-frame-protocol';

interface StreamStore {
  streams: Record<string, StreamState>;
  streamIndexMap: Record<string, string>;
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
  handleDetectionResponse: (response: DetectionResponse) => void;
  resolveStreamId: (streamIndex: number) => string;
  setSendTextFn: (fn: ((data: string) => void) | null) => void;
}

let sendTextFn: ((data: string) => void) | null = null;

export const useStreamStore = create<StreamStore>((set, get) => ({
  streams: {},
  streamIndexMap: {},
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
        recordPending: false,
        recordState: 'idle',
        recordFile: '',
        encodedFrames: 0,
        decodedFrames: 0,
        recordInputFrames: 0,
        recordDroppedFrames: 0,
        recordDurationMs: 0,
        recordStartedAtMs: 0,
        recordBytesWritten: 0,
        recordPacketsWritten: 0,
        recordDecodeFailures: 0,
        recordWriteFailures: 0,
        recordProfile: {},
        recordContainer: 'raw_h264',
        recordError: '',
        detection: { available: false },
        detectionPending: false,
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
    if (command.type === 'set_record_enabled') {
      const state = get();
      if (!state.streams[command.stream_id]) {
        state.addStream(command.stream_id);
      }
      state.updateStream(command.stream_id, {
        recordPending: true,
        recordError: '',
        ...(command.enabled
          ? {
              recordState: 'starting',
              recordContainer: command.container ?? 'raw_h264',
              recordFile: '',
              recordDurationMs: 0,
              recordStartedAtMs: 0,
              recordBytesWritten: 0,
              recordPacketsWritten: 0,
              recordInputFrames: 0,
              recordDroppedFrames: 0,
              recordDecodeFailures: 0,
              recordWriteFailures: 0,
            }
          : { recordState: 'stopping' }),
      });
    }

    if (command.type === 'set_detect_enabled') {
      const state = get();
      if (!state.streams[command.stream_id]) {
        state.addStream(command.stream_id);
      }
      state.updateStream(command.stream_id, {
        detectionPending: true,
      });
    }

    if (command.type === 'set_detection_config') {
      const state = get();
      if (!state.streams[command.stream_id]) {
        state.addStream(command.stream_id);
      }
      state.updateStream(command.stream_id, {
        detectionPending: true,
      });
    }

    if (sendTextFn) {
      sendTextFn(JSON.stringify(command));
    } else {
      if (command.type === 'set_record_enabled') {
        get().updateStream(command.stream_id, {
          recordPending: false,
          recordError: 'websocket_not_connected',
        });
      }
      if (command.type === 'set_detect_enabled' || command.type === 'set_detection_config') {
        get().updateStream(command.stream_id, {
          detectionPending: false,
          detection: { available: false, error: 'websocket_not_connected' },
        });
      }
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
    const streamIndex = status.stream_index;
    const store = get();
    const aliasId = typeof streamIndex === 'number' ? String(streamIndex) : '';

    if (aliasId && aliasId !== streamId && store.streams[aliasId]) {
      set((state) => {
        if (!state.streams[aliasId]) return state;
        const aliasStream = state.streams[aliasId];
        const targetStream = state.streams[streamId];
        const { [aliasId]: _, ...rest } = state.streams;
        const mergedStream = {
          ...(targetStream ?? aliasStream),
          ...aliasStream,
          ...(targetStream
            ? {
                recording: targetStream.recording,
                recordPending: targetStream.recordPending,
                recordState: targetStream.recordState,
                recordFile: targetStream.recordFile,
                encodedFrames: targetStream.encodedFrames,
                decodedFrames: targetStream.decodedFrames,
                recordInputFrames: targetStream.recordInputFrames,
                recordDroppedFrames: targetStream.recordDroppedFrames,
                recordDurationMs: targetStream.recordDurationMs,
                recordStartedAtMs: targetStream.recordStartedAtMs,
                recordBytesWritten: targetStream.recordBytesWritten,
                recordPacketsWritten: targetStream.recordPacketsWritten,
                recordDecodeFailures: targetStream.recordDecodeFailures,
                recordWriteFailures: targetStream.recordWriteFailures,
                recordProfile: targetStream.recordProfile,
                recordContainer: targetStream.recordContainer,
                recordError: targetStream.recordError,
              }
            : {}),
          streamId,
          streamIndex,
        };
        return {
          streams: {
            ...rest,
            [streamId]: mergedStream,
          },
        };
      });
    } else if (!store.streams[streamId]) {
      get().addStream(streamId);
    } else if (typeof streamIndex === 'number') {
      get().updateStream(streamId, { streamIndex });
    }

    if (typeof streamIndex === 'number') {
      set((state) => ({
        streamIndexMap: {
          ...state.streamIndexMap,
          [String(streamIndex)]: streamId,
        },
      }));
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
      streamIndex,
      pixelFormat,
      pixelFormatName: pixelFormatToName(pixelFormat),
      isFormatSupported: isFormatSupported(pixelFormat),
      dropCount: status.dropped_frames,
      ...(status.detection ? {
        detection: {
          available: status.detection.available,
          error: status.detection.error,
          state: status.detection.state as 'idle' | 'running' | 'error' | undefined,
          modelName: status.detection.model_name,
          npuCoreMask: status.detection.npu_core_mask,
          config: status.detection.config ? {
            inferEveryNFrames: status.detection.config.infer_every_n_frames,
            scoreThreshold: status.detection.config.score_threshold,
            nmsThreshold: status.detection.config.nms_threshold,
          } : undefined,
          metrics: status.detection.metrics ? {
            inputFrames: status.detection.metrics.input_frames,
            inferredFrames: status.detection.metrics.inferred_frames,
            lastObjectCount: status.detection.metrics.last_object_count,
          } : undefined,
          lastError: status.detection.last_error,
        },
      } : {}),
    });
  },

  handleRecordStatus: (status) => {
    const streamId = status.stream_id || '0';
    const store = get();

    if (!store.streams[streamId]) {
      store.addStream(streamId);
    }

    store.updateStream(streamId, {
      recording: Boolean(status.recording),
      recordPending: false,
      recordState: status.state ?? (status.recording ? 'recording' : 'idle'),
      recordFile: status.file ?? '',
      encodedFrames: status.encoded_frames ?? 0,
      decodedFrames: status.decoded_frames ?? 0,
      recordInputFrames: status.input_frames ?? 0,
      recordDroppedFrames: status.dropped_frames ?? 0,
      recordDurationMs: status.duration_ms ?? 0,
      recordStartedAtMs: status.recording
        ? Date.now() - (status.duration_ms ?? 0)
        : 0,
      recordBytesWritten: status.bytes_written ?? 0,
      recordPacketsWritten: status.packets_written ?? 0,
      recordDecodeFailures: status.decode_failures ?? 0,
      recordWriteFailures: status.write_failures ?? 0,
      recordProfile: status.profile ?? {},
      recordContainer: status.container ?? store.streams[streamId]?.recordContainer ?? 'raw_h264',
      recordError: status.error ?? '',
      dropCount: status.dropped_frames ?? store.streams[streamId]?.dropCount ?? 0,
    });
  },

  handleDetectionResponse: (response) => {
    const streamId = response.stream_id || '0';
    const store = get();

    if (!store.streams[streamId]) {
      store.addStream(streamId);
    }

    const currentDetection = store.streams[streamId]?.detection ?? { available: false };

    store.updateStream(streamId, {
      detectionPending: false,
      detection: {
        ...currentDetection,
        available: response.error_code === 'connect_failed' ? false : true,
        state: response.state as 'idle' | 'running' | 'error' | undefined,
        error: response.ok ? undefined : (response.message ?? response.error_code),
      },
    });
  },

  resolveStreamId: (streamIndex) => {
    const key = String(streamIndex);
    return get().streamIndexMap[key] ?? key;
  },

  setSendTextFn: (fn) => {
    sendTextFn = fn;
  },
}));
