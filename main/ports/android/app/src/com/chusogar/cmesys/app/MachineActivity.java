package com.chusogar.cmesys.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** The machine: its screen, the on-screen keyboard and the menu (laid out
 *  after Hataroid: the menu bar opens with BACK or the corner button and
 *  pauses the machine; the media come from the phone's files). The same for
 *  every machine of the port; what differs is in Machine. */
public final class MachineActivity extends Activity implements KeyboardView.Listener, SoftMenu.Listener {
    static final String PREF_RECENT = "recent_media";
    private static final int MAX_RECENT = 10;
    private static final int PICK = 0x100;            // + slot

    private Machine machine;
    private Emulator emu;
    private ScreenView view;
    private KeyboardView keyboard;
    private SoftMenu menu;
    private FrameLayout root;
    private LinearLayout cornerButtons;
    SharedPreferences prefs;

    private boolean keyboardShown;
    private String startedWith = "";            // the model / ROM the machine was started with
    private int model;
    private final String[] slotName = new String[4];
    private int[] padActions;
    private boolean menuPausedIt;

    // --- life cycle -------------------------------------------------------
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        PreferenceManager.setDefaultValues(this, R.xml.machine, false);
        PreferenceManager.setDefaultValues(this, R.xml.preferences, true);
        prefs = PreferenceManager.getDefaultSharedPreferences(this);
        machine = Machine.create();

        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new ScreenView(this, machine);
        root.addView(view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        keyboard = new KeyboardView(this, machine, machine.glyphRom(this));
        keyboard.setListener(this);
        keyboard.setVisibility(View.GONE);
        root.addView(keyboard, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));
        keyboard.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
            @Override public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                updateInset();
            }
        });

        // The corner buttons: the keyboard and the menu (as Hataroid's on-screen icons)
        cornerButtons = new LinearLayout(this);
        cornerButtons.setOrientation(LinearLayout.HORIZONTAL);
        cornerButtons.addView(cornerButton(R.drawable.ic_keyboard, new View.OnClickListener() {
            @Override public void onClick(View v) { setKeyboardShown(!keyboardShown); }
        }));
        cornerButtons.addView(cornerButton(R.drawable.ic_settings, new View.OnClickListener() {
            @Override public void onClick(View v) { showMenu(); }
        }));
        root.addView(cornerButtons, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.RIGHT));
        setContentView(root);
        // System bars that stay on screen (navigation bar): the keyboard and the
        // picture go above them
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                v.setPadding(in.getSystemWindowInsetLeft(), 0, in.getSystemWindowInsetRight(),
                        in.getSystemWindowInsetBottom());
                return in.consumeSystemWindowInsets();
            }
        });

        menu = new SoftMenu(this);
        menu.setListener(this);
        emu = new Emulator(view);
        applySettings();
        emu.start();
        startMachine(state == null && !opening(getIntent()));
        setKeyboardShown(prefs.getBoolean("keyboard_start", true));
        if (state == null) openIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        openIntent(intent);
    }

    private static boolean opening(Intent i) {
        return i != null && Intent.ACTION_VIEW.equals(i.getAction()) && i.getData() != null;
    }

    /** "Open with" from a file manager: the file into the first slot. */
    private void openIntent(Intent i) {
        if (opening(i)) loadUri(i.getData(), 0);
    }

    private ImageButton cornerButton(int icon, View.OnClickListener l) {
        ImageButton b = new ImageButton(this);
        b.setImageResource(icon);
        b.setBackgroundColor(Color.TRANSPARENT);
        b.setAlpha(0.55f);
        int p = (int) (6 * getResources().getDisplayMetrics().density);
        b.setPadding(p, p, p, p);
        b.setOnClickListener(l);
        b.setFocusable(false);
        return b;
    }

    @Override
    protected void onResume() {
        super.onResume();
        immersive();
        applySettings();
        if (!restartChoice().equals(startedWith)) {
            startMachine(false);
            toast("Máquina reiniciada con la nueva configuración");
        }
        if (!menu.isShowing()) emu.setSuspended(false);
    }

    @Override
    protected void onPause() {
        super.onPause();
        keyboard.releaseAll();
        emu.releaseAll();
        emu.setSuspended(true);
    }

    @Override
    protected void onDestroy() {
        menu.dismiss();
        emu.stop();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive();
    }

    @Override
    public void onConfigurationChanged(Configuration c) {
        super.onConfigurationChanged(c);
        updateInset();
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    // --- the machine ------------------------------------------------------------
    private String restartChoice() {
        return machine.restartKey() + "=" + prefs.getString(machine.restartKey(), "") + "/"
                + prefs.getString("rom_custom_uri", "");
    }

    int model() { return model; }

    /** (Re)starts the machine with the model and ROMs of the settings. */
    private void startMachine(boolean coldStart) {
        startedWith = restartChoice();
        model = machine.model(prefs);
        final byte[][] roms;
        try {
            roms = machine.roms(this, prefs, model);
        } catch (IOException e) {
            toast("No se pueden leer las ROM: " + e.getMessage());
            return;
        }
        if (roms == null) {
            toast("Faltan las ROM de este modelo en la app");
            return;
        }
        final int m = model;
        final boolean fastLoad = prefs.getBoolean("fast_load", true);
        java.util.Arrays.fill(slotName, null);
        emu.setStarted(false);
        emu.post(new Runnable() {
            @Override public void run() {
                boolean ok = Core.init(m, roms, System.currentTimeMillis() / 1000);
                if (ok) {
                    Core.command(fastLoad ? Core.CMD_FAST_LOAD_ON : Core.CMD_FAST_LOAD_OFF);
                    Core.turbo(emu.isTurbo());
                }
                emu.setStarted(ok);
                if (!ok) {
                    runOnUiThread(new Runnable() {
                        @Override public void run() { toast("Las ROM no son válidas para este modelo"); }
                    });
                }
            }
        });
        if (coldStart && prefs.getBoolean("autoload_last", false)) {
            List<String[]> r = recent();
            if (!r.isEmpty()) loadUri(Uri.parse(r.get(0)[0]), 0);
        }
    }

    private void applySettings() {
        view.setScale(prefs.getString("scale", ScreenView.SCALE_FIT));
        view.setSmooth(prefs.getBoolean("smooth", true));
        view.setOverlay(prefs.getBoolean("show_fps", false), prefs.getBoolean("show_leds", true));
        keyboard.setHaptic(prefs.getBoolean("haptic", true));
        keyboard.setAlpha(Integer.parseInt(prefs.getString("keyboard_alpha", "85")) / 100f);
        cornerButtons.setVisibility(prefs.getBoolean("corner_buttons", true) ? View.VISIBLE : View.GONE);
        emu.setSound(prefs.getBoolean("sound", true));
        padActions = machine.padActions(prefs.getString("pad", machine.defaultPad()));
        updateInset();
    }

    // --- keyboard -----------------------------------------------------------
    private void setKeyboardShown(boolean shown) {
        keyboardShown = shown;
        if (!shown) keyboard.releaseAll();
        keyboard.setVisibility(shown ? View.VISIBLE : View.GONE);
        updateInset();
    }

    /** Portrait, or the overlay setting off: the picture goes above the keyboard. */
    private void updateInset() {
        boolean portrait = getResources().getConfiguration().orientation == Configuration.ORIENTATION_PORTRAIT;
        boolean overlay = !portrait && prefs.getBoolean("keyboard_overlay", false);
        view.setBottomInset(keyboardShown && !overlay ? keyboard.getHeight() : 0);
    }

    @Override
    public void onMachineKey(int code, boolean down) {
        if (down) emu.keyDown(code);
        else emu.keyUp(code);
    }

    @Override
    public void onKeyboardAction(int action) {
        if (action == KeyboardView.ACTION_MENU) showMenu();
        else if (action == KeyboardView.ACTION_HIDE) setKeyboardShown(false);
    }

    /** Types: chords of keys pressed in turn (Machine.typing). */
    void type(int[][] chords) { emu.type(chords); }

    private void padAction(int action, boolean down) {
        if (action == Machine.NONE) return;
        if (Machine.isJoy(action)) emu.joy((action >> 8) & 0xff, action & 0xff, down);
        else onMachineKey(action, down);
    }

    // Physical keyboards and game pads
    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int code = e.getKeyCode();
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        if (e.getAction() == KeyEvent.ACTION_MULTIPLE) return super.dispatchKeyEvent(e);
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_BUTTON_START
                || code == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (!down && e.getRepeatCount() == 0) {
                if (menu.isShowing()) menu.dismiss();
                else showMenu();
            }
            return true;
        }
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN
                || code == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return super.dispatchKeyEvent(e);
        }
        if (down && e.getRepeatCount() > 0) return true;   // the machines repeat keys themselves
        int src = e.getSource();
        boolean pad = (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (pad) {
            int b = Machine.padButton(code);
            if (b >= 0 && padActions != null) {
                padAction(padActions[b], down);
                return true;
            }
        }
        int[] keys = machine.keys(code);
        if (keys != null) {
            for (int k : keys) onMachineKey(k, down);
            return true;
        }
        return super.dispatchKeyEvent(e);
    }

    // Pads whose cross and sticks are axes (HAT_X/Y, X/Y): held directions
    private final boolean[] axisDir = new boolean[4];   // up, down, left, right

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        int src = e.getSource();
        if ((src & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE) {
            return super.onGenericMotionEvent(e);
        }
        float x = e.getAxisValue(MotionEvent.AXIS_HAT_X), y = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (Math.abs(x) < 0.5f) x = e.getAxisValue(MotionEvent.AXIS_X);
        if (Math.abs(y) < 0.5f) y = e.getAxisValue(MotionEvent.AXIS_Y);
        boolean[] now = {y < -0.5f, y > 0.5f, x < -0.5f, x > 0.5f};
        for (int i = 0; i < 4; i++) {
            if (now[i] != axisDir[i]) {
                axisDir[i] = now[i];
                if (padActions != null) padAction(padActions[Machine.P_UP + i], now[i]);
            }
        }
        return true;
    }

    // --- the menu -----------------------------------------------------------
    SoftMenu.Item item(int icon, String text, Runnable action) {
        return new SoftMenu.Item(icon, text, action);
    }

    private void showMenu() {
        if (menu.isShowing()) return;
        keyboard.releaseAll();
        emu.releaseAll();
        menuPausedIt = true;
        emu.setSuspended(true);       // (as Hataroid: the machine waits while the menu is open)
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(item(R.drawable.ic_mdv, machine.mediaLabel(), new Runnable() {
            @Override public void run() { showList(machine.mediaLabel(), MENU_MEDIA); }
        }));
        items.add(item(R.drawable.ic_reset, "Reset", new Runnable() {
            @Override public void run() { showList("Reset", MENU_RESET); }
        }));
        items.add(item(R.drawable.ic_pause, emu.isPaused() ? "Continuar" : "Pausa", new Runnable() {
            @Override public void run() {
                emu.setPaused(!emu.isPaused());
                toast(emu.isPaused() ? "En pausa" : "En marcha");
            }
        }));
        items.add(item(R.drawable.ic_settings, "Ajustes", new Runnable() {
            @Override public void run() { startActivity(new Intent(MachineActivity.this, SettingsActivity.class)); }
        }));
        items.add(item(R.drawable.ic_quit, "Salir", new Runnable() {
            @Override public void run() { finish(); }
        }));
        items.add(item(R.drawable.ic_help, "Ayuda", new Runnable() {
            @Override public void run() { startActivity(new Intent(MachineActivity.this, HelpActivity.class)); }
        }));
        items.add(item(R.drawable.ic_keyboard, "Teclado", new Runnable() {
            @Override public void run() { setKeyboardShown(!keyboardShown); }
        }));
        items.add(item(emu.isTurbo() ? R.drawable.ic_turbo : R.drawable.ic_speed,
                emu.isTurbo() ? "Turbo" : "Velocidad normal", new Runnable() {
                    @Override public void run() {
                        emu.setTurbo(!emu.isTurbo());
                        toast(emu.isTurbo() ? "Turbo activado" : "Velocidad normal");
                    }
                }));
        items.add(item(R.drawable.ic_screen, "Pantalla", new Runnable() {
            @Override public void run() { showScreenMenu(); }
        }));
        items.add(item(R.drawable.ic_keys, machine.keysLabel(), new Runnable() {
            @Override public void run() { showKeysMenu(); }
        }));
        items.add(item(R.drawable.ic_folder, "Recientes", new Runnable() {
            @Override public void run() { showRecentMenu(); }
        }));
        items.add(item(R.drawable.ic_eject, "Expulsar", new Runnable() {
            @Override public void run() { showList("Expulsar", MENU_EJECT); }
        }));
        menu.show(root, items);
    }

    private static final int MENU_MEDIA = 0, MENU_RESET = 1, MENU_EJECT = 2;

    private void showList(String title, int which) {
        List<SoftMenu.Item> items = new ArrayList<>();
        int status = emu.status();
        if (which == MENU_MEDIA) machine.mediaMenu(this, items, status);
        else if (which == MENU_RESET) machine.resetMenu(this, items, status);
        else ejectMenu(items, status);
        menu.showList(root, title, items);
    }

    /** Expulsar: the machine's eject items of its media menu (all of them
     *  with "Expulsar" / "Sacar"), or a note. */
    private void ejectMenu(List<SoftMenu.Item> items, int status) {
        List<SoftMenu.Item> all = new ArrayList<>();
        machine.mediaMenu(this, all, status);
        for (SoftMenu.Item it : all) {
            if (it.icon == R.drawable.ic_eject) items.add(it);
        }
        if ((status & Core.TAPE_PLAYING) != 0) {
            items.add(item(R.drawable.ic_eject, "Parar la cinta", commandAction(Core.CMD_TAPE_TOGGLE, null, false)));
        }
        if (items.isEmpty()) items.add(item(R.drawable.ic_eject, "(nada que expulsar: se cambia al abrir otro)", null));
    }

    @Override
    public void onMenuClosed() {
        if (menuPausedIt) {
            menuPausedIt = false;
            emu.setSuspended(false);
        }
        immersive();
    }

    Runnable resetAction() {
        return new Runnable() {
            @Override public void run() {
                emu.post(new Runnable() { @Override public void run() { Core.reset(); } });
            }
        };
    }

    /** A Core.command; after = a reset first (the QL's boot of MDV1). */
    Runnable commandAction(final int cmd, final String note, final boolean resetFirst) {
        return new Runnable() {
            @Override public void run() {
                emu.post(new Runnable() {
                    @Override public void run() {
                        if (resetFirst) Core.reset();
                        Core.command(cmd);
                    }
                });
                if (note != null) toast(note);
            }
        };
    }

    Runnable ejectAction(final int slot, final String note) {
        return new Runnable() {
            @Override public void run() {
                emu.post(new Runnable() { @Override public void run() { Core.eject(slot); } });
                slotName[machine.nameSlot(slot)] = null;
                toast(note);
            }
        };
    }

    /** The name of what is in a drive / slot (null: empty). */
    String slotName(int slot) {
        return slot >= 0 && slot < slotName.length ? slotName[slot] : null;
    }

    private void showScreenMenu() {
        final String[] values = getResources().getStringArray(R.array.scale_values);
        final String[] names = getResources().getStringArray(R.array.scale_entries);
        String current = prefs.getString("scale", ScreenView.SCALE_FIT);
        List<SoftMenu.Item> items = new ArrayList<>();
        for (int i = 0; i < values.length; i++) {
            final String v = values[i];
            items.add(item(R.drawable.ic_screen, (v.equals(current) ? "• " : "") + names[i], new Runnable() {
                @Override public void run() {
                    prefs.edit().putString("scale", v).apply();
                    applySettings();
                }
            }));
        }
        final boolean smooth = prefs.getBoolean("smooth", true);
        items.add(item(R.drawable.ic_screen, smooth ? "Suavizado: desactivar" : "Suavizado: activar", new Runnable() {
            @Override public void run() {
                prefs.edit().putBoolean("smooth", !smooth).apply();
                applySettings();
            }
        }));
        menu.showList(root, "Pantalla", items);
    }

    /** The machine's keys a phone keyboard lacks, pressed together for a moment. */
    private void showKeysMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        for (final Object[] s : machine.shortcuts()) {
            items.add(item(R.drawable.ic_keys, (String) s[0], new Runnable() {
                @Override public void run() { emu.tap((int[]) s[1]); }
            }));
        }
        menu.showList(root, machine.keysLabel(), items);
    }

    private void showRecentMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        for (final String[] r : recent()) {
            items.add(item(R.drawable.ic_file, r[1], new Runnable() {
                @Override public void run() { loadUri(Uri.parse(r[0]), 0); }
            }));
        }
        if (items.isEmpty()) items.add(item(R.drawable.ic_folder, "(nada abierto todavía)", null));
        menu.showList(root, "Recientes", items);
    }

    // --- media ----------------------------------------------------------------
    Runnable pickAction(final int slot) {
        return new Runnable() {
            @Override public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try {
                    startActivityForResult(i, PICK + slot);
                } catch (Exception e) {
                    toast("No hay selector de archivos");
                }
            }
        };
    }

    /** A file of the APK's assets (the QL's built-in cartridge). */
    Runnable loadAssetAction(final String asset, final String name, final int slot) {
        return new Runnable() {
            @Override public void run() {
                try (InputStream in = getAssets().open(asset)) {
                    loadStream(in, name, slot);
                } catch (IOException e) {
                    toast("No se puede leer " + name);
                }
            }
        };
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request < PICK || request >= PICK + slotName.length) return;
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
        loadUri(uri, request - PICK);
    }

    private void loadUri(Uri uri, int slot) {
        String name = displayName(uri);
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException();
            if (loadStream(in, name, slot)) addRecent(uri, name);
        } catch (IOException | SecurityException e) {
            toast("No se puede leer el archivo");
        }
    }

    /** Copies the file to the app's cache with its own name (the cores tell
     *  the media by the extension) and loads it on the emulation thread. */
    private boolean loadStream(InputStream in, final String name, final int slot) throws IOException {
        File dir = new File(getCacheDir(), "media");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException();
        String safe = name.replaceAll("[/\\\\:*?\"<>|]", "_");
        final File f = new File(dir, "s" + slot + "_" + safe);
        try (OutputStream out = new FileOutputStream(f)) {
            byte[] buf = new byte[65536];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
                if (total > 64L << 20) throw new IOException("too big");
            }
        }
        emu.post(new Runnable() {
            @Override public void run() {
                final String err = Core.load(f.getAbsolutePath(), slot);
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        if (err == null) {
                            slotName[machine.nameSlot(slot)] = name;
                            toast("Cargado: " + name);
                        } else {
                            toast(name + ": " + err);
                        }
                    }
                });
            }
        });
        return true;
    }

    private List<String[]> recent() {
        List<String[]> out = new ArrayList<>();
        String s = prefs.getString(PREF_RECENT, "");
        for (String line : s.split("\n")) {
            int tab = line.indexOf('\t');
            if (tab > 0) out.add(new String[] {line.substring(0, tab), line.substring(tab + 1)});
        }
        return out;
    }

    private void addRecent(Uri uri, String name) {
        List<String[]> r = recent();
        StringBuilder sb = new StringBuilder(uri.toString()).append('\t').append(name);
        int n = 1;
        for (String[] e : r) {
            if (e[0].equals(uri.toString()) || n >= MAX_RECENT) continue;
            sb.append('\n').append(e[0]).append('\t').append(e[1]);
            n++;
        }
        prefs.edit().putString(PREF_RECENT, sb.toString()).apply();
    }

    // --- files ---------------------------------------------------------------
    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception ignored) {
        }
        if (name == null) name = uri.getLastPathSegment();
        return name != null ? name : "archivo";
    }

    /** The file's bytes (at most limit + 1, to tell a wrong size). */
    byte[] readUri(Uri uri, int limit) throws IOException {
        ContentResolver cr = getContentResolver();
        try (InputStream in = cr.openInputStream(uri)) {
            if (in == null) return null;
            return readAll(in, limit + 1);
        }
    }

    static byte[] readAll(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (out.size() > limit) break;
        }
        return out.toByteArray();
    }

    void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }
}
