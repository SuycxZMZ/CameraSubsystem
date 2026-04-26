/** WebFrameHeader magic number - "WPFR" in ASCII */
export const WEB_FRAME_MAGIC = 0x57504652;

/** WebFrameHeader protocol version */
export const WEB_FRAME_VERSION = 1;

/** WebFrameHeader total size in bytes (packed struct) */
export const WEB_FRAME_HEADER_SIZE = 92;

/**
 * Pixel format enum - must match Gateway's WebPixelFormat
 * @see gateway/include/web_preview/web_frame_protocol.h
 */
export enum WebPixelFormat {
  Unknown = 0,
  Jpeg = 1,
  Rgb = 2,
  Rgba = 3,
  Nv12 = 4,
  Yuyv = 5,
  Uyvy = 6,
}

/**
 * Transform flags enum - must match Gateway's WebTransformFlags
 */
export enum WebTransformFlags {
  None = 0,
  Unsupported = 1 << 0,
}

/**
 * Parsed WebFrameHeader - matches Gateway's packed struct layout.
 * All fields are little-endian.
 *
 * Total size: 92 bytes
 *   offset  0: uint32  magic
 *   offset  4: uint32  version
 *   offset  8: uint32  header_size
 *   offset 12: uint32  stream_id
 *   offset 16: uint64  frame_id
 *   offset 24: uint64  timestamp_ns
 *   offset 32: uint32  width
 *   offset 36: uint32  height
 *   offset 40: uint32  pixel_format
 *   offset 44: uint32  stride_y
 *   offset 48: uint32  stride_uv
 *   offset 52: uint32  payload_size
 *   offset 56: uint32  transform_flags
 *   offset 60: uint8[32] reserved
 */
export interface WebFrameHeader {
  magic: number;
  version: number;
  headerSize: number;
  streamId: number;
  frameId: number;
  timestampNs: number;
  width: number;
  height: number;
  pixelFormat: WebPixelFormat;
  strideY: number;
  strideUv: number;
  payloadSize: number;
  transformFlags: number;
}

/** Convert WebPixelFormat enum value to human-readable name */
export function pixelFormatToName(format: WebPixelFormat): string {
  switch (format) {
    case WebPixelFormat.Jpeg:
      return 'JPEG';
    case WebPixelFormat.Rgb:
      return 'RGB';
    case WebPixelFormat.Rgba:
      return 'RGBA';
    case WebPixelFormat.Nv12:
      return 'NV12';
    case WebPixelFormat.Yuyv:
      return 'YUYV';
    case WebPixelFormat.Uyvy:
      return 'UYVY';
    default:
      return 'UNKNOWN';
  }
}

/** Check if a pixel format is supported for rendering in the browser */
export function isFormatSupported(format: WebPixelFormat): boolean {
  return format === WebPixelFormat.Jpeg;
}
