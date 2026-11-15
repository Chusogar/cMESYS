package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Commodore Amiga 500 (PAL), with Kickstart 1.3 (or 1.2, 2.04, 3.1:
 *  the "kick" setting), two drives (DF0, DF1: slots 0 and 1) and a mouse
 *  (the picture is its trackpad). Key codes: the A500's raw key codes
 *  (amiga_core.h); 0x70 is a physical keyboard's Caps Lock (each press
 *  toggles it; the on-screen one locks). */
final class Amiga extends Machine {
    static final int GRAVE = 0x00, N1 = 0x01, N0 = 0x0A, MINUS = 0x0B, EQUALS = 0x0C, BACKSLASH = 0x0D, KP0 = 0x0F;
    static final int Q = 0x10, LBRACKET = 0x1A, RBRACKET = 0x1B, KP1 = 0x1D, KP2 = 0x1E, KP3 = 0x1F;
    static final int A = 0x20, SEMICOLON = 0x29, QUOTE = 0x2A, HASH = 0x2B, KP4 = 0x2D, KP5 = 0x2E, KP6 = 0x2F;
    static final int LESS = 0x30, Z = 0x31, COMMA = 0x38, PERIOD = 0x39, SLASH = 0x3A, KPDOT = 0x3C, KP7 = 0x3D, KP8 = 0x3E, KP9 = 0x3F;
    static final int SPACE = 0x40, BACKSPACE = 0x41, TAB = 0x42, KPENTER = 0x43, RETURN = 0x44, ESC = 0x45, DEL = 0x46;
    static final int KPMINUS = 0x4A, UP = 0x4C, DOWN = 0x4D, RIGHT = 0x4E, LEFT = 0x4F, F1 = 0x50;
    static final int KPLPAREN = 0x5A, KPRPAREN = 0x5B, KPSLASH = 0x5C, KPSTAR = 0x5D, KPPLUS = 0x5E, HELP = 0x5F;
    static final int LSHIFT = 0x60, RSHIFT = 0x61, CAPS = 0x62, CTRL = 0x63, LALT = 0x64, RALT = 0x65, LAMIGA = 0x66, RAMIGA = 0x67;
    static final int CAPS_PHYSICAL = 0x70;

    // the letters by their raw codes (the A500's rows: QWERTYUIOP, ASDFGHJKL, ZXCVBNM)
    private static final String ROWS = "QWERTYUIOP";
    private static final String ROW2 = "ASDFGHJKL";
    private static final String ROW3 = "ZXCVBNM";

    static int letter(char c) {
        int i = ROWS.indexOf(c);
        if (i >= 0) return Q + i;
        i = ROW2.indexOf(c);
        if (i >= 0) return A + i;
        return Z + ROW3.indexOf(c);
    }

    Amiga() {
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
        map(KeyEvent.KEYCODE_FORWARD_DEL, DEL);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, LSHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, RSHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, LALT);
        map(KeyEvent.KEYCODE_ALT_RIGHT, RALT);
        map(KeyEvent.KEYCODE_META_LEFT, LAMIGA);
        map(KeyEvent.KEYCODE_META_RIGHT, RAMIGA);
        map(KeyEvent.KEYCODE_PAGE_UP, LAMIGA);
        map(KeyEvent.KEYCODE_PAGE_DOWN, RAMIGA);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS_PHYSICAL);
        map(KeyEvent.KEYCODE_MOVE_END, HELP);
        map(KeyEvent.KEYCODE_INSERT, BACKSLASH);
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
        map(KeyEvent.KEYCODE_BACKSLASH, HASH);
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

    @Override String title() { return "Commodore Amiga 500"; }

    @Override String restartKey() { return "kick"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String want = p.getString("kick", "kick13");
        byte[] kick = asset(a, "roms/" + want + ".rom");
        if (kick == null) kick = asset(a, "roms/kick13.rom");
        if (kick == null) return null;
        return new byte[][] {kick};
    }

    @Override boolean usesMouse() { return true; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".adf"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String d0 = a.slotName(0), d1 = a.slotName(1);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (d0 != null ? "Cambiar el disco de DF0… (" + d0 + ")" : "Meter un disco en DF0… (.adf)"), a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (d1 != null ? "Cambiar el disco de DF1… (" + d1 + ")" : "Meter un disco en DF1… (unidad externa)"), a.pickAction(1)));
        if (d0 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar DF0", a.ejectAction(0, "DF0 vacía")));
        if (d1 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar DF1", a.ejectAction(1, "DF1 desconectada")));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reset (Ctrl + Amiga + Amiga)", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas Amiga"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"Ctrl + Amiga + Amiga (reset)", new int[] {CTRL, LAMIGA, RAMIGA}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"HELP", new int[] {HELP}});
        s.add(new Object[] {"ESPACIO", new int[] {SPACE}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"F1", new int[] {F1}});
        s.add(new Object[] {"F10", new int[] {F1 + 9}});
        s.add(new Object[] {"Amiga der. + M (pantalla siguiente)", new int[] {RAMIGA, letter('M')}});
        s.add(new Object[] {"Amiga izq. + N (Workbench delante)", new int[] {LAMIGA, letter('N')}});
        return s;
    }

    @Override String defaultPad() { return "joy"; }

    @Override
    int[] padActions(String mode) {
        // the joystick (port 2), fire on A and X; B space, Y return, SELECT ESC,
        // L1 F1, R1 HELP; or the cursor keys
        int port = 2;
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, RETURN, LALT, RAMIGA, ESC, F1, HELP};
        }
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT),
            joy(port, Core.JOY_RIGHT), joy(port, Core.JOY_FIRE), SPACE, joy(port, Core.JOY_FIRE), RETURN, ESC,
            F1, HELP};
    }
}
