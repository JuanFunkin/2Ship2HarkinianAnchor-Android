
package com.dishii.mm;
import org.libsdl.app.SDLActivity;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.FileOutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;

import android.Manifest;
import android.content.pm.PackageManager;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import android.os.Build;
import android.widget.Toast;

import android.util.Log;

import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.widget.ImageView;

//This class is the main SDLActivity and just sets up a bunch of default files and the input overlay
public class MainActivity extends SDLActivity{

    SharedPreferences preferences;
    private static final CountDownLatch setupLatch = new CountDownLatch(1);

    @Override
    protected void onCreate(Bundle savedInstanceState) {

        preferences = getSharedPreferences("com.dishii.mm.prefs",Context.MODE_PRIVATE);

        // Check if storage permissions are granted
        if (hasStoragePermission()) {
            doVersionCheck();
            checkAndSetupFiles();
        } else {
            requestStoragePermission();
        }

        super.onCreate(savedInstanceState);

        setupControllerOverlay();
        attachController();
    }

    public static void waitForSetupFromNative() {
        try {
            setupLatch.await();  // Block until setup is complete
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    private void doVersionCheck(){
        int currentVersion = BuildConfig.VERSION_CODE;
        int storedVersion = preferences.getInt("appVersion", 1);

        if (currentVersion > storedVersion) {
            deleteOutdatedAssets();
            preferences.edit().putInt("appVersion", currentVersion).apply();
        }
    }

    private void deleteOutdatedAssets() {
        File targetRootFolder = new File(Environment.getExternalStorageDirectory(), "2S2H");

        File sohFile = new File(targetRootFolder, "2ship.o2r");
        File ootFile = new File(targetRootFolder, "mm.o2r");
        File assetsFolder = new File(targetRootFolder, "assets");

        deleteIfExists(sohFile);
        deleteIfExists(ootFile);
        deleteRecursiveIfExists(assetsFolder);
    }

    private void deleteIfExists(File file) {
        if (file.exists()) {
            if (file.delete()) {
                Log.i("deleteAssets", "Deleted file: " + file.getAbsolutePath());
            } else {
                Log.w("deleteAssets", "Failed to delete file: " + file.getAbsolutePath());
            }
        } else {
            Log.i("deleteAssets", "File not found (skipped): " + file.getAbsolutePath());
        }
    }

    private void deleteRecursiveIfExists(File dir) {
        if (dir.exists()) {
            deleteRecursive(dir);
            Log.i("deleteAssets", "Deleted directory: " + dir.getAbsolutePath());
        } else {
            Log.i("deleteAssets", "Directory not found (skipped): " + dir.getAbsolutePath());
        }
    }

    private void deleteRecursive(File fileOrDirectory) {
        if (fileOrDirectory.isDirectory()) {
            File[] children = fileOrDirectory.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        fileOrDirectory.delete();
    }



    // Check if storage permission is granted
    private boolean hasStoragePermission() {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED &&
                ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        == PackageManager.PERMISSION_GRANTED;
    }

    private static final int STORAGE_PERMISSION_REQUEST_CODE = 2296;
    private static final int FILE_PICKER_REQUEST_CODE = 0;

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ → MANAGE_EXTERNAL_STORAGE
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, STORAGE_PERMISSION_REQUEST_CODE);
            } else {
                // Already granted
                checkAndSetupFiles();
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6–10 → request READ/WRITE at runtime
            ActivityCompat.requestPermissions(this,
                    new String[]{
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                    },
                    STORAGE_PERMISSION_REQUEST_CODE);
        } else {
            // Below Android 6 → permissions granted at install time
            checkAndSetupFiles();
        }
    }

