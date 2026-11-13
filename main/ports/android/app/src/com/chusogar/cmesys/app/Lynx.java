package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Atari Lynx. Its "keys" are the console's buttons (bit numbers of
 *  lynx_core.h) and the cross's diagonals (9..12, jni/m_lynx.c). */
final class Lynx extends Machine {
    static final int A = 0, B = 1, OPT2 = 2, OPT1 = 3, RIGHT = 4, LEFT = 5, DOWN = 6, UP = 7, PAUSE = 8;

    Lynx() {
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_X, A);
        map(KeyEvent.KEYCODE_Z, B);
        map(KeyEvent.KEYCODE_1, OPT1);
        map(KeyEvent.KEYCODE_2, OPT2);
        map(KeyEvent.KEYCODE_ENTER, PAUSE);
        map(KeyEvent.KEYCODE_P, PAUSE);
    }

    @Override String title() { return "Atari Lynx"; }

    @Override String restartKey() { return "lynx_model"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/boot.rom")};
    }

    @Override float displayAspect(int w, int h) { return w / (float) h; }

    @Override String mediaLabel() { return "Cartucho"; }

    @Override String mediaExtensions() { return ".lnx .lyx .o"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir cartucho… (.lnx)", a.pickAction(0)));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el cartucho" + (n != null ? "  (" + n + ")" : ""),
                n == null ? null : a.ejectAction(0, "Cartucho fuera")));
    }

    @Override String keysLabel() { return "Botones Lynx"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"PAUSE", new int[] {PAUSE}});
        s.add(new Object[] {"OPTION 1", new int[] {OPT1}});
        s.add(new Object[] {"OPTION 2", new int[] {OPT2}});
        s.add(new Object[] {"PAUSE + OPTION 1 (reiniciar el juego)", new int[] {PAUSE, OPT1}});
        s.add(new Object[] {"PAUSE + OPTION 2 (girar la pantalla)", new int[] {PAUSE, OPT2}});
        return s;
    }

    @Override String defaultPad() { return "pad"; }

    @Override
    int[] padActions(String mode) {
        return new int[] {joy(1, Core.JOY_UP), joy(1, Core.JOY_DOWN), joy(1, Core.JOY_LEFT), joy(1, Core.JOY_RIGHT),
            joy(1, Core.JOY_FIRE), joy(1, Core.JOY_FIRE2), A, B, PAUSE, OPT1, OPT2};
    }
}
