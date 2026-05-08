import type { StreamState } from '@/types/stream';

interface RecordingStatusProps {
  stream: StreamState;
}

function formatDuration(ms: number): string {
  const totalSeconds = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${seconds.toString().padStart(2, '0')}`;
}

function formatBytes(bytes: number): string {
  if (bytes <= 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function fileName(path: string): string {
  if (!path) return '';
  const index = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  return index >= 0 ? path.slice(index + 1) : path;
}

function formatProfile(stream: StreamState): string {
  const { recordProfile } = stream;
  const parts: string[] = [];
  if (recordProfile.width && recordProfile.height) {
    parts.push(`${recordProfile.width}x${recordProfile.height}`);
  }
  if (recordProfile.fps) parts.push(`${recordProfile.fps}fps`);
  if (recordProfile.bitrate) parts.push(`${Math.round(recordProfile.bitrate / 1000)}kbps`);
  if (recordProfile.gop) parts.push(`GOP ${recordProfile.gop}`);
  return parts.join(' / ');
}

function formatRecordError(error: string): string {
  const messages: Record<string, string> = {
    codec_server_not_available: '编码服务不可用，请确认 camera_codec_server 已启动',
    codec_server_write_failed: '发送录制命令失败，请检查 codec socket',
    codec_server_timeout: '编码服务响应超时',
    codec_server_read_failed: '读取编码服务响应失败',
    unsupported_container: '当前录制格式不支持',
    already_recording: '该流已经在录制',
    not_recording: '该流当前未在录制',
    output_dir_not_writable: '录制目录不可写',
    jpeg_decoder_not_available: 'JPEG 解码器不可用',
    recording_io_error: '录制文件写入失败',
  };
  return messages[error] ?? error;
}

export function RecordingStatus({ stream }: RecordingStatusProps) {
  const hasRecordInfo =
    stream.recording ||
    stream.recordPending ||
    Boolean(stream.recordFile) ||
    Boolean(stream.recordError) ||
    stream.encodedFrames > 0 ||
    stream.decodedFrames > 0;

  if (!hasRecordInfo) return null;

  const durationMs =
    stream.recording && stream.recordStartedAtMs > 0
      ? Date.now() - stream.recordStartedAtMs
      : stream.recordDurationMs;
  const statusText = stream.recordPending
    ? stream.recordState === 'stopping'
      ? '停止中'
      : '启动中'
    : stream.recording
      ? '录制中'
      : '已停止';
  const profileText = formatProfile(stream);
  const hasFileStats = stream.recordBytesWritten > 0 || stream.recordPacketsWritten > 0;
  const hasInputStats = stream.recordInputFrames > 0 || stream.recordDroppedFrames > 0;

  return (
    <div className="border-t border-zinc-800 bg-zinc-950/70 px-3 py-2 text-xs text-zinc-300">
      <div className="flex flex-wrap items-center gap-x-3 gap-y-1">
        <span className={stream.recording ? 'font-medium text-red-300' : 'text-zinc-400'}>
          {statusText}
        </span>
        <span>时长 {formatDuration(durationMs)}</span>
        {hasFileStats && (
          <span>
            文件 {formatBytes(stream.recordBytesWritten)}
            {stream.recordPacketsWritten > 0 && ` / ${stream.recordPacketsWritten} packets`}
          </span>
        )}
        {hasInputStats && (
          <span>
            输入 {stream.recordInputFrames}
            {stream.recordDroppedFrames > 0 && ` / 丢弃 ${stream.recordDroppedFrames}`}
          </span>
        )}
        <span>编码 {stream.encodedFrames}</span>
        <span>解码 {stream.decodedFrames}</span>
        {(stream.recordDecodeFailures > 0 || stream.recordWriteFailures > 0) && (
          <span className="text-amber-300">
            异常 d:{stream.recordDecodeFailures} w:{stream.recordWriteFailures}
          </span>
        )}
      </div>
      {(stream.recordFile || profileText || stream.recordError) && (
        <div className="mt-1 flex flex-wrap items-center gap-x-3 gap-y-1 text-zinc-500">
          {stream.recordFile && <span>输出 {fileName(stream.recordFile)}</span>}
          {profileText && <span>{profileText}</span>}
          {stream.recordError && (
            <span className="text-red-300" title={stream.recordError}>
              {formatRecordError(stream.recordError)}
            </span>
          )}
        </div>
      )}
    </div>
  );
}
