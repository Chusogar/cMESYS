package com.chusogar.cmesys.ql;

import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.widget.Toast;

import java.io.IOException;
import java.io.InputStream;

/** The settings (as Hataroid's): system ROM, screen, keyboard, sound and
 *  controls. A ROM of the phone's files can be chosen as "custom". */
public final class SettingsActivity extends PreferenceActivity {
    private static final int PICK_ROM = 1;

    @Override
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        addPreferencesFromResource(R.xml.preferences);
        updateRomSummary();
    }

    @SuppressWarnings("deprecation")
    private void updateRomSummary() {
        SharedPreferences p = PreferenceManager.getDefaultSharedPreferences(this);
        Preference pick = findPreference("rom_custom_pick");
        if (pick != null) {
            String name = p.getString("rom_custom_name", null);
            pick.setSummary(name != null ? name : getString(R.string.pref_rom_custom_none));
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public boolean onPreferenceTreeClick(PreferenceScreen screen, Preference pref) {
        if ("rom_custom_pick".equals(pref.getKey())) {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            try {
                startActivityForResult(i, PICK_ROM);
            } catch (Exception e) {
                Toast.makeText(this, R.string.no_picker, Toast.LENGTH_SHORT).show();
            }
            return true;
        }
        return super.onPreferenceTreeClick(screen, pref);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        int size = -1;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in != null) size = QLActivity.readAll(in, QLNative.ROM_BYTES + 1).length;
        } catch (IOException | SecurityException e) {
            size = -1;
        }
        if (size != QLNative.ROM_BYTES) {
            Toast.makeText(this, getString(R.string.rom_bad_size), Toast.LENGTH_LONG).show();
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
        String name = uri.getLastPathSegment();
        PreferenceManager.getDefaultSharedPreferences(this).edit()
                .putString("rom_custom_uri", uri.toString())
                .putString("rom_custom_name", name)
                .putString("rom", "custom")
                .apply();
        ListPreference rom = (ListPreference) findPreference("rom");
        if (rom != null) rom.setValue("custom");
        updateRomSummary();
    }
}
