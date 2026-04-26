import { useCallback, useState } from 'react';
import { Header } from '@/components/Header';
import { StreamGrid } from '@/components/StreamGrid';
import { useWebSocket } from '@/hooks/useWebSocket';
import { useFrameReceiver } from '@/hooks/useFrameReceiver';
import { useStreamStore } from '@/stores/useStreamStore';
import type { ConnectionState } from '@/types/gateway-command';
import type { CommandResult, GatewayStatus } from '@/types/gateway-command';

function App() {
  const { onBinaryMessage } = useFrameReceiver();
  const [connectionState, setConnectionState] = useState<ConnectionState>('disconnected');

  // Compute WebSocket URL from current location
  const wsUrl = `ws://${window.location.host}/ws`;

  const handleTextMessage = useCallback((data: string) => {
    try {
      const json = JSON.parse(data);

      if (json.type === 'status') {
        useStreamStore.getState().handleGatewayStatus(json as GatewayStatus);
      } else if (json.type === 'command_result') {
        useStreamStore.getState().handleCommandResult(json as CommandResult);
      } else {
        console.warn('[App] Unknown text message type:', json.type);
      }
    } catch {
      console.warn('[App] Failed to parse text message:', data);
    }
  }, []);

  const handleConnectionChange = useCallback((state: ConnectionState) => {
    setConnectionState(state);
    useStreamStore.getState().setConnectionState(state);
  }, []);

  const { sendText } = useWebSocket({
    url: wsUrl,
    onBinaryMessage,
    onTextMessage: handleTextMessage,
    onConnectionChange: handleConnectionChange,
  });

  // Inject sendText into store for command sending
  useStreamStore.getState().setSendTextFn(sendText);

  return (
    <div className="flex min-h-screen flex-col bg-zinc-950 text-zinc-50">
      <Header connectionState={connectionState} />
      <main className="flex flex-1 flex-col">
        <StreamGrid />
      </main>
    </div>
  );
}

export default App;
