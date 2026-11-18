package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Apple ][, ][+, //e and //e Enhanced (apple2_core.h): the model
 *  setting, a Disk II in slot 6 (drive 1) when the APK has its PROM. Key
 *  codes: the machine's keys (A2K_*: ASCII-like, one per key of the ][ and
 *  //e keyboards); the core's encoder makes the characters. Each model has
 *  its keyboard: the ]['s (keyboard_ii.txt) or the //e's (keyboard_iie.txt). */
final class Apple2 extends Machine {
    static final int CTRL = 0x01, SHIFT = 0x02, RSHIFT = 0x03, CAPS = 0x04, OPEN_APPLE = 0x05, CLOSED_APPLE = 0x06;
    static final int RESET = 0x07, LEFT = 0x08, TAB = 0x09, DOWN = 0x0A, UP = 0x0B, RETURN = 0x0D, REPT = 0x0E;
    static final int RIGHT = 0x15, ESC = 0x1B, SPACE = 0x20, QUOTE = 0x27, COMMA = 0x2C, MINUS = 0x2D, PERIOD = 0x2E;
    static final int SLASH = 0x2F, COLON = 0x3A, SEMICOLON = 0x3B, EQUALS = 0x3D, LBRACKET = 0x5B, BACKSLASH = 0x5C;
    static final int RBRACKET = 0x5D, GRAVE = 0x60, DELETE = 0x7F;

    Apple2() {
        int[] az = new int[26], d = new int[10];
        for (int i = 0; i < 26; i++) az[i] = 'A' + i;
        for (int i = 0; i < 10; i++) d[i] = '0' + i;
        mapLetters(az, d);
        map(KeyEvent.KEYCODE_ENTER, RETURN);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, RETURN);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_DEL, LEFT);                 // (the ]['s backspace: the left arrow)
        map(KeyEvent.KEYCODE_FORWARD_DEL, DELETE);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, RSHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, OPEN_APPLE);
        map(KeyEvent.KEYCODE_ALT_RIGHT, CLOSED_APPLE);
        map(KeyEvent.KEYCODE_META_LEFT, OPEN_APPLE);
        map(KeyEvent.KEYCODE_META_RIGHT, CLOSED_APPLE);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_F12, RESET);
        map(KeyEvent.KEYCODE_BREAK, RESET);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
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
        chars(' ', SPACE);
        chars('\r', RETURN);
        chars(',', COMMA);
        chars('.', PERIOD);
        chars('#', SHIFT, '3');                          // (the same on both keyboards)
        chars('$', SHIFT, '4');
    }

    @Override String title() { return "Apple II"; }

    @Override String defaultModel() { return "3"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String dir = "roms/" + romSet(model) + "/";
        return new byte[][] {asset(a, dir + "main.rom"), asset(a, dir + "chr.rom"), asset(a, dir + "disk.rom"),
            asset(a, dir + "aid.rom")};
    }

    @Override
    String romSet(int model) {
        switch (model) {
            case 0: return "apple2";
            case 1: return "apple2p";
            case 2: return "apple2e";
            default: return "apple2ee";
        }
    }

    @Override String keyboardFile(int model) { return model >= 2 ? "keyboard_iie.txt" : "keyboard_ii.txt"; }

    /** 560 dots by 192 lines on a 4:3 monitor: each line is shown twice. */
    @Override int lineScale() { return 2; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".dsk .do .po .nib"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String n = a.slotName(0);
        if ((status & Core.HAS_DISK) == 0) {
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_file, "(sin la PROM de la Disk II: discos no)", null));
            return;
        }
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (n != null ? "Cambiar el disco… (" + n + ")" : "Meter un disco y arrancarlo… (.dsk .po .nib)"), a.pickAction(0)));
        if (n != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco", a.ejectAction(0, "Unidad vacía")));
        items.add(typeItem(a, "Escribir PR#6 (arrancar el disco)", "PR#6\r"));
        items.add(typeItem(a, "Escribir CATALOG", "CATALOG\r"));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "CTRL-RESET (la RAM se queda)",
                a.commandAction(Core.CMD_RESET_KEY, null, false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Apagar y encender (arranca el disco)", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas Apple"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"CTRL-RESET", new int[] {CTRL, RESET}});
        s.add(new Object[] {"CTRL-C (parar)", new int[] {CTRL, 'C'}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        s.add(new Object[] {"Open Apple + CTRL-RESET (//e: reiniciar)", new int[] {OPEN_APPLE, CTRL, RESET}});
        return s;
    }

    @Override String defaultPad() { return "joy"; }

    @Override
    int[] padActions(String mode) {
        if ("cursors".equals(mode)) {
            return new int[] {UP, DOWN, LEFT, RIGHT, SPACE, RETURN, ESC, 'Y', ESC, 'N', 'Y'};
        }
        return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT), joy(1, Core.JOY_RIGHT),
            joy(1, Core.JOY_FIRE), joy(1, Core.JOY_FIRE2), SPACE, RETURN, ESC, 'Y', 'N'};
    }
}
