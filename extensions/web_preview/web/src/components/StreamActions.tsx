import { Button } from '@/components/ui/button';
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from '@/components/ui/tooltip';
import { Play, Square, Circle, ScanSearch, Camera } from 'lucide-react';
import type { StreamStatus } from '@/types/stream';
import type { ConnectionState } from '@/types/gateway-command';
import { useStreamStore } from '@/stores/useStreamStore';

interface StreamActionsProps {
  streamId: string;
  status: StreamStatus;
  connectionState: ConnectionState;
  canvasRef?: React.RefObject<HTMLCanvasElement | null>;
}

export function StreamActions({
  streamId,
  status,
  connectionState,
  canvasRef,
}: StreamActionsProps) {
  const sendCommand = useStreamStore((s) => s.sendCommand);
  const stream = useStreamStore((s) => s.streams[streamId]);
  const isConnected = connectionState === 'connected';
  const isStreaming = status === 'streaming' || status === 'subscribed';
  const isRecording = Boolean(stream?.recording);
  const isRecordPending = Boolean(stream?.recordPending);
  const recordButtonClass = isRecording
    ? 'h-8 w-8 border-red-500 bg-red-950 text-red-200 hover:bg-red-900'
    : 'h-8 w-8';

  const handleSubscribe = () => {
    sendCommand({ type: 'subscribe_stream', stream_id: streamId });
  };

  const handleUnsubscribe = () => {
    sendCommand({ type: 'unsubscribe_stream', stream_id: streamId });
  };

  const handleRecord = () => {
    sendCommand({ type: 'set_record_enabled', stream_id: streamId, enabled: !isRecording });
  };

  const handleDetect = () => {
    sendCommand({ type: 'set_detect_enabled', stream_id: streamId, enabled: true });
  };

  const handleSnapshot = () => {
    const canvas = canvasRef?.current;
    if (!canvas) return;

    canvas.toBlob((blob) => {
      if (!blob) return;
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `${streamId}_${Date.now()}.png`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    }, 'image/png');
  };

  return (
    <TooltipProvider delayDuration={300}>
      <div className="flex items-center gap-1.5 p-2">
        {/* Start / Stop */}
        {!isStreaming ? (
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                variant="default"
                size="icon"
                className="h-8 w-8"
                disabled={!isConnected}
                onClick={handleSubscribe}
              >
                <Play className="h-4 w-4" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>
              {!isConnected ? '连接断开' : '开始预览'}
            </TooltipContent>
          </Tooltip>
        ) : (
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                variant="default"
                size="icon"
                className="h-8 w-8"
                disabled={!isConnected}
                onClick={handleUnsubscribe}
              >
                <Square className="h-4 w-4" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>
              {!isConnected ? '连接断开' : '停止预览'}
            </TooltipContent>
          </Tooltip>
        )}

        {/* Record */}
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="outline"
              size="icon"
              className={recordButtonClass}
              disabled={!isConnected || isRecordPending}
              onClick={handleRecord}
            >
              {isRecording ? <Square className="h-4 w-4" /> : <Circle className="h-4 w-4" />}
            </Button>
          </TooltipTrigger>
          <TooltipContent>
            {!isConnected
              ? '连接断开'
              : isRecordPending
                ? '录制状态切换中'
                : isRecording
                  ? '停止录制'
                  : '开始录制'}
          </TooltipContent>
        </Tooltip>

        {/* Detect (disabled - not supported) */}
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="outline"
              size="icon"
              className="h-8 w-8"
              disabled
              onClick={handleDetect}
            >
              <ScanSearch className="h-4 w-4" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>检测功能暂未实现</TooltipContent>
        </Tooltip>

        {/* Snapshot */}
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="outline"
              size="icon"
              className="h-8 w-8"
              disabled={!isStreaming}
              onClick={handleSnapshot}
            >
              <Camera className="h-4 w-4" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>
            {!isStreaming ? '无活跃流' : '保存快照'}
          </TooltipContent>
        </Tooltip>
      </div>
    </TooltipProvider>
  );
}
