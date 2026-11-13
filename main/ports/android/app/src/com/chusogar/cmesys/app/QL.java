package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.net.Uri;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Sinclair QL: its system ROM (JS, JM, Minerva or one of the phone's
 *  files) and the microdrive cartridges (QLAY .mdv). Key codes: the IPC's
 *  matrix row * 8 + bit (ql_core.h). */
final class QL extends Machine {
    static int k(int row, int bit) { return (row << 3) | bit; }

    static final int F4 = k(0, 0), F1 = k(0, 1), N5 = k(0, 2), F2 = k(0, 3), F3 = k(0, 4), F5 = k(0, 5), N4 = k(0, 6), N7 = k(0, 7);
    static final int ENTER = k(1, 0), LEFT = k(1, 1), UP = k(1, 2), ESC = k(1, 3), RIGHT = k(1, 4), BACKSLASH = k(1, 5), SPACE = k(1, 6), DOWN = k(1, 7);
    static final int RBRACKET = k(2, 0), Z = k(2, 1), PERIOD = k(2, 2), C = k(2, 3), B = k(2, 4), POUND = k(2, 5), M = k(2, 6), QUOTE = k(2, 7);
    static final int LBRACKET = k(3, 0), CAPS = k(3, 1), K = k(3, 2), S = k(3, 3), F = k(3, 4), EQUALS = k(3, 5), G = k(3, 6), SEMICOLON = k(3, 7);
    static final int L = k(4, 0), N3 = k(4, 1), H = k(4, 2), N1 = k(4, 3), A = k(4, 4), P = k(4, 5), D = k(4, 6), J = k(4, 7);
    static final int N9 = k(5, 0), W = k(5, 1), I = k(5, 2), TAB = k(5, 3), R = k(5, 4), MINUS = k(5, 5), Y = k(5, 6), O = k(5, 7);
    static final int N8 = k(6, 0), N2 = k(6, 1), N6 = k(6, 2), Q = k(6, 3), E = k(6, 4), N0 = k(6, 5), T = k(6, 6), U = k(6, 7);
    static final int SHIFT = k(7, 0), CTRL = k(7, 1), ALT = k(7, 2), X = k(7, 3), V = k(7, 4), SLASH = k(7, 5), N = k(7, 6), COMMA = k(7, 7);

    static final int ROM_BYTES = 0xc000, MDV_BYTES = 255 * 686;

