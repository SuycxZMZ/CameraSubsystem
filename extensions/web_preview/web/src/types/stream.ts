import type { WebPixelFormat } from './web-frame-protocol'

export type StreamStatus =
  | 'idle'
  | 'subscribed'
  | 'unsubscribed'
  | 'streaming'
  | 'unsupported_format'
  | 'error'

export interface DetectionState {
  available: boolean;
  error?: string;
  state?: 'idle' | 'running' | 'error';
  modelName?: string;
  npuCoreMask?: number;
  performanceProfileApplied?: boolean;
  config?: {
    inferEveryNFrames: number;
    scoreThreshold: number;
    nmsThreshold: number;
  };
  metrics?: {
    inputFrames: number;
    inferredFrames: number;
    lastObjectCount: number;
  };
  lastError?: string;
}

export interface StreamState {
  streamId: string;
  streamIndex?: number;
  status: StreamStatus;
  width: number;
  height: number;
  pixelFormat: WebPixelFormat;
  pixelFormatName: string;
  fps: number;
  frameCount: number;
  dropCount: number;
  errorCount: number;
  lastFramePayload: Uint8Array | null;
  lastFrameTimestamp: number;
  isFormatSupported: boolean;
  recording: boolean;
  recordPending: boolean;
  recordState: string;
  recordFile: string;
  encodedFrames: number;
  decodedFrames: number;
  recordInputFrames: number;
  recordDroppedFrames: number;
  recordDurationMs: number;
  recordStartedAtMs: number;
  recordBytesWritten: number;
  recordPacketsWritten: number;
  recordDecodeFailures: number;
  recordWriteFailures: number;
  recordProfile: {
    width?: number;
    height?: number;
    fps?: number;
    bitrate?: number;
    gop?: number;
  };
  recordContainer: string;
  recordError: string;
  detection: DetectionState;
  detectionPending: boolean;
}