    public void checkAndSetupFiles() {
        File targetRootFolder = new File(Environment.getExternalStorageDirectory(), "2S2H");
        File assetsFolder = new File(targetRootFolder, "assets");
        File sohOtrFile = new File(targetRootFolder, "2ship.o2r");

        boolean isMissingAssets = !assetsFolder.exists() || assetsFolder.listFiles() == null || assetsFolder.listFiles().length == 0;
        boolean isMissingSohOtr = !sohOtrFile.exists();

        if (!targetRootFolder.exists() || isMissingAssets || isMissingSohOtr) {
            new AlertDialog.Builder(this)
                    .setTitle("Setup Required")
                    .setMessage("Some required files are missing. The app will create them (~30s). Press OK to begin.")
                    .setCancelable(false)
                    .setPositiveButton("OK", (dialog, which) -> {
                        Executors.newSingleThreadExecutor().execute(() -> {
                            runOnUiThread(() -> Toast.makeText(this, "Setting up files...", Toast.LENGTH_SHORT).show());
                            setupFilesInBackground(targetRootFolder);
                        });
                    })
                    .show();
        } else {
            // No setup needed, still need to count down
            setupLatch.countDown();
        }
    }


    private void setupFilesInBackground(File targetRootFolder) {

        File sourceOldRoot = getExternalFilesDir(null);
        File sourceSavesDir = new File(sourceOldRoot, "saves"); // how to tell if there's anything to migrate

        // === Migration from old Android/data/.../files/ directory ===
        if (sourceOldRoot != null && sourceSavesDir.isDirectory()) {
            Log.i("setupFiles", "Migrating old data from: " + sourceOldRoot.getAbsolutePath());

            File[] sourceFiles = sourceOldRoot.listFiles();
            if (sourceFiles != null) {
                for (File file : sourceFiles) {
                    String name = file.getName();
                    if (name.equals("assets") || name.equals("2ship.o2r") || name.equals("mm.o2r")) {
                        continue; // Skip these
                    }

                    File dest = new File(targetRootFolder, name);
                    try {
                        if (file.isDirectory()) {
                            AssetCopyUtil.copyDirectory(file, dest);
                        } else {
                            AssetCopyUtil.copyFile(file, dest);
                        }
                        Log.i("setupFiles", "Migrated: " + name);
                    } catch (IOException e) {
                        Log.e("setupFiles", "Failed to migrate: " + name, e);
                    }
                }
            }

            runOnUiThread(() -> Toast.makeText(this, "Save data migrated", Toast.LENGTH_SHORT).show());
        }

        // Ensure root folder exists
        if (!targetRootFolder.exists()) {
            if (!targetRootFolder.mkdirs()) {
                Log.e("setupFiles", "Failed to create root folder");
                runOnUiThread(() -> Toast.makeText(this, "Failed to create folder", Toast.LENGTH_LONG).show());
                setupLatch.countDown();
                return;
            }
        }

        // Always ensure mods folder exists
        File targetModsDir = new File(targetRootFolder, "mods");
        if (!targetModsDir.exists()) {
            targetModsDir.mkdirs();
        }

        // Copy assets/ from internal
        File targetAssetsDir = new File(targetRootFolder, "assets");
        try {
            if (!targetAssetsDir.exists()) {
                targetAssetsDir.mkdirs();
            }
            AssetCopyUtil.copyAssetsToExternal(this, "assets", targetAssetsDir.getAbsolutePath());
            runOnUiThread(() -> Toast.makeText(this, "Assets copied", Toast.LENGTH_SHORT).show());
        } catch (IOException e) {
            e.printStackTrace();
            runOnUiThread(() -> Toast.makeText(this, "Error copying assets", Toast.LENGTH_LONG).show());
        }

        // Copy 2ship.o2r from internal assets
        File targetOtrFile = new File(targetRootFolder, "2ship.o2r");
        try (InputStream in = getAssets().open("2ship.o2r");
             OutputStream out = new FileOutputStream(targetOtrFile)) {

            byte[] buffer = new byte[1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }

            runOnUiThread(() -> Toast.makeText(this, "2ship.o2r copied", Toast.LENGTH_SHORT).show());

        } catch (IOException e) {
            e.printStackTrace();
            runOnUiThread(() -> Toast.makeText(this, "Error copying 2ship.o2r", Toast.LENGTH_LONG).show());
        }

        setupLatch.countDown();
    }




