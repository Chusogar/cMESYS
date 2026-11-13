package com.chusogar.cmesys.arcade;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;

import com.chusogar.cmesys.app.HelpActivity;
import com.chusogar.cmesys.app.R;
import com.chusogar.cmesys.app.SoftMenu;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/** One game: its monitor, the arcade controls and the menu (the same bar
 *  as the computers', after Hataroid). It runs in its own process
 *  (":game"), which loads only this game's library and ends with it. */
public final class GameActivity extends Activity implements ControlsView.Listener, SoftMenu.Listener {
    static final String EXTRA_GAME = "game";

    private Game game;
    private ArcadeScreen screen;
    private ControlsView controls;
    private SoftMenu menu;
    private FrameLayout root;
    private Thread thread;
    private SharedPreferences prefs;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean controlsShown = true;

    // what is held: on-screen controls, keys / pad
    private int touchBits, keyBits;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        game = Game.find(String.valueOf(getIntent().getStringExtra(EXTRA_GAME)));
        if (game == null) {
            finish();
            return;
        }
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        prefs = ArcadeSettings.prefs(this);

        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        screen = new ArcadeScreen(this);
        root.addView(screen, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        controls = new ControlsView(this);
        controls.setGame(game);
        controls.setListener(this);
        root.addView(controls, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
            @Override public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                layoutPanel();
            }
        });
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                v.setPadding(in.getSystemWindowInsetLeft(), 0, in.getSystemWindowInsetRight(), in.getSystemWindowInsetBottom());
                return in.consumeSystemWindowInsets();
            }
        });
        setContentView(root);
        menu = new SoftMenu(this);
        menu.setListener(this);
        applySettings();

        Native.load(game.id);
        Native.setScreen(screen);
        final File dir = new File(getFilesDir(), game.id);
        if (!dir.isDirectory()) dir.mkdirs();
        thread = new Thread(new Runnable() {
            @Override public void run() {
                Native.runGame(dir.getAbsolutePath());
                runOnUiThread(new Runnable() { @Override public void run() { finish(); } });
            }
        }, game.id);
        thread.start();
    }

    @Override
    protected void onResume() {
        super.onResume();
        immersive();
        prefs = ArcadeSettings.prefs(this);       // (changed in the settings' process)
        applySettings();
        if (!menu.isShowing()) Native.setSuspended(false);
    }

    @Override
    protected void onPause() {
        super.onPause();
        controls.releaseAll();
        keyBits = 0;
        updateKeys();
        Native.setSuspended(true);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        menu.dismiss();
        // the game saves its NVRAM when it quits; then the process ends (the
        // next game loads its own library in a new one)
        Native.requestQuit();
        Native.setPaused(false);
        Native.setSuspended(false);
        if (thread != null) {
            try {
                thread.join(1500);
            } catch (InterruptedException ignored) {
            }
        }
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive();
    }

    @Override
    public void onConfigurationChanged(Configuration c) {
        super.onConfigurationChanged(c);
        layoutPanel();
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void applySettings() {
        screen.setScale(prefs.getString("scale", ArcadeScreen.SCALE_FIT));
        screen.setSmooth(prefs.getBoolean("smooth", true));
        screen.setShowFps(prefs.getBoolean("show_fps", false));
        controls.setHaptic(prefs.getBoolean("haptic", true));
        controls.setControlSize(Integer.parseInt(prefs.getString("controls_size", "100")) / 100f);
        controls.setAlpha(Integer.parseInt(prefs.getString("controls_alpha", "85")) / 100f);
        Native.setSound(prefs.getBoolean("sound", true));
        layoutPanel();
    }

    /** Portrait: the picture at the top and the control panel below it;
     *  landscape: the controls over the picture's sides. */
    private void layoutPanel() {
        int w = root.getWidth() - root.getPaddingLeft() - root.getPaddingRight();
        int h = root.getHeight() - root.getPaddingBottom();
        if (w <= 0 || h <= 0) return;
        boolean portrait = h > w;
        boolean vertical = game.id.equals("retofinv");
        int pw = vertical ? 3 : 4, ph = vertical ? 4 : 3;
        int picH = ArcadeScreen.pictureHeight(w, pw, ph);
        if (portrait && controlsShown && h - picH > h * 0.28f) {
            screen.setBottomInset(h - picH, true);
            controls.setPanelTop(picH);
        } else if (portrait && controlsShown) {
            int panel = (int) (h * 0.36f);                // a tall vertical game: smaller picture
            screen.setBottomInset(panel, true);
            controls.setPanelTop(h - panel);
        } else {
            screen.setBottomInset(0, false);
            controls.setPanelTop(-1);
        }
        controls.setVisibility(controlsShown ? View.VISIBLE : View.GONE);
    }

    // --- controls ---------------------------------------------------------------------
    private void updateKeys() {
        Native.setHeld(touchBits | keyBits);
    }

    @Override
    public void onControls(int bits) {
        touchBits = bits;
        updateKeys();
    }

    /** A coin: it reaches the game as the animated coin drops into the slot
     *  (one after another, each for a few of the game's frames: Native). */
    @Override
    public void onCoin() {
        handler.postDelayed(new Runnable() {
            @Override public void run() { Native.insertCoin(Native.COIN1); }
        }, 300);
    }

    @Override
    public void onMenu() { showMenu(); }

    // Physical keyboards (the desktop's keys) and game pads
    private int keyBit(int code, boolean pad) {
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: return Native.UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return Native.DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return Native.LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return Native.RIGHT;
            case KeyEvent.KEYCODE_5: return Native.COIN1;
            case KeyEvent.KEYCODE_6: return Native.COIN2;
            case KeyEvent.KEYCODE_1: return Native.START1;
            case KeyEvent.KEYCODE_2: return Native.START2;
            case KeyEvent.KEYCODE_Z: case KeyEvent.KEYCODE_CTRL_LEFT: case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
                return Native.B1;
            case KeyEvent.KEYCODE_X: case KeyEvent.KEYCODE_ALT_LEFT: case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_BUTTON_Y:
                return Native.B2;
            case KeyEvent.KEYCODE_C: case KeyEvent.KEYCODE_SPACE: case KeyEvent.KEYCODE_BUTTON_X:
                return Native.B3;
            case KeyEvent.KEYCODE_BUTTON_SELECT: case KeyEvent.KEYCODE_BUTTON_L1: return Native.COIN1;
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_BUTTON_R1: case KeyEvent.KEYCODE_ENTER:
                return Native.START1;
            default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int code = e.getKeyCode();
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        if (e.getAction() == KeyEvent.ACTION_MULTIPLE) return super.dispatchKeyEvent(e);
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_BUTTON_MODE
                || code == KeyEvent.KEYCODE_ESCAPE) {
            if (!down && e.getRepeatCount() == 0) {
                if (menu.isShowing()) menu.dismiss();
                else showMenu();
            }
            return true;
        }
        if (code == KeyEvent.KEYCODE_F3) {
            if (down) Native.requestReset();
            return true;
        }
        if (code == KeyEvent.KEYCODE_P) {
            if (down && e.getRepeatCount() == 0) Native.setPaused(!Native.isPaused());
            return true;
        }
        int bit = keyBit(code, false);
        if (bit == 0) return super.dispatchKeyEvent(e);
        if (down && e.getRepeatCount() > 0) return true;
        if (bit == Native.COIN1 || bit == Native.COIN2) {        // a coin per press
            if (down) {
                Native.insertCoin(bit);
                if (bit == Native.COIN1) controls.animateCoin();
            }
            return true;
        }
        if (down) keyBits |= bit;
        else keyBits &= ~bit;
        updateKeys();
        return true;
    }

    private int axisBits;

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE) {
            return super.onGenericMotionEvent(e);
        }
        float x = e.getAxisValue(MotionEvent.AXIS_HAT_X), y = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (Math.abs(x) < 0.5f) x = e.getAxisValue(MotionEvent.AXIS_X);
        if (Math.abs(y) < 0.5f) y = e.getAxisValue(MotionEvent.AXIS_Y);
        int bits = (y < -0.5f ? Native.UP : 0) | (y > 0.5f ? Native.DOWN : 0) | (x < -0.5f ? Native.LEFT : 0)
                | (x > 0.5f ? Native.RIGHT : 0);
        int dirs = Native.UP | Native.DOWN | Native.LEFT | Native.RIGHT;
        keyBits = (keyBits & ~axisBits) | bits;
        axisBits = bits & dirs;
        updateKeys();
        return true;
    }

    // --- menu ---------------------------------------------------------------------------
    private SoftMenu.Item item(int icon, String text, Runnable r) { return new SoftMenu.Item(icon, text, r); }

    private void showMenu() {
        if (menu.isShowing()) return;
        controls.releaseAll();
        Native.setSuspended(true);
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(item(R.drawable.ic_folder, "Juegos", new Runnable() {
            @Override public void run() { finish(); }
        }));
        items.add(item(R.drawable.ic_reset, "Reset", new Runnable() {
            @Override public void run() { Native.requestReset(); toast("Reset"); }
        }));
        items.add(item(R.drawable.ic_pause, Native.isPaused() ? "Continuar" : "Pausa", new Runnable() {
            @Override public void run() { Native.setPaused(!Native.isPaused()); }
        }));
        items.add(item(R.drawable.ic_settings, "Ajustes", new Runnable() {
            @Override public void run() { startActivity(new Intent(GameActivity.this, ArcadeSettings.class)); }
        }));
        items.add(item(R.drawable.ic_quit, "Salir", new Runnable() {
            @Override public void run() { finish(); }
        }));
        items.add(item(R.drawable.ic_help, "Ayuda", new Runnable() {
            @Override public void run() { startActivity(new Intent(GameActivity.this, HelpActivity.class)); }
        }));
        items.add(item(R.drawable.ic_keyboard, "Controles", new Runnable() {
            @Override public void run() {
                controlsShown = !controlsShown;
                if (!controlsShown) controls.releaseAll();
                layoutPanel();
            }
        }));
        items.add(item(Native.isTurbo() ? R.drawable.ic_turbo : R.drawable.ic_speed,
                Native.isTurbo() ? "Turbo" : "Velocidad normal", new Runnable() {
                    @Override public void run() {
                        Native.turbo(!Native.isTurbo());
                        toast(Native.isTurbo() ? "Turbo activado" : "Velocidad normal");
                    }
                }));
        items.add(item(R.drawable.ic_screen, "Pantalla", new Runnable() {
            @Override public void run() { showScreenMenu(); }
        }));
        items.add(item(R.drawable.ic_keys, "Jugador 2", new Runnable() {
            @Override public void run() { showPlayer2Menu(); }
        }));
        menu.show(root, items);
    }

    private void showScreenMenu() {
        String cur = prefs.getString("scale", ArcadeScreen.SCALE_FIT);
        String[][] modes = {{ArcadeScreen.SCALE_FIT, "Proporción del monitor"}, {ArcadeScreen.SCALE_INTEGER, "Múltiplo entero"},
            {ArcadeScreen.SCALE_STRETCH, "Ocupar todo"}};
        List<SoftMenu.Item> items = new ArrayList<>();
        for (final String[] m : modes) {
            items.add(item(R.drawable.ic_screen, (m[0].equals(cur) ? "• " : "") + m[1], new Runnable() {
                @Override public void run() {
                    prefs.edit().putString("scale", m[0]).apply();
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

    private void showPlayer2Menu() {
        List<SoftMenu.Item> items = new ArrayList<>();
        items.add(item(R.drawable.ic_keys, "Moneda (jugador 2)", new Runnable() {
            @Override public void run() { Native.insertCoin(Native.COIN2); }
        }));
        items.add(item(R.drawable.ic_keys, "START jugador 2", new Runnable() {
            @Override public void run() { Native.tap(Native.START2); }
        }));
        menu.showList(root, "Jugador 2", items);
    }

    @Override
    public void onMenuClosed() {
        Native.setSuspended(false);
        immersive();
    }

    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_SHORT).show(); }
}
