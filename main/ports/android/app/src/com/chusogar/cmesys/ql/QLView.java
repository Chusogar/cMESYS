package com.chusogar.cmesys.ql;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/** The QL's screen: the 512x256 picture scaled into the view (keeping the
 *  4:3 of a TV, square pixels, filling it, or whole multiples), drawn from the
 *  emulation thread, with the microdrive lights and the frame rate on top. */
final class QLView extends SurfaceView implements SurfaceHolder.Callback, Emulator.Screen {
    static final String SCALE_43 = "fit43", SCALE_SQUARE = "square", SCALE_STRETCH = "stretch", SCALE_INTEGER = "integer";

    private final Bitmap bitmap = Bitmap.createBitmap(QLNative.WIDTH, QLNative.HEIGHT, Bitmap.Config.ARGB_8888);
    private final Paint paint = new Paint();
    private final Paint ledOn = new Paint(), ledOff = new Paint(), text = new Paint();
    private final Rect src = new Rect(0, 0, QLNative.WIDTH, QLNative.HEIGHT);
    private final Rect dst = new Rect();
    private volatile boolean hasSurface;
    private volatile String scale = SCALE_43;
    private volatile boolean showFps, showLeds = true;
    private volatile int bottomInset;
    private final float density;

    QLView(Context context) {
        super(context);
        getHolder().addCallback(this);
        density = context.getResources().getDisplayMetrics().density;
        ledOn.setColor(Color.rgb(255, 40, 30));
        ledOff.setColor(Color.argb(110, 90, 20, 20));
        text.setColor(Color.argb(200, 255, 255, 255));
        text.setTextSize(12 * density);
        text.setAntiAlias(true);
        setFocusable(false);
    }

    void setScale(String s) { scale = s; redraw(); }

    void setSmooth(boolean smooth) {
        paint.setFilterBitmap(smooth);
        redraw();
    }

    void setOverlay(boolean fps, boolean leds) {
        showFps = fps;
        showLeds = leds;
    }

    /** Pixels at the bottom covered by the on-screen keyboard (the picture goes above). */
    void setBottomInset(int px) {
        bottomInset = px;
        redraw();
    }

    @Override public void surfaceCreated(SurfaceHolder h) { hasSurface = true; redraw(); }
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int hgt) { redraw(); }
    @Override public void surfaceDestroyed(SurfaceHolder h) { hasSurface = false; }

    private void place(int w, int h) {
        h = Math.max(1, h - bottomInset);
        float ratio;   // width / height of the picture shown
        switch (scale) {
            case SCALE_SQUARE: ratio = 2f; break;            // 512x256 square pixels
            case SCALE_STRETCH: dst.set(0, 0, w, h); return;
            case SCALE_INTEGER: {
                int f = Math.max(1, Math.min(w / QLNative.WIDTH, h / (QLNative.HEIGHT * 2)));
                int dw = QLNative.WIDTH * f, dh = QLNative.HEIGHT * 2 * f;
                if (w < QLNative.WIDTH || h < QLNative.HEIGHT * 2) {   // too small: whole picture
                    ratio = 4f / 3f;
                    break;
                }
                dst.set((w - dw) / 2, (h - dh) / 2, (w + dw) / 2, (h + dh) / 2);
                return;
            }
            default: ratio = 4f / 3f; break;                // a QL on a TV
        }
        int dw = w, dh = (int) (w / ratio);
        if (dh > h) {
            dh = h;
            dw = (int) (h * ratio);
        }
        dst.set((w - dw) / 2, (h - dh) / 2, (w - dw) / 2 + dw, (h - dh) / 2 + dh);
    }

    private boolean mdv1, mdv2;
    private float fps;

    @Override
    public void showFrame(int[] pixels, float fps, boolean mdv1, boolean mdv2) {
        synchronized (bitmap) {
            bitmap.setPixels(pixels, 0, QLNative.WIDTH, 0, 0, QLNative.WIDTH, QLNative.HEIGHT);
            this.mdv1 = mdv1;
            this.mdv2 = mdv2;
            this.fps = fps;
        }
        redraw();
    }

    /** Draws the last picture again (after a change of size or of settings). */
    void redraw() {
        if (!hasSurface) return;
        SurfaceHolder holder = getHolder();
        Canvas c;
        try {
            c = holder.lockCanvas();
        } catch (Exception e) {
            return;
        }
        if (c == null) return;
        try {
            c.drawColor(Color.BLACK);
            synchronized (bitmap) {
                place(c.getWidth(), c.getHeight());
                c.drawBitmap(bitmap, src, dst, paint);
                if (showLeds) {
                    float r = 4 * density, y = dst.top + 8 * density;
                    c.drawCircle(dst.right - 24 * density, y, r, mdv1 ? ledOn : ledOff);
                    c.drawCircle(dst.right - 10 * density, y, r, mdv2 ? ledOn : ledOff);
                }
                if (showFps) c.drawText(String.format("%.1f fps", fps), dst.left + 6 * density, dst.top + 14 * density, text);
            }
        } finally {
            holder.unlockCanvasAndPost(c);
        }
    }
}
