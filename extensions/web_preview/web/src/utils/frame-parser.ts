import {
  WEB_FRAME_MAGIC,
  WEB_FRAME_VERSION,
  WEB_FRAME_HEADER_SIZE,
  type WebFrameHeader,
  WebPixelFormat,
} from '@/types/web-frame-protocol'

export interface ParsedFrame {
  header: WebFrameHeader;
  payload: Uint8Array;
}

/**
 * Parse a WebSocket Binary Frame into WebFrameHeader + payload.
 * The binary layout must match Gateway's `#pragma pack(push, 1)` struct.
 * All multi-byte fields are little-endian.
 */
export function parseWebFrameHeader(data: ArrayBuffer): ParsedFrame | null {
  if (data.byteLength < WEB_FRAME_HEADER_SIZE) {
    console.warn(
      `[frame-parser] Buffer too small: ${data.byteLength} < ${WEB_FRAME_HEADER_SIZE}`,
    );
    return null;
  }

  const view = new DataView(data);

  const magic = view.getUint32(0, true);
  if (magic !== WEB_FRAME_MAGIC) {
    console.warn(
      `[frame-parser] Invalid magic: 0x${magic.toString(16)} !== 0x${WEB_FRAME_MAGIC.toString(16)}`,
    );
    return null;
  }

  const version = view.getUint32(4, true);
  if (version !== WEB_FRAME_VERSION) {
    console.warn(
      `[frame-parser] Unsupported version: ${version} !== ${WEB_FRAME_VERSION}`,
    );
    return null;
  }

  const headerSize = view.getUint32(8, true);
  const streamId = view.getUint32(12, true);
  const frameId = view.getBigUint64(16, true);
  const timestampNs = view.getBigUint64(24, true);
  const width = view.getUint32(32, true);
  const height = view.getUint32(36, true);
  const pixelFormat = view.getUint32(40, true) as WebPixelFormat;
  const strideY = view.getUint32(44, true);
  const strideUv = view.getUint32(48, true);
  const payloadSize = view.getUint32(52, true);
  const transformFlags = view.getUint32(56, true);

  // Validate payload bounds
  const expectedSize = headerSize + payloadSize;
  if (data.byteLength < expectedSize) {
    console.warn(
      `[frame-parser] Buffer too small for payload: ${data.byteLength} < ${expectedSize} (headerSize=${headerSize}, payloadSize=${payloadSize})`,
    );
    return null;
  }

  const header: WebFrameHeader = {
    magic,
    version,
    headerSize,
    streamId,
    frameId: Number(frameId),
    timestampNs: Number(timestampNs),
    width,
    height,
    pixelFormat,
    strideY,
    strideUv,
    payloadSize,
    transformFlags,
  };

  const payload = new Uint8Array(data, headerSize, payloadSize);

  return { header, payload };
}
