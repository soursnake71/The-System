package com.thesnakedevice.wheel;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/**
 * BLUETOOTH MIDI: the Bluetooth LE MIDI devices around (scanned by {@link BleMidi}); tapping one connects it, and the
 * last one connected can be reconnected without a scan. A full-screen overlay like the library.
 */
@SuppressLint("ViewConstructor")
final class BleView extends LinearLayout {
    interface Callbacks {
        void bleClose();
        void bleScan();
        void bleConnect(BleMidi.Found f);
        void bleReconnect();
    }

    private final Callbacks cb;
    private final float dp;
    private final TextView status, reconnect, scanBtn;
    private final LinearLayout rows;
    /** What the status says the device is doing: connecting to this address (shown on its row). */
    String connecting;
    /** Addresses of devices connected through this screen. */
    final java.util.Set<String> connected = new java.util.HashSet<>();

    BleView(Context c, Callbacks cb) {
        super(c);
        this.cb = cb;
        dp = c.getResources().getDisplayMetrics().density;
        setOrientation(VERTICAL);
        setBackgroundColor(Pal.BG);
        setClickable(true);
        int pad = px(6);
        setPadding(pad, pad, pad, pad);

        LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        int barH = px(48);
        bar.addView(button("◀ BACK", Pal.TEXT, v -> cb.bleClose()), new LayoutParams(px(96), barH));
        bar.addView(space());
        TextView title = new TextView(c);
        title.setText("BLUETOOTH MIDI");
        title.setTextColor(Pal.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        title.setGravity(Gravity.CENTER_VERTICAL);
        bar.addView(title, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, barH));
        bar.addView(space());
        status = new TextView(c);
        status.setTextColor(Pal.DIM);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        status.setTypeface(Typeface.DEFAULT_BOLD);
        status.setMaxLines(2);
        status.setGravity(Gravity.CENTER_VERTICAL);
        bar.addView(status, new LayoutParams(0, barH, 1f));
        bar.addView(space());
        reconnect = button("RECONNECT", Pal.CYAN, v -> cb.bleReconnect());
        bar.addView(reconnect, new LayoutParams(px(220), barH));
        bar.addView(space());
        scanBtn = button("SCAN", Pal.GREEN, v -> cb.bleScan());
        bar.addView(scanBtn, new LayoutParams(px(110), barH));
        addView(bar, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, barH));

        TextView hint = new TextView(c);
        hint.setText("The Numark MixTrack Go connects over Bluetooth only from here (pairing it in Android's settings doesn't work). "
                + "Switch the controller on, scan, tap it.");
        hint.setTextColor(Pal.DIM);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        hint.setPadding(px(4), px(6), px(4), px(6));
        addView(hint, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView sv = new ScrollView(c);
        sv.setBackgroundColor(Pal.PANEL);
        rows = new LinearLayout(c);
        rows.setOrientation(VERTICAL);
        sv.addView(rows, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        addView(sv, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setReconnect(null);
    }

    private int px(float v) { return Math.round(v * dp); }

    private View space() { View v = new View(getContext()); v.setLayoutParams(new LayoutParams(px(6), 1)); return v; }

    private GradientDrawable box(int fillCol, int edge) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fillCol);
        g.setCornerRadius(6 * dp);
        g.setStroke(Math.max(1, px(1.5f)), edge);
        return g;
    }

    private TextView button(String label, int color, View.OnClickListener l) {
        TextView t = new TextView(getContext());
        t.setText(label);
        t.setTextColor(color);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        t.setGravity(Gravity.CENTER);
        t.setMaxLines(1);
        t.setEllipsize(android.text.TextUtils.TruncateAt.END);
        t.setPadding(px(6), 0, px(6), 0);
        t.setBackground(box(Pal.RAISED, color == Pal.TEXT ? Pal.EDGE_HI : color));
        t.setOnClickListener(l);
        return t;
    }

    void setStatus(boolean scanning, String msg) {
        status.setText(msg == null ? "" : msg);
        scanBtn.setText(scanning ? "SCANNING…" : "SCAN");
    }

    /** The last device connected (name, or null = none: the button hides). */
    void setReconnect(String name) {
        reconnect.setVisibility(name == null ? GONE : VISIBLE);
        if (name != null) reconnect.setText("RECONNECT " + name);
    }

    void setDevices(List<BleMidi.Found> list) {
        rows.removeAllViews();
        if (list.isEmpty()) {
            TextView t = new TextView(getContext());
            t.setText("No Bluetooth MIDI devices found yet");
            t.setTextColor(Pal.FAINT);
            t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
            t.setGravity(Gravity.CENTER);
            rows.addView(t, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(64)));
            return;
        }
        for (int i = 0; i < list.size(); ++i) {
            BleMidi.Found f = list.get(i);
            LinearLayout r = new LinearLayout(getContext());
            r.setOrientation(HORIZONTAL);
            r.setGravity(Gravity.CENTER_VERTICAL);
            r.setPadding(px(12), 0, px(12), 0);
            r.setBackgroundColor((i & 1) == 1 ? 0xff111316 : Pal.PANEL);
            boolean mt = MidiIn.isMixtrackName(f.name);
            TextView name = new TextView(getContext());
            name.setText((mt ? "● " : "") + f.label());
            name.setTextColor(mt ? Pal.DECK[0] : Pal.TEXT);
            name.setTypeface(Typeface.DEFAULT_BOLD);
            name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
            name.setMaxLines(1);
            r.addView(name, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            TextView addr = new TextView(getContext());
            addr.setText(f.address + "   " + f.rssi + " dBm");
            addr.setTextColor(Pal.DIM);
            addr.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            r.addView(addr, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            r.addView(space());
            boolean on = connected.contains(f.address), busy = f.address.equals(connecting);
            TextView go = button(on ? "CONNECTED" : busy ? "CONNECTING…" : "CONNECT", on ? Pal.GREEN : Pal.CYAN, v -> cb.bleConnect(f));
            r.addView(go, new LayoutParams(px(140), px(40)));
            r.setOnClickListener(v -> cb.bleConnect(f));
            rows.addView(r, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(56)));
        }
    }
}
