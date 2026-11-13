package com.chusogar.cmesys.arcade;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import com.chusogar.cmesys.app.HelpActivity;
import com.chusogar.cmesys.app.R;

/** The games, as MAME for Android lists them: each with a marquee-like
 *  badge in its colour, its name, maker and year, and its controls. A tap
 *  starts it (GameActivity, in its own process). */
public final class GameListActivity extends Activity {
    private float dp;

    private int dp(float v) { return (int) (v * dp + 0.5f); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        dp = getResources().getDisplayMetrics().density;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff121318);

        // the title bar: name, count, settings and help
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(16), dp(12), dp(8), dp(12));
        bar.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {0xff3a4a7a, 0xff1c2238}));
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.mipmap.ic_launcher);
        bar.addView(logo, new LinearLayout.LayoutParams(dp(40), dp(40)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(12), 0, 0, 0);
        TextView t = new TextView(this);
        t.setText("cMESYS Arcade");
        t.setTextColor(Color.WHITE);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        titles.addView(t);
        TextView sub = new TextView(this);
        sub.setText(Game.ALL.length + " juegos");
        sub.setTextColor(0xffb8c0d8);
        sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        titles.addView(sub);
        bar.addView(titles, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        bar.addView(barButton(R.drawable.ic_settings, new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(GameListActivity.this, ArcadeSettings.class));
            }
        }));
        bar.addView(barButton(R.drawable.ic_help, new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(GameListActivity.this, HelpActivity.class));
            }
        }));
        root.addView(bar);

        ListView list = new ListView(this);
        list.setDivider(new ColorDrawable(0xff22242c));
        list.setDividerHeight(1);
        list.setAdapter(new BaseAdapter() {
            @Override public int getCount() { return Game.ALL.length; }
            @Override public Object getItem(int p) { return Game.ALL[p]; }
            @Override public long getItemId(int p) { return p; }
            @Override public View getView(int p, View convert, ViewGroup parent) { return row(Game.ALL[p]); }
        });
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> parent, View v, int position, long id) {
                Intent i = new Intent(GameListActivity.this, GameActivity.class);
                i.putExtra(GameActivity.EXTRA_GAME, Game.ALL[position].id);
                startActivity(i);
            }
        });
        root.addView(list, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
    }

    private ImageButton barButton(int icon, View.OnClickListener l) {
        ImageButton b = new ImageButton(this);
        b.setImageResource(icon);
        b.setBackgroundColor(Color.TRANSPARENT);
        b.setPadding(dp(8), dp(8), dp(8), dp(8));
        b.setOnClickListener(l);
        return b;
    }

    /** The game's badge: its colour, its initials, a lit-marquee gradient. */
    private View badge(Game g) {
        TextView b = new TextView(this);
        StringBuilder s = new StringBuilder();
        for (String w : g.title.split("[ .&]+")) {
            if (!w.isEmpty() && s.length() < 2 && Character.isLetterOrDigit(w.charAt(0))) s.append(w.charAt(0));
        }
        if (s.length() == 1) {                 // one word: its capitals (RoboCop: RC)
            for (int i = 1; i < g.title.length() && s.length() < 2; i++) {
                if (Character.isUpperCase(g.title.charAt(i))) s.append(g.title.charAt(i));
            }
            if (s.length() == 1) s.append(g.title.charAt(1));
        }
        if (g.id.equals("polepos2")) s = new StringBuilder("P2");
        b.setText(s.toString().toUpperCase());
        b.setTextColor(Color.WHITE);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        b.setGravity(Gravity.CENTER);
        b.setShadowLayer(4 * dp, 0, dp, 0xc0000000);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {lighter(g.color), g.color, darker(g.color)});
        bg.setCornerRadius(10 * dp);
        bg.setStroke(dp(2), 0x40ffffff);
        b.setBackground(bg);
        return b;
    }

    private static int lighter(int c) {
        return Color.rgb(Color.red(c) + (255 - Color.red(c)) / 3, Color.green(c) + (255 - Color.green(c)) / 3,
                Color.blue(c) + (255 - Color.blue(c)) / 3);
    }

    private static int darker(int c) {
        return Color.rgb(Color.red(c) / 2, Color.green(c) / 2, Color.blue(c) / 2);
    }

    private View row(Game g) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(14), dp(10), dp(14), dp(10));
        r.addView(badge(g), new LinearLayout.LayoutParams(dp(56), dp(56)));
        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        text.setPadding(dp(14), 0, 0, 0);
        TextView name = new TextView(this);
        name.setText(g.title);
        name.setTextColor(Color.WHITE);
        name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        text.addView(name);
        TextView by = new TextView(this);
        by.setText(g.maker + " · " + g.year);
        by.setTextColor(0xffa8adbc);
        by.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        text.addView(by);
        TextView how = new TextView(this);
        StringBuilder s = new StringBuilder();
        s.append(g.joystick == Game.JOY_8WAY ? "Joystick · " : g.joystick == Game.JOY_2WAY ? "Joystick ◀▶ · " : "");
        for (int i = 0; i < g.buttons.length; i++) s.append(i > 0 ? ", " : "").append(g.buttons[i]);
        how.setText(s);
        how.setTextColor(0xff7d8396);
        how.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        text.addView(how);
        r.addView(text, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView play = new TextView(this);
        play.setText("▶");
        play.setTextColor(g.color);
        play.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        r.addView(play);
        return r;
    }
}
