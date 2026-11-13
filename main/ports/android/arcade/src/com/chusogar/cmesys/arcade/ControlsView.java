package com.chusogar.cmesys.arcade;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/** The arcade controls on the screen, in the manner of MAME for Android: an
 *  arcade joystick (a metal base with a red ball top on its shaft that
 *  follows the finger: 8 ways, or 2 for the games that only go left and
 *  right), the game's round buttons with their names, START, and a coin
 *  door whose slot takes a coin with an animation each time it is pressed.
 *
 *  Portrait: a control panel below the picture. Landscape: over the
 *  picture's sides (translucent). Several fingers at once; a finger can
 *  slide from button to button. */
final class ControlsView extends View {
    interface Listener {
        void onControls(int bits);       // Native.UP.. / B1.. / START1 held
        void onCoin();                   // a coin into the slot
        void onMenu();
    }

    private static final int[] BUTTON_COLORS = {0xffe8402e, 0xff2e7de8, 0xfff2c230, 0xff3cb85a};
    private static final long COIN_MS = 750;

    private Game game;
    private Listener listener;
    private final float dp;
    private boolean haptic = true;
    private float size = 1f;
    private int panelTop = -1;           // portrait: the panel's top; -1: over the picture
    private int panelColor = 0xff8a6a2a;

    // layout
    private float jx, jy, jr;            // joystick centre, base radius
    private final List<RectF> buttonsAt = new ArrayList<>();
    private float br;                    // button radius
    private final RectF startRect = new RectF(), coinRect = new RectF(), menuRect = new RectF();
    private final RectF slot = new RectF();

    // state
    private int joyPointer = -1;
    private float knobX, knobY;          // offset of the knob from the centre
    private int joyBits;
    private final int[] buttonPointer = new int[16];   // button index under each pointer, -1 none
    private int startPointer = -1, coinPointer = -1, menuPointer = -1;
    private int sent = -1;
    private final List<Long> coins = new ArrayList<>(); // animation start times

    // paints
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    ControlsView(Context c) {
        super(c);
        dp = c.getResources().getDisplayMetrics().density;
        stroke.setStyle(Paint.Style.STROKE);
        label.setTextAlign(Paint.Align.CENTER);
        label.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        java.util.Arrays.fill(buttonPointer, -1);
    }

    void setGame(Game g) {
        game = g;
        panelColor = g.color;
        requestLayout();
        invalidate();
    }

    void setListener(Listener l) { listener = l; }

    void setHaptic(boolean h) { haptic = h; }

    void setControlSize(float s) {
        size = s;
        layoutControls();
        invalidate();
    }

    /** Portrait: the panel starts at this y (below the picture); -1: over the picture. */
    void setPanelTop(int y) {
        if (y != panelTop) {
            panelTop = y;
            layoutControls();
            invalidate();
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        layoutControls();
    }

    // --- layout ----------------------------------------------------------------------
    private void layoutControls() {
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0 || game == null) return;
        int nb = game.buttons.length;
        buttonsAt.clear();
        float small = 34 * dp * size;           // START / coin height
        if (panelTop >= 0) {
            // Portrait panel: coin door and START on top, joystick left, buttons right
            float top = panelTop, ph = h - top;
            float rowY = top + Math.min(ph * 0.16f, 46 * dp);
            float lower = rowY + small * 0.9f;
            float cy = lower + (h - lower) * 0.5f;
            jr = Math.min(Math.min(w * 0.19f, (h - lower) * 0.42f), 92 * dp * size);
            jx = w * 0.25f;
            jy = cy;
            br = Math.min(Math.min(w * 0.085f, (h - lower) * 0.19f), 40 * dp * size);
            placeButtons(nb, w * 0.73f, cy, Math.min(w * 0.24f, br * 3.1f));
            coinRect.set(w * 0.5f - small * 2.6f, rowY - small * 0.62f, w * 0.5f - small * 0.35f, rowY + small * 0.62f);
            startRect.set(w * 0.5f + small * 0.35f, rowY - small * 0.5f, w * 0.5f + small * 2.6f, rowY + small * 0.5f);
            menuRect.set(w - 44 * dp, top + 6 * dp, w - 8 * dp, top + 42 * dp);
        } else {
            // Landscape: over the picture's sides
            jr = Math.min(h * 0.17f, 80 * dp) * size;
            jx = Math.max(jr * 1.35f, w * 0.12f);
            jy = h - jr * 1.45f;
            br = Math.min(h * 0.075f, 36 * dp) * size;
            float bx = w - Math.max(br * 3.6f, w * 0.12f);
            placeButtons(nb, bx, h - br * 3.2f, br * 3.1f);
            // the coin door low enough for the coin to be seen coming down into it
            float cTop = Math.max(small * 2.1f, h * 0.16f);
            coinRect.set(jx - small * 1.15f, cTop, jx + small * 1.15f, cTop + small * 1.4f);
            startRect.set(bx - small * 1.15f, cTop + small * 0.15f, bx + small * 1.15f, cTop + small * 1.25f);
            menuRect.set(w - 44 * dp, h * 0.5f - 18 * dp, w - 8 * dp, h * 0.5f + 18 * dp);
            if (startRect.right > menuRect.left) startRect.offset(menuRect.left - startRect.right - 6 * dp, 0);
        }
        float sw = Math.max(5 * dp, coinRect.width() * 0.09f);
        float scx = coinRect.centerX(), sh = coinRect.height() * 0.42f;
        slot.set(scx - sw / 2, coinRect.top + coinRect.height() * 0.16f, scx + sw / 2,
                coinRect.top + coinRect.height() * 0.16f + sh);
    }

