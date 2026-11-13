package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The ZX Spectrum family: 48K, 128K, +3, Pentagon, Pentagon 1024 and
 *  Scorpion ZS-256 (zxspec_core.h zx_model_t), each with the ROMs of its
 *  MAME set. Key codes: matrix row * 8 + bit. */
final class Spectrum extends Machine {
    static int k(int row, int bit) { return (row << 3) | bit; }

    static final int CAPS = k(0, 0), Z = k(0, 1), X = k(0, 2), C = k(0, 3), V = k(0, 4);
    static final int A = k(1, 0), S = k(1, 1), D = k(1, 2), F = k(1, 3), G = k(1, 4);
    static final int Q = k(2, 0), W = k(2, 1), E = k(2, 2), R = k(2, 3), T = k(2, 4);
    static final int N1 = k(3, 0), N2 = k(3, 1), N3 = k(3, 2), N4 = k(3, 3), N5 = k(3, 4);
    static final int N0 = k(4, 0), N9 = k(4, 1), N8 = k(4, 2), N7 = k(4, 3), N6 = k(4, 4);
    static final int P = k(5, 0), O = k(5, 1), I = k(5, 2), U = k(5, 3), Y = k(5, 4);
    static final int ENTER = k(6, 0), L = k(6, 1), K = k(6, 2), J = k(6, 3), H = k(6, 4);
    static final int SPACE = k(7, 0), SYMB = k(7, 1), M = k(7, 2), N = k(7, 3), B = k(7, 4);

    static final String[] MODEL_DIRS = {"spectrum48", "spectrum128", "plus3", "pentagon", "pentagon1024", "scorpion"};

    Spectrum() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, ENTER);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, ENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, CAPS);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SYMB);
        map(KeyEvent.KEYCODE_CTRL_LEFT, SYMB);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, SYMB);
        map(KeyEvent.KEYCODE_ALT_LEFT, SYMB);
        map(KeyEvent.KEYCODE_DEL, CAPS, N0);                 // DELETE
        map(KeyEvent.KEYCODE_ESCAPE, CAPS, SPACE);           // BREAK
        map(KeyEvent.KEYCODE_DPAD_LEFT, CAPS, N5);
        map(KeyEvent.KEYCODE_DPAD_DOWN, CAPS, N6);
        map(KeyEvent.KEYCODE_DPAD_UP, CAPS, N7);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, CAPS, N8);
        map(KeyEvent.KEYCODE_PERIOD, SYMB, M);
        map(KeyEvent.KEYCODE_COMMA, SYMB, N);
        map(KeyEvent.KEYCODE_MINUS, SYMB, J);
        map(KeyEvent.KEYCODE_EQUALS, SYMB, L);
        map(KeyEvent.KEYCODE_SLASH, SYMB, V);
        map(KeyEvent.KEYCODE_SEMICOLON, SYMB, O);
        map(KeyEvent.KEYCODE_APOSTROPHE, SYMB, P);           // "
        chars(' ', SPACE);
        chars('\r', ENTER);
        chars('"', SYMB, P);
    }

    @Override String title() { return "ZX Spectrum"; }

    @Override String defaultModel() { return "1"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        if (model < 0 || model >= MODEL_DIRS.length) model = 0;
        String dir = "roms/" + MODEL_DIRS[model] + "/";
        byte[] rom48 = asset(a, dir + "rom48.rom");
        if (rom48 == null) return null;
        return new byte[][] {rom48, asset(a, dir + "banked.rom")};
    }

    @Override int leds() { return 2; }

    @Override String mediaExtensions() { return ".tap .tzx .pzx .csw .sna .z80 .rzx .trd .scl .dsk .rom .if2"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir archivo… (cinta, snapshot, disco, cartucho)", a.pickAction(0)));
        if ((status & Core.TAPE_LOADED) != 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv, (status & Core.TAPE_PLAYING) != 0 ? "Cinta: pausa" : "Cinta: reproducir",
                    a.commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        items.add(typeItem(a, "Escribir LOAD \"\" (BASIC 48K)", "J\"\"\r"));
        items.add(typeItem(a, "ENTER (menú 128K: Tape Loader)", "\r"));
        int model = a.model();
        if (model >= 3) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_turbo, "Botón mágico (NMI)", a.commandAction(Core.CMD_MAGIC, "Botón mágico", false)));
        }
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "Ahora: " + (n != null ? n : "nada"), null));
    }

    @Override String keysLabel() { return "Teclas Spectrum"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"BREAK (CAPS SHIFT + ESPACIO)", new int[] {CAPS, SPACE}});
        s.add(new Object[] {"DELETE (CAPS SHIFT + 0)", new int[] {CAPS, N0}});
        s.add(new Object[] {"EDIT (CAPS SHIFT + 1)", new int[] {CAPS, N1}});
        s.add(new Object[] {"CAPS LOCK (CAPS SHIFT + 2)", new int[] {CAPS, N2}});
        s.add(new Object[] {"GRAPHICS (CAPS SHIFT + 9)", new int[] {CAPS, N9}});
        s.add(new Object[] {"Modo extendido (CAPS + SYMBOL)", new int[] {CAPS, SYMB}});
        s.add(new Object[] {"ENTER", new int[] {ENTER}});
        return s;
    }

    @Override String defaultPad() { return "kempston"; }

    @Override
    int[] padActions(String mode) {
        switch (mode) {
            case "sinclair1":    // 6 left, 7 right, 8 down, 9 up, 0 fire
                return new int[] {N9, N8, N6, N7, N0, ENTER, N0, SPACE, SPACE, NONE, NONE};
            case "sinclair2":    // 1 left, 2 right, 3 down, 4 up, 5 fire
                return new int[] {N4, N3, N1, N2, N5, ENTER, N5, SPACE, SPACE, NONE, NONE};
            case "cursor":       // 5 left, 6 down, 7 up, 8 right, 0 fire
                return new int[] {N7, N6, N5, N8, N0, ENTER, N0, SPACE, SPACE, NONE, NONE};
            default:             // Kempston
                return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT),
                    joy(1, Core.JOY_RIGHT), joy(1, Core.JOY_FIRE), ENTER, joy(1, Core.JOY_FIRE), SPACE, SPACE,
                    NONE, NONE};
        }
    }
}
