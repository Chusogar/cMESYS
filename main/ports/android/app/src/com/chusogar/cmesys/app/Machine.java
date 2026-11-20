package com.chusogar.cmesys.app;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.SparseArray;
import android.view.KeyEvent;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** What makes each machine of the port different: its ROMs and models, its
 *  media menu, the keys of a physical keyboard and of a game pad, and the
 *  key presses that type a command. The screen, keyboard, menu and settings
 *  are the same for all (MachineActivity). One APK runs one machine
 *  (MachineId.ID, written by build_apk.sh). */
abstract class Machine {
    static Machine create() {
        switch (MachineId.ID) {
            case "ql": return new QL();
            case "zxspec": return new Spectrum();
            case "cpc": return new Cpc();
            case "c64": return new C64();
            case "msx": return new Msx();
            case "lynx": return new Lynx();
            case "amiga": return new Amiga();
            case "apple2": return new Apple2();
            case "apple2gs": return new Apple2gs();
            case "macplus": return new MacPlus();
            case "macii": return new MacII();
            case "specnext": return new SpecNext();
            case "samcoupe": return new SamCoupe();
            case "exelv": return new Exelv();
            default:
                if (ConsoleInfo.TITLE != null) return new Console();
                throw new IllegalStateException("unknown machine " + MachineId.ID);
        }
    }

    // --- pad actions: a key, or joystick bits on a port ------------------------
    static final int NONE = -1;

    static int key(int code) { return code; }

    static int joy(int port, int bits) { return 0x10000 | port << 8 | bits; }

    static boolean isJoy(int action) { return action >= 0x10000; }

    // the buttons of a pad, in the order of padActions()
    static final int P_UP = 0, P_DOWN = 1, P_LEFT = 2, P_RIGHT = 3, P_A = 4, P_B = 5, P_X = 6, P_Y = 7,
            P_SELECT = 8, P_L1 = 9, P_R1 = 10, P_COUNT = 11;