    /** n buttons around (cx, cy): one, a pair on a slant, or three in an arc (as a cabinet's). */
    private void placeButtons(int n, float cx, float cy, float spread) {
        float[][] at;
        switch (n) {
            case 1: at = new float[][] {{0, 0}}; break;
            case 2: at = new float[][] {{-0.42f, 0.22f}, {0.42f, -0.22f}}; break;
            default: at = new float[][] {{-0.62f, 0.26f}, {0, -0.12f}, {0.62f, 0.26f}}; break;
        }
        for (int i = 0; i < n && i < at.length; i++) {
            float x = cx + at[i][0] * spread, y = cy + at[i][1] * spread;
            buttonsAt.add(new RectF(x - br, y - br, x + br, y + br));
        }
    }

    // --- drawing -------------------------------------------------------------------------
    @Override
    protected void onDraw(Canvas c) {
        if (game == null) return;
        int w = getWidth(), h = getHeight();
        if (panelTop >= 0) drawPanel(c, w, h);
        if (game.joystick != Game.JOY_NONE) drawJoystick(c);
        for (int i = 0; i < buttonsAt.size(); i++) drawButton(c, i);
        drawStart(c);
        drawCoinDoor(c);
        drawMenu(c);
        if (!coins.isEmpty()) postInvalidateOnAnimation();
    }

    private void drawPanel(Canvas c, int w, int h) {
        float top = panelTop;
        fill.setShader(new LinearGradient(0, top, 0, h, 0xff23252d, 0xff0f1014, Shader.TileMode.CLAMP));
        c.drawRect(0, top, w, h, fill);
        fill.setShader(null);
        // the game's colour as a stripe along the panel's edge, then a bevel
        fill.setColor(panelColor);
        c.drawRect(0, top, w, top + 4 * dp, fill);
        fill.setColor(0x40ffffff);
        c.drawRect(0, top + 4 * dp, w, top + 5 * dp, fill);
    }

