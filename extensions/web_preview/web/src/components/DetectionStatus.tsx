import { Badge } from '@/components/ui/badge';
import type { StreamState } from '@/types/stream';

interface DetectionStatusProps {
  stream: StreamState;
}

export function DetectionStatus({ stream }: DetectionStatusProps) {
  const { detection } = stream;

  if (!detection.available) {
    return (
      <div className="flex items-center gap-2 px-3 py-2 text-xs text-zinc-500">
        <Badge variant="outline" className="border-zinc-700 text-zinc-500">
          检测
        </Badge>
        <span>服务不可用{detection.error ? `: ${detection.error}` : ''}</span>
      </div>
    );
  }

  const stateColor =
    detection.state === 'running'
      ? 'border-green-500 text-green-400'
      : detection.state === 'error'
        ? 'border-red-500 text-red-400'
        : 'border-zinc-600 text-zinc-400';

  const stateLabel =
    detection.state === 'running'
      ? '运行中'
      : detection.state === 'error'
        ? '错误'
        : '空闲';

  return (
    <div className="flex flex-wrap items-center gap-2 px-3 py-2 text-xs">
      <Badge variant="outline" className={stateColor}>
        检测: {stateLabel}
      </Badge>

      {detection.metrics && (
        <>
          <span className="text-zinc-400">
            推理: {detection.metrics.inferredFrames} 帧
          </span>
          <span className="text-zinc-400">
            目标: {detection.metrics.lastObjectCount} 个
          </span>
        </>
      )}

      {detection.config && (
        <span className="text-zinc-500">
          间隔: {detection.config.inferEveryNFrames} 帧
        </span>
      )}

      {detection.modelName && (
        <span className="text-zinc-500">
          模型: {detection.modelName}
        </span>
      )}

      {detection.lastError && (
        <span className="text-red-400">错误: {detection.lastError}</span>
      )}
    </div>
  );
}