    QL() {
        mapLetters(new int[] {A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z},
                new int[] {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9});
        map(KeyEvent.KEYCODE_ENTER, ENTER);
        map(KeyEvent.KEYCODE_NUMPAD_ENTER, ENTER);
        map(KeyEvent.KEYCODE_SPACE, SPACE);
        map(KeyEvent.KEYCODE_ESCAPE, ESC);
        map(KeyEvent.KEYCODE_TAB, TAB);
        map(KeyEvent.KEYCODE_CAPS_LOCK, CAPS);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_SHIFT_LEFT, SHIFT);
        map(KeyEvent.KEYCODE_SHIFT_RIGHT, SHIFT);
        map(KeyEvent.KEYCODE_CTRL_LEFT, CTRL);
        map(KeyEvent.KEYCODE_CTRL_RIGHT, CTRL);
        map(KeyEvent.KEYCODE_ALT_LEFT, ALT);
        map(KeyEvent.KEYCODE_ALT_RIGHT, ALT);
        map(KeyEvent.KEYCODE_MINUS, MINUS);
        map(KeyEvent.KEYCODE_EQUALS, EQUALS);
        map(KeyEvent.KEYCODE_LEFT_BRACKET, LBRACKET);
        map(KeyEvent.KEYCODE_RIGHT_BRACKET, RBRACKET);
        map(KeyEvent.KEYCODE_SEMICOLON, SEMICOLON);
        map(KeyEvent.KEYCODE_APOSTROPHE, QUOTE);
        map(KeyEvent.KEYCODE_BACKSLASH, BACKSLASH);
        map(KeyEvent.KEYCODE_GRAVE, POUND);
        map(KeyEvent.KEYCODE_COMMA, COMMA);
        map(KeyEvent.KEYCODE_PERIOD, PERIOD);
        map(KeyEvent.KEYCODE_SLASH, SLASH);
        map(KeyEvent.KEYCODE_F1, F1);
        map(KeyEvent.KEYCODE_F2, F2);
        map(KeyEvent.KEYCODE_F3, F3);
        map(KeyEvent.KEYCODE_F4, F4);
        map(KeyEvent.KEYCODE_F5, F5);
        map(KeyEvent.KEYCODE_DEL, CTRL, LEFT);          // Backspace = CTRL+LEFT
        map(KeyEvent.KEYCODE_FORWARD_DEL, CTRL, RIGHT);
    }

    @Override String title() { return "Sinclair QL"; }

    @Override String restartKey() { return "rom"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        String choice = p.getString("rom", "js");
        byte[] rom = null;
        if ("custom".equals(choice)) {
            String uri = p.getString("rom_custom_uri", null);
            if (uri != null) {
                try {
                    rom = a.readUri(Uri.parse(uri), ROM_BYTES);
                } catch (IOException | SecurityException e) {
                    rom = null;
                }
            }
        } else {
            rom = asset(a, "roms/" + choice + ".rom");
        }
        if (rom == null || rom.length != ROM_BYTES) {
            a.toast("ROM elegida no disponible: se usa la JS");
            rom = asset(a, "roms/js.rom");
        }
        byte[] ipc = asset(a, "roms/ipc.rom");
        return rom == null || ipc == null ? null : new byte[][] {rom, ipc};
    }

    @Override int lineScale() { return 2; }

    @Override int nameSlot(int slot) { return slot == 0 ? 1 : slot; }

    @Override int leds() { return 2; }

    @Override String mediaLabel() { return "Microdrive"; }

    @Override String mediaExtensions() { return ".mdv"; }

    @Override
    void mediaMenu(final MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv1, "Cargar en MDV1 y arrancar…", a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv1, "Insertar en MDV1…", a.pickAction(1)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv2, "Insertar en MDV2…", a.pickAction(2)));
        if (hasAsset(a, "cartridge.mdv")) {
            byte[] t = asset(a, "cartridge.txt");
            String name = t != null ? new String(t).trim() : "cartucho";
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_firmware, "Cartucho incluido: " + name,
                    a.loadAssetAction("cartridge.mdv", name + ".mdv", 0)));
        }
        for (int d = 1; d <= 2; d++) {
            String n = a.slotName(d);
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar MDV" + d + "  (" + (n != null ? n : "vacío") + ")",
                    n == null ? null : a.ejectAction(d, "MDV" + d + " vacío")));
        }
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reset en frío", a.resetAction()));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_mdv1, "Reset y arrancar MDV1 (F1)",
                a.slotName(1) == null ? null : a.commandAction(Core.CMD_AUTOBOOT, null, true)));
    }

    @Override String keysLabel() { return "Teclas QL"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"F1", new int[] {F1}});
        s.add(new Object[] {"F2", new int[] {F2}});
        s.add(new Object[] {"F3", new int[] {F3}});
        s.add(new Object[] {"F4", new int[] {F4}});
        s.add(new Object[] {"F5", new int[] {F5}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"CTRL+ESPACIO (Break)", new int[] {CTRL, SPACE}});
        s.add(new Object[] {"CTRL+C (cambiar de trabajo)", new int[] {CTRL, C}});
        s.add(new Object[] {"CTRL+F5 (congelar pantalla)", new int[] {CTRL, F5}});
        s.add(new Object[] {"CTRL+ALT+7 (reset en Minerva)", new int[] {CTRL, ALT, N7}});
        return s;
    }

    @Override String defaultPad() { return "ctl1"; }

    /** "ctl1": cursors + SPACE; "ctl2": the QL's second joystick port (F1
     *  left, F3 right, F4 up, F2 down, F5 fire). */
    @Override
    int[] padActions(String mode) {
        boolean ctl2 = "ctl2".equals(mode);
        return new int[] {
            ctl2 ? F4 : UP, ctl2 ? F2 : DOWN, ctl2 ? F1 : LEFT, ctl2 ? F3 : RIGHT,
            ctl2 ? F5 : SPACE, ENTER, ctl2 ? F5 : SPACE, ENTER, ESC, NONE, NONE,
        };
    }
}
