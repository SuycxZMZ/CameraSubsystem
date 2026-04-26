import { useRef, useEffect } from 'react';
import { WebPixelFormat, pixelFormatToName } from '@/types/web-frame-protocol';

interface FrameCanvasProps {
  payload: Uint8Array | null;
  pixelFormat: WebPixelFormat;
  width: number;
  height: number;
}

export function FrameCanvas({ payload, pixelFormat, width, height }: FrameCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const prevBitmapRef = useRef<ImageBitmap | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Handle no payload
    if (!payload || payload.length === 0) {
      canvas.width = width || 640;
      canvas.height = height || 480;
      ctx.fillStyle = '#050505';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#a1a1aa';
      ctx.font = '16px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('等待帧数据...', canvas.width / 2, canvas.height / 2);
      return;
    }

    // Handle unsupported format
    if (pixelFormat !== WebPixelFormat.Jpeg) {
      canvas.width = width || 640;
      canvas.height = height || 480;
      ctx.fillStyle = '#050505';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#ef4444';
      ctx.font = '16px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(
        `不支持的格式: ${pixelFormatToName(pixelFormat)}`,
        canvas.width / 2,
        canvas.height / 2,
      );
      return;
    }

    // Render JPEG payload using createImageBitmap
    const blob = new Blob([payload], { type: 'image/jpeg' });

    createImageBitmap(blob)
      .then((bitmap) => {
        // Release previous bitmap
        if (prevBitmapRef.current) {
          prevBitmapRef.current.close();
        }
        prevBitmapRef.current = bitmap;

        canvas.width = width || bitmap.width;
        canvas.height = height || bitmap.height;
        ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
      })
      .catch(() => {
        // JPEG decode failed - keep previous frame, don't crash
        console.warn('[FrameCanvas] JPEG decode failed');
      });
  }, [payload, pixelFormat, width, height]);

  // Cleanup bitmap on unmount
  useEffect(() => {
    return () => {
      if (prevBitmapRef.current) {
        prevBitmapRef.current.close();
        prevBitmapRef.current = null;
      }
    };
  }, []);

  return (
    <canvas
      ref={canvasRef}
      className="w-full rounded-md"
      style={{
        objectFit: 'contain',
        background: '#050505',
        maxHeight: '72vh',
      }}
    />
  );
}
