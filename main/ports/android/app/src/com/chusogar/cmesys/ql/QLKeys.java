package com.chusogar.cmesys.ql;

import android.view.KeyEvent;

/** QL keyboard matrix codes (ql_core.h: row << 3 | bit) and the Android key map. */
final class QLKeys {
    private QLKeys() {}

    static int k(int row, int bit) { return (row << 3) | bit; }

    static final int F4 = k(0, 0), F1 = k(0, 1), N5 = k(0, 2), F2 = k(0, 3), F3 = k(0, 4), F5 = k(0, 5), N4 = k(0, 6), N7 = k(0, 7);
    static final int ENTER = k(1, 0), LEFT = k(1, 1), UP = k(1, 2), ESC = k(1, 3), RIGHT = k(1, 4), BACKSLASH = k(1, 5), SPACE = k(1, 6), DOWN = k(1, 7);
    static final int RBRACKET = k(2, 0), Z = k(2, 1), PERIOD = k(2, 2), C = k(2, 3), B = k(2, 4), POUND = k(2, 5), M = k(2, 6), QUOTE = k(2, 7);
    static final int LBRACKET = k(3, 0), CAPS = k(3, 1), K = k(3, 2), S = k(3, 3), F = k(3, 4), EQUALS = k(3, 5), G = k(3, 6), SEMICOLON = k(3, 7);
    static final int L = k(4, 0), N3 = k(4, 1), H = k(4, 2), N1 = k(4, 3), A = k(4, 4), P = k(4, 5), D = k(4, 6), J = k(4, 7);
    static final int N9 = k(5, 0), W = k(5, 1), I = k(5, 2), TAB = k(5, 3), R = k(5, 4), MINUS = k(5, 5), Y = k(5, 6), O = k(5, 7);
    static final int N8 = k(6, 0), N2 = k(6, 1), N6 = k(6, 2), Q = k(6, 3), E = k(6, 4), N0 = k(6, 5), T = k(6, 6), U = k(6, 7);
    static final int SHIFT = k(7, 0), CTRL = k(7, 1), ALT = k(7, 2), X = k(7, 3), V = k(7, 4), SLASH = k(7, 5), N = k(7, 6), COMMA = k(7, 7);

    /** A QL key for an Android key code (-1: none). Backspace and Delete are
     *  CTRL+LEFT / CTRL+RIGHT (see extra()). */
    static int fromAndroid(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_A: return A; case KeyEvent.KEYCODE_B: return B; case KeyEvent.KEYCODE_C: return C;
            case KeyEvent.KEYCODE_D: return D; case KeyEvent.KEYCODE_E: return E; case KeyEvent.KEYCODE_F: return F;
            case KeyEvent.KEYCODE_G: return G; case KeyEvent.KEYCODE_H: return H; case KeyEvent.KEYCODE_I: return I;
            case KeyEvent.KEYCODE_J: return J; case KeyEvent.KEYCODE_K: return K; case KeyEvent.KEYCODE_L: return L;
            case KeyEvent.KEYCODE_M: return M; case KeyEvent.KEYCODE_N: return N; case KeyEvent.KEYCODE_O: return O;
            case KeyEvent.KEYCODE_P: return P; case KeyEvent.KEYCODE_Q: return Q; case KeyEvent.KEYCODE_R: return R;
            case KeyEvent.KEYCODE_S: return S; case KeyEvent.KEYCODE_T: return T; case KeyEvent.KEYCODE_U: return U;
            case KeyEvent.KEYCODE_V: return V; case KeyEvent.KEYCODE_W: return W; case KeyEvent.KEYCODE_X: return X;
            case KeyEvent.KEYCODE_Y: return Y; case KeyEvent.KEYCODE_Z: return Z;
            case KeyEvent.KEYCODE_0: return N0; case KeyEvent.KEYCODE_1: return N1; case KeyEvent.KEYCODE_2: return N2;
            case KeyEvent.KEYCODE_3: return N3; case KeyEvent.KEYCODE_4: return N4; case KeyEvent.KEYCODE_5: return N5;
            case KeyEvent.KEYCODE_6: return N6; case KeyEvent.KEYCODE_7: return N7; case KeyEvent.KEYCODE_8: return N8;
            case KeyEvent.KEYCODE_9: return N9;
            case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return ENTER;
            case KeyEvent.KEYCODE_SPACE: return SPACE;
            case KeyEvent.KEYCODE_ESCAPE: return ESC;
            case KeyEvent.KEYCODE_TAB: return TAB;
            case KeyEvent.KEYCODE_CAPS_LOCK: return CAPS;
            case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT; case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
            case KeyEvent.KEYCODE_DPAD_UP: return UP; case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
            case KeyEvent.KEYCODE_SHIFT_LEFT: case KeyEvent.KEYCODE_SHIFT_RIGHT: return SHIFT;
            case KeyEvent.KEYCODE_CTRL_LEFT: case KeyEvent.KEYCODE_CTRL_RIGHT: return CTRL;
            case KeyEvent.KEYCODE_ALT_LEFT: case KeyEvent.KEYCODE_ALT_RIGHT: return ALT;
            case KeyEvent.KEYCODE_MINUS: return MINUS; case KeyEvent.KEYCODE_EQUALS: return EQUALS;
            case KeyEvent.KEYCODE_LEFT_BRACKET: return LBRACKET; case KeyEvent.KEYCODE_RIGHT_BRACKET: return RBRACKET;
            case KeyEvent.KEYCODE_SEMICOLON: return SEMICOLON; case KeyEvent.KEYCODE_APOSTROPHE: return QUOTE;
            case KeyEvent.KEYCODE_BACKSLASH: return BACKSLASH; case KeyEvent.KEYCODE_GRAVE: return POUND;
            case KeyEvent.KEYCODE_COMMA: return COMMA; case KeyEvent.KEYCODE_PERIOD: return PERIOD;
            case KeyEvent.KEYCODE_SLASH: return SLASH;
            case KeyEvent.KEYCODE_F1: return F1; case KeyEvent.KEYCODE_F2: return F2; case KeyEvent.KEYCODE_F3: return F3;
            case KeyEvent.KEYCODE_F4: return F4; case KeyEvent.KEYCODE_F5: return F5;
            default: return -1;
        }
    }

    /** Keys that are two QL keys: Backspace = CTRL+LEFT, Delete = CTRL+RIGHT. */
    static int[] extra(int code) {
        if (code == KeyEvent.KEYCODE_DEL) return new int[] {CTRL, LEFT};
        if (code == KeyEvent.KEYCODE_FORWARD_DEL) return new int[] {CTRL, RIGHT};
        return null;
    }

    /** A game pad button or stick direction as a QL key, by the joystick
     *  setting: "ctl1" (cursors + SPACE) or "ctl2" (F1 left, F3 right, F4 up,
     *  F2 down, F5 fire: the QL's second joystick port). */
    static int fromGamepad(int code, String mapping) {
        boolean ctl2 = "ctl2".equals(mapping);
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_LEFT: return ctl2 ? F1 : LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return ctl2 ? F3 : RIGHT;
            case KeyEvent.KEYCODE_DPAD_UP: return ctl2 ? F4 : UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return ctl2 ? F2 : DOWN;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_BUTTON_X: case KeyEvent.KEYCODE_DPAD_CENTER:
                return ctl2 ? F5 : SPACE;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BUTTON_Y: return ENTER;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return ESC;
            default: return -1;
        }
    }
}
