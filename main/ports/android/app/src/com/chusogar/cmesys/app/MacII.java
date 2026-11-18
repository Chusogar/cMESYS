package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Macintosh II (macii_core.h): a 68020, 8 MB, the Display Card 8*24
 *  at 640 x 480, the Apple Extended Keyboard and mouse (ADB: the picture is
 *  its trackpad) and a SCSI hard disk (written in place). */
final class MacII extends AdbKeys {
    static final int CONTROL = 0x36, LEFT = 0x3B, RIGHT = 0x3C, DOWN = 0x3D, UP = 0x3E;
    static final int HELP = 0x72, HOME = 0x73, PGUP = 0x74, FWD_DEL = 0x75, END = 0x77, PGDN = 0x79, POWER = 0x7F;
    static final int[] FKEYS = {0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F};

    MacII() {
        super(CONTROL, LEFT, RIGHT, DOWN, UP);
        for (int i = 0; i < 12; i++) map(KeyEvent.KEYCODE_F1 + i, FKEYS[i]);
        map(KeyEvent.KEYCODE_FORWARD_DEL, FWD_DEL);
        map(KeyEvent.KEYCODE_INSERT, HELP);
        map(KeyEvent.KEYCODE_MOVE_HOME, HOME);
        map(KeyEvent.KEYCODE_MOVE_END, END);
        map(KeyEvent.KEYCODE_PAGE_UP, PGUP);
        map(KeyEvent.KEYCODE_PAGE_DOWN, PGDN);
    }

    @Override String title() { return "Macintosh II"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/macii.rom"), asset(a, "roms/card.rom")};
    }

    @Override int mouseWidth() { return 640; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".hda .hdv .img .dsk"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String h = a.slotName(0);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (h != null ? "Cambiar el disco SCSI… (" + h + ")" : "Conectar un disco duro SCSI y arrancar… (.hda .img)"), a.pickAction(0)));
        if (h != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Desconectar el disco SCSI", a.ejectAction(0, "Sin disco SCSI")));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reinicio", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas Mac"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"⌘ + E (expulsar)", new int[] {COMMAND, E}});
        s.add(new Object[] {"⌘ + Q (salir)", new int[] {COMMAND, Q}});
        s.add(new Object[] {"⌘ + W (cerrar)", new int[] {COMMAND, W}});
        s.add(new Object[] {"⌘ + . (cancelar)", new int[] {COMMAND, PERIOD}});
        s.add(new Object[] {"Encendido (tecla ◁)", new int[] {POWER}});
        s.add(new Object[] {"Control + ⌘ + Encendido (reinicio)", new int[] {CONTROL, COMMAND, POWER}});
        return s;
    }
}
