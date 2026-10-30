package io.github.vpet;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity plus pack import: the game asks for a pack, the player picks
 * a .zip in the system file picker, and it is unpacked into the app's pack
 * folder (Android/data/io.github.vpet/files/packs/). The game polls
 * importState() and importMessage() from its own thread.
 */
public class GameActivity extends SDLActivity {
    private static final int PICK_PACK = 4201;

    // 0 idle, 1 picking or unpacking, 2 done (message = pack folder), 3 failed (message = reason).
    private static volatile int sState = 0;
    private static volatile String sMessage = "";

    /** Called from the game: opens the file picker. */
    public static void pickPack() {
        final SDLActivity activity = mSingleton;
        if (activity == null) {
            finish(3, "NOT READY");
            return;
        }
        sState = 1;
        sMessage = "";
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");  // some file managers don't tag .zip files as zip
            try {
                activity.startActivityForResult(intent, PICK_PACK);
            } catch (Exception e) {
                finish(3, "NO FILE PICKER");
            }
        });
    }

    public static int importState() { return sState; }

    public static String importMessage() { return sMessage; }

    /** Called from the game once it has read a finished result. */
    public static void clearImport() {
        sState = 0;
        sMessage = "";
    }

    private static void finish(int state, String message) {
        sMessage = message;
        sState = state;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_PACK) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            finish(3, "CANCELLED");
            return;
        }
        final Uri uri = data.getData();
        new Thread(() -> {
            try {
                finish(2, unpack(uri));
            } catch (IOException e) {
                finish(3, e.getMessage() == null ? "COULD NOT READ THE ZIP" : e.getMessage());
            }
        }).start();
    }

    // Unpacks the zip into a scratch folder, finds the folder holding
    // pack.json (the zip's root or one folder down) and moves it into packs/.
    private String unpack(Uri uri) throws IOException {
        File root = new File(getExternalFilesDir(null), "packs");
        File scratch = new File(getCacheDir(), "pack_import");
        deleteTree(scratch);
        if (!scratch.mkdirs() && !scratch.isDirectory()) throw new IOException("NO SPACE TO UNPACK");
        String scratchPath = scratch.getCanonicalPath() + File.separator;

        byte[] buffer = new byte[64 * 1024];
        int files = 0;
        try (InputStream in = getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(in)) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                String name = entry.getName().replace('\\', '/');
                if (name.startsWith("__MACOSX/")) continue;
                File out = new File(scratch, name);
                // Never write outside the scratch folder ("../" in a name).
                if (!out.getCanonicalPath().startsWith(scratchPath)) throw new IOException("BAD ZIP");
                if (entry.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream os = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) os.write(buffer, 0, n);
                }
                ++files;
            }
        } catch (IllegalArgumentException e) {
            throw new IOException("NOT A ZIP FILE");
        }
        if (files == 0) throw new IOException("NOT A ZIP FILE");

        File pack = null;
        String name;
        if (new File(scratch, "pack.json").isFile()) {
            pack = scratch;
            name = baseName(uri);
        } else {
            File[] children = scratch.listFiles();
            if (children != null)
                for (File child : children)
                    if (child.isDirectory() && new File(child, "pack.json").isFile()) pack = child;
            if (pack == null) throw new IOException("NO PACK.JSON IN THE ZIP");
            name = pack.getName();
        }
        name = name.replaceAll("[^A-Za-z0-9_.-]", "_");
        if (name.isEmpty() || name.startsWith(".")) name = "pack";

        root.mkdirs();
        File dest = new File(root, name);
        deleteTree(dest);  // importing again replaces the old copy
        if (!pack.renameTo(dest)) {
            copyTree(pack, dest);
        }
        deleteTree(scratch);
        return name;
    }

    // The picked file's name without ".zip", for zips with pack.json at the root.
    private String baseName(Uri uri) {
        String name = "pack";
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) name = c.getString(0);
        } catch (Exception ignored) {
        }
        return name.toLowerCase().endsWith(".zip") ? name.substring(0, name.length() - 4) : name;
    }

    private static void copyTree(File from, File to) throws IOException {
        if (from.isDirectory()) {
            to.mkdirs();
            File[] children = from.listFiles();
            if (children != null)
                for (File child : children) copyTree(child, new File(to, child.getName()));
            return;
        }
        byte[] buffer = new byte[64 * 1024];
        try (InputStream in = new java.io.FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
            for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
        }
    }

    private static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null)
            for (File child : children) deleteTree(child);
        f.delete();
    }
}
