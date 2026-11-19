package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The SAM Coupe (samcoupe_core.h): 512 KB, two disk drives (slots 0 and 1:
 *  .mgt .dsk .sad .sdf) and tapes (.tap .tzx; slot 2, or any slot by their
 *  name). Key codes: the SAM's matrix, column * 8 + bit (SK_*); the
 *  on-screen keyboard is a photo of the SAM's (tools/gen_samcoupe_vkb.py). */
final class SamCoupe extends Machine {
    static int k(int col, int bit) { return (col << 3) | bit; }
    static final int SHIFT = k(0, 0), Z = k(0, 1), X = k(0, 2), C = k(0, 3), V = k(0, 4), F1 = k(0, 5), F2 = k(0, 6), F3 = k(0, 7);
    static final int A = k(1, 0), S = k(1, 1), D = k(1, 2), F = k(1, 3), G = k(1, 4), F4 = k(1, 5), F5 = k(1, 6), F6 = k(1, 7);
    static final int Q = k(2, 0), W = k(2, 1), E = k(2, 2), R = k(2, 3), T = k(2, 4), F7 = k(2, 5), F8 = k(2, 6), F9 = k(2, 7);
    static final int N1 = k(3, 0), N2 = k(3, 1), N3 = k(3, 2), N4 = k(3, 3), N5 = k(3, 4), ESC = k(3, 5), TAB = k(3, 6), CAPS = k(3, 7);
    static final int N0 = k(4, 0), N9 = k(4, 1), N8 = k(4, 2), N7 = k(4, 3), N6 = k(4, 4), MINUS = k(4, 5), PLUS = k(4, 6), DELETE = k(4, 7);
    static final int P = k(5, 0), O = k(5, 1), I = k(5, 2), U = k(5, 3), Y = k(5, 4), EQUALS = k(5, 5), QUOTE = k(5, 6), F0 = k(5, 7);
    static final int RETURN = k(6, 0), L = k(6, 1), K = k(6, 2), J = k(6, 3), H = k(6, 4), SEMICOLON = k(6, 5), COLON = k(6, 6), EDIT = k(6, 7);
    static final int SPACE = k(7, 0), SYMBOL = k(7, 1), M = k(7, 2), N = k(7, 3), B = k(7, 4), COMMA = k(7, 5), PERIOD = k(7, 6), INV = k(7, 7);
    static final int CNTRL = k(8, 0), UP = k(8, 1), DOWN = k(8, 2), LEFT = k(8, 3), RIGHT = k(8, 4);

    SamCoupe() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        // typed upper case letters with SHIFT
        int[] az = {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z};
        for (int i = 0; i < 26; i++) chars((char) ('A' + i), SHIFT, az[i]);
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CNTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, SYMBOL);
        map(KeyEvent.KEYCODE_ALT_LEFT, SYMBOL);
        map(KeyEvent.KEYCODE_ALT_RIGHT, SYMBOL);
        map(KeyEvent.KEYCODE_DEL, DELETE);
        map(KeyEvent.KEYCODE_FORWARD_DEL, DELETE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_MOVE_HOME, EDIT);
        map(KeyEvent.KEYCODE_MOVE_END, INV);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_SLASH, COLON);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_NUMPAD_ADD, PLUS);
        map(KeyEvent.KEYCODE_PLUS, PLUS);
        int[] fk = {F1, F2, F3, F4, F5, F6, F7, F8, F9, F0};
        for (int i = 0; i < fk.length; i++) map(KeyEvent.KEYCODE_F1 + i, fk[i]);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('"', QUOTE);
        chars(',', COMMA);
        chars('.', PERIOD);
        chars(';', SEMICOLON);
        chars(':', COLON);
        chars('=', EQUALS);
        chars('+', PLUS);
        chars('-', MINUS);
    }

    @Override String title() { return "SAM Coupé"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/sam.rom")};
    }

    /** 768 dots by 312 lines (384 T-states a line, two dots each): each line twice. */
    @Override int lineScale() { return 2; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos y cintas"; }

    @Override String mediaExtensions() { return ".mgt .dsk .sad .sdf .img .tap .tzx"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String d0 = a.slotName(0), d1 = a.slotName(1), t = a.slotName(2);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                d0 != null ? "Cambiar el disco de la unidad 1… (" + d0 + ")" : "Meter un disco en la unidad 1… (.mgt .dsk)",
                a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                d1 != null ? "Cambiar el disco de la unidad 2… (" + d1 + ")" : "Meter un disco en la unidad 2…",
                a.pickAction(1)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv,
                t != null ? "Cambiar la cinta… (" + t + ")" : "Meter una cinta… (.tap .tzx)", a.pickAction(2)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_turbo, "BOOT (arrancar el disco de la unidad 1)",
                a.commandAction(Core.CMD_AUTOBOOT, "BOOT", false)));
        if ((status & Core.TAPE_LOADED) != 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv,
                    (status & Core.TAPE_PLAYING) != 0 ? "Cinta: parar" : "Cinta: reproducir",
                    a.commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        items.add(typeItem(a, "Escribir LOAD \"\" (cinta)", "LOAD \"\"\r"));
        items.add(typeItem(a, "Escribir DIR (disco)", "DIR\r"));
        if (d0 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco de la unidad 1",
                a.ejectAction(0, "Unidad 1 vacía")));
        if (d1 != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco de la unidad 2",
                a.ejectAction(1, "Unidad 2 vacía")));
        if (t != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar la cinta",
                a.ejectAction(2, "Sin cinta")));
    }

    @Override String keysLabel() { return "Teclas SAM"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"F9 (BOOT)", new int[] {F9}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"EDIT", new int[] {EDIT}});
        s.add(new Object[] {"DELETE", new int[] {DELETE}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"ESPACIO", new int[] {SPACE}});
        s.add(new Object[] {"F0", new int[] {F0}});
        s.add(new Object[] {"F1", new int[] {F1}});
        return s;
    }

    @Override String defaultPad() { return "joy1"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, RETURN, ESC, F9, ESC, RETURN, SPACE};
        }
        int port = "joy2".equals(mode) ? 2 : 1;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT),
            joy(port, Core.JOY_RIGHT), joy(port, Core.JOY_FIRE), joy(port, Core.JOY_FIRE2), RETURN, SPACE, ESC, F9, ESC};
    }
}
