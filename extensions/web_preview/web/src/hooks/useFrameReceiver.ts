import { useRef, useCallback } from 'react';
import { parseWebFrameHeader } from '@/utils/frame-parser';
import { FpsCounter } from '@/utils/fps-counter';
import { useStreamStore } from '@/stores/useStreamStore';
import {
  pixelFormatToName,
  isFormatSupported,
} from '@/types/web-frame-protocol';

/**
 * Hook that provides an onBinaryMessage callback for useWebSocket.
 * Parses WebFrameHeader from binary frames and updates the stream store.
 */
export function useFrameReceiver() {
  const fpsCountersRef = useRef<Record<string, FpsCounter>>({});
  const store = useStreamStore;

  const onBinaryMessage = useCallback((data: ArrayBuffer) => {
    const result = parseWebFrameHeader(data);
    if (!result) {
      // Parse failed - increment error count on all known streams
      // (We don't know which stream it belongs to, so we skip)
      console.warn('[useFrameReceiver] Failed to parse frame');
      return;
    }

    const { header, payload } = result;
    const streamId = String(header.streamId);
    const now = performance.now();

    // Ensure stream exists in store
    const streams = store.getState().streams;
    if (!streams[streamId]) {
      store.getState().addStream(streamId);
    }

    // Get or create FPS counter for this stream
    if (!fpsCountersRef.current[streamId]) {
      fpsCountersRef.current[streamId] = new FpsCounter();
    }
    const fpsCounter = fpsCountersRef.current[streamId];
    fpsCounter.recordFrame(now);

    const formatSupported = isFormatSupported(header.pixelFormat);
    const formatName = pixelFormatToName(header.pixelFormat);

    store.getState().updateStream(streamId, {
      width: header.width,
      height: header.height,
      pixelFormat: header.pixelFormat,
      pixelFormatName: formatName,
      isFormatSupported: formatSupported,
      fps: fpsCounter.getFps(),
      frameCount: (streams[streamId]?.frameCount ?? 0) + 1,
      lastFramePayload: formatSupported ? payload : null,
      lastFrameTimestamp: now,
      status: formatSupported ? 'streaming' : 'unsupported_format',
    });
  }, [store]);

  return { onBinaryMessage };
}
