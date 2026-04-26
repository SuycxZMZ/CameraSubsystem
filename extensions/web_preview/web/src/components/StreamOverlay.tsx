import { Badge } from '@/components/ui/badge';
import type { StreamStatus } from '@/types/stream';

interface StreamOverlayProps {
  streamId: string;
  width: number;
  height: number;
  pixelFormatName: string;
  fps: number;
  status: StreamStatus;
  errorCount: number;
}

export function StreamOverlay({
  streamId,
  width,
  height,
  pixelFormatName,
  fps,
  status,
  errorCount,
}: StreamOverlayProps) {
  return (
    <div className="absolute inset-x-0 top-0 flex items-start justify-between p-2">
      {/* Left: stream name */}
      <div className="flex items-center gap-2">
        <span className="rounded bg-black/60 px-1.5 py-0.5 text-sm font-semibold text-white">
          {streamId}
        </span>
        {status === 'unsupported_format' && (
          <Badge variant="destructive" className="text-xs">
            不支持格式
          </Badge>
        )}
        {status === 'error' && (
          <Badge variant="destructive" className="text-xs">
            错误
          </Badge>
        )}
      </div>

      {/* Right: FPS */}
      <div className="flex items-center gap-2">
        {errorCount > 0 && (
          <span className="rounded bg-red-500/80 px-1.5 py-0.5 text-xs text-white">
            err:{errorCount}
          </span>
        )}
        <span className="rounded bg-black/60 px-1.5 py-0.5 text-sm text-zinc-300">
          {fps} FPS
        </span>
      </div>

      {/* Bottom: resolution + format */}
      {width > 0 && height > 0 && (
        <div className="absolute bottom-2 left-2">
          <span className="rounded bg-black/60 px-1.5 py-0.5 text-xs text-zinc-400">
            {width}x{height} {pixelFormatName}
          </span>
        </div>
      )}
    </div>
  );
}
