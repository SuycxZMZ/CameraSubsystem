const recordErrorMessages: Record<string, string> = {
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
  websocket_not_connected: 'WebSocket 未连接，无法发送录制命令',
};

export function formatRecordError(error: string): string {
  return recordErrorMessages[error] ?? error;
}
