package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Atari 1040ST (PAL, colour monitor), with TOS 1.04 (or 1.02, 1.00,
 *  EmuTOS: the "tos" setting), two drives (A, B: slots 0 and 1) and a mouse
 *  (the picture is its trackpad). Key codes: the IKBD's scan codes
 *  (st_core.h). */
final class AtariSt extends Machine {
    static final int ESC = 0x01, N1 = 0x02, N0 = 0x0b, MINUS = 0x0c, EQUALS = 0x0d, BACKSPACE = 0x0e, TAB = 0x0f;
    static final int Q = 0x10, LBRACKET = 0x1a, RBRACKET = 0x1b, RETURN = 0x1c, CTRL = 0x1d, A = 0x1e;
    static final int SEMICOLON = 0x27, QUOTE = 0x28, GRAVE = 0x29, LSHIFT = 0x2a, BACKSLASH = 0x2b, Z = 0x2c;
    static final int COMMA = 0x33, PERIOD = 0x34, SLASH = 0x35, RSHIFT = 0x36, ALT = 0x38, SPACE = 0x39, CAPS = 0x3a;
    static final int F1 = 0x3b, CLRHOME = 0x47, UP = 0x48, KPMINUS = 0x4a, LEFT = 0x4b, RIGHT = 0x4d, KPPLUS = 0x4e;
    static final int DOWN = 0x50, INSERT = 0x52, DELETE = 0x53, ISO = 0x60, UNDO = 0x61, HELP = 0x62;
    static final int KPLPAREN = 0x63, KPRPAREN = 0x64, KPSLASH = 0x65, KPSTAR = 0x66, KP7 = 0x67, KP8 = 0x68, KP9 = 0x69;
    static final int KP4 = 0x6a, KP5 = 0x6b, KP6 = 0x6c, KP1 = 0x6d, KP2 = 0x6e, KP3 = 0x6f, KP0 = 0x70, KPDOT = 0x71;
    static final int KPENTER = 0x72;

    private static final String ROW1 = "QWERTYUIOP";
    private static final String ROW2 = "ASDFGHJKL";
    private static final String ROW3 = "ZXCVBNM";

    static int letter(char c) {
        int i = ROW1.indexOf(c);
        if (i >= 0) return Q + i;
        i = ROW2.indexOf(c);
        if (i >= 0) return A + i;
        return Z + ROW3.indexOf(c);
    }

    AtariSt() {
        int[] az = new int[26];
        for (int i = 0; i < 26; i++) az[i] = letter((char) ('A' + i));
        int[] digits = new int[10];
        digits[0] = N0;
        for (int i = 1; i < 10; i++) digits[i] = N1 + i - 1;
        mapLetters(az, digits);
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, KPENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_DEL, BACKSPACE);
        map(KeyEvent.KEYCODE_FORWARD_DEL, DELETE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, LSHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, RSHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, ALT);
        map(KeyEvent.KEYCODE_ALT_RIGHT, ALT);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_MOVE_HOME, CLRHOME);
        map(KeyEvent.KEYCODE_INSERT, INSERT);
        map(KeyEvent.KEYCODE_PAGE_UP, HELP);
        map(KeyEvent.KEYCODE_PAGE_DOWN, UNDO);
        map(KeyEvent.KEYCODE_MOVE_END, UNDO);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        for (int i = 0; i < 10; i++) map(KeyEvent.KEYCODE_F1 + i, F1 + i);
        map(KeyEvent.KEYCODE_GRAVE, GRAVE);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, LBRACKET);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, RBRACKET);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_BACKSLASH, BACKSLASH);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        int[] kp = {KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9};
        for (int i = 0; i < 10; i++) map(KeyEvent.KEYCODE_NUMPAD_0 + i, kp[i]);
        map(KeyEvent.KEYCODE_NUMPAD_DOT, KPDOT);
        map(KeyEvent.KEYCODE_NUMPAD_ADD, KPPLUS);
        map(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, KPMINUS);
        map(KeyEvent.KEYCODE_NUMPAD_MULTIPLY, KPSTAR);
        map(KeyEvent.KEYCODE_NUMPAD_DIVIDE, KPSLASH);
        map(KeyEvent.KEYCODE_NUM_LOCK, KPLPAREN);
        map(KeyEvent.KEYCODE_SCROLL_LOCK, KPRPAREN);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('.', PERIOD);
        chars(',', COMMA);
    }

    @Override String title() { return "Atari 1040ST"; }

    @Override String restartKey() { return "tos"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String want = p.getString("tos", "tos104");
        byte[] tos = asset(a, "roms/" + want + ".rom");
        for (String other : new String[] {"tos104", "tos102", "tos100", "etos"}) {
            if (tos == null) tos = asset(a, "roms/" + other + ".rom");
        }
        return new byte[][] {tos};
    }

    @Override boolean usesMouse() { return true; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".st .msa .stx"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String d0 = a.slotName(0), d1 = a.slotName(1);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (d0 != null ? "Cambiar el disco de A… (" + d0 + ")" : "Meter un disco en A… (.st .msa .stx)"), a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (d1 != null ? "Cambiar el disco de B… (" + d1 + ")" : "Meter un disco en B…"), a.pickAction(1)));
        if (d0 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar A", a.ejectAction(0, "A vacía")));
        if (d1 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar B", a.ejectAction(1, "B vacía")));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reset", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas ST"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"HELP", new int[] {HELP}});
        s.add(new Object[] {"UNDO", new int[] {UNDO}});
        s.add(new Object[] {"ESPACIO", new int[] {SPACE}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"F1", new int[] {F1}});
        s.add(new Object[] {"F10", new int[] {F1 + 9}});
        s.add(new Object[] {"Alternate + Clr Home (ratón con el teclado)", new int[] {ALT, CLRHOME}});
        return s;
    }

    @Override String defaultPad() { return "joy"; }

    @Override
    int[] padActions(String mode) {
        // the joystick (port 1), fire on A and X; B space, Y return, SELECT ESC,
        // L1 F1, R1 HELP; or the cursor keys
        int port = 1;
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, RETURN, ALT, CTRL, ESC, F1, HELP};
        }
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT),
            joy(port, Core.JOY_RIGHT), joy(port, Core.JOY_FIRE), SPACE, joy(port, Core.JOY_FIRE), RETURN, ESC,
            F1, HELP};
    }
}
