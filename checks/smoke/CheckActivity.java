package com.salihefee.securelockcheck;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;
import android.widget.TextView;

public class CheckActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        TextView text = new TextView(this);
        text.setText("JNI hook check\nThis window requests FLAG_SECURE\nScreenshot should show this text with the module enabled.");
        text.setTextSize(24);
        text.setPadding(48, 100, 48, 0);
        setContentView(text);
        registerScreenCaptureCallback(getMainExecutor(), () -> Log.i("SecureLockCheck", "SCREENSHOT_CALLBACK"));
        Log.i("SecureLockCheck", "Observer registration returned successfully; app FLAG_SECURE=" +
            ((getWindow().getAttributes().flags & WindowManager.LayoutParams.FLAG_SECURE) != 0));
    }
}
