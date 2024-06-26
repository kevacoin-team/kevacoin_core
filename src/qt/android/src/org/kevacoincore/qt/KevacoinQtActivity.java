package org.kevacoincore.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class KevacoinQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File kevacoinDir = new File(getFilesDir().getAbsolutePath() + "/.kevacoin");
        if (!kevacoinDir.exists()) {
            kevacoinDir.mkdir();
        }

        super.onCreate(savedInstanceState);
    }
}
