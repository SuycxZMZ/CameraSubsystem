import type { WebPixelFormat } from './web-frame-protocol'

export type StreamStatus =
  | 'idle'
  | 'subscribed'
  | 'unsubscribed'
  | 'streaming'
  | 'unsupported_format'
  | 'error'

export interface StreamState {
  streamId: string;
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
}