    static int padButton(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_UP: return P_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return P_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return P_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return P_RIGHT;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: return P_A;
            case KeyEvent.KEYCODE_BUTTON_B: return P_B;
            case KeyEvent.KEYCODE_BUTTON_X: return P_X;
            case KeyEvent.KEYCODE_BUTTON_Y: return P_Y;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return P_SELECT;
            case KeyEvent.KEYCODE_BUTTON_L1: return P_L1;
            case KeyEvent.KEYCODE_BUTTON_R1: return P_R1;
            default: return -1;
        }
    }

    // --- what each machine says -----------------------------------------------------
    /** "Sinclair QL", "ZX Spectrum"... */
    abstract String title();

    /** The setting whose change restarts the machine (its model or ROM). */
    String restartKey() { return "model"; }

    String defaultModel() { return "0"; }

    int model(SharedPreferences p) {
        try {
            return Integer.parseInt(p.getString("model", defaultModel()));
        } catch (NumberFormatException e) {
            return Integer.parseInt(defaultModel());
        }
    }

    /** The ROMs for Core.init (from the APK's assets/roms; null entries: not in the
     *  APK, then the core looks for them in the app's roms folder). */
    abstract byte[][] roms(MachineActivity a, SharedPreferences p, int model) throws IOException;

    /** The folder of the app's roms/ the ROMs of this model go in (MAME's set names). */
    String romSet(int model) {
        switch (MachineId.ID) {
            case "cpc": return "cpc6128";
            default: return MachineId.ID;
        }
    }

    /** Width / height of the picture as the machine's monitor showed it. */
    float displayAspect(int w, int h) { return 4f / 3f; }

    /** Picture rows each machine line takes in whole-multiple scaling (the QL's 256 lines: 2). */
    int lineScale() { return 1; }

    /** The drive a load into this slot fills (the QL's slot 0 boots MDV1). */
    int nameSlot(int slot) { return slot; }

    /** Lights over the picture: status bits LED1, LED2. */
    int leds() { return 0; }

    /** The media menu's label (the bar) and its items. */
    String mediaLabel() { return "Medios"; }

    abstract void mediaMenu(MachineActivity a, List<SoftMenu.Item> items, int status);

    /** The reset menu: a plain reset, and more on some machines. */
    void resetMenu(MachineActivity a, List<SoftMenu.Item> items, int status) {
        items.add(a.item(com.chusogar.cmesys.app.R.drawable.ic_reset, "Reset", a.resetAction()));
    }

    /** The keys menu's label and its shortcuts ({label, int[] keys pressed together}). */
    String keysLabel() { return "Teclas"; }

    abstract List<Object[]> shortcuts();

    /** A file the core uses where it is (a disk image it reads and writes as
     *  it goes) instead of a copy: by default the large ones. */
    boolean inPlace(String name, long size) { return size > (64L << 20); }

    /** The on-screen keyboard of a model (assets: its keyboard.txt). */
    String keyboardFile(int model) { return "keyboard.txt"; }

    /** The help page (assets). */
    String help() { return "help.html"; }

    /** The extensions the media picker shows first (all files can be chosen). */
    abstract String mediaExtensions();

    /** The pad mapping setting's default value. */
    abstract String defaultPad();

    /** The action of each pad button (P_*) for a pad setting. */
    abstract int[] padActions(String mode);

    /** A pad's START: an action of the machine's (the consoles' START), or
     *  NONE: it opens the menu. */
    int padStart() { return NONE; }

    /** The machine's keys for an Android key code (a physical keyboard), or null. */
    int[] keys(int keyCode) {
        return keymap.get(keyCode);
    }

    /** A mouse: touching the picture moves it as a trackpad (Trackpad). */
    boolean usesMouse() { return false; }

    /** The machine's picture width the mouse moves over (pixels). */
    int mouseWidth() { return 320; }

    /** The character ROM for the keyboard picture (the C64's PETSCII fronts). */
    byte[] glyphRom(MachineActivity a) { return null; }

    // --- helpers for the machines --------------------------------------------------
    private final SparseArray<int[]> keymap = new SparseArray<>();
    private final Map<Character, int[]> charmap = new HashMap<>();

    /** An Android key = these keys of the machine. */
    void map(int keyCode, int... codes) { keymap.put(keyCode, codes); }

    /** A..Z and 0..9 (codes in that order). */
    void mapLetters(int[] az, int[] digits09) {
        for (int i = 0; i < 26; i++) {
            map(KeyEvent.KEYCODE_A + i, az[i]);
            chars((char) ('A' + i), az[i]);
            chars((char) ('a' + i), az[i]);
        }
        for (int i = 0; i < 10; i++) {
            map(KeyEvent.KEYCODE_0 + i, digits09[i]);
            chars((char) ('0' + i), digits09[i]);
        }
    }

    /** A character typed = these keys held together. */
    void chars(char c, int... codes) { charmap.put(c, codes); }

    /** A pause in typed text (Emulator.type): after a RETURN that more text
     *  follows, the machine gets time to carry the line out (a LOAD) before
     *  the next one, whose keys it would not read meanwhile. */
    static final int[] TYPE_PAUSE = {-1};

    /** The key presses that type text (unknown characters are left out). */
    int[][] typing(String text) {
        List<int[]> out = new ArrayList<>();
        for (int i = 0; i < text.length(); i++) {
            int[] k = charmap.get(text.charAt(i));
            if (k != null) out.add(k);
            if (text.charAt(i) == '\r' && i + 1 < text.length()) out.add(TYPE_PAUSE);
        }
        return out.toArray(new int[0][]);
    }

    /** A menu item that types text. */
    SoftMenu.Item typeItem(final MachineActivity a, String label, final String text) {
        return a.item(com.chusogar.cmesys.app.R.drawable.ic_keys, label, new Runnable() {
            @Override public void run() { a.type(typing(text)); }
        });
    }

    static byte[] asset(Context c, String name) {
        try (InputStream in = c.getAssets().open(name)) {
            return MachineActivity.readAll(in, 1 << 20);
        } catch (IOException e) {
            return null;
        }
    }

    static boolean hasAsset(Context c, String name) {
        try (InputStream in = c.getAssets().open(name)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
