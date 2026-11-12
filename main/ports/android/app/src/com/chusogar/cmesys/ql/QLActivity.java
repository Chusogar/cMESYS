package com.chusogar.cmesys.ql;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.preference.PreferenceManager;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/** The Sinclair QL: the screen, the on-screen keyboard and the menu (laid out
 *  after Hataroid: the menu bar opens with BACK or the corner button and
 *  pauses the QL; the microdrive cartridges come from the phone's files). */
public final class QLActivity extends Activity implements QLKeyboardView.Listener, SoftMenu.Listener {
    private static final int PICK_BOOT = 1, PICK_MDV1 = 2, PICK_MDV2 = 3;
    static final String PREF_RECENT = "recent_carts";
    private static final int MAX_RECENT = 8;

    private Emulator emu;
    private QLView view;
    private QLKeyboardView keyboard;
    private SoftMenu menu;
    private FrameLayout root;
    private LinearLayout cornerButtons;
    private final Handler handler = new Handler();
    private SharedPreferences prefs;

    private boolean keyboardShown;
    private String romLoaded = "";             // the ROM the QL was started with
    private final String[] cartName = new String[2];
    private String padMapping = "ctl1";
    private boolean menuPausedIt;

    // --- life cycle -------------------------------------------------------
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        PreferenceManager.setDefaultValues(this, R.xml.preferences, false);
        prefs = PreferenceManager.getDefaultSharedPreferences(this);

        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new QLView(this);
        root.addView(view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        keyboard = new QLKeyboardView(this);
        keyboard.setListener(this);
        keyboard.setVisibility(View.GONE);
        root.addView(keyboard, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));
        keyboard.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
            @Override public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                updateInset();
            }
        });

        // The corner buttons: the menu and the keyboard (as Hataroid's on-screen icons)
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

        menu = new SoftMenu(this);
        menu.setListener(this);
        emu = new Emulator(view);
        applySettings();
        startQL(state == null);
        setKeyboardShown(prefs.getBoolean("keyboard_start", true));
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
        String rom = romChoice();
        if (!rom.equals(romLoaded)) restartWithRom();
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

    // --- the QL -------------------------------------------------------------
    private String romChoice() {
        return prefs.getString("rom", "js");
    }

    /** The system ROM chosen in the settings (48 KB) and the IPC program. */
    private byte[][] loadRoms() {
        String choice = romChoice();
        byte[] rom = null;
        try {
            if ("custom".equals(choice)) {
                String uri = prefs.getString("rom_custom_uri", null);
                if (uri != null) rom = readUri(Uri.parse(uri), QLNative.ROM_BYTES);
            } else {
                rom = readAsset("roms/" + choice + ".rom");
            }
        } catch (IOException e) {
            rom = null;
        }
        if (rom == null || rom.length != QLNative.ROM_BYTES) {
            toast(getString(R.string.rom_missing));
            try {
                rom = readAsset("roms/js.rom");
                choice = "js";
            } catch (IOException e) {
                return null;
            }
        }
        try {
            romLoaded = romChoice();     // (also when it fell back to JS: asked once)
            return new byte[][] {rom, readAsset("roms/ipc.rom")};
        } catch (IOException e) {
            return null;
        }
    }

    private void startQL(boolean coldStart) {
        final byte[][] roms = loadRoms();
        if (roms == null) {
            toast(getString(R.string.no_roms));
            return;
        }
        final byte[] boot = coldStart && prefs.getBoolean("autoload_last", false) ? lastCartridge() : null;
        emu.post(new Runnable() {
            @Override public void run() {
                QLNative.init(roms[0], roms[1], System.currentTimeMillis() / 1000);
                if (boot != null) QLNative.mount(boot);
            }
        });
        emu.start();
    }

    private void restartWithRom() {
        final byte[][] roms = loadRoms();
        if (roms == null) return;
        emu.post(new Runnable() {
            @Override public void run() { QLNative.init(roms[0], roms[1], System.currentTimeMillis() / 1000); }
        });
        toast(getString(R.string.rom_changed));
    }

    private void applySettings() {
        view.setScale(prefs.getString("scale", QLView.SCALE_43));
        view.setSmooth(prefs.getBoolean("smooth", true));
        view.setOverlay(prefs.getBoolean("show_fps", false), prefs.getBoolean("show_leds", true));
        keyboard.setHaptic(prefs.getBoolean("haptic", true));
        keyboard.setAlpha(Integer.parseInt(prefs.getString("keyboard_alpha", "85")) / 100f);
        cornerButtons.setVisibility(prefs.getBoolean("corner_buttons", true) ? View.VISIBLE : View.GONE);
        emu.setSound(prefs.getBoolean("sound", true));
        padMapping = prefs.getString("pad", "ctl1");
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
        boolean overlay = !portrait && prefs.getBoolean("keyboard_overlay", true);
        view.setBottomInset(keyboardShown && !overlay ? keyboard.getHeight() : 0);
    }

    @Override
    public void onQLKey(int code, boolean down) {
        if (down) emu.keyDown(code);
        else emu.keyUp(code);
    }

    @Override
    public void onKeyboardAction(int action) {
        if (action == QLKeyboardView.ACTION_MENU) showMenu();
        else if (action == QLKeyboardView.ACTION_HIDE) setKeyboardShown(false);
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
        if (down && e.getRepeatCount() > 0) return true;   // the QL repeats keys itself
        int src = e.getSource();
        boolean pad = (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (pad) {
            int k = QLKeys.fromGamepad(code, padMapping);
            if (k >= 0) {
                onQLKey(k, down);
                return true;
            }
        }
        int[] two = QLKeys.extra(code);
        if (two != null) {
            for (int k : two) onQLKey(k, down);
            return true;
        }
        int k = QLKeys.fromAndroid(code);
        if (k >= 0) {
            onQLKey(k, down);
            return true;
        }
        return super.dispatchKeyEvent(e);
    }

    // Pads whose cross and sticks are axes (HAT_X/Y, X/Y): held directions as keys
    private final boolean[] axisDir = new boolean[4];   // left, right, up, down

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
        boolean[] now = {x < -0.5f, x > 0.5f, y < -0.5f, y > 0.5f};
        int[] codes = {KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_UP,
                KeyEvent.KEYCODE_DPAD_DOWN};
        for (int i = 0; i < 4; i++) {
            if (now[i] != axisDir[i]) {
                axisDir[i] = now[i];
                onQLKey(QLKeys.fromGamepad(codes[i], padMapping), now[i]);
            }
        }
        return true;
    }

    // --- the menu -----------------------------------------------------------
    private void showMenu() {
        if (menu.isShowing()) return;
        keyboard.releaseAll();
        emu.releaseAll();
        menuPausedIt = true;
        emu.setSuspended(true);       // (as Hataroid: the machine waits while the menu is open)
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(new SoftMenu.Item(R.drawable.ic_mdv, getString(R.string.m_microdrive), new Runnable() {
            @Override public void run() { showMicrodriveMenu(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_reset, getString(R.string.m_reset), new Runnable() {
            @Override public void run() { showResetMenu(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_pause, getString(emu.isPaused() ? R.string.m_resume : R.string.m_pause),
                new Runnable() {
                    @Override public void run() {
                        emu.setPaused(!emu.isPaused());
                        toast(getString(emu.isPaused() ? R.string.paused : R.string.resumed));
                    }
                }));
        items.add(new SoftMenu.Item(R.drawable.ic_settings, getString(R.string.m_settings), new Runnable() {
            @Override public void run() { startActivity(new Intent(QLActivity.this, SettingsActivity.class)); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_quit, getString(R.string.m_quit), new Runnable() {
            @Override public void run() { finish(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_help, getString(R.string.m_help), new Runnable() {
            @Override public void run() { startActivity(new Intent(QLActivity.this, HelpActivity.class)); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_keyboard, getString(R.string.m_keyboard), new Runnable() {
            @Override public void run() { setKeyboardShown(!keyboardShown); }
        }));
        items.add(new SoftMenu.Item(emu.isTurbo() ? R.drawable.ic_turbo : R.drawable.ic_speed,
                getString(emu.isTurbo() ? R.string.m_speed_turbo : R.string.m_speed_normal), new Runnable() {
                    @Override public void run() {
                        emu.setTurbo(!emu.isTurbo());
                        toast(getString(emu.isTurbo() ? R.string.turbo_on : R.string.turbo_off));
                    }
                }));
        items.add(new SoftMenu.Item(R.drawable.ic_screen, getString(R.string.m_screen), new Runnable() {
            @Override public void run() { showScreenMenu(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_keys, getString(R.string.m_keys), new Runnable() {
            @Override public void run() { showKeysMenu(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_folder, getString(R.string.m_recent), new Runnable() {
            @Override public void run() { showRecentMenu(); }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_eject, getString(R.string.m_eject), new Runnable() {
            @Override public void run() { showEjectMenu(); }
        }));
        menu.show(root, items);
    }

    @Override
    public void onMenuClosed() {
        if (menuPausedIt) {
            menuPausedIt = false;
            emu.setSuspended(false);
        }
        immersive();
    }

    private String driveLabel(int d) {
        String n = cartName[d];
        return "MDV" + (d + 1) + ": " + (n != null ? n : getString(R.string.empty));
    }

    private void showMicrodriveMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(new SoftMenu.Item(R.drawable.ic_mdv1, getString(R.string.mdv_boot), pick(PICK_BOOT)));
        items.add(new SoftMenu.Item(R.drawable.ic_mdv1, getString(R.string.mdv_insert1), pick(PICK_MDV1)));
        items.add(new SoftMenu.Item(R.drawable.ic_mdv2, getString(R.string.mdv_insert2), pick(PICK_MDV2)));
        if (assetExists("cartridge.mdv")) {
            items.add(new SoftMenu.Item(R.drawable.ic_firmware, getString(R.string.mdv_builtin, builtinName()),
                    new Runnable() {
                        @Override public void run() {
                            try {
                                boot(readAsset("cartridge.mdv"), builtinName());
                            } catch (IOException e) {
                                toast(e.getMessage());
                            }
                        }
                    }));
        }
        items.add(new SoftMenu.Item(R.drawable.ic_mdv1, driveLabel(0), null));
        items.add(new SoftMenu.Item(R.drawable.ic_mdv2, driveLabel(1), null));
        menu.showList(root, getString(R.string.m_microdrive), items);
    }

    private void showEjectMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        for (int d = 0; d < 2; d++) {
            final int drive = d;
            items.add(new SoftMenu.Item(R.drawable.ic_eject, getString(R.string.eject_drive, d + 1) + "  (" + driveLabel(d) + ")",
                    cartName[d] == null ? null : new Runnable() {
                        @Override public void run() {
                            emu.post(new Runnable() {
                                @Override public void run() { QLNative.cartInsert(drive, null); }
                            });
                            cartName[drive] = null;
                            toast(getString(R.string.ejected, drive + 1));
                        }
                    }));
        }
        menu.showList(root, getString(R.string.m_eject), items);
    }

    private void showResetMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(new SoftMenu.Item(R.drawable.ic_reset, getString(R.string.reset_cold), new Runnable() {
            @Override public void run() {
                emu.post(new Runnable() { @Override public void run() { QLNative.reset(); } });
            }
        }));
        items.add(new SoftMenu.Item(R.drawable.ic_mdv1, getString(R.string.reset_boot), cartName[0] == null ? null : new Runnable() {
            @Override public void run() {
                emu.post(new Runnable() {
                    @Override public void run() {
                        QLNative.reset();
                        QLNative.autoboot();
                    }
                });
            }
        }));
        menu.showList(root, getString(R.string.m_reset), items);
    }

    private void showScreenMenu() {
        final String[] values = getResources().getStringArray(R.array.scale_values);
        final String[] names = getResources().getStringArray(R.array.scale_entries);
        String current = prefs.getString("scale", QLView.SCALE_43);
        List<SoftMenu.Item> items = new ArrayList<>();
        for (int i = 0; i < values.length; i++) {
            final String v = values[i];
            items.add(new SoftMenu.Item(R.drawable.ic_screen, (v.equals(current) ? "• " : "") + names[i], new Runnable() {
                @Override public void run() {
                    prefs.edit().putString("scale", v).apply();
                    applySettings();
                }
            }));
        }
        final boolean smooth = prefs.getBoolean("smooth", true);
        items.add(new SoftMenu.Item(R.drawable.ic_screen, getString(smooth ? R.string.smooth_off : R.string.smooth_on),
                new Runnable() {
                    @Override public void run() {
                        prefs.edit().putBoolean("smooth", !smooth).apply();
                        applySettings();
                    }
                }));
        menu.showList(root, getString(R.string.m_screen), items);
    }

    /** The QL keys a phone keyboard lacks, pressed together for a moment. */
    private void showKeysMenu() {
        final Object[][] shortcuts = {
            {"F1", new int[] {QLKeys.F1}}, {"F2", new int[] {QLKeys.F2}}, {"F3", new int[] {QLKeys.F3}},
            {"F4", new int[] {QLKeys.F4}}, {"F5", new int[] {QLKeys.F5}}, {"ESC", new int[] {QLKeys.ESC}},
            {getString(R.string.key_break), new int[] {QLKeys.CTRL, QLKeys.SPACE}},
            {getString(R.string.key_switch), new int[] {QLKeys.CTRL, QLKeys.C}},
            {getString(R.string.key_freeze), new int[] {QLKeys.CTRL, QLKeys.F5}},
            {"CTRL+ALT+7 (" + getString(R.string.key_reset_qdos) + ")", new int[] {QLKeys.CTRL, QLKeys.ALT, QLKeys.N7}},
        };
        List<SoftMenu.Item> items = new ArrayList<>();
        for (final Object[] s : shortcuts) {
            items.add(new SoftMenu.Item(R.drawable.ic_keys, (String) s[0], new Runnable() {
                @Override public void run() { emu.tap((int[]) s[1]); }
            }));
        }
        menu.showList(root, getString(R.string.m_keys), items);
    }

    private void showRecentMenu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        for (final String[] r : recent()) {
            items.add(new SoftMenu.Item(R.drawable.ic_file, r[1], new Runnable() {
                @Override public void run() { loadUri(Uri.parse(r[0]), PICK_BOOT); }
            }));
        }
        if (items.isEmpty()) items.add(new SoftMenu.Item(R.drawable.ic_folder, getString(R.string.no_recent), null));
        menu.showList(root, getString(R.string.m_recent), items);
    }

    // --- cartridges ---------------------------------------------------------
    private Runnable pick(final int request) {
        return new Runnable() {
            @Override public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try {
                    startActivityForResult(i, request);
                } catch (Exception e) {
                    toast(getString(R.string.no_picker));
                }
            }
        };
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
        loadUri(uri, request);
    }

    private void loadUri(Uri uri, int request) {
        byte[] image;
        try {
            image = readUri(uri, QLNative.MDV_BYTES);
        } catch (IOException | SecurityException e) {
            toast(getString(R.string.cannot_read));
            return;
        }
        String name = displayName(uri);
        if (image == null || image.length != QLNative.MDV_BYTES) {
            toast(getString(R.string.not_mdv, name));
            return;
        }
        addRecent(uri, name);
        if (request == PICK_BOOT) {
            boot(image, name);
        } else {
            final int drive = request == PICK_MDV2 ? 1 : 0;
            final byte[] img = image;
            emu.post(new Runnable() {
                @Override public void run() { QLNative.cartInsert(drive, img); }
            });
            cartName[drive] = name;
            toast(getString(R.string.inserted, name, drive + 1));
        }
    }

    /** mdv1 = image, reset and boot (the QL's F1 at the start-up prompt). */
    private void boot(final byte[] image, String name) {
        emu.post(new Runnable() {
            @Override public void run() { QLNative.mount(image); }
        });
        cartName[0] = name;
        toast(getString(R.string.booting, name));
    }

    private byte[] lastCartridge() {
        List<String[]> r = recent();
        if (r.isEmpty()) return null;
        try {
            byte[] b = readUri(Uri.parse(r.get(0)[0]), QLNative.MDV_BYTES);
            if (b != null && b.length == QLNative.MDV_BYTES) {
                cartName[0] = r.get(0)[1];
                return b;
            }
        } catch (IOException | SecurityException ignored) {
        }
        return null;
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
        if (name != null && name.toLowerCase().endsWith(".mdv")) name = name.substring(0, name.length() - 4);
        return name != null ? name : "?";
    }

    /** The file's bytes (at most limit + 1, to tell a wrong size). */
    byte[] readUri(Uri uri, int limit) throws IOException {
        ContentResolver cr = getContentResolver();
        try (InputStream in = cr.openInputStream(uri)) {
            if (in == null) return null;
            return readAll(in, limit + 1);
        }
    }

    private byte[] readAsset(String name) throws IOException {
        try (InputStream in = getAssets().open(name)) {
            return readAll(in, 1 << 20);
        }
    }

    private boolean assetExists(String name) {
        try (InputStream in = getAssets().open(name)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    private String builtinName() {
        try {
            return new String(readAsset("cartridge.txt"), "UTF-8").trim();
        } catch (IOException e) {
            return "cartridge";
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

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }
}