    private void drawJoystick(Canvas c) {
        float r = jr;
        // base: a dark metal plate with a ring and the dust washer
        fill.setColor(0x60000000);
        c.drawCircle(jx, jy + 3 * dp, r * 1.02f, fill);
        fill.setShader(new RadialGradient(jx - r * 0.25f, jy - r * 0.3f, r * 1.3f,
                new int[] {0xff4a4d56, 0xff2a2c33, 0xff15161a}, new float[] {0f, 0.55f, 1f}, Shader.TileMode.CLAMP));
        c.drawCircle(jx, jy, r, fill);
        fill.setShader(null);
        stroke.setColor(0xff5c606a);
        stroke.setStrokeWidth(2 * dp);
        c.drawCircle(jx, jy, r - dp, stroke);
        stroke.setColor(0x50000000);
        stroke.setStrokeWidth(3 * dp);
        c.drawCircle(jx, jy, r * 0.82f, stroke);
        // direction marks
        fill.setColor(0xffb8bcc6);
        boolean two = game.joystick == Game.JOY_2WAY;
        for (int d = 0; d < 4; d++) {
            if (two && (d == 0 || d == 2)) continue;
            double a = Math.PI / 2 * d - Math.PI / 2;
            float mx = jx + (float) Math.cos(a) * r * 0.9f, my = jy + (float) Math.sin(a) * r * 0.9f;
            boolean lit = (d == 0 && (joyBits & Native.UP) != 0) || (d == 1 && (joyBits & Native.RIGHT) != 0)
                    || (d == 2 && (joyBits & Native.DOWN) != 0) || (d == 3 && (joyBits & Native.LEFT) != 0);
            fill.setColor(lit ? 0xffff6a50 : 0xffb8bcc6);
            arrow(c, mx, my, a, r * 0.075f);
        }
        if (two) {
            // the slot it moves in
            fill.setColor(0xff0b0c0f);
            RectF g = new RectF(jx - r * 0.62f, jy - r * 0.07f, jx + r * 0.62f, jy + r * 0.07f);
            c.drawRoundRect(g, r * 0.07f, r * 0.07f, fill);
        } else {
            fill.setColor(0xff0b0c0f);
            c.drawCircle(jx, jy, r * 0.3f, fill);
        }
        // shaft and ball top, leaning to the knob's offset
        float kx = jx + knobX, ky = jy + knobY;
        stroke.setColor(0xff8c919b);
        stroke.setStrokeWidth(r * 0.16f);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        c.drawLine(jx, jy, kx, ky, stroke);
        stroke.setStrokeCap(Paint.Cap.BUTT);
        float kr = r * 0.42f;
        fill.setColor(0x70000000);
        c.drawCircle(kx + kr * 0.12f, ky + kr * 0.22f, kr, fill);
        fill.setShader(new RadialGradient(kx - kr * 0.35f, ky - kr * 0.4f, kr * 1.5f,
                new int[] {0xffff8a7a, 0xffe02a22, 0xff7a0a08}, new float[] {0f, 0.45f, 1f}, Shader.TileMode.CLAMP));
        c.drawCircle(kx, ky, kr, fill);
        fill.setShader(null);
        fill.setColor(0xb0ffffff);
        c.drawOval(new RectF(kx - kr * 0.55f, ky - kr * 0.72f, kx - kr * 0.05f, ky - kr * 0.38f), fill);
    }

    private void arrow(Canvas c, float x, float y, double a, float s) {
        path.reset();
        path.moveTo(x + (float) Math.cos(a) * s * 1.6f, y + (float) Math.sin(a) * s * 1.6f);
        path.lineTo(x + (float) Math.cos(a + 2.4) * s, y + (float) Math.sin(a + 2.4) * s);
        path.lineTo(x + (float) Math.cos(a - 2.4) * s, y + (float) Math.sin(a - 2.4) * s);
        path.close();
        c.drawPath(path, fill);
    }

    private boolean buttonDown(int i) {
        for (int p : buttonPointer) if (p == i) return true;
        return false;
    }

    private static int darker(int c, float f) {
        return Color.argb(Color.alpha(c), (int) (Color.red(c) * f), (int) (Color.green(c) * f), (int) (Color.blue(c) * f));
    }

    private static int lighter(int c, float f) {
        return Color.argb(Color.alpha(c), Color.red(c) + (int) ((255 - Color.red(c)) * f),
                Color.green(c) + (int) ((255 - Color.green(c)) * f), Color.blue(c) + (int) ((255 - Color.blue(c)) * f));
    }

    private void drawButton(Canvas c, int i) {
        RectF b = buttonsAt.get(i);
        float x = b.centerX(), y = b.centerY(), r = br;
        boolean down = buttonDown(i);
        int col = BUTTON_COLORS[i % BUTTON_COLORS.length];
        // bezel
        fill.setColor(0x60000000);
        c.drawCircle(x, y + 3 * dp, r * 1.18f, fill);
        fill.setShader(new LinearGradient(x, y - r * 1.15f, x, y + r * 1.15f, 0xff3c3f47, 0xff141519, Shader.TileMode.CLAMP));
        c.drawCircle(x, y, r * 1.15f, fill);
        fill.setShader(null);
        // the cap: concave, lower and darker when pressed
        float cr = down ? r * 0.93f : r;
        float cy = down ? y + 2 * dp : y - dp;
        if (!down) {
            fill.setColor(darker(col, 0.45f));
            c.drawCircle(x, y + 2.5f * dp, cr, fill);
        }
        int top = down ? darker(col, 0.7f) : col;
        fill.setShader(new RadialGradient(x, cy + cr * 0.35f, cr * 1.25f,
                new int[] {lighter(top, 0.25f), top, darker(top, 0.6f)}, new float[] {0f, 0.6f, 1f}, Shader.TileMode.CLAMP));
        c.drawCircle(x, cy, cr, fill);
        fill.setShader(null);
        fill.setColor(down ? 0x30ffffff : 0x70ffffff);
        c.drawOval(new RectF(x - cr * 0.55f, cy - cr * 0.8f, x + cr * 0.35f, cy - cr * 0.42f), fill);
        // the name, under it
        label.setColor(0xffe8e8ec);
        label.setTextSize(Math.max(10 * dp, r * 0.34f));
        c.drawText(game.buttons[i], x, y + r * 1.15f + label.getTextSize() * 1.15f, label);
    }

