package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.List;

/** Any console of main/drivers/consoles (jni/m_cons.c), described by
 *  ConsoleInfo (written by build_apk.sh from machines/<id>/console.txt).
 *  Its "keys": 0.. the console's keys (cons.h's CK_*), 100.. player 1's pad
 *  (100 up, 101 down, 102 left, 103 right, 104-107 buttons 1-4, 108 START,
 *  109 SELECT, 110 COIN, 111-114 the diagonals). */
final class Console extends Machine {
    static final int UP = 100, DOWN = 101, LEFT = 102, RIGHT = 103, B1 = 104, B2 = 105, B3 = 106, B4 = 107,
            START = 108, SELECT = 109, COIN = 110;
    // cons.h's keys (dsp-cpp's Key enum)
    static final int CK_A = 0, CK_0 = 26, CK_ENTER = 36, CK_SPACE = 37, CK_LSHIFT = 38, CK_RSHIFT = 39,
            CK_LCTRL = 40, CK_BACKSPACE = 42, CK_UP = 43, CK_DOWN = 44, CK_LEFT = 45, CK_RIGHT = 46, CK_COMMA = 47,
            CK_PERIOD = 48, CK_SEMICOLON = 49, CK_QUOTE = 50, CK_SLASH = 51, CK_MINUS = 52, CK_ESCAPE = 53,
            CK_TAB = 54, CK_CAPSLOCK = 55, CK_HOME = 56, CK_EQUALS = 58, CK_F1 = 62;

    Console() {
        boolean keyboard = ConsoleInfo.KEYS == 2, keys = ConsoleInfo.KEYS != 0;
        map(KeyEvent.KEYCODE_DPAD_UP, keys ? new int[] {UP, CK_UP} : new int[] {UP});
        map(KeyEvent.KEYCODE_DPAD_DOWN, keys ? new int[] {DOWN, CK_DOWN} : new int[] {DOWN});
        map(KeyEvent.KEYCODE_DPAD_LEFT, keys ? new int[] {LEFT, CK_LEFT} : new int[] {LEFT});
        map(KeyEvent.KEYCODE_DPAD_RIGHT, keys ? new int[] {RIGHT, CK_RIGHT} : new int[] {RIGHT});
        map(KeyEvent.KEYCODE_CTRL_LEFT, B1);
        map(KeyEvent.KEYCODE_ALT_LEFT, B2);
        if (keys) {
            int[] az = new int[26], digits = new int[10];
            for (int i = 0; i < 26; i++) az[i] = CK_A + i;
            for (int i = 0; i < 10; i++) digits[i] = CK_0 + i;
            mapLetters(az, digits);
            map(KeyEvent.KEYCODE_SPACE, CK_SPACE);
            map(KeyEvent.KEYCODE_SHIFT_LEFT, CK_LSHIFT);
            map(KeyEvent.KEYCODE_SHIFT_RIGHT, CK_RSHIFT);
            map(KeyEvent.KEYCODE_DEL, CK_BACKSPACE);
            map(KeyEvent.KEYCODE_COMMA, CK_COMMA);
            map(KeyEvent.KEYCODE_PERIOD, CK_PERIOD);
            map(KeyEvent.KEYCODE_SEMICOLON, CK_SEMICOLON);
            map(KeyEvent.KEYCODE_APOSTROPHE, CK_QUOTE);
            map(KeyEvent.KEYCODE_SLASH, CK_SLASH);
            map(KeyEvent.KEYCODE_MINUS, CK_MINUS);
            map(KeyEvent.KEYCODE_EQUALS, CK_EQUALS);
            map(KeyEvent.KEYCODE_ESCAPE, CK_ESCAPE);
            map(KeyEvent.KEYCODE_TAB, CK_TAB);
            map(KeyEvent.KEYCODE_CAPS_LOCK, CK_CAPSLOCK);
            map(KeyEvent.KEYCODE_MOVE_HOME, CK_HOME);
            for (int i = 0; i < 12; i++) map(KeyEvent.KEYCODE_F1 + i, CK_F1 + i);
        }
        if (!keyboard) {
            map(KeyEvent.KEYCODE_ENTER, keys ? new int[] {START, CK_ENTER} : new int[] {START});
            if (!keys) {
                map(KeyEvent.KEYCODE_Z, B1);
                map(KeyEvent.KEYCODE_X, B2);
                map(KeyEvent.KEYCODE_C, B3);
                map(KeyEvent.KEYCODE_V, B4);
                map(KeyEvent.KEYCODE_5, COIN);
            }
            map(KeyEvent.KEYCODE_SHIFT_RIGHT, keys ? new int[] {SELECT, CK_RSHIFT} : new int[] {SELECT});
        } else {
            map(KeyEvent.KEYCODE_ENTER, CK_ENTER);
        }
    }

    @Override String title() { return ConsoleInfo.TITLE; }

    @Override String restartKey() { return "cons_model"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) {
        return new byte[0][];                       // (a BIOS is built into the core)
    }

    @Override float displayAspect(int w, int h) { return ConsoleInfo.ASPECT > 0 ? ConsoleInfo.ASPECT : w / (float) h; }

    @Override String mediaLabel() { return ConsoleInfo.MEDIA_NAME; }

    @Override String mediaExtensions() { return ConsoleInfo.EXTS + " .zip"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                "Abrir " + ConsoleInfo.MEDIA_NAME.toLowerCase() + "… (" + ConsoleInfo.EXTS + ")", a.pickAction(0)));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject,
                "Sacar " + ConsoleInfo.MEDIA_NAME.toLowerCase() + (n != null ? "  (" + n + ")" : ""),
                n == null ? null : a.ejectAction(0, ConsoleInfo.MEDIA_NAME + " fuera")));
    }

    @Override String keysLabel() { return "Botones"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        if (ConsoleInfo.START != null) s.add(new Object[] {ConsoleInfo.START, new int[] {START}});
        if (ConsoleInfo.SELECT != null) s.add(new Object[] {ConsoleInfo.SELECT, new int[] {SELECT}});
        for (int i = 0; i < 4; i++)
            if (ConsoleInfo.BUTTONS[i] != null) s.add(new Object[] {ConsoleInfo.BUTTONS[i], new int[] {B1 + i}});
        if (ConsoleInfo.KEYS == 1)
            for (int d = 0; d < 10; d++) s.add(new Object[] {"Tecla " + d, new int[] {CK_0 + d}});
        return s;
    }

    @Override String defaultPad() { return "pad"; }

    @Override
    int[] padActions(String mode) {
        // up, down, left, right, A, B, X, Y, SELECT, L1, R1 (START: padStart)
        return new int[] {UP, DOWN, LEFT, RIGHT, B1, B2, B3, B4, SELECT, COIN, START};
    }

    @Override int padStart() { return START; }
}