    private native void nativeHandleSelectedFile(String filePath);

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == FILE_PICKER_REQUEST_CODE && resultCode == RESULT_OK && data != null) {
            // Handle file selection
            Uri selectedFileUri = data.getData();
            String fileName = "MM.z64";

            File destinationDirectory = new File(Environment.getExternalStorageDirectory(), "2S2H");
            File destinationFile = new File(destinationDirectory, fileName);

            if (destinationDirectory != null && selectedFileUri != null) {
                try {
                    InputStream in = getContentResolver().openInputStream(selectedFileUri);
                    OutputStream out = new FileOutputStream(destinationFile);

                    byte[] buffer = new byte[4096];
                    int bytesRead;
                    while ((bytesRead = in.read(buffer)) != -1) {
                        out.write(buffer, 0, bytesRead);
                    }

                    in.close();
                    out.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }

            // Now pass the path of the file in the new folder
            nativeHandleSelectedFile(destinationFile.getPath());

        } else if (requestCode == STORAGE_PERMISSION_REQUEST_CODE) {
            // Handle MANAGE_EXTERNAL_STORAGE result
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                if (Environment.isExternalStorageManager()) {
                    checkAndSetupFiles();
                } else {
                    Toast.makeText(this, "Storage permission is required to access files.", Toast.LENGTH_LONG).show();
                }
            }
        }
    }


    public void openFilePicker() {
        // Create an Intent to open the file picker dialog
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("*/*");

        // Start the file picker dialog
        startActivityForResult(intent, 0);
    }

    // Check if external storage is available and writable
    private boolean isExternalStorageWritable() {
        String state = Environment.getExternalStorageState();
        return Environment.MEDIA_MOUNTED.equals(state);
    }


    public native void attachController();
    public native void detachController();
    // Native method for setting button state
    public native void setButton(int button, boolean value);
    public native void setCameraState(int axis, float value);

    // Native method for setting joystick axis value
    public native void setAxis(int axis, short value);

    // ------------------------------------------------------------------
    // Controles táctiles configurables (reemplaza el overlay XML anterior)
    // ------------------------------------------------------------------
    private TouchControlsView touchView;
    private ControlsConfig controlsConfig;
    private ControlsSettings controlsSettings;

    private void setupControllerOverlay() {
        controlsConfig = ControlsConfig.load(this);

        touchView = new TouchControlsView(this, controlsConfig, new TouchControlsView.Listener() {
            @Override
            public void onButton(int code, boolean down) {
                setButton(code, down);
            }

            @Override
            public void onAxis(int axis, short value) {
                setAxis(axis, value);
            }

            @Override
            public void onCamera(int axis, float value) {
                setCameraState(axis, value);
            }

            @Override
            public void onOpenSettings() {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        controlsSettings.showMain();
                    }
                });
            }

            @Override
            public void onElementTapped(ControlsConfig.Element element) {
                controlsSettings.showElement(element);
            }

            @Override
            public void onEditDone() {
                touchView.setEditMode(false);
                controlsConfig.save(MainActivity.this);
            }
        });

        controlsSettings = new ControlsSettings(this, controlsConfig, touchView, new Runnable() {
            @Override
            public void run() {
                touchView.setEditMode(true);
            }
        });

        ViewGroup content = (ViewGroup) getContentView();
        content.addView(touchView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
    }

    // Estos dos métodos pueden ser llamados desde código nativo; se conservan con el mismo nombre.
    boolean TouchAreaEnabled = true;

    void DisableTouchArea() {
        TouchAreaEnabled = false;
        if (touchView != null) touchView.setLookAllowed(false);
    }

    void EnableTouchArea() {
        TouchAreaEnabled = true;
        if (touchView != null) touchView.setLookAllowed(true);
    }
}
