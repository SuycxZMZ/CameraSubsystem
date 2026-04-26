export type GatewayCommand =
  | { type: 'subscribe_stream'; stream_id: string }
  | { type: 'unsubscribe_stream'; stream_id: string }
  | { type: 'set_stream_enabled'; stream_id: string; enabled: boolean }
  | { type: 'set_record_enabled'; stream_id: string; enabled: boolean }
  | { type: 'set_detect_enabled'; stream_id: string; enabled: boolean };

export interface CommandResult {
  type: 'command_result';
  status: 'success' | 'error' | 'not_supported';
  reason?: string;
}

export interface GatewayStatus {
  type: 'status';
  stream_id: string;
  status: string;
  width: number;
  height: number;
  format: string;
  input_frames: number;
  published_frames: number;
  dropped_frames: number;
  unsupported_frames: number;
}

export type ConnectionState =
  | 'connecting'
  | 'connected'
  | 'disconnecting'
  | 'disconnected';
