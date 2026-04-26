/**
 * FPS counter using a sliding window approach.
 * Records frame timestamps and counts frames within the last windowMs.
 */
export class FpsCounter {
  private frameTimestamps: number[] = [];
  private readonly windowMs: number;

  constructor(windowMs = 1000) {
    this.windowMs = windowMs;
  }

  /** Record a frame arrival at the given timestamp (ms) */
  recordFrame(timestamp: number): void {
    this.frameTimestamps.push(timestamp);
    this.pruneOld(timestamp);
  }

  /** Get the current FPS (frames in the last windowMs) */
  getFps(): number {
    const now = performance.now();
    this.pruneOld(now);
    return this.frameTimestamps.length;
  }

  /** Remove timestamps older than windowMs from the reference time */
  private pruneOld(referenceTime: number): void {
    const cutoff = referenceTime - this.windowMs;
    while (
      this.frameTimestamps.length > 0 &&
      this.frameTimestamps[0] < cutoff
    ) {
      this.frameTimestamps.shift();
    }
  }
}
