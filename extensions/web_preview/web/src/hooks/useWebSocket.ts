import { useRef, useCallback, useEffect, useState } from 'react';
import { calculateReconnectDelay } from '@/utils/reconnect';
import type { ConnectionState } from '@/types/gateway-command';

export interface UseWebSocketOptions {
  url: string;
  reconnectInterval?: number;
  maxReconnectInterval?: number;
  onBinaryMessage?: (data: ArrayBuffer) => void;
  onTextMessage?: (data: string) => void;
}

export interface UseWebSocketReturn {
  connectionState: ConnectionState;
  sendText: (data: string) => void;
  sendBinary: (data: ArrayBuffer) => void;
}

export function useWebSocket({
  url,
  reconnectInterval = 1000,
  maxReconnectInterval = 30000,
  onBinaryMessage,
  onTextMessage,
}: UseWebSocketOptions): UseWebSocketReturn {
  const [connectionState, setConnectionState] = useState<ConnectionState>('disconnected');

  // Use refs for values that shouldn't trigger re-renders or re-connections
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const reconnectAttemptRef = useRef(0);
  const intentionalCloseRef = useRef(false);
  const mountedRef = useRef(false);

  // Stable refs for callbacks
  const onBinaryRef = useRef(onBinaryMessage);
  const onTextRef = useRef(onTextMessage);
  onBinaryRef.current = onBinaryMessage;
  onTextRef.current = onTextMessage;

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
        wsRef.current = null;
        setConnectionState('disconnected');

        // Only auto-reconnect if not intentionally closed and still mounted
        if (!intentionalCloseRef.current && mountedRef.current) {
          const delay = calculateReconnectDelay(
            reconnectAttemptRef.current,
            reconnectInterval,
            maxReconnectInterval,
          );
          reconnectAttemptRef.current += 1;
          reconnectTimerRef.current = setTimeout(() => {
            if (mountedRef.current) {
              connect();
            }
          }, delay);
        }
      };

      ws.onerror = () => {
        // onclose will fire after onerror, state is handled there
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
  }, [url, reconnectInterval, maxReconnectInterval, clearReconnectTimer]);

  const sendText = useCallback((data: string) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(data);
    }
  }, []);

  const sendBinary = useCallback((data: ArrayBuffer) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(data);
    }
  }, []);

  // Connect on mount, disconnect on unmount
  useEffect(() => {
    mountedRef.current = true;
    connect();

    return () => {
      mountedRef.current = false;
      intentionalCloseRef.current = true;
      clearReconnectTimer();
      reconnectAttemptRef.current = 0;

      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [connect, clearReconnectTimer]);

  return {
    connectionState,
    sendText,
    sendBinary,
  };
}
