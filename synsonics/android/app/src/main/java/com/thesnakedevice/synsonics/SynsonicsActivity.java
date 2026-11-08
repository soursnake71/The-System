package com.thesnakedevice.synsonics;

import android.app.NativeActivity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

/**
 * The app is C++ (src/main/cpp). This class exists only because Android's file picker (the Storage Access
 * Framework) answers through onActivityResult, which a plain NativeActivity can't receive. The C++ side calls
 * openDocument / createDocument, polls resultOp / resultUri, then opens the file with openFd and does all the
 * reading, writing, decoding and encoding itself.
 */
public class SynsonicsActivity extends NativeActivity {
  private static final int BASE = 4100;
  /** -1: nothing new; >= 0: the file operation that got a file; -2: the picker was cancelled */
  public volatile int resultOp = -1;
  public volatile String resultUri = null;

  public void openDocument(final int op, final String[] mimes) {
    runOnUiThread(() -> {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("*/*");
      i.putExtra(Intent.EXTRA_MIME_TYPES, mimes);
      try { startActivityForResult(i, BASE + op); } catch (Exception e) { resultOp = -2; }
    });
  }

  public void createDocument(final int op, final String mime, final String name) {
    runOnUiThread(() -> {
      Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType(mime);
      i.putExtra(Intent.EXTRA_TITLE, name);
      try { startActivityForResult(i, BASE + op); } catch (Exception e) { resultOp = -2; }
    });
  }

  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (request < BASE || request > BASE + 16) return;
    if (result == RESULT_OK && data != null && data.getData() != null) {
      resultUri = data.getData().toString();
      resultOp = request - BASE;
    } else {
      resultOp = -2;
    }
  }

  /** A file descriptor the C++ side owns (and closes); mode "r" or "wt". -1 if it can't be opened. */
  public int openFd(String uri, String mode) {
    try {
      ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(Uri.parse(uri), mode);
      return pfd == null ? -1 : pfd.detachFd();
    } catch (Exception e) {
      return -1;
    }
  }

  public String displayName(String uri) {
    try (Cursor c = getContentResolver().query(Uri.parse(uri), new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
      if (c != null && c.moveToFirst()) return c.getString(0);
    } catch (Exception e) {
      // fall through
    }
    return "file";
  }
}
