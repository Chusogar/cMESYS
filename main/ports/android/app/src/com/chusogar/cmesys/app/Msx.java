package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The MSX1 and MSX2 (msx_core.h), with the disk ROM when the APK has it.
 *  Key codes: matrix row * 8 + bit (international layout). */
final class Msx extends Machine {
    static int k(int row, int bit) { return (row << 3) | bit; }

    static final int N0 = k(0, 0), N1 = k(0, 1), N2 = k(0, 2), N3 = k(0, 3), N4 = k(0, 4), N5 = k(0, 5), N6 = k(0, 6), N7 = k(0, 7);
    static final int N8 = k(1, 0), N9 = k(1, 1), MINUS = k(1, 2), EQUALS = k(1, 3), BACKSLASH = k(1, 4), LBRACKET = k(1, 5), RBRACKET = k(1, 6), SEMICOLON = k(1, 7);
    static final int QUOTE = k(2, 0), GRAVE = k(2, 1), COMMA = k(2, 2), PERIOD = k(2, 3), SLASH = k(2, 4), DEAD = k(2, 5);
    static final int SHIFT = k(6, 0), CTRL = k(6, 1), GRAPH = k(6, 2), CAPS = k(6, 3), CODE = k(6, 4), F1 = k(6, 5), F2 = k(6, 6), F3 = k(6, 7);
    static final int F4 = k(7, 0), F5 = k(7, 1), ESC = k(7, 2), TAB = k(7, 3), STOP = k(7, 4), BS = k(7, 5), SELECT = k(7, 6), RETURN = k(7, 7);
    static final int SPACE = k(8, 0), HOME = k(8, 1), INS = k(8, 2), DEL = k(8, 3), LEFT = k(8, 4), UP = k(8, 5), DOWN = k(8, 6), RIGHT = k(8, 7);

    /** A..Z: row 2 bit 6 onwards. */
    static int letter(char c) {
        int o = c - 'A' + 6;
        return k(2 + o / 8, o % 8);
    }

    Msx() {
        int[] az = new int[26];
        for (int i = 0; i < 26; i++) az[i] = letter((char) ('A' + i));
        mapLetters(az, new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, GRAPH);
        map(KeyEvent.KEYCODE_ALT_RIGHT, CODE);
        map(KeyEvent.KEYCODE_DEL, BS);
        map(KeyEvent.KEYCODE_FORWARD_DEL, DEL);
        map(KeyEvent.KEYCODE_INSERT, INS);
        map(KeyEvent.KEYCODE_MOVE_HOME, HOME);
        map(KeyEvent.KEYCODE_MOVE_END, STOP);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_F1, F1);
        map(KeyEvent.KEYCODE_F2, F2);
        map(KeyEvent.KEYCODE_F3, F3);
        map(KeyEvent.KEYCODE_F4, F4);
        map(KeyEvent.KEYCODE_F5, F5);
        map(KeyEvent.KEYCODE_F6, SELECT);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_BACKSLASH, BACKSLASH);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, LBRACKET);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, RBRACKET);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_GRAVE, GRAVE);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('"', SHIFT, QUOTE);
        chars(':', SHIFT, SEMICOLON);
        chars(',', COMMA);
        chars('.', PERIOD);
    }

    @Override String title() { return "MSX"; }

    @Override String defaultModel() { return "2"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String dir = model == 1 ? "roms/msx1/" : "roms/msx2/";
        byte[] bios = asset(a, dir + "bios.rom");
        if (bios == null) return null;
        return new byte[][] {bios, model == 1 ? null : asset(a, dir + "sub.rom"), asset(a, dir + "disk.rom")};
    }

    @Override String mediaExtensions() { return ".rom .mx1 .mx2 .dsk .cas .tsx .tzx"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir archivo… (cartucho, disco, cinta)", a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el cartucho (reset)", a.ejectAction(0, "Cartucho fuera")));
        items.add(typeItem(a, "Escribir FILES (disco)", "FILES\r"));
        if ((status & Core.HAS_DISK) == 0) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "(sin ROM de disco: discos no)", null));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "Ahora: " + (n != null ? n : "nada"), null));
    }

    @Override String keysLabel() { return "Teclas MSX"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"STOP", new int[] {STOP}});
        s.add(new Object[] {"CTRL + STOP (Break)", new int[] {CTRL, STOP}});
        s.add(new Object[] {"F1", new int[] {F1}});
        s.add(new Object[] {"F2", new int[] {F2}});
        s.add(new Object[] {"F3", new int[] {F3}});
        s.add(new Object[] {"F4", new int[] {F4}});
        s.add(new Object[] {"F5", new int[] {F5}});
        s.add(new Object[] {"SELECT", new int[] {SELECT}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        return s;
    }

    @Override String defaultPad() { return "joy1"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, k(4, 2) /* M */, SPACE, RETURN, ESC, F1, F5};
        }
        int port = "joy2".equals(mode) ? 2 : 1;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT),
            joy(port, Core.JOY_RIGHT), joy(port, Core.JOY_FIRE), joy(port, Core.JOY_FIRE2), SPACE, RETURN, ESC, F1, F5};
    }
}
