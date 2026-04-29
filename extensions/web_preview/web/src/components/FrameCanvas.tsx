import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from 'react';
import { WebPixelFormat, pixelFormatToName } from '@/types/web-frame-protocol';

interface FrameCanvasProps {
  payload: Uint8Array | null;
  pixelFormat: WebPixelFormat;
  width: number;
  height: number;
}

export const FrameCanvas = forwardRef<HTMLCanvasElement, FrameCanvasProps>(
  function FrameCanvas({ payload, pixelFormat, width, height }, ref) {
    const containerRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const prevBitmapRef = useRef<ImageBitmap | null>(null);

    useImperativeHandle(ref, () => canvasRef.current as HTMLCanvasElement, []);

    const getCanvasLayout = useCallback(() => {
      const canvas = canvasRef.current;
      const container = containerRef.current;
      if (!canvas || !container) return null;

      const sourceWidth = width || 16;
      const sourceHeight = height || 9;
      const rect = container.getBoundingClientRect();
      const cssWidth = Math.max(1, rect.width || sourceWidth);
      const cssHeight = Math.max(
        1,
        rect.height || (cssWidth * sourceHeight) / sourceWidth,
      );
      const dpr = Math.max(1, window.devicePixelRatio || 1);
      const backingWidth = Math.max(1, Math.round(cssWidth * dpr));
      const backingHeight = Math.max(1, Math.round(cssHeight * dpr));

      if (canvas.width !== backingWidth || canvas.height !== backingHeight) {
        canvas.width = backingWidth;
        canvas.height = backingHeight;
      }

      const ctx = canvas.getContext('2d');
      if (!ctx) return null;

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, cssWidth, cssHeight);

      return { ctx, cssWidth, cssHeight };
    }, [height, width]);

    const drawMessage = useCallback(
      (message: string, color: string) => {
        const layout = getCanvasLayout();
        if (!layout) return;

        const { ctx, cssWidth, cssHeight } = layout;
        ctx.fillStyle = '#050505';
        ctx.fillRect(0, 0, cssWidth, cssHeight);
        ctx.fillStyle = color;
        ctx.font = '16px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(message, cssWidth / 2, cssHeight / 2);
      },
      [getCanvasLayout],
    );

    const drawBitmap = useCallback(
      (bitmap: ImageBitmap) => {
        const layout = getCanvasLayout();
        if (!layout) return;

        const { ctx, cssWidth, cssHeight } = layout;
        const sourceWidth = width || bitmap.width;
        const sourceHeight = height || bitmap.height;
        const sourceAspect = sourceWidth / sourceHeight;
        const canvasAspect = cssWidth / cssHeight;
        const drawWidth =
          canvasAspect > sourceAspect ? cssHeight * sourceAspect : cssWidth;
        const drawHeight =
          canvasAspect > sourceAspect ? cssHeight : cssWidth / sourceAspect;
        const offsetX = (cssWidth - drawWidth) / 2;
        const offsetY = (cssHeight - drawHeight) / 2;

        ctx.fillStyle = '#050505';
        ctx.fillRect(0, 0, cssWidth, cssHeight);
        ctx.imageSmoothingEnabled = true;
        ctx.imageSmoothingQuality = 'high';
        ctx.drawImage(bitmap, offsetX, offsetY, drawWidth, drawHeight);
      },
      [getCanvasLayout, height, width],
    );

    useEffect(() => {
      if (!payload || payload.length === 0) {
        drawMessage('等待帧数据...', '#a1a1aa');
        return;
      }

      if (pixelFormat !== WebPixelFormat.Jpeg) {
        drawMessage(`不支持的格式: ${pixelFormatToName(pixelFormat)}`, '#ef4444');
        return;
      }

      const blob = new Blob([payload], { type: 'image/jpeg' });

      createImageBitmap(blob)
        .then((bitmap) => {
          if (prevBitmapRef.current) {
            prevBitmapRef.current.close();
          }
          prevBitmapRef.current = bitmap;
          drawBitmap(bitmap);
        })
        .catch(() => {
          console.warn('[FrameCanvas] JPEG decode failed');
        });
    }, [drawBitmap, drawMessage, payload, pixelFormat]);

    useEffect(() => {
      const container = containerRef.current;
      if (!container) return;

      const redraw = () => {
        if (prevBitmapRef.current) {
          drawBitmap(prevBitmapRef.current);
        } else {
          drawMessage('等待帧数据...', '#a1a1aa');
        }
      };

      redraw();

      if (typeof ResizeObserver === 'undefined') {
        window.addEventListener('resize', redraw);
        return () => window.removeEventListener('resize', redraw);
      }

      const resizeObserver = new ResizeObserver(redraw);
      resizeObserver.observe(container);
      return () => resizeObserver.disconnect();
    }, [drawBitmap, drawMessage]);

    useEffect(() => {
      return () => {
        if (prevBitmapRef.current) {
          prevBitmapRef.current.close();
          prevBitmapRef.current = null;
        }
      };
    }, []);

    return (
      <div
        ref={containerRef}
        className="relative w-full overflow-hidden rounded-md bg-zinc-950"
        style={{
          aspectRatio: width > 0 && height > 0 ? `${width} / ${height}` : '16 / 9',
          maxHeight: '72vh',
        }}
      >
        <canvas ref={canvasRef} className="block h-full w-full" />
      </div>
    );
  },
);
