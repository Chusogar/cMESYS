package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The ZX Spectrum Next (specnext_core.h): it boots as the real one, the
 *  boot ROM (the APK's) loading TBBLUE.FW and NextZXOS from an SD card image
 *  (used where it is, written as a real card); a .nex is loaded once
 *  NextZXOS is up. Key codes: the 40 of the Spectrum matrix (row * 8 + bit)
 *  and the Next's extra keys (NK_*); the on-screen keyboard is a photo of
 *  the Next's. */
final class SpecNext extends Machine {
    static int k(int row, int bit) { return (row << 3) | bit; }
    static final int CAPS = k(0, 0), Z = k(0, 1), X = k(0, 2), C = k(0, 3), V = k(0, 4);
    static final int A = k(1, 0), S = k(1, 1), D = k(1, 2), F = k(1, 3), G = k(1, 4);
    static final int Q = k(2, 0), W = k(2, 1), E = k(2, 2), R = k(2, 3), T = k(2, 4);
    static final int N1 = k(3, 0), N2 = k(3, 1), N3 = k(3, 2), N4 = k(3, 3), N5 = k(3, 4);
    static final int N0 = k(4, 0), N9 = k(4, 1), N8 = k(4, 2), N7 = k(4, 3), N6 = k(4, 4);
    static final int P = k(5, 0), O = k(5, 1), I = k(5, 2), U = k(5, 3), Y = k(5, 4);
    static final int ENTER = k(6, 0), L = k(6, 1), K = k(6, 2), J = k(6, 3), H = k(6, 4);
    static final int SPACE = k(7, 0), SYMB = k(7, 1), M = k(7, 2), N = k(7, 3), B = k(7, 4);
    // the Next's extra keys
    static final int SEMICOLON = 0x40, QUOTE = 0x41, COMMA = 0x42, PERIOD = 0x43, UP = 0x44, DOWN = 0x45,
        LEFT = 0x46, RIGHT = 0x47, DELETE = 0x48, EDIT = 0x49, BREAK = 0x4a, INV_VIDEO = 0x4b, TRUE_VIDEO = 0x4c,
        GRAPH = 0x4d, CAPS_LOCK = 0x4e, EXTEND = 0x4f;
    // PC keys typed as Symbol Shift combinations
    static final int MINUS = 0x50, EQUALS = 0x51, PLUS = 0x52, SLASH = 0x53, ASTERISK = 0x54;

    SpecNext() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, ENTER);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, ENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, CAPS);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, CAPS);
        map(KeyEvent.KEYCODE_CTRL_LEFT, SYMB);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, SYMB);
        map(KeyEvent.KEYCODE_ALT_LEFT, SYMB);
        map(KeyEvent.KEYCODE_DEL, DELETE);
        map(KeyEvent.KEYCODE_FORWARD_DEL, DELETE);
        map(KeyEvent.KEYCODE_ESCAPE, BREAK);
        map(KeyEvent.KEYCODE_GRAVE, EDIT);
        map(KeyEvent.KEYCODE_TAB, EXTEND);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS_LOCK);
        map(KeyEvent.KEYCODE_INSERT, GRAPH);
        map(KeyEvent.KEYCODE_MOVE_HOME, TRUE_VIDEO);
        map(KeyEvent.KEYCODE_MOVE_END, INV_VIDEO);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_NUMPAD_ADD, PLUS);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        map(KeyEvent.KEYCODE_NUMPAD_DIVIDE, SLASH);
        map(KeyEvent.KEYCODE_NUMPAD_MULTIPLY, ASTERISK);
        chars(' ', SPACE);
        chars('\r', ENTER);
        chars('"', QUOTE);
        chars(',', COMMA);
        chars('.', PERIOD);
        chars(';', SEMICOLON);
    }

    @Override String title() { return "ZX Spectrum Next"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/boot.rom")};
    }

    /** 640 dots by 256 lines (the 320 x 256 VGA area at double width): each line twice. */
    @Override int lineScale() { return 2; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Tarjeta SD"; }

    @Override String mediaExtensions() { return ".img .mmc .hdf .sd .nex"; }

    /** The SD card image is used where it is (2 GB and more; the Next writes on it). */
    @Override boolean inPlace(String name, long size) { return !name.toLowerCase().endsWith(".nex"); }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                n != null ? "Cambiar la tarjeta SD… (" + n + ")" : "Meter una tarjeta SD (.img) o abrir un .nex…",
                a.pickAction(0)));
        if (n != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar la tarjeta SD",
                a.ejectAction(0, "Sin tarjeta SD")));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_turbo, "Botón NMI (menú de NextZXOS)",
                a.commandAction(Core.CMD_MAGIC, "NMI", false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_turbo, "Botón DRIVE (NMI de la DivMMC)",
                a.commandAction(Core.CMD_DRIVE, "DRIVE", false)));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Botón RESET (reinicio suave)",
                a.commandAction(Core.CMD_RESET_KEY, null, false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Apagar y encender", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas Next"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"BREAK", new int[] {BREAK}});
        s.add(new Object[] {"EDIT", new int[] {EDIT}});
        s.add(new Object[] {"DELETE", new int[] {DELETE}});
        s.add(new Object[] {"EXTEND MODE", new int[] {EXTEND}});
        s.add(new Object[] {"ENTER", new int[] {ENTER}});
        s.add(new Object[] {"ESPACIO", new int[] {SPACE}});
        s.add(new Object[] {"Y (sí)", new int[] {Y}});
        s.add(new Object[] {"N (no)", new int[] {N}});
        return s;
    }

    @Override String defaultPad() { return "kempston"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, ENTER, SPACE, BREAK, EDIT, BREAK, Y, N};
        }
        return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT), joy(1, Core.JOY_RIGHT),
            joy(1, Core.JOY_FIRE), joy(1, Core.JOY_FIRE2), ENTER, SPACE, BREAK, Y, N};
    }
}
