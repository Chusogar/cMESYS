package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Atari ST. Keys are the IKBD's scan codes (atarist_core.h); the screen
 *  is a touchpad for the mouse; a pad is the joystick (jni/m_atarist.c). */
final class AtariST extends Machine {
    static final int ESC = 0x01, RETURN = 0x1c, SPACE = 0x39, LSHIFT = 0x2a, CONTROL = 0x1d, ALT = 0x38,
            UNDO = 0x61, HELP = 0x62, UP = 0x48, DOWN = 0x50, LEFT = 0x4b, RIGHT = 0x4d;

    AtariST() {
        int[] az = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                    0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
        int[] digits = {0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a};
        mapLetters(az, digits);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_TAB, 0x0f);
        map(KeyEvent.KEYCODE_DEL, 0x0e);
        map(KeyEvent.KEYCODE_FORWARD_DEL, 0x53);
        map(KeyEvent.KEYCODE_INSERT, 0x52);
        map(KeyEvent.KEYCODE_MOVE_HOME, 0x47);
        map(KeyEvent.KEYCODE_PAGE_UP, UNDO);
        map(KeyEvent.KEYCODE_PAGE_DOWN, HELP);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, LSHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, 0x36);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CONTROL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CONTROL);
        map(KeyEvent.KEYCODE_ALT_LEFT, ALT);
        map(KeyEvent.KEYCODE_ALT_RIGHT, ALT);
        map(KeyEvent.KEYCODE_CAPS_LOCK, 0x3a);
        map(KeyEvent.KEYCODE_MINUS, 0x0c);
        map(KeyEvent.KEYCODE_EQUALS, 0x0d);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, 0x1a);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, 0x1b);
        map(KeyEvent.KEYCODE_SEMICOLON, 0x27);
        map(KeyEvent.KEYCODE_APOSTROPHE, 0x28);
        map(KeyEvent.KEYCODE_GRAVE, 0x29);
        map(KeyEvent.KEYCODE_BACKSLASH, 0x2b);
        map(KeyEvent.KEYCODE_COMMA, 0x33);
        map(KeyEvent.KEYCODE_PERIOD, 0x34);
        map(KeyEvent.KEYCODE_SLASH, 0x35);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        for (int i = 0; i < 10; i++) map(KeyEvent.KEYCODE_F1 + i, 0x3b + i);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('.', 0x34);
        chars(',', 0x33);
        chars(':', LSHIFT, 0x27);
        chars('\\', 0x2b);
        chars('-', 0x0c);
        chars('*', LSHIFT, 0x09);
    }

    @Override String title() { return "Atari ST"; }

    @Override boolean usesMouse() { return true; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        byte[] tos = asset(a, "roms/tos.img");
        return tos == null ? null : new byte[][] {tos};
    }

    @Override float displayAspect(int w, int h) { return 4f / 3f; }

    @Override int lineScale() { return 1; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".st .msa .stx"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        for (int d = 0; d < 2; d++) {
            String drive = d == 0 ? "A:" : "B:";
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Disco en " + drive + "… (.st .msa .stx)", a.pickAction(d)));
            String n = a.slotName(d);
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco de " + drive + (n != null ? "  (" + n + ")" : ""),
                    n == null ? null : a.ejectAction(d, "Disco fuera de " + drive)));
        }
    }

    @Override String keysLabel() { return "Teclas ST"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"UNDO", new int[] {UNDO}});
        s.add(new Object[] {"HELP", new int[] {HELP}});
        s.add(new Object[] {"CONTROL+ALTERNATE+DELETE (reset)", new int[] {CONTROL, ALT, 0x53}});
        return s;
    }

    @Override String defaultPad() { return "joy1"; }

    @Override
    int[] padActions(String mode) {
        int port = "joy0".equals(mode) ? 2 : 1;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT), joy(port, Core.JOY_RIGHT),
            joy(port, Core.JOY_FIRE), joy(port, Core.JOY_FIRE), SPACE, RETURN, ESC, UNDO, HELP};
    }
}
