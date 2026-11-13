package com.chusogar.cmesys.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/** The machine's screen: its picture scaled into the view (keeping the
 *  machine's monitor shape, square pixels, filling it, or whole multiples),
 *  drawn from the emulation thread, with the drive lights and the frame rate
 *  on top. */
final class ScreenView extends SurfaceView implements SurfaceHolder.Callback, Emulator.Screen {
    static final String SCALE_FIT = "fit43", SCALE_SQUARE = "square", SCALE_STRETCH = "stretch", SCALE_INTEGER = "integer";

    private final Machine machine;
    private Bitmap bitmap;
    private final Object lock = new Object();
    private final Paint paint = new Paint();
    private final Paint ledOn = new Paint(), ledOff = new Paint(), text = new Paint();
    private final Rect src = new Rect();
    private final Rect dst = new Rect();
    private volatile boolean hasSurface;
    private volatile String scale = SCALE_FIT;
    private volatile boolean showFps, showLeds = true;
    private volatile int bottomInset;
    private final float density;
    private int status;
    private float fps;

    ScreenView(Context context, Machine machine) {
        super(context);
        this.machine = machine;
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

    private void place(int w, int h, int pw, int ph) {
        h = Math.max(1, h - bottomInset);
        float ratio;   // width / height of the picture shown
        switch (scale) {
            case SCALE_SQUARE: ratio = pw / (float) (ph * machine.lineScale()); break;
            case SCALE_STRETCH: dst.set(0, 0, w, h); return;
            case SCALE_INTEGER: {
                int lh = ph * machine.lineScale();
                int f = Math.min(w / pw, h / lh);
                if (f < 1) {                                  // too small: whole picture
                    ratio = machine.displayAspect(pw, ph);
                    break;
                }
                int dw = pw * f, dh = lh * f;
                dst.set((w - dw) / 2, (h - dh) / 2, (w + dw) / 2, (h + dh) / 2);
                return;
            }
            default: ratio = machine.displayAspect(pw, ph); break;
        }
        int dw = w, dh = (int) (w / ratio);
        if (dh > h) {
            dh = h;
            dw = (int) (h * ratio);
        }
        dst.set((w - dw) / 2, (h - dh) / 2, (w - dw) / 2 + dw, (h - dh) / 2 + dh);
    }

    @Override
    public void showFrame(int[] pixels, int w, int h, float fps, int status) {
        synchronized (lock) {
            if (bitmap == null || bitmap.getWidth() != w || bitmap.getHeight() != h) {
                bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            }
            bitmap.setPixels(pixels, 0, w, 0, 0, w, h);
            this.status = status;
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
            synchronized (lock) {
                if (bitmap == null) return;
                src.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
                place(c.getWidth(), c.getHeight(), bitmap.getWidth(), bitmap.getHeight());
                c.drawBitmap(bitmap, src, dst, paint);
                int leds = machine.leds();
                if (showLeds && leds > 0) {
                    float r = 4 * density, y = dst.top + 8 * density;
                    for (int i = 0; i < leds; i++) {
                        boolean on = (status & (1 << i)) != 0;
                        c.drawCircle(dst.right - (10 + 14 * (leds - 1 - i)) * density, y, r, on ? ledOn : ledOff);
                    }
                }
                if (showFps) c.drawText(String.format("%.1f fps", fps), dst.left + 6 * density, dst.top + 14 * density, text);
            }
        } finally {
            holder.unlockCanvasAndPost(c);
        }
    }
}
