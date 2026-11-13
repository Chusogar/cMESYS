package com.chusogar.cmesys.arcade;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.View;

/** The game's monitor: its picture scaled by the GPU (a hardware-accelerated
 *  view, drawn on the UI thread: the emulation thread only copies the
 *  pixels), 4:3 (3:4 for the vertical games), whole multiples or the whole
 *  view; at the top when the controls are below it (portrait). */
final class ArcadeScreen extends View implements Native.Screen {
    static final String SCALE_FIT = "fit", SCALE_INTEGER = "integer", SCALE_STRETCH = "stretch";

    private final Object lock = new Object();
    private int[] back = new int[0];
    private int backW, backH;
    private float backFps;
    private boolean fresh;
    private Bitmap bitmap;
    private int[] front = new int[0];
    private float fps;
    private final Paint paint = new Paint();
    private final Paint text = new Paint();
    private final Rect src = new Rect(), dst = new Rect();
    private volatile String scale = SCALE_FIT;
    private volatile boolean showFps, top;
    private volatile int bottomInset;
    private final float density;

    ArcadeScreen(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
        text.setColor(Color.argb(200, 255, 255, 255));
        text.setTextSize(12 * density);
        text.setAntiAlias(true);
        setBackgroundColor(Color.BLACK);
    }

    void setScale(String s) { scale = s; invalidate(); }

    void setSmooth(boolean s) { paint.setFilterBitmap(s); invalidate(); }

    void setShowFps(boolean f) { showFps = f; }

    /** The controls take this much at the bottom (the picture goes above, at the top). */
    void setBottomInset(int px, boolean atTop) {
        bottomInset = px;
        top = atTop;
        invalidate();
    }

    /** Where the picture is (after the first frame), for the controls. */
    Rect pictureRect() { return dst; }

    /** Height the picture takes across the whole width (portrait). */
    static int pictureHeight(int viewW, int picW, int picH) {
        float ratio = picW >= picH ? 4f / 3f : 3f / 4f;
        return (int) (viewW / ratio);
    }

    private void place(int w, int h, int pw, int ph) {
        h = Math.max(1, h - bottomInset);
        if (SCALE_STRETCH.equals(scale)) {
            dst.set(0, 0, w, h);
            return;
        }
        int dw, dh;
        if (SCALE_INTEGER.equals(scale) && w / pw >= 1 && h / ph >= 1) {
            int f = Math.min(w / pw, h / ph);
            dw = pw * f;
            dh = ph * f;
        } else {
            float ratio = pw >= ph ? 4f / 3f : 3f / 4f;    // the monitor's shape
            dw = w;
            dh = (int) (w / ratio);
            if (dh > h) {
                dh = h;
                dw = (int) (h * ratio);
            }
        }
        int y = top ? 0 : (h - dh) / 2;
        dst.set((w - dw) / 2, y, (w - dw) / 2 + dw, y + dh);
    }

    @Override
    public void showFrame(int[] pixels, int w, int h, float fps) {
        int n = w * h;
        synchronized (lock) {
            if (back.length != n) back = new int[n];
            System.arraycopy(pixels, 0, back, 0, n);
            backW = w;
            backH = h;
            backFps = fps;
            fresh = true;
        }
        postInvalidateOnAnimation();
    }

    @Override
    protected void onDraw(Canvas c) {
        synchronized (lock) {
            if (fresh) {
                if (bitmap == null || bitmap.getWidth() != backW || bitmap.getHeight() != backH) {
                    bitmap = Bitmap.createBitmap(backW, backH, Bitmap.Config.ARGB_8888);
                }
                if (front.length != back.length) front = new int[back.length];
                System.arraycopy(back, 0, front, 0, back.length);
                fps = backFps;
                fresh = false;
            } else if (bitmap == null) {
                return;
            }
        }
        int bw = bitmap.getWidth(), bh = bitmap.getHeight();
        if (front.length == bw * bh) bitmap.setPixels(front, 0, bw, 0, 0, bw, bh);
        src.set(0, 0, bw, bh);
        place(getWidth(), getHeight(), bw, bh);
        c.drawBitmap(bitmap, src, dst, paint);
        if (showFps) c.drawText(String.format("%.1f fps", fps), dst.left + 6 * density, dst.top + 14 * density, text);
    }
}
