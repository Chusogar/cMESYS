package com.chusogar.cmesys.app;

import android.content.SharedPreferences;
import android.view.KeyEvent;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** The Macintosh Plus (macplus_core.h): 4 MB, its M0110A keyboard and
 *  mouse (the picture is its trackpad), a floppy drive (400K / 800K / 1.44
 *  MB .dsk images, Disk Copy 4.2) and a SCSI hard disk (larger images:
 *  .hda / .hdv / .img, written in place). Key codes: the Mac's. */
final class MacPlus extends AdbKeys {
    static final int CONTROL = 0x3B, LEFT = 0x7B, RIGHT = 0x7C, DOWN = 0x7D, UP = 0x7E;

    MacPlus() {
        super(-1, LEFT, RIGHT, DOWN, UP);
        map(KeyEvent.KEYCODE_CTRL_LEFT, COMMAND);     // (the Plus has no Control key: Command)
        map(KeyEvent.KEYCODE_CTRL_RIGHT, COMMAND);
    }

    @Override String title() { return "Macintosh Plus"; }

    @Override
    byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException {
        return new byte[][] {asset(a, "roms/rom_even.rom"), asset(a, "roms/rom_odd.rom")};
    }

    /** Square dots. */
    @Override float displayAspect(int w, int h) { return w / (float) h; }

    @Override int mouseWidth() { return 512; }

    @Override int leds() { return 1; }

    @Override String mediaLabel() { return "Discos"; }

    @Override String mediaExtensions() { return ".dsk .image .img .dc42 .hda .hdv"; }

    @Override
    void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        String f = a.slotName(0), h = a.slotName(1);
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (f != null ? "Cambiar el disquete… (" + f + ")" : "Meter un disquete… (.dsk, 400K / 800K / 1,44 MB)"), a.pickAction(0)));
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_folder,
                (h != null ? "Cambiar el disco SCSI… (" + h + ")" : "Conectar un disco duro SCSI… (.hda .img)"), a.pickAction(1)));
        if (f != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Expulsar el disquete", a.ejectAction(0, "Disquetera vacía")));
        if (h != null) items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_eject, "Desconectar el disco SCSI", a.ejectAction(1, "Sin disco SCSI")));
    }

    @Override
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reinicio (botón de reset)", a.resetAction()));
    }

    @Override String keysLabel() { return "Teclas Mac"; }

    @Override
    List<Object[]> shortcuts() {
        List<Object[]> s = new ArrayList<>();
        s.add(new Object[] {"⌘ + E (expulsar)", new int[] {COMMAND, E}});
        s.add(new Object[] {"⌘ + Q (salir)", new int[] {COMMAND, Q}});
        s.add(new Object[] {"⌘ + W (cerrar)", new int[] {COMMAND, W}});
        s.add(new Object[] {"⌘ + . (cancelar)", new int[] {COMMAND, PERIOD}});
        s.add(new Object[] {"⌘ + ⇧ + 1 (expulsar el disquete)", new int[] {COMMAND, SHIFT, N1}});
        s.add(new Object[] {"RETURN", new int[] {RETURN}});
        return s;
    }
}
