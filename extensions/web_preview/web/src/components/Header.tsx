import { ConnectionStatus } from './ConnectionStatus';
import type { ConnectionState } from '@/types/gateway-command';

interface HeaderProps {
  connectionState: ConnectionState;
}

export function Header({ connectionState }: HeaderProps) {
  return (
    <header className="flex items-center justify-between border-b border-zinc-800 px-5 py-3">
      <h1 className="text-lg font-semibold text-zinc-50">
        CameraSubsystem Web Preview
      </h1>
      <ConnectionStatus connectionState={connectionState} />
    </header>
  );
}
