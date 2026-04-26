import { useRef } from 'react';
import { Card } from '@/components/ui/card';
import { FrameCanvas } from './FrameCanvas';
import { StreamOverlay } from './StreamOverlay';
import { StreamActions } from './StreamActions';
import { useStreamStore } from '@/stores/useStreamStore';

interface StreamTileProps {
  streamId: string;
}

export function StreamTile({ streamId }: StreamTileProps) {
  const stream = useStreamStore((s) => s.streams[streamId]);
  const connectionState = useStreamStore((s) => s.connectionState);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  if (!stream) return null;

  const isStreaming =
    stream.status === 'streaming' || stream.status === 'subscribed';

  return (
    <Card className="overflow-hidden border-zinc-800 bg-zinc-900">
      {/* Canvas + Overlay container */}
      <div className="relative">
        <div ref={() => {
          // Forward the canvas ref from FrameCanvas
          // We use a callback approach since FrameCanvas manages its own canvas
        }}>
          <FrameCanvas
            payload={stream.lastFramePayload}
            pixelFormat={stream.pixelFormat}
            width={stream.width}
            height={stream.height}
          />
        </div>
        {isStreaming && (
          <StreamOverlay
            streamId={stream.streamId}
            width={stream.width}
            height={stream.height}
            pixelFormatName={stream.pixelFormatName}
            fps={stream.fps}
            status={stream.status}
            errorCount={stream.errorCount}
          />
        )}
      </div>

      {/* Actions */}
      <StreamActions
        streamId={stream.streamId}
        status={stream.status}
        connectionState={connectionState}
        canvasRef={canvasRef}
      />
    </Card>
  );
}
