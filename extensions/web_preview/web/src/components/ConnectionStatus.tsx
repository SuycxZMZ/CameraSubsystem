import type { ConnectionState } from '@/types/gateway-command';
import { cn } from '@/lib/utils';

interface ConnectionStatusProps {
  connectionState: ConnectionState;
}

const statusConfig: Record<
  ConnectionState,
  { label: string; dotClass: string; textClass: string }
> = {
  connecting: {
    label: '连接中...',
    dotClass: 'bg-yellow-500',
    textClass: 'text-yellow-500',
  },
  connected: {
    label: '已连接',
    dotClass: 'bg-green-500',
    textClass: 'text-green-500',
  },
  disconnecting: {
    label: '断开中...',
    dotClass: 'bg-yellow-500',
    textClass: 'text-yellow-500',
  },
  disconnected: {
    label: '已断开',
    dotClass: 'bg-red-500',
    textClass: 'text-red-500',
  },
};

export function ConnectionStatus({ connectionState }: ConnectionStatusProps) {
  const config = statusConfig[connectionState];

  return (
    <div className="flex items-center gap-2">
      <span
        className={cn('inline-block h-2.5 w-2.5 rounded-full', config.dotClass)}
      />
      <span className={cn('text-sm font-medium', config.textClass)}>
        {config.label}
      </span>
    </div>
  );
}
