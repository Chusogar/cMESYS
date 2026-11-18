package com.chusogar.cmesys.app;

import android.view.KeyEvent;

/** The keyboards of the Apple IIGS and the Macs: Apple's key codes (the
 *  Mac's raw codes, ADB's), the same for the letters, digits and symbols;
 *  each machine gives the codes of its other keys. */
abstract class AdbKeys extends Machine {
    static final int A = 0x00, S = 0x01, D = 0x02, F = 0x03, H = 0x04, G = 0x05, Z = 0x06, X = 0x07, C = 0x08, V = 0x09;
    static final int B = 0x0B, Q = 0x0C, W = 0x0D, E = 0x0E, R = 0x0F, Y = 0x10, T = 0x11;
    static final int N1 = 0x12, N2 = 0x13, N3 = 0x14, N4 = 0x15, N6 = 0x16, N5 = 0x17, EQUALS = 0x18, N9 = 0x19;
    static final int N7 = 0x1A, MINUS = 0x1B, N8 = 0x1C, N0 = 0x1D, RBRACKET = 0x1E, O = 0x1F, U = 0x20;
    static final int LBRACKET = 0x21, I = 0x22, P = 0x23, RETURN = 0x24, L = 0x25, J = 0x26, QUOTE = 0x27, K = 0x28;
    static final int SEMICOLON = 0x29, BACKSLASH = 0x2A, COMMA = 0x2B, SLASH = 0x2C, N = 0x2D, M = 0x2E, PERIOD = 0x2F;
    static final int TAB = 0x30, SPACE = 0x31, GRAVE = 0x32, DELETE = 0x33, ESC = 0x35, COMMAND = 0x37, SHIFT = 0x38;
    static final int CAPS = 0x39, OPTION = 0x3A;
    static final int KP_PERIOD = 0x41, KP_MUL = 0x43, KP_PLUS = 0x45, KP_CLEAR = 0x47, KP_DIV = 0x4B, KP_ENTER = 0x4C;
    static final int KP_MINUS = 0x4E, KP_EQUALS = 0x51;
    static final int[] KP = {0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5B, 0x5C};

    final int ctrl, left, right, down, up;

    AdbKeys(int ctrl, int left, int right, int down, int up) {
        this.ctrl = ctrl;
        this.left = left;
        this.right = right;
        this.down = down;
        this.up = up;
        int[] az = {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z};
        mapLetters(az, new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, KP_ENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_DEL, DELETE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        if (ctrl >= 0) {
            map(KeyEvent.KEYCODE_CTRL_LEFT, ctrl);
            map(KeyEvent.KEYCODE_CTRL_RIGHT, ctrl);
        }
        map(KeyEvent.KEYCODE_ALT_LEFT, OPTION);
        map(KeyEvent.KEYCODE_ALT_RIGHT, OPTION);
        map(KeyEvent.KEYCODE_META_LEFT, COMMAND);
        map(KeyEvent.KEYCODE_META_RIGHT, COMMAND);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_DPAD_UP, up);
        map(KeyEvent.KEYCODE_DPAD_DOWN, down);
        map(KeyEvent.KEYCODE_DPAD_LEFT, left);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, right);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, LBRACKET);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, RBRACKET);
        map(KeyEvent.KEYCODE_BACKSLASH, BACKSLASH);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_GRAVE, GRAVE);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        for (int i = 0; i < 10; i++) map(KeyEvent.KEYCODE_NUMPAD_0 + i, KP[i]);
        map(KeyEvent.KEYCODE_NUMPAD_DOT, KP_PERIOD);
        map(KeyEvent.KEYCODE_NUMPAD_ADD, KP_PLUS);
        map(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, KP_MINUS);
        map(KeyEvent.KEYCODE_NUMPAD_MULTIPLY, KP_MUL);
        map(KeyEvent.KEYCODE_NUMPAD_DIVIDE, KP_DIV);
        map(KeyEvent.KEYCODE_NUMPAD_EQUALS, KP_EQUALS);
        map(KeyEvent.KEYCODE_NUM_LOCK, KP_CLEAR);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars(',', COMMA);
        chars('.', PERIOD);
        chars('-', MINUS);
        chars('/', SLASH);
        chars('#', SHIFT, N3);
        chars('"', SHIFT, QUOTE);
        chars(':', SHIFT, SEMICOLON);
    }

    @Override boolean usesMouse() { return true; }

    @Override String defaultPad() { return "cursors"; }

    @Override
    int[] padActions(String mode) {
        return new int[] {up, down, left, right, RETURN, SPACE, ESC, TAB, ESC, COMMAND, OPTION};
    }
}
