package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Commodore 64 (PAL) with its 1541 drive. Key codes: the matrix column
 *  * 8 + row (c64_core.h); 100 is RESTORE. */
final class C64 extends Machine {
    static int k(int col, int row) { return (col << 3) | row; }

    static final int DEL = k(0, 0), RETURN = k(0, 1), CRSR_LR = k(0, 2), F7 = k(0, 3), F1 = k(0, 4), F3 = k(0, 5), F5 = k(0, 6), CRSR_UD = k(0, 7);
    static final int N3 = k(1, 0), W = k(1, 1), A = k(1, 2), N4 = k(1, 3), Z = k(1, 4), S = k(1, 5), E = k(1, 6), LSHIFT = k(1, 7);
    static final int N5 = k(2, 0), R = k(2, 1), D = k(2, 2), N6 = k(2, 3), C = k(2, 4), F = k(2, 5), T = k(2, 6), X = k(2, 7);
    static final int N7 = k(3, 0), Y = k(3, 1), G = k(3, 2), N8 = k(3, 3), B = k(3, 4), H = k(3, 5), U = k(3, 6), V = k(3, 7);
    static final int N9 = k(4, 0), I = k(4, 1), J = k(4, 2), N0 = k(4, 3), M = k(4, 4), K = k(4, 5), O = k(4, 6), N = k(4, 7);
    static final int PLUS = k(5, 0), P = k(5, 1), L = k(5, 2), MINUS = k(5, 3), PERIOD = k(5, 4), COLON = k(5, 5), AT = k(5, 6), COMMA = k(5, 7);
    static final int POUND = k(6, 0), ASTERISK = k(6, 1), SEMICOLON = k(6, 2), HOME = k(6, 3), RSHIFT = k(6, 4), EQUALS = k(6, 5), UPARROW = k(6, 6), SLASH = k(6, 7);
    static final int N1 = k(7, 0), LEFTARROW = k(7, 1), CTRL = k(7, 2), N2 = k(7, 3), SPACE = k(7, 4), CBM = k(7, 5), Q = k(7, 6), RUNSTOP = k(7, 7);
    static final int RESTORE = 100;

    C64() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, RUNSTOP);
        map(KeyEvent.KEYCODE_TAB, CTRL);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, LSHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, RSHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, CBM);
        map(KeyEvent.KEYCODE_ALT_RIGHT, CBM);
        map(KeyEvent.KEYCODE_DEL, DEL);
        map(KeyEvent.KEYCODE_MOVE_HOME, HOME);
        map(KeyEvent.KEYCODE_PAGE_DOWN, RESTORE);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, CRSR_LR);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LSHIFT, CRSR_LR);
        map(KeyEvent.KEYCODE_DPAD_DOWN, CRSR_UD);
        map(KeyEvent.KEYCODE_DPAD_UP, LSHIFT, CRSR_UD);
        map(KeyEvent.KEYCODE_F1, F1);
        map(KeyEvent.KEYCODE_F2, LSHIFT, F1);
        map(KeyEvent.KEYCODE_F3, F3);
        map(KeyEvent.KEYCODE_F4, LSHIFT, F3);
        map(KeyEvent.KEYCODE_F5, F5);
        map(KeyEvent.KEYCODE_F6, LSHIFT, F5);
        map(KeyEvent.KEYCODE_F7, F7);
        map(KeyEvent.KEYCODE_F8, LSHIFT, F7);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_PLUS, PLUS);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, AT);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, ASTERISK);
        map(KeyEvent.KEYCODE_SEMICOLON, COLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, SEMICOLON);
        map(KeyEvent.KEYCODE_GRAVE, LEFTARROW);
        map(KeyEvent.KEYCODE_BACKSLASH, POUND);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('"', LSHIFT, N2);
        chars('$', LSHIFT, N4);
        chars('*', ASTERISK);
        chars(',', COMMA);
        chars('.', PERIOD);
        chars(':', COLON);
    }

    @Override String title() { return "Commodore 64"; }

    @Override String restartKey() { return "c64_model"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        byte[] kernal = asset(a, "roms/kernal.rom"), basic = asset(a, "roms/basic.rom"), chargen = asset(a, "roms/chargen.rom");
        if (kernal == null || basic == null || chargen == null) return null;
        return new byte[][] {kernal, basic, chargen, asset(a, "roms/dos1541.rom")};
    }

    @Override byte[] glyphRom(MachineActivity a) { return asset(a, "roms/chargen.rom"); }

    @Override int leds() { return 2; }

    @Override String mediaExtensions() { return ".d64 .g64 .prg .t64 .tap"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir archivo… (disco, programa, cinta)", a.pickAction(0)));
        if ((status & Core.TAPE_LOADED) != 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv, (status & Core.TAPE_PLAYING) != 0 ? "Cinta: STOP" : "Cinta: PLAY",
                    a.commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        items.add(typeItem(a, "Escribir LOAD\"*\",8,1 (disco)", "LOAD\"*\",8,1\r"));
        items.add(typeItem(a, "Escribir LOAD\"$\",8 y LIST (directorio)", "LOAD\"$\",8\rLIST\r"));
        items.add(typeItem(a, "Escribir LOAD (cinta)", "LOAD\r"));
        items.add(typeItem(a, "Escribir RUN", "RUN\r"));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "Ahora: " + (n != null ? n : "nada"), null));
    }

    @Override String keysLabel() { return "Teclas C64"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"RUN/STOP", new int[] {RUNSTOP}});
        s.add(new Object[] {"RUN/STOP + RESTORE", new int[] {RUNSTOP, RESTORE}});
        s.add(new Object[] {"F1", new int[] {F1}});
        s.add(new Object[] {"F3", new int[] {F3}});
        s.add(new Object[] {"F5", new int[] {F5}});
        s.add(new Object[] {"F7", new int[] {F7}});
        s.add(new Object[] {"CLR/HOME", new int[] {HOME}});
        s.add(new Object[] {"SHIFT + CLR/HOME (borrar)", new int[] {LSHIFT, HOME}});
        s.add(new Object[] {"C= + SHIFT (mayúsculas / minúsculas)", new int[] {CBM, LSHIFT}});
        return s;
    }

    @Override String defaultPad() { return "joy2"; }

    @Override
    int[] padActions(String mode) {
        int port = "joy1".equals(mode) ? 1 : 2;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT),
            joy(port, Core.JOY_RIGHT), joy(port, Core.JOY_FIRE), SPACE, joy(port, Core.JOY_FIRE), RETURN, RUNSTOP,
            F1, F7};
    }
}