    private void drawStart(Canvas c) {
        RectF r = startRect;
        boolean down = startPointer >= 0;
        float rr = r.height() * 0.22f;
        fill.setColor(0x60000000);
        c.drawRoundRect(new RectF(r.left, r.top + 3 * dp, r.right, r.bottom + 3 * dp), rr, rr, fill);
        fill.setColor(0xff17181c);
        c.drawRoundRect(new RectF(r.left - 3 * dp, r.top - 3 * dp, r.right + 3 * dp, r.bottom + 3 * dp), rr, rr, fill);
        int base = down ? 0xffffe680 : 0xfff2c230;
        fill.setShader(new LinearGradient(0, r.top, 0, r.bottom, lighter(base, 0.3f), darker(base, 0.75f), Shader.TileMode.CLAMP));
        RectF cap = new RectF(r);
        if (down) cap.offset(0, 1.5f * dp);
        c.drawRoundRect(cap, rr, rr, fill);
        fill.setShader(null);
        if (down) {   // lit
            fill.setColor(0x50fff0a0);
            c.drawRoundRect(new RectF(r.left - 6 * dp, r.top - 6 * dp, r.right + 6 * dp, r.bottom + 6 * dp), rr * 2, rr * 2, fill);
        }
        label.setColor(0xff2a1e00);
        label.setTextSize(r.height() * 0.36f);
        c.drawText("1P START", cap.centerX(), cap.centerY() + label.getTextSize() * 0.36f, label);
    }

