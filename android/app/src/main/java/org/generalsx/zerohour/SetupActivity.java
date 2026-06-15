package org.generalsx.zerohour;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * @port Android self-contained build — first-run game-data extractor.
 *
 * The full game assets (~2.2GB of .big files + Data/) are bundled in the APK as
 * assets/gamedata.zip. On first launch we unpack them into the app's internal files
 * dir (where the native engine chdir's and reads them), write a completion marker,
 * then hand off to ZerohourActivity (the SDL game). Subsequent launches see the
 * marker and jump straight to the game. This makes the APK install-and-play with
 * no separate asset deployment.
 */
public class SetupActivity extends Activity {

    private static final String MARKER = ".gamedata_ok_v1";
    private TextView mStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        File marker = new File(getFilesDir(), MARKER);
        if (marker.exists()) {
            // Already installed — go straight to the game.
            launchGame();
            return;
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.rgb(0xB0, 0x12, 0x12));
        root.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        TextView title = new TextView(this);
        title.setText("GENERALS ZERO HOUR");
        title.setTextColor(Color.WHITE);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        mStatus = new TextView(this);
        mStatus.setText("Installing game data…\nFirst launch only — please wait.");
        mStatus.setTextColor(Color.rgb(0xF0, 0xE0, 0xC0));
        mStatus.setTextSize(16);
        mStatus.setGravity(Gravity.CENTER);
        mStatus.setPadding(40, 40, 40, 40);
        root.addView(mStatus);

        setContentView(root);

        new Thread(this::extract, "gamedata-extract").start();
    }

    private void setStatus(final String s) {
        runOnUiThread(() -> { if (mStatus != null) mStatus.setText(s); });
    }

    private void extract() {
        File filesDir = getFilesDir();
        File tmpMarker = new File(filesDir, MARKER);
        long bytes = 0;
        int count = 0;
        try {
            // The game-data zip is bundled split into <2GB parts (gamedata000.pak, 001, …)
            // because AGP can't process a single >2GB asset. Concatenate the parts back into
            // the original zip byte-stream. ACCESS_STREAMING so parts aren't mmap'd.
            java.util.Vector<InputStream> parts = new java.util.Vector<>();
            for (int p = 0; ; p++) {
                String name = String.format(java.util.Locale.US, "gamedata%03d.pak", p);
                try {
                    parts.add(getAssets().open(name, android.content.res.AssetManager.ACCESS_STREAMING));
                } catch (java.io.IOException notFound) {
                    break;
                }
            }
            if (parts.isEmpty()) throw new java.io.IOException("no gamedata*.pak parts in APK assets");
            setStatus("Installing game data…\n(" + parts.size() + " parts, first launch only)");
            InputStream rawIn = new java.io.SequenceInputStream(parts.elements());
            ZipInputStream zis = new ZipInputStream(new BufferedInputStream(rawIn, 1 << 20));
            byte[] buf = new byte[1 << 20];
            ZipEntry e;
            while ((e = zis.getNextEntry()) != null) {
                File out = new File(filesDir, e.getName());
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                FileOutputStream fos = new FileOutputStream(out);
                BufferedOutputStream bos = new BufferedOutputStream(fos, 1 << 20);
                int r;
                while ((r = zis.read(buf)) != -1) {
                    bos.write(buf, 0, r);
                    bytes += r;
                }
                bos.flush();
                bos.close();
                fos.close();
                zis.closeEntry();
                count++;
                if ((count % 4) == 0) {
                    setStatus("Installing game data…\n" + count + " files, "
                            + (bytes / (1024 * 1024)) + " MB\n(first launch only)");
                }
            }
            zis.close();

            // Completion marker written LAST so an interrupted extract re-runs cleanly.
            FileOutputStream mk = new FileOutputStream(tmpMarker);
            mk.write(("ok " + count + " files " + bytes + " bytes\n").getBytes("UTF-8"));
            mk.close();

            setStatus("Done — starting game…");
            launchGame();
        } catch (Throwable ex) {
            setStatus("Install failed:\n" + ex + "\n(free space? reinstall?)");
        }
    }

    private void launchGame() {
        Intent i = new Intent(this, ZerohourActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(i);
        finish();
    }
}
