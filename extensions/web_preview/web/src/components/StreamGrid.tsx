import { StreamTile } from './StreamTile';
import { useStreamStore } from '@/stores/useStreamStore';
import { ConnectionStatus } from './ConnectionStatus';

/**
 * Calculate grid columns based on stream count.
 * 1 stream  -> 1 col
 * 2 streams -> 2 cols
 * 3-4       -> 2 cols
 * 5-6       -> 3 cols
 * 7-9       -> 3 cols
 * 10+       -> 4 cols + scroll
 */
function getGridColumns(streamCount: number): number {
  if (streamCount <= 1) return 1;
  if (streamCount <= 2) return 2;
  if (streamCount <= 4) return 2;
  if (streamCount <= 6) return 3;
  if (streamCount <= 9) return 3;
  return 4;
}

export function StreamGrid() {
  const streams = useStreamStore((s) => s.streams);
  const connectionState = useStreamStore((s) => s.connectionState);

  const streamIds = Object.keys(streams);
  const streamCount = streamIds.length;

  // Empty state
  if (streamCount === 0) {
    return (
      <div className="flex flex-1 items-center justify-center p-8">
        <div className="text-center">
          <div className="mb-4 text-zinc-500">
            <svg
              className="mx-auto h-16 w-16"
              fill="none"
              viewBox="0 0 24 24"
              stroke="currentColor"
              strokeWidth={1}
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                d="M15 10l4.553-2.276A1 1 0 0121 8.618v6.764a1 1 0 01-1.447.894L15 14M5 18h8a2 2 0 002-2V8a2 2 0 00-2-2H5a2 2 0 00-2 2v8a2 2 0 002 2z"
              />
            </svg>
          </div>
          <h2 className="mb-2 text-lg font-medium text-zinc-300">
            暂无活跃流
          </h2>
          <p className="text-sm text-zinc-500">
            {connectionState === 'connected'
              ? '等待 Gateway 推送流数据...'
              : '请先连接 Gateway'}
          </p>
          <div className="mt-4">
            <ConnectionStatus connectionState={connectionState} />
          </div>
        </div>
      </div>
    );
  }

  const cols = getGridColumns(streamCount);
  const shouldScroll = streamCount > 9;

  return (
    <div
      className={`grid gap-4 p-4 ${
        shouldScroll ? 'max-h-[calc(100vh-56px)] overflow-y-auto' : ''
      }`}
      style={{
        gridTemplateColumns: `repeat(${cols}, minmax(0, 1fr))`,
      }}
    >
      {streamIds.map((id) => (
        <StreamTile key={id} streamId={id} />
      ))}
    </div>
  );
}
