package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Exelvision EXL-100 and EXELTEL (exelv_core.h): the model setting, a
 *  cartridge (slot 0) and a cassette (slot 1). Key codes: the infrared
 *  keyboard's channels, row * 8 + column (EK_*); the on-screen keyboard is
 *  the EXL-100's AZERTY one, drawn (tools/gen_exelv_vkb.py). SHIFT, CTL and
 *  FCT are pressed before the key on the real one; a chord is sent that way. */
final class Exelv extends Machine {
    static int ch(int row, int col) { return row * 8 + col; }
    static final int Z = ch(0, 0), UP = ch(0, 1), RIGHT = ch(0, 2), DOWN = ch(0, 3), LEFT = ch(0, 4), E = ch(0, 5);
    static final int SPACE = ch(0, 6), UNDERSCORE = ch(0, 7);
    static final int CTL = ch(1, 0), LOCK = ch(1, 1), X = ch(1, 2), SLASH = ch(1, 3), ESC = ch(1, 4), R = ch(1, 5);
    static final int QUESTION = ch(1, 6), FCT = ch(1, 7);
    static final int TAB = ch(2, 0), HOME = ch(2, 1), V = ch(2, 2), PLUS = ch(2, 3), N1 = ch(2, 4), N6 = ch(2, 5);
    static final int N8 = ch(2, 6), N0 = ch(2, 7);
    static final int A = ch(3, 0), BACKSPACE = ch(3, 1), C = ch(3, 2), PERIOD = ch(3, 3), N2 = ch(3, 4), N3 = ch(3, 5);
    static final int N9 = ch(3, 6), HASH = ch(3, 7);
    static final int SHIFT = ch(4, 0), O = ch(4, 1), H = ch(4, 2), LESS = ch(4, 3), T = ch(4, 4), M = ch(4, 5);
    static final int N = ch(4, 6), G = ch(4, 7);
    static final int S = ch(5, 0), U = ch(5, 1), K = ch(5, 2), GREATER = ch(5, 3), Y = ch(5, 4), B = ch(5, 6), D = ch(5, 7);
    static final int W = ch(6, 0), P = ch(6, 1), J = ch(6, 2), TILDE = ch(6, 3), N4 = ch(6, 4), N7 = ch(6, 5);
    static final int STAR = ch(6, 6), F = ch(6, 7);
    static final int Q = ch(7, 0), I = ch(7, 1), L = ch(7, 2), ENTER = ch(7, 3), N5 = ch(7, 4), PERCENT = ch(7, 5);

    Exelv() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, ENTER);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, ENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTL);
        map(KeyEvent.KEYCODE_ALT_LEFT, FCT);
        map(KeyEvent.KEYCODE_ALT_RIGHT, STAR);
        map(KeyEvent.KEYCODE_CAPS_LOCK, LOCK);
        map(KeyEvent.KEYCODE_DEL, BACKSPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_MOVE_HOME, HOME);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_MINUS, HASH);
        map(KeyEvent.KEYCODE_EQUALS, UNDERSCORE);
        map(KeyEvent.KEYCODE_APOSTROPHE, PLUS);
        map(KeyEvent.KEYCODE_SEMICOLON, PERCENT);
        map(KeyEvent.KEYCODE_COMMA, QUESTION);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, TILDE);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, LESS);
        map(KeyEvent.KEYCODE_BACKSLASH, GREATER);
        chars(' ', SPACE);
        chars('\r', ENTER);
        chars('?', QUESTION);
        chars('.', PERIOD);
        chars('/', SLASH);
        chars('+', PLUS);
        chars('#', HASH);
        chars('_', UNDERSCORE);
        chars('%', PERCENT);
    }

    @Override String title() { return "Exelvision"; }

    @Override String defaultModel() { return "0"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String dir = "roms/" + romSet(model) + "/";
        return new byte[][] {asset(a, dir + "main.rom"), asset(a, dir + "sub.rom"), asset(a, dir + "sys.rom")};
    }

    @Override String romSet(int model) { return model == 1 ? "exeltel" : "exl100"; }

    @Override String mediaLabel() { return "Cartuchos y casetes"; }

    @Override String mediaExtensions() { return ".bin .rom .cart .k7 .wav .zip"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String c = a.slotName(0), t = a.slotName(1);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                c != null ? "Cambiar el cartucho… (" + c + ")" : "Meter un cartucho… (.bin .rom)", a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv,
                t != null ? "Cambiar el casete… (" + t + ")" : "Meter un casete… (.k7 .wav)", a.pickAction(1)));
        if ((status & Core.TAPE_LOADED) != 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv,
                    (status & Core.TAPE_PLAYING) != 0 ? "Casete: parar" : "Casete: reproducir",
                    a.commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        if (c != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el cartucho (reinicia)",
                a.ejectAction(0, "Sin cartucho")));
        if (t != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el casete",
                a.ejectAction(1, "Sin casete")));
    }

    @Override String keysLabel() { return "Teclas EXL"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"ENTER", new int[] {ENTER}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"ESPACIO", new int[] {SPACE}});
        s.add(new Object[] {"FCT", new int[] {FCT}});
        s.add(new Object[] {"CTL", new int[] {CTL}});
        s.add(new Object[] {"SHIFT", new int[] {SHIFT}});
        s.add(new Object[] {"1", new int[] {N1}});
        s.add(new Object[] {"2", new int[] {N2}});
        s.add(new Object[] {"3", new int[] {N3}});
        return s;
    }

    @Override String defaultPad() { return "joy"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, ENTER, ESC, N1, ESC, N2, N1};
        }
        return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT), joy(1, Core.JOY_RIGHT),
            joy(1, Core.JOY_FIRE), ENTER, ENTER, SPACE, ESC, N1, N2};
    }
}
