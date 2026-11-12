package com.chusogar.cmesys.ql;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.PopupWindow;
import android.widget.TextView;

import java.util.List;

/** The menu over the emulator, in the manner of Hataroid's soft menu: a bar
 *  at the bottom of the screen with two rows of icons (cartridges, reset,
 *  pause, settings...), and for some of them a list of choices in a window
 *  with a title bar. */
final class SoftMenu {
    /** An icon of the bar or a line of a list. */
    static final class Item {
        final int icon;
        final String text;
        final Runnable action;

        Item(int icon, String text, Runnable action) {
            this.icon = icon;
            this.text = text;
            this.action = action;
        }
    }

    interface Listener {
        void onMenuClosed();
    }

    private final Activity activity;
    private final float density;
    private PopupWindow bar, list;
    private Listener listener;
    private boolean keepOpen;   // a list opened from the bar: the emulation stays paused

    SoftMenu(Activity activity) {
        this.activity = activity;
        density = activity.getResources().getDisplayMetrics().density;
    }

    void setListener(Listener l) { listener = l; }

    boolean isShowing() {
        return (bar != null && bar.isShowing()) || (list != null && list.isShowing());
    }

    private int dp(float v) { return (int) (v * density + 0.5f); }

    /** The bar: items in two rows (the first row first). */
    void show(View anchor, List<Item> items) {
        dismiss();
        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        View line = new View(activity);
        line.setBackgroundColor(Color.argb(150, 255, 255, 255));
        root.addView(line, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));

        int perRow = (items.size() + 1) / 2;
        for (int r = 0; r < 2; r++) {
            LinearLayout row = new LinearLayout(activity);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setBackgroundColor(Color.argb(230, 0, 0, 0));
            row.setPadding(0, dp(4), 0, dp(4));
            for (int i = r * perRow; i < Math.min(items.size(), (r + 1) * perRow); i++) {
                row.addView(cell(items.get(i)), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            }
            for (int i = items.size(); i < (r + 1) * perRow; i++) {    // keep the columns
                row.addView(new View(activity), new LinearLayout.LayoutParams(0, 1, 1f));
            }
            root.addView(row);
        }
        bar = new PopupWindow(root, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, true);
        bar.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        bar.setOutsideTouchable(true);
        bar.setAnimationStyle(android.R.style.Animation_InputMethod);
        bar.setOnDismissListener(new PopupWindow.OnDismissListener() {
            @Override public void onDismiss() { closed(); }
        });
        bar.showAtLocation(anchor, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL, 0, 0);
    }

    private View cell(final Item item) {
        LinearLayout cell = new LinearLayout(activity);
        cell.setOrientation(LinearLayout.VERTICAL);
        cell.setGravity(Gravity.CENTER_HORIZONTAL);
        cell.setPadding(dp(2), dp(6), dp(2), dp(4));
        cell.setBackgroundResource(android.R.drawable.list_selector_background);
        ImageView icon = new ImageView(activity);
        icon.setImageResource(item.icon);
        icon.setAlpha(0.9f);
        cell.addView(icon, new LinearLayout.LayoutParams(dp(40), dp(40)));
        TextView label = new TextView(activity);
        label.setText(item.text);
        label.setTextColor(Color.WHITE);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        label.setGravity(Gravity.CENTER_HORIZONTAL);
        label.setMaxLines(2);
        cell.addView(label);
        cell.setClickable(true);
        cell.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                keepOpen = true;            // the action may open a list
                if (bar != null) bar.dismiss();
                keepOpen = false;
                item.action.run();
                if (!isShowing()) closed();
            }
        });
        return cell;
    }

    /** A list of choices in a window with a title bar (Hataroid's sub-menus). */
    void showList(View anchor, String title, final List<Item> items) {
        if (list != null) list.dismiss();
        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable frame = new GradientDrawable();
        frame.setColor(Color.rgb(24, 26, 34));
        frame.setStroke(dp(1), Color.rgb(110, 110, 120));
        root.setBackground(frame);

        TextView head = new TextView(activity);
        head.setText(title);
        head.setTextColor(Color.WHITE);
        head.setTypeface(Typeface.DEFAULT_BOLD);
        head.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        head.setPadding(dp(14), dp(10), dp(14), dp(10));
        head.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {Color.rgb(70, 90, 130), Color.rgb(30, 40, 64)}));
        root.addView(head, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ListView lv = new ListView(activity);
        lv.setDivider(new ColorDrawable(Color.argb(102, 136, 136, 136)));
        lv.setDividerHeight(1);
        lv.setAdapter(new BaseAdapter() {
            @Override public int getCount() { return items.size(); }
            @Override public Object getItem(int p) { return items.get(p); }
            @Override public long getItemId(int p) { return p; }
            @Override public View getView(int p, View convert, ViewGroup parent) {
                LinearLayout row = new LinearLayout(activity);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(dp(12), dp(8), dp(12), dp(8));
                Item it = items.get(p);
                ImageView icon = new ImageView(activity);
                if (it.icon != 0) icon.setImageResource(it.icon);
                row.addView(icon, new LinearLayout.LayoutParams(dp(32), dp(32)));
                TextView t = new TextView(activity);
                t.setText(it.text);
                t.setTextColor(it.action != null ? Color.WHITE : Color.GRAY);
                t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
                t.setPadding(dp(12), 0, 0, 0);
                row.addView(t);
                return row;
            }
            @Override public boolean isEnabled(int p) { return items.get(p).action != null; }
        });
        lv.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                Item it = items.get(position);
                keepOpen = true;
                if (list != null) list.dismiss();
                keepOpen = false;
                if (it.action != null) it.action.run();
                if (!isShowing()) closed();
            }
        });
        root.addView(lv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        int w = Math.min(activity.getWindow().getDecorView().getWidth() - dp(32), dp(460));
        list = new PopupWindow(root, w, ViewGroup.LayoutParams.WRAP_CONTENT, true);
        list.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        list.setOutsideTouchable(true);
        list.setOnDismissListener(new PopupWindow.OnDismissListener() {
            @Override public void onDismiss() { closed(); }
        });
        list.showAtLocation(anchor, Gravity.CENTER, 0, 0);
    }

    void dismiss() {
        keepOpen = true;
        if (bar != null && bar.isShowing()) bar.dismiss();
        if (list != null && list.isShowing()) list.dismiss();
        keepOpen = false;
    }

    private void closed() {
        if (keepOpen || isShowing()) return;
        if (listener != null) listener.onMenuClosed();
    }
}
