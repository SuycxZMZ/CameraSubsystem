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
}
