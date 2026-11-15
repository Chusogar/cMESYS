package com.chusogar.cmesys.app;

import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/** The picture as a trackpad for the machine's mouse (the Amiga's), as on a
 *  laptop: a finger moving moves the pointer (as far on the machine's
 *  screen as the finger goes on the picture, a little faster); a tap is a
 *  left click; tap and then touch again and move: the left button held
 *  (dragging); a second finger down is the right button, held while it
 *  stays (the Amiga's menus: hold it and move the first finger). A real
 *  mouse (USB / Bluetooth) works too: its motion and its buttons. */
final class Trackpad implements View.OnTouchListener, View.OnGenericMotionListener {
    private static final float SPEED = 1.5f;      // machine pixels per picture pixel
    private static final int TAP_MS = 220, DOUBLE_MS = 280;

    private final ScreenView view;
    private final int machineWidth;                // the machine's picture, pixels
    private Emulator emu;
    private final int slop;
    private int pointerId = -1;
    private float lastX, lastY, fracX, fracY, moved;
    private long downAt, lastTapAt;
    private int maxPointers;
    private boolean dragging, rightHeld;
    private float mouseX = Float.NaN, mouseY;
    private int mouseButtons;

    Trackpad(ScreenView view, int machineWidth) {
        this.view = view;
        this.machineWidth = machineWidth;
        slop = ViewConfiguration.get(view.getContext()).getScaledTouchSlop();
        view.setOnTouchListener(this);
        view.setOnGenericMotionListener(this);
    }

    void setEmulator(Emulator e) { emu = e; }

    private float scale() {
        int w = view.pictureWidth();
        return w > 0 ? SPEED * machineWidth / (float) w : SPEED;
    }

    private void move(float dx, float dy) {
        float s = scale();
        fracX += dx * s;
        fracY += dy * s;
        int ix = (int) fracX, iy = (int) fracY;
        fracX -= ix;
        fracY -= iy;
        if ((ix != 0 || iy != 0) && emu != null) emu.mouseMove(ix, iy);
    }

    private void buttons() {
        if (emu != null) emu.mouseButtons((dragging ? 1 : 0) | (rightHeld ? 2 : 0) | mouseButtons);
    }

    @Override
    public boolean onTouch(View v, MotionEvent e) {
        if (emu == null) return false;
        if (e.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE) return realMouse(e);
        long now = SystemClock.uptimeMillis();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointerId = e.getPointerId(0);
                lastX = e.getX();
                lastY = e.getY();
                moved = 0;
                downAt = now;
                maxPointers = 1;
                if (now - lastTapAt < DOUBLE_MS) {          // tap, then touch again: drag with the button held
                    dragging = true;
                    buttons();
                }
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                maxPointers = Math.max(maxPointers, e.getPointerCount());
                if (!rightHeld) {
                    rightHeld = true;
                    buttons();
                }
                return true;
            case MotionEvent.ACTION_MOVE: {
                int i = e.findPointerIndex(pointerId);
                if (i < 0) return true;
                float dx = e.getX(i) - lastX, dy = e.getY(i) - lastY;
                lastX = e.getX(i);
                lastY = e.getY(i);
                moved += Math.abs(dx) + Math.abs(dy);
                if (moved > slop || dragging || rightHeld) move(dx, dy);
                return true;
            }
            case MotionEvent.ACTION_POINTER_UP: {
                int up = e.getPointerId(e.getActionIndex());
                if (up == pointerId) {                      // the moving finger left: follow another
                    int other = e.getActionIndex() == 0 ? 1 : 0;
                    pointerId = e.getPointerId(other);
                    lastX = e.getX(other);
                    lastY = e.getY(other);
                }
                if (e.getPointerCount() <= 2 && rightHeld) {
                    rightHeld = false;
                    buttons();
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                boolean tap = e.getActionMasked() == MotionEvent.ACTION_UP && !dragging && maxPointers == 1
                        && moved <= slop && now - downAt < TAP_MS;
                if (dragging || rightHeld) {
                    dragging = rightHeld = false;
                    buttons();
                }
                if (tap) {
                    emu.mouseClick();
                    lastTapAt = now;
                } else {
                    lastTapAt = 0;
                }
                pointerId = -1;
                return true;
            default:
                return true;
        }
    }

    /** A real mouse over the picture: its motion (from where it was) and buttons. */
    private boolean realMouse(MotionEvent e) {
        int b = e.getButtonState();
        mouseButtons = ((b & MotionEvent.BUTTON_PRIMARY) != 0 ? 1 : 0) | ((b & MotionEvent.BUTTON_SECONDARY) != 0 ? 2 : 0);
        if (e.getActionMasked() == MotionEvent.ACTION_UP) mouseButtons = 0;
        follow(e.getX(), e.getY());
        buttons();
        return true;
    }

    private void follow(float x, float y) {
        if (!Float.isNaN(mouseX)) move((x - mouseX) / SPEED, (y - mouseY) / SPEED);
        mouseX = x;
        mouseY = y;
    }

    @Override
    public boolean onGenericMotion(View v, MotionEvent e) {
        if (emu == null || e.getToolType(0) != MotionEvent.TOOL_TYPE_MOUSE) return false;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_HOVER_MOVE:
            case MotionEvent.ACTION_HOVER_ENTER:
                follow(e.getX(), e.getY());
                return true;
            case MotionEvent.ACTION_HOVER_EXIT:
                mouseX = Float.NaN;
                return true;
            default:
                return false;
        }
    }
}
