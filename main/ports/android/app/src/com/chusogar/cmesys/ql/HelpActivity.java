package com.chusogar.cmesys.ql;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebView;

/** Help and credits (assets/help.html). */
public final class HelpActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        WebView web = new WebView(this);
        web.setBackgroundColor(0xff181a22);
        web.loadUrl("file:///android_asset/help.html");
        setContentView(web);
    }
}
