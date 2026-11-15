package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Atari 2600 / VCS. Its "keys" are the joystick and the console's
 *  buttons (jni/m_a2600.c): up 0, down 1, left 2, right 3, FIRE 4, GAME
 *  RESET 5, GAME SELECT 6, the diagonals 7..10, the second joystick 16..20.
 *  The switches (TV type, difficulties) are commands of the reset menu. */
final class A2600 extends Machine {
    static final int UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3, FIRE = 4, RESET = 5, SELECT = 6;
    static final int P2 = 16;

    A2600() {
        map(KeyEvent.KEYCODE_DPAD_UP, UP);
        map(KeyEvent.KEYCODE_DPAD_DOWN, DOWN);
        map(KeyEvent.KEYCODE_DPAD_LEFT, LEFT);
        map(KeyEvent.KEYCODE_DPAD_RIGHT, RIGHT);
        map(KeyEvent.KEYCODE_SPACE, FIRE);
        map(KeyEvent.KEYCODE_Z, FIRE);
        map(KeyEvent.KEYCODE_CTRL_LEFT, FIRE);
        map(KeyEvent.KEYCODE_F1, RESET);
        map(KeyEvent.KEYCODE_ENTER, RESET);
        map(KeyEvent.KEYCODE_F2, SELECT);
        map(KeyEvent.KEYCODE_TAB, SELECT);
        map(KeyEvent.KEYCODE_W, P2 + UP);
        map(KeyEvent.KEYCODE_S, P2 + DOWN);
        map(KeyEvent.KEYCODE_A, P2 + LEFT);
        map(KeyEvent.KEYCODE_D, P2 + RIGHT);
        map(KeyEvent.KEYCODE_Q, P2 + FIRE);
    }

    @Override String title() { return "Atari 2600"; }

    @Override String restartKey() { return "a2600_model"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[0][];
    }

    @Override float displayAspect(int w, int h) { return 4f / 3f; }

    @Override String mediaLabel() { return "Cartucho"; }

    @Override String mediaExtensions() { return ".a26 .bin .rom"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder, "Abrir cartucho… (.a26 .bin)", a.pickAction(0)));
        String n = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el cartucho" + (n != null ? "  (" + n + ")" : ""),
                n == null ? null : a.ejectAction(0, "Cartucho fuera")));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Apagar y encender", a.resetAction()));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_screen, "TV: color / blanco y negro",
                a.commandAction(Core.CMD_TV_TYPE, "TV cambiada", false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_settings, "Dificultad izquierda A / B",
                a.commandAction(Core.CMD_DIFF_LEFT, "Dificultad izquierda cambiada", false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_settings, "Dificultad derecha A / B",
                a.commandAction(Core.CMD_DIFF_RIGHT, "Dificultad derecha cambiada", false)));
    }

    @Override String keysLabel() { return "Consola"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"GAME RESET", new int[] {RESET}});
        s.add(new Object[] {"GAME SELECT", new int[] {SELECT}});
        return s;
    }

    @Override String defaultPad() { return "pad"; }

    @Override
    int[] padActions(String mode) {
        int port = "pad2".equals(mode) ? 2 : 1;
        return new int[] {joy(port, Core.JOY_UP), joy(port, Core.JOY_DOWN), joy(port, Core.JOY_LEFT), joy(port, Core.JOY_RIGHT),
            joy(port, Core.JOY_FIRE), joy(port, Core.JOY_FIRE), RESET, SELECT, SELECT, RESET, SELECT};
    }
}
