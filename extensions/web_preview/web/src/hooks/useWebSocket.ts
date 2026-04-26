import { useRef, useCallback, useEffect } from 'react';
import { calculateReconnectDelay } from '@/utils/reconnect';
import type { ConnectionState } from '@/types/gateway-command';

export interface UseWebSocketOptions {
  url: string;
  reconnectInterval?: number;
  maxReconnectInterval?: number;
  onBinaryMessage?: (data: ArrayBuffer) => void;
  onTextMessage?: (data: string) => void;
  onConnectionChange?: (state: ConnectionState) => void;
}

export interface UseWebSocketReturn {
  connectionState: ConnectionState;
  sendText: (data: string) => void;
  sendBinary: (data: ArrayBuffer) => void;
  connect: () => void;
  disconnect: () => void;
}

export function useWebSocket({
  url,
  reconnectInterval = 1000,
  maxReconnectInterval = 30000,
  onBinaryMessage,
  onTextMessage,
  onConnectionChange,
}: UseWebSocketOptions): UseWebSocketReturn {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const reconnectAttemptRef = useRef(0);
  const connectionStateRef = useRef<ConnectionState>('disconnected');
  const intentionalCloseRef = useRef(false);

  // Stable refs for callbacks to avoid re-creating the WebSocket on callback changes
  const onBinaryRef = useRef(onBinaryMessage);
  const onTextRef = useRef(onTextMessage);
  const onConnectionChangeRef = useRef(onConnectionChange);
  onBinaryRef.current = onBinaryMessage;
  onTextRef.current = onTextMessage;
  onConnectionChangeRef.current = onConnectionChange;

  const setConnectionState = useCallback((state: ConnectionState) => {
    connectionStateRef.current = state;
    onConnectionChangeRef.current?.(state);
  }, []);

  const clearReconnectTimer = useCallback(() => {
    if (reconnectTimerRef.current !== null) {
      clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
  }, []);

  const connect = useCallback(() => {
    // Close existing connection if any
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    clearReconnectTimer();
    intentionalCloseRef.current = false;
    setConnectionState('connecting');

    try {
      const ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      wsRef.current = ws;

      ws.onopen = () => {
        reconnectAttemptRef.current = 0;
        setConnectionState('connected');
      };

      ws.onclose = () => {
        setConnectionState('disconnected');
        wsRef.current = null;

        if (!intentionalCloseRef.current) {
          // Auto-reconnect with exponential backoff
          const delay = calculateReconnectDelay(
            reconnectAttemptRef.current,
            reconnectInterval,
            maxReconnectInterval,
          );
          reconnectAttemptRef.current += 1;
          reconnectTimerRef.current = setTimeout(() => {
            connect();
          }, delay);
        }
      };

      ws.onerror = () => {
        // onclose will fire after onerror, so we don't need to handle state here
      };

      ws.onmessage = (event: MessageEvent) => {
        if (event.data instanceof ArrayBuffer) {
          onBinaryRef.current?.(event.data);
        } else if (typeof event.data === 'string') {
          onTextRef.current?.(event.data);
        }
      };
    } catch {
      setConnectionState('disconnected');
    }
  }, [url, reconnectInterval, maxReconnectInterval, setConnectionState, clearReconnectTimer]);

  const disconnect = useCallback(() => {
    intentionalCloseRef.current = true;
    clearReconnectTimer();
    reconnectAttemptRef.current = 0;

    if (wsRef.current) {
      setConnectionState('disconnecting');
      wsRef.current.close();
      wsRef.current = null;
    }
  }, [setConnectionState, clearReconnectTimer]);

  const sendText = useCallback((data: string) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(data);
    } else {
      console.warn('[useWebSocket] Cannot send text: not connected');
    }
  }, []);

  const sendBinary = useCallback((data: ArrayBuffer) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(data);
    } else {
      console.warn('[useWebSocket] Cannot send binary: not connected');
    }
  }, []);

  // Auto-connect on mount, disconnect on unmount
  useEffect(() => {
    connect();
    return () => {
      disconnect();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return {
    connectionState: connectionStateRef.current,
    sendText,
    sendBinary,
    connect,
    disconnect,
  };
}
