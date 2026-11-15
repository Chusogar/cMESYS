package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Atari 800XL (and the 400/800). Keys are POKEY's scan codes
 *  (atari8_core.h) and jni/m_atari8.c's SHIFT 64, CONTROL 65, BREAK 66,
 *  START 67, SELECT 68, OPTION 69. */
final class Atari8 extends Machine {
    static final int SHIFT = 64, CONTROL = 65, BREAK = 66, START = 67, SELECT = 68, OPTION = 69;
    static final int RETURN = 0x0c, SPACE = 0x21, ESC = 0x1c;

    Atari8() {
        int[] az = {0x3f, 0x15, 0x12, 0x3a, 0x2a, 0x38, 0x3d, 0x39, 0x0d, 0x01, 0x05, 0x00, 0x25,
                    0x23, 0x08, 0x0a, 0x2f, 0x28, 0x3e, 0x2d, 0x0b, 0x10, 0x2e, 0x16, 0x2b, 0x17};
        int[] digits = {0x32, 0x1f, 0x1e, 0x1a, 0x18, 0x1d, 0x1b, 0x33, 0x35, 0x30};
        mapLetters(az, digits);
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, 0x2c);
        map(KeyEvent.KEYCODE_DEL, 0x34);
        map(KeyEvent.KEYCODE_FORWARD_DEL, 0x34);
        map(KeyEvent.KEYCODE_CAPS_LOCK, 0x3c);
        map(KeyEvent.KEYCODE_SEMICOLON, 0x02);
        map(KeyEvent.KEYCODE_EQUALS, 0x0f);
        map(KeyEvent.KEYCODE_MINUS, 0x0e);
        map(KeyEvent.KEYCODE_COMMA, 0x20);
        map(KeyEvent.KEYCODE_PERIOD, 0x22);
        map(KeyEvent.KEYCODE_SLASH, 0x26);
        map(KeyEvent.KEYCODE_PLUS, 0x06);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, 0x06);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, 0x07);
        map(KeyEvent.KEYCODE_STAR, 0x07);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CONTROL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CONTROL);
        map(KeyEvent.KEYCODE_F2, START);
        map(KeyEvent.KEYCODE_F3, SELECT);
        map(KeyEvent.KEYCODE_F4, OPTION);
        map(KeyEvent.KEYCODE_F6, BREAK);
        map(KeyEvent.KEYCODE_F7, 0x27);
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars('"', SHIFT, 0x1e);
        chars('*', 0x07);
        chars('+', 0x06);
        chars('-', 0x0e);
        chars('=', 0x0f);
        chars(',', 0x20);
        chars('.', 0x22);
        chars('/', 0x26);
        chars(';', 0x02);
        chars(':', SHIFT, 0x02);
        chars('(', SHIFT, 0x30);
        chars(')', SHIFT, 0x32);
        chars('!', SHIFT, 0x1f);
        chars('?', SHIFT, 0x26);
    }

    @Override String title() { return "Atari 800XL"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        if (model == 1) {
            byte[] os = asset(a, "roms/os800.rom");
            return os == null ? null : new byte[][] {os};
        }
        byte[] os = asset(a, "roms/osxl.rom");
        return os == null ? null : new byte[][] {os, asset(a, "roms/basic.rom")};
    }

    @Override String mediaLabel() { return "Disco"; }

    @Override String mediaExtensions() { return ".atr"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir disco… (.atr: arranca)", a.pickAction(0)));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco (BASIC)" + (n != null ? "  (" + n + ")" : ""),
                n == null ? null : a.ejectAction(0, "Disco fuera")));
    }

    @Override String keysLabel() { return "Teclas Atari"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"START", new int[] {START}});
        s.add(new Object[] {"SELECT", new int[] {SELECT}});
        s.add(new Object[] {"OPTION", new int[] {OPTION}});
        s.add(new Object[] {"BREAK", new int[] {BREAK}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        return s;
    }

    @Override String defaultPad() { return "joy1"; }

    @Override
    int[] padActions(String mode) {
        int port = "joy2".equals(mode) ? 2 : 1;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT), joy(port, Core.JOY_RIGHT),
            joy(port, Core.JOY_FIRE), joy(port, Core.JOY_FIRE), SPACE, RETURN, SELECT, START, OPTION};
    }
}
