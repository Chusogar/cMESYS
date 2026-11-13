package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Amstrad CPC 6128 (cpc_core.h; the 464 and 664 need ROMs the APK does
 *  not carry). Key codes: matrix line * 8 + bit. */
final class Cpc extends Machine {
    static int k(int line, int bit) { return (line << 3) | bit; }

    static final int UP = k(0, 0), RIGHT = k(0, 1), DOWN = k(0, 2), F9 = k(0, 3), F6 = k(0, 4), F3 = k(0, 5), KPENTER = k(0, 6), KPDOT = k(0, 7);
    static final int LEFT = k(1, 0), COPY = k(1, 1), F7 = k(1, 2), F8 = k(1, 3), F5 = k(1, 4), F1 = k(1, 5), F2 = k(1, 6), F0 = k(1, 7);
    static final int CLR = k(2, 0), LBRACKET = k(2, 1), RETURN = k(2, 2), RBRACKET = k(2, 3), F4 = k(2, 4), SHIFT = k(2, 5), BACKSLASH = k(2, 6), CTRL = k(2, 7);
    static final int CARET = k(3, 0), MINUS = k(3, 1), AT = k(3, 2), P = k(3, 3), SEMICOLON = k(3, 4), COLON = k(3, 5), SLASH = k(3, 6), PERIOD = k(3, 7);
    static final int N0 = k(4, 0), N9 = k(4, 1), O = k(4, 2), I = k(4, 3), L = k(4, 4), K = k(4, 5), M = k(4, 6), COMMA = k(4, 7);
    static final int N8 = k(5, 0), N7 = k(5, 1), U = k(5, 2), Y = k(5, 3), H = k(5, 4), J = k(5, 5), N = k(5, 6), SPACE = k(5, 7);
    static final int N6 = k(6, 0), N5 = k(6, 1), R = k(6, 2), T = k(6, 3), G = k(6, 4), F = k(6, 5), B = k(6, 6), V = k(6, 7);
    static final int N4 = k(7, 0), N3 = k(7, 1), E = k(7, 2), W = k(7, 3), S = k(7, 4), D = k(7, 5), C = k(7, 6), X = k(7, 7);
    static final int N1 = k(8, 0), N2 = k(8, 1), ESC = k(8, 2), Q = k(8, 3), TAB = k(8, 4), A = k(8, 5), CAPSLOCK = k(8, 6), Z = k(8, 7);
    static final int DEL = k(9, 7);

    static final int MODEL_6128 = 2;

    Cpc() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, KPENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPSLOCK);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, COPY);
        map(KeyEvent.KEYCODE_ALT_RIGHT, COPY);
        map(KeyEvent.KEYCODE_DEL, DEL);
        map(KeyEvent.KEYCODE_FORWARD_DEL, CLR);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, CARET);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, LBRACKET);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, RBRACKET);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, COLON);
        map(KeyEvent.KEYCODE_GRAVE, AT);
        map(KeyEvent.KEYCODE_BACKSLASH, BACKSLASH);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        int[] fk = {F1, F2, F3, F4, F5, F6, F7, F8, F9, F0};
        for (int i = 0; i < fk.length; i++) map(KeyEvent.KEYCODE_F1 + i, fk[i]);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('"', SHIFT, N2);
        chars('|', SHIFT, AT);
        chars(':', COLON);
    }

    @Override String title() { return "Amstrad CPC 6128"; }

    @Override String restartKey() { return "cpc_model"; }

    @Override int model(SharedPreferences p) { return MODEL_6128; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        byte[] os = asset(a, "roms/os.rom");
        return os == null ? null : new byte[][] {os, asset(a, "roms/amsdos.rom")};
    }

    @Override int leds() { return 1; }

    @Override String mediaExtensions() { return ".dsk .sna .cdt .tzx .csw"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir archivo… (disco, cinta, snapshot)", a.pickAction(0)));
        if ((status & Core.TAPE_LOADED) != 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv, (status & Core.TAPE_PLAYING) != 0 ? "Cinta: pausa" : "Cinta: reproducir",
                    a.commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        items.add(typeItem(a, "Escribir CAT (disco)", "CAT\r"));
        items.add(typeItem(a, "Escribir RUN\" (cargar)", "RUN\"\r"));
        items.add(typeItem(a, "Escribir |TAPE y RUN\" (cinta)", "|TAPE\rRUN\"\r"));
        items.add(typeItem(a, "Escribir |CPM", "|CPM\r"));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_screen, "Monitor verde / color", a.commandAction(Core.CMD_MONITOR, null, false)));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "Ahora: " + (n != null ? n : "nada"), null));
    }

    @Override String keysLabel() { return "Teclas CPC"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"COPIA", new int[] {COPY}});
        s.add(new Object[] {"INTRO", new int[] {RETURN}});
        s.add(new Object[] {"CTRL+MAYS+ESC (reset)", new int[] {CTRL, SHIFT, ESC}});
        s.add(new Object[] {"f0", new int[] {F0}});
        s.add(new Object[] {"f1", new int[] {F1}});
        s.add(new Object[] {"f2", new int[] {F2}});
        return s;
    }

    @Override String defaultPad() { return "joy"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, COPY, SPACE, RETURN, ESC, NONE, NONE};
        }
        return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT), joy(1, Core.JOY_RIGHT),
            joy(1, Core.JOY_FIRE), joy(1, Core.JOY_FIRE2), SPACE, RETURN, ESC, NONE, NONE};
    }
}
