import { useCallback, useEffect } from 'react';
import { Header } from '@/components/Header';
import { StreamGrid } from '@/components/StreamGrid';
import { useWebSocket } from '@/hooks/useWebSocket';
import { useFrameReceiver } from '@/hooks/useFrameReceiver';
import { useStreamStore } from '@/stores/useStreamStore';
import type { CommandResult, GatewayStatus, RecordStatus } from '@/types/gateway-command';

function App() {
  const { onBinaryMessage } = useFrameReceiver();

  // Compute WebSocket URL from current location
  const wsUrl = `ws://${window.location.host}/ws`;

  const handleTextMessage = useCallback((data: string) => {
    try {
      const json = JSON.parse(data);

      if (json.type === 'status') {
        useStreamStore.getState().handleGatewayStatus(json as GatewayStatus);
      } else if (json.type === 'record_status') {
        useStreamStore.getState().handleRecordStatus(json as RecordStatus);
      } else if (json.type === 'command_result') {
        useStreamStore.getState().handleCommandResult(json as CommandResult);
      } else {
        console.warn('[App] Unknown text message type:', json.type);
      }
    } catch {
      console.warn('[App] Failed to parse text message:', data);
    }
  }, []);

  const { connectionState, sendText } = useWebSocket({
    url: wsUrl,
    onBinaryMessage,
    onTextMessage: handleTextMessage,
  });

  // Sync connection state to store and inject sendText
  useEffect(() => {
    useStreamStore.getState().setConnectionState(connectionState);
    useStreamStore.getState().setSendTextFn(sendText);
  }, [connectionState, sendText]);

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
