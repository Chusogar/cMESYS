package com.chusogar.cmesys.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;

/** The machine's screen: its picture scaled into the view (keeping the
 *  machine's monitor shape, square pixels, filling it, or whole multiples),
 *  with the drive lights and the frame rate on top.
 *
 *  The emulation thread only hands over each frame's pixels (a copy); the
 *  view draws the latest one on the UI thread, scaled by the GPU (a
 *  hardware-accelerated view). Scaling in software on the emulation thread,
 *  as a SurfaceView's canvas does, took longer than a frame on a phone's
 *  screen and starved the sound. */
final class ScreenView extends View implements Emulator.Screen {
    static final String SCALE_FIT = "fit43", SCALE_SQUARE = "square", SCALE_STRETCH = "stretch", SCALE_INTEGER = "integer";

    private final Machine machine;
    private final Object lock = new Object();
    // the latest frame from the emulation thread (under lock)
    private int[] back = new int[0];
    private int backW, backH, backStatus;
    private float backFps;
    private boolean fresh;
    // what is drawn (UI thread)
    private Bitmap bitmap;
    private int[] front = new int[0];
    private int status;
    private float fps;

    private final Paint paint = new Paint();
    private final Paint ledOn = new Paint(), ledOff = new Paint(), text = new Paint();
    private final Rect src = new Rect();
    private final Rect dst = new Rect();
    private volatile String scale = SCALE_FIT;
    private volatile boolean showFps, showLeds = true;
    private volatile int bottomInset;
    private final float density;

    ScreenView(Context context, Machine machine) {
        super(context);
        this.machine = machine;
        density = context.getResources().getDisplayMetrics().density;
        ledOn.setColor(Color.rgb(255, 40, 30));
        ledOff.setColor(Color.argb(110, 90, 20, 20));
        text.setColor(Color.argb(200, 255, 255, 255));
        text.setTextSize(12 * density);
        text.setAntiAlias(true);
        setFocusable(false);
        setBackgroundColor(Color.BLACK);
    }

    void setScale(String s) { scale = s; invalidate(); }

    // --- the machine's mouse: the screen as a touchpad ------------------------------
    //   drag: the pointer moves (as much as the finger over the picture)
    //   tap: a left click; a two-finger tap: a right click
    //   hold still, then drag: the left button held (dragging windows, icons)
    private float lastX, lastY, downX, downY, fracX, fracY;
    private long downTime;
    private boolean moved, dragging;
    private int maxPointers, buttons;
    private final Runnable holdLeft = new Runnable() {
        @Override public void run() {
            if (moved || maxPointers > 1) return;
            dragging = true;
            buttons = 1;
            Core.mouse(0, 0, buttons);
        }
    };
    private final Runnable release = new Runnable() {
        @Override public void run() {
            buttons = 0;
            Core.mouse(0, 0, 0);
        }
    };

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (!machine.usesMouse()) return super.onTouchEvent(e);
        final float k = dst.width() > 0 && bitmap != null ? bitmap.getWidth() / (float) dst.width() : 1f;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX = lastX = e.getX();
                downY = lastY = e.getY();
                downTime = e.getEventTime();
                moved = dragging = false;
                maxPointers = 1;
                fracX = fracY = 0;
                removeCallbacks(release);
                postDelayed(holdLeft, 450);
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                maxPointers = Math.max(maxPointers, e.getPointerCount());
                removeCallbacks(holdLeft);
                return true;
            case MotionEvent.ACTION_MOVE: {
                if (e.getPointerCount() > 1) return true;
                float x = e.getX(), y = e.getY();
                if (Math.abs(x - downX) + Math.abs(y - downY) > 10 * density) {
                    if (!moved) removeCallbacks(holdLeft);
                    moved = true;
                }
                fracX += (x - lastX) * k;
                fracY += (y - lastY) * k;
                lastX = x;
                lastY = y;
                int dx = (int) fracX, dy = (int) fracY;
                fracX -= dx;
                fracY -= dy;
                if (moved && (dx != 0 || dy != 0)) Core.mouse(dx, dy, buttons);
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                removeCallbacks(holdLeft);
                if (dragging) {
                    dragging = false;
                    buttons = 0;
                    Core.mouse(0, 0, 0);
                } else if (!moved && e.getActionMasked() == MotionEvent.ACTION_UP
                        && e.getEventTime() - downTime < 350) {
                    buttons = maxPointers > 1 ? 2 : 1;
                    Core.mouse(0, 0, buttons);
                    postDelayed(release, 90);
                }
                return true;
            default:
                return true;
        }
    }

    void setSmooth(boolean smooth) {
        paint.setFilterBitmap(smooth);
        invalidate();
    }

    void setOverlay(boolean fps, boolean leds) {
        showFps = fps;
        showLeds = leds;
    }

    /** Pixels at the bottom covered by the on-screen keyboard (the picture goes above). */
    void setBottomInset(int px) {
        bottomInset = px;
        invalidate();
    }

    void redraw() { postInvalidate(); }

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

    /** A frame's picture, on the emulation thread: copied, drawn later. */
    @Override
    public void showFrame(int[] pixels, int w, int h, float fps, int status) {
        int n = w * h;
        synchronized (lock) {
            if (back.length != n) back = new int[n];
            System.arraycopy(pixels, 0, back, 0, n);
            backW = w;
            backH = h;
            backStatus = status;
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
                status = backStatus;
                fps = backFps;
                fresh = false;
            } else if (bitmap == null) {
                return;
            }
        }
        if (front.length == bitmap.getWidth() * bitmap.getHeight()) {
            bitmap.setPixels(front, 0, bitmap.getWidth(), 0, 0, bitmap.getWidth(), bitmap.getHeight());
        }
        src.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
        place(getWidth(), getHeight(), bitmap.getWidth(), bitmap.getHeight());
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
}
