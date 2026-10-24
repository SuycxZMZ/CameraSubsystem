export type GatewayCommand =
  | { type: 'subscribe_stream'; stream_id: string }
  | { type: 'unsubscribe_stream'; stream_id: string }
  | { type: 'set_stream_enabled'; stream_id: string; enabled: boolean }
  | { type: 'set_record_enabled'; stream_id: string; enabled: boolean; container?: 'raw_h264' | 'mp4' }
  | { type: 'set_detect_enabled'; stream_id: string; enabled: boolean }
  | { type: 'set_detection_config'; stream_id: string; infer_every_n_frames?: number; score_threshold?: number; nms_threshold?: number };

export interface CommandResult {
  type: 'command_result';
  status: 'success' | 'error' | 'not_supported';
  reason?: string;
}

export interface RecordStatus {
  type: 'record_status';
  request_id?: string;
  stream_id: string;
  recording?: boolean;
  state?: string;
  codec?: string;
  container?: string;
  file?: string;
  encoded_frames?: number;
  decoded_frames?: number;
  dropped_frames?: number;
  input_frames?: number;
  duration_ms?: number;
  bytes_written?: number;
  packets_written?: number;
  decode_failures?: number;
  write_failures?: number;
  error?: string;
  profile?: {
    width?: number;
    height?: number;
    fps?: number;
    bitrate?: number;
    gop?: number;
  };
}

export interface DetectionStatus {
  type: 'detection_status';
  stream_id: string;
  available: boolean;
  error?: string;
  state?: string;
  model_name?: string;
  npu_core_mask?: number;
  performance_profile?: {
    applied: boolean;
  };
  config?: {
    infer_every_n_frames: number;
    score_threshold: number;
    nms_threshold: number;
  };
  metrics?: {
    input_frames: number;
    inferred_frames: number;
    last_object_count: number;
  };
  last_error?: string;
}

export interface DetectionResponse {
  type: 'detection_response';
  stream_id: string;
  ok: boolean;
  state?: string;
  error_code?: string;
  message?: string;
}

export interface GatewayStatus {
  type: 'status';
  stream_id: string;
  stream_index?: number;
  status: string;
  width: number;
  height: number;
  format: string;
  input_frames: number;
  published_frames: number;
  dropped_frames: number;
  unsupported_frames: number;
  detection?: {
    available: boolean;
    error?: string;
    state?: string;
    model_name?: string;
    npu_core_mask?: number;
    performance_profile?: {
      applied: boolean;
    };
    config?: {
      infer_every_n_frames: number;
      score_threshold: number;
      nms_threshold: number;
    };
    metrics?: {
      input_frames: number;
      inferred_frames: number;
      last_object_count: number;
    };
    last_error?: string;
  };
}

export type ConnectionState =
  | 'connecting'
  | 'connected'
  | 'disconnecting'
  | 'disconnected';
