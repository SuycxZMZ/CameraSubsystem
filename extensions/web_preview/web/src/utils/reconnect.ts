/**
 * Calculate reconnect delay using exponential backoff.
 * Sequence: baseInterval, 2*base, 4*base, 8*base, ..., maxInterval
 *
 * @param attempt - 0-based reconnect attempt number
 * @param baseInterval - initial delay in ms (default 1000)
 * @param maxInterval - maximum delay in ms (default 30000)
 */
export function calculateReconnectDelay(
  attempt: number,
  baseInterval = 1000,
  maxInterval = 30000,
): number {
  const delay = baseInterval * Math.pow(2, attempt);
  return Math.min(delay, maxInterval);
}