    private void drawCoinDoor(Canvas c) {
        RectF r = coinRect;
        float rr = 6 * dp;
        // the plate, with its screws
        fill.setColor(0x60000000);
        c.drawRoundRect(new RectF(r.left, r.top + 3 * dp, r.right, r.bottom + 3 * dp), rr, rr, fill);
        fill.setShader(new LinearGradient(0, r.top, 0, r.bottom, 0xff3a3d46, 0xff17181c, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, rr, rr, fill);
        fill.setShader(null);
        stroke.setColor(coinPointer >= 0 ? 0xffffb050 : 0xff5a5e68);
        stroke.setStrokeWidth(1.5f * dp);
        c.drawRoundRect(r, rr, rr, stroke);
        fill.setColor(0xff7a7e88);
        float sr = 1.8f * dp, si = 5 * dp;
        c.drawCircle(r.left + si, r.top + si, sr, fill);
        c.drawCircle(r.right - si, r.top + si, sr, fill);
        c.drawCircle(r.left + si, r.bottom - si, sr, fill);
        c.drawCircle(r.right - si, r.bottom - si, sr, fill);

        // the lit slot (brighter for a moment after each coin)
        long now = SystemClock.uptimeMillis();
        float flash = 0;
        for (long t0 : coins) {
            float t = (now - t0) / (float) COIN_MS;
            if (t > 0.45f && t < 1f) flash = Math.max(flash, 1f - (t - 0.45f) / 0.55f);
        }
        fill.setColor(Color.argb((int) (70 + 140 * flash), 255, 150, 40));
        c.drawRoundRect(new RectF(slot.left - 4 * dp, slot.top - 4 * dp, slot.right + 4 * dp, slot.bottom + 4 * dp),
                6 * dp, 6 * dp, fill);
        fill.setShader(new LinearGradient(0, slot.top, 0, slot.bottom, 0xffffd27a, 0xffff8a1a, Shader.TileMode.CLAMP));
        c.drawRoundRect(slot, slot.width() / 2, slot.width() / 2, fill);
        fill.setShader(null);
        fill.setColor(0xff0c0c0e);
        c.drawRoundRect(new RectF(slot.centerX() - slot.width() * 0.16f, slot.top + 2 * dp,
                slot.centerX() + slot.width() * 0.16f, slot.bottom - 2 * dp), dp, dp, fill);

        label.setColor(0xffffc070);
        label.setTextSize(Math.max(9 * dp, r.height() * 0.17f));
        c.drawText("MONEDA", r.centerX(), r.bottom - r.height() * 0.1f, label);

        // the coins going in
        for (int i = coins.size() - 1; i >= 0; i--) {
            float t = (now - coins.get(i)) / (float) COIN_MS;
            if (t >= 1f) {
                coins.remove(i);
                continue;
            }
            drawCoin(c, t);
        }
    }

    /** A coin at time t (0..1) of its way in: it comes down over the slot
     *  turning edge on, slides into it (hidden past the slot's top) and a
     *  "+1" rises. */
    private void drawCoin(Canvas c, float t) {
        float cr = Math.max(slot.width() * 1.9f, coinRect.width() * 0.17f);
        float x = slot.centerX();
        float startY = coinRect.top - cr * 1.6f, overY = slot.top - cr * 0.15f;
        float y, sx;
        if (t < 0.4f) {                                   // falls and turns edge on
            float u = t / 0.4f;
            y = startY + (overY - startY) * (u * u);
            sx = (float) Math.max(0.12, Math.cos(u * Math.PI / 2));
        } else {                                          // slides into the slot
            float u = Math.min(1f, (t - 0.4f) / 0.35f);
            y = overY + (slot.bottom - overY + cr) * u;
            sx = 0.12f;
        }
        if (t < 0.75f) {
            c.save();
            if (t >= 0.4f) c.clipRect(coinRect.left - cr * 2, startY - cr * 2, coinRect.right + cr * 2, slot.top + dp);
            c.translate(x, y);
            c.scale(sx, 1f);
            fill.setShader(new RadialGradient(-cr * 0.3f, -cr * 0.35f, cr * 1.5f,
                    new int[] {0xfffff2b0, 0xffe6b432, 0xff8a6410}, new float[] {0f, 0.5f, 1f}, Shader.TileMode.CLAMP));
            c.drawCircle(0, 0, cr, fill);
            fill.setShader(null);
            stroke.setColor(0xffb08420);
            stroke.setStrokeWidth(cr * 0.14f);
            c.drawCircle(0, 0, cr * 0.78f, stroke);
            label.setColor(0xff7a5608);
            label.setTextSize(cr * 1.0f);
            c.drawText("1", 0, cr * 0.36f, label);
            c.restore();
        }
        if (t > 0.5f) {                                   // +1
            float u = (t - 0.5f) / 0.5f;
            label.setColor(Color.argb((int) (255 * (1 - u)), 255, 214, 100));
            label.setTextSize(coinRect.height() * 0.3f);
            c.drawText("+1", coinRect.right + label.getTextSize() * 0.7f, coinRect.top + coinRect.height() * 0.4f - u * coinRect.height() * 0.5f, label);
        }
    }

    private void drawMenu(Canvas c) {
        RectF r = menuRect;
        fill.setColor(menuPointer >= 0 ? 0x90ffffff : 0x50000000);
        c.drawRoundRect(r, 8 * dp, 8 * dp, fill);
        fill.setColor(0xd0ffffff);
        float lw = r.width() * 0.5f, x0 = r.centerX() - lw / 2;
        for (int i = -1; i <= 1; i++) {
            float y = r.centerY() + i * r.height() * 0.2f;
            c.drawRoundRect(new RectF(x0, y - 1.5f * dp, x0 + lw, y + 1.5f * dp), dp, dp, fill);
        }
    }

    /** The coin animation without a touch (a coin key of a keyboard or pad). */
    void animateCoin() {
        coins.add(SystemClock.uptimeMillis());
        invalidate();
    }

    // --- touch -------------------------------------------------------------------------------
    private int buttonAt(float x, float y) {
        int best = -1;
        float bestD = Float.MAX_VALUE;
        for (int i = 0; i < buttonsAt.size(); i++) {
            RectF b = buttonsAt.get(i);
            float dx = x - b.centerX(), dy = y - b.centerY();
            float d = dx * dx + dy * dy;
            if (d < br * br * 2.1f && d < bestD) {      // a generous circle around each
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    private boolean inJoystickZone(float x, float y) {
        if (game.joystick == Game.JOY_NONE) return false;
        float dx = x - jx, dy = y - jy;
        if (dx * dx + dy * dy < jr * jr * 2.6f) return true;
        // portrait: the panel's left half; landscape: the left third below the coin door
        if (panelTop >= 0) return y > coinRect.bottom && x < getWidth() * 0.47f;
        return x < getWidth() * 0.3f && y > coinRect.bottom + 10 * dp;
    }

    private void moveKnob(float x, float y) {
        float dx = x - jx, dy = y - jy;
        float d = (float) Math.sqrt(dx * dx + dy * dy);
        float max = jr * 0.58f;
        boolean two = game.joystick == Game.JOY_2WAY;
        if (two) dy = 0;
        float dd = two ? Math.abs(dx) : d;
        float k = dd > max ? max / dd : 1f;
        knobX = dx * k;
        knobY = dy * k;
        int bits = 0;
        if (dd > jr * 0.22f) {
            if (two) {
                bits = dx < 0 ? Native.LEFT : Native.RIGHT;
            } else {
                double a = Math.atan2(dy, dx);              // 8 sectors of 45 degrees
                int s = (int) Math.round(a / (Math.PI / 4)) & 7;
                int[] dirs = {Native.RIGHT, Native.RIGHT | Native.DOWN, Native.DOWN, Native.DOWN | Native.LEFT,
                    Native.LEFT, Native.LEFT | Native.UP, Native.UP, Native.UP | Native.RIGHT};
                bits = dirs[s];
            }
        }
        if (bits != joyBits && haptic && bits != 0) performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK);
        joyBits = bits;
    }

    private void releaseJoystick() {
        joyPointer = -1;
        knobX = knobY = 0;
        joyBits = 0;
    }

    private void down(int id, float x, float y) {
        if (id >= buttonPointer.length) return;
        if (menuRect.contains(x, y)) {
            menuPointer = id;
            return;
        }
        RectF cr = new RectF(coinRect);
        cr.inset(-8 * dp, -8 * dp);
        if (cr.contains(x, y)) {
            coinPointer = id;
            coins.add(SystemClock.uptimeMillis());
            if (haptic) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            if (listener != null) listener.onCoin();
            return;
        }
        RectF sr = new RectF(startRect);
        sr.inset(-8 * dp, -8 * dp);
        if (sr.contains(x, y)) {
            startPointer = id;
            if (haptic) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            return;
        }
        int b = buttonAt(x, y);
        if (b >= 0) {
            buttonPointer[id] = b;
            if (haptic) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            return;
        }
        if (joyPointer < 0 && inJoystickZone(x, y)) {
            joyPointer = id;
            moveKnob(x, y);
        }
    }

    private void up(int id, boolean act) {
        if (id >= buttonPointer.length) return;
        if (id == joyPointer) releaseJoystick();
        if (id == startPointer) startPointer = -1;
        if (id == coinPointer) coinPointer = -1;
        if (id == menuPointer) {
            menuPointer = -1;
            if (act && listener != null) listener.onMenu();
        }
        buttonPointer[id] = -1;
    }

    /** Lets go of everything. */
    void releaseAll() {
        releaseJoystick();
        startPointer = coinPointer = menuPointer = -1;
        java.util.Arrays.fill(buttonPointer, -1);
        send();
        invalidate();
    }

    private void send() {
        int bits = joyBits;
        for (int p : buttonPointer) if (p >= 0 && p < game.buttonBits.length) bits |= game.buttonBits[p];
        if (startPointer >= 0) bits |= Native.START1;
        if (bits != sent) {
            sent = bits;
            if (listener != null) listener.onControls(bits);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (game == null) return false;
        int a = e.getActionMasked(), idx = e.getActionIndex();
        switch (a) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                down(e.getPointerId(idx), e.getX(idx), e.getY(idx));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int p = 0; p < e.getPointerCount(); p++) {
                    int id = e.getPointerId(p);
                    if (id >= buttonPointer.length) continue;
                    float x = e.getX(p), y = e.getY(p);
                    if (id == joyPointer) {
                        moveKnob(x, y);
                    } else if (buttonPointer[id] >= 0) {
                        int b = buttonAt(x, y);                 // sliding to another button
                        if (b >= 0 && b != buttonPointer[id]) {
                            buttonPointer[id] = b;
                            if (haptic) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                        }
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(e.getPointerId(idx), true);
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                return true;
            default:
                break;
        }
        send();
        invalidate();
        // touches outside every control go through (none here: the view is the controls)
        return true;
    }
}
