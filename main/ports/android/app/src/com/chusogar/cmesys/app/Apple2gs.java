package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Apple IIGS (ROM 3, apple2gs_core.h) with 4 MB of RAM, its ADB
 *  keyboard and mouse (the picture is its trackpad) and four SmartPort
 *  disks (slot 0 boots: 3.5" .po / .2mg / .hdv images, ProDOS order). */
final class Apple2gs extends AdbKeys {
    static final int CTRL = 0x36, LEFT = 0x3B, RIGHT = 0x3C, DOWN = 0x3D, UP = 0x3E, RESET = 0x7F;

    Apple2gs() {
        super(CTRL, LEFT, RIGHT, DOWN, UP);
        map(KeyEvent.KEYCODE_F12, RESET);
        map(KeyEvent.KEYCODE_BREAK, RESET);
        map(KeyEvent.KEYCODE_FORWARD_DEL, DELETE);
    }

    @Override String title() { return "Apple IIGS"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/rom_fc.rom"), asset(a, "roms/rom_fe.rom"), asset(a, "roms/chr.rom")};
    }

    @Override int lineScale() { return 2; }

    @Override int mouseWidth() { return 640; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".po .2mg .hdv .dsk"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        for (int u = 0; u < 4; u++) {
            String n = a.slotName(u);
            String what = u == 0 ? "el disco de arranque (reinicia)" : "el disco " + (u + 1);
            items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                    (n != null ? "Cambiar " + what + "… (" + n + ")" : "Meter " + what + "… (.po .2mg .hdv)"), a.pickAction(u)));
        }
        for (int u = 0; u < 4; u++) {
            if (a.slotName(u) != null) {
                items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Sacar el disco " + (u + 1),
                        a.ejectAction(u, "Disco " + (u + 1) + " fuera")));
            }
        }
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "CTRL-RESET",
                a.commandAction(Core.CMD_RESET_KEY, null, false)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Apagar y encender", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas IIGS"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"CTRL-RESET", new int[] {CTRL, RESET}});
        s.add(new Object[] {"Panel de control (CTRL + Apple + ESC)", new int[] {CTRL, COMMAND, ESC}});
        s.add(new Object[] {"Reinicio (CTRL + Apple + RESET)", new int[] {CTRL, COMMAND, RESET}});
        s.add(new Object[] {"Apple + Q (salir)", new int[] {COMMAND, Q}});
        s.add(new Object[] {"ESC", new int[] {ESC}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        return s;
    }
}
