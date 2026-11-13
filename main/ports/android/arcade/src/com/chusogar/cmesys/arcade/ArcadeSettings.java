package com.chusogar.cmesys.arcade;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;

import com.chusogar.cmesys.app.R;

/** The settings (screen, controls, sound), shared by the list and the
 *  games: a game runs in its own process, so it reads them again on each
 *  resume (MODE_MULTI_PROCESS). */
public final class ArcadeSettings extends PreferenceActivity {
    @SuppressWarnings("deprecation")
    static SharedPreferences prefs(Context c) {
        PreferenceManager.setDefaultValues(c, R.xml.arcade_prefs, false);
        return c.getSharedPreferences(c.getPackageName() + "_preferences", Context.MODE_MULTI_PROCESS);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getPreferenceManager().setSharedPreferencesMode(Context.MODE_MULTI_PROCESS);
        addPreferencesFromResource(R.xml.arcade_prefs);
    }
}
