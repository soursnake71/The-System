package com.thesnakedevice.wheel;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.AbsListView;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/** The full-screen crate browser: crates on the left, tracks on the right, search, LOAD A / LOAD B. */
@SuppressLint("ViewConstructor")
final class LibraryView extends LinearLayout {

    interface Callbacks {
        void chooseFolder();
        void closeLibrary();
        void load(int deck, Library.Track t);
        void crateChosen(String crateId);
    }

    private final Library lib;
    private final Callbacks cb;
    private final float dp;
    private final TextView status;
    private final EditText search;
    private final ListView crateList, trackList;
    private final CrateAdapter crateAdapter = new CrateAdapter();
    private final TrackAdapter trackAdapter = new TrackAdapter();
    private List<Library.Crate> crates = Collections.emptyList();
    private List<Library.Track> shown = new ArrayList<>();
    int crateSel = 0;
    Library.Track selected;
    /** MIDI encoder target: 0 crates, 1 tracks. */
    int focusList = 0;
    String[] deckPaths = { null, null };   // what each deck has (for the A / B marks)

    LibraryView(Context c, Library lib, Callbacks cb) {
        super(c);
        this.lib = lib;
        this.cb = cb;
        dp = c.getResources().getDisplayMetrics().density;
        setOrientation(VERTICAL);
        setBackgroundColor(Pal.BG);
        setClickable(true);   // swallow touches meant for the decks underneath
        int pad = px(6);
        setPadding(pad, pad, pad, pad);

        LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        int barH = px(48);
        bar.addView(button("◀ BACK", Pal.TEXT, v -> cb.closeLibrary()), new LayoutParams(px(96), barH));
        bar.addView(space());
        bar.addView(button("SD CARD / FOLDER…", Pal.TEXT, v -> cb.chooseFolder()), new LayoutParams(px(170), barH));
        bar.addView(space());
        status = new TextView(c);
        status.setTextColor(Pal.DIM);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        status.setTypeface(Typeface.DEFAULT_BOLD);
        status.setMaxLines(2);
        bar.addView(status, new LayoutParams(0, barH, 1f));
        bar.addView(space());
        search = new EditText(c);
        search.setHint("Search title / artist / key / BPM");
        search.setHintTextColor(Pal.FAINT);
        search.setTextColor(Pal.TEXT);
        search.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        search.setSingleLine(true);
        search.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        search.setImeOptions(EditorInfo.IME_ACTION_SEARCH | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        search.setBackground(box(Pal.WELL, Pal.EDGE_HI));
        search.setPadding(px(10), 0, px(10), 0);
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int d) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int d) {}
            @Override public void afterTextChanged(Editable s) { refilter(); }
        });
        search.setOnEditorActionListener((v, id, e) -> { hideKeyboard(); return true; });
        bar.addView(search, new LayoutParams(0, barH, 1.4f));
        bar.addView(space());
        bar.addView(button("LOAD A", Pal.DECK[0], v -> loadSel(0)), new LayoutParams(px(96), barH));
        bar.addView(space());
        bar.addView(button("LOAD B", Pal.DECK[1], v -> loadSel(1)), new LayoutParams(px(96), barH));
        addView(bar, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, barH));

        LinearLayout body = new LinearLayout(c);
        body.setOrientation(HORIZONTAL);
        body.setPadding(0, px(6), 0, 0);
        crateList = list(c);
        crateList.setAdapter(crateAdapter);
        crateList.setOnItemClickListener((p, v, pos, id) -> { focusList = 0; pickCrate(pos); });
        trackList = list(c);
        trackList.setAdapter(trackAdapter);
        trackList.setOnItemClickListener((p, v, pos, id) -> {
            focusList = 1;
            if (pos >= 0 && pos < shown.size()) { selected = shown.get(pos); trackAdapter.notifyDataSetChanged(); }
            hideKeyboard();
        });
        body.addView(crateList, new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        View gap = new View(c);
        body.addView(gap, new LayoutParams(px(6), 1));
        body.addView(trackList, new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 2.6f));
        addView(body, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
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
        t.setBackground(box(Pal.RAISED, color == Pal.TEXT ? Pal.EDGE_HI : color));
        t.setOnClickListener(l);
        return t;
    }

    private ListView list(Context c) {
        ListView l = new ListView(c);
        l.setBackgroundColor(Pal.PANEL);
        l.setDivider(null);
        l.setDividerHeight(0);
        l.setSelector(new android.graphics.drawable.ColorDrawable(0));
        l.setFastScrollEnabled(true);
        l.setCacheColorHint(Pal.PANEL);
        l.setOnScrollListener(new AbsListView.OnScrollListener() {
            @Override public void onScrollStateChanged(AbsListView v, int s) { if (s != SCROLL_STATE_IDLE) hideKeyboard(); }
            @Override public void onScroll(AbsListView v, int a, int b, int c2) {}
        });
        return l;
    }

    void hideKeyboard() {
        InputMethodManager im = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (im != null) im.hideSoftInputFromWindow(search.getWindowToken(), 0);
        search.clearFocus();
    }

    private void loadSel(int deck) {
        if (selected == null) { status.setText("Pick a track first"); return; }
        hideKeyboard();
        cb.load(deck, selected);
    }

    /** The library changed (scan progress, tags arrived). */
    void refresh(boolean tagsOnly, String lastCrateId) {
        status.setText(lib.status + (lib.treeName.isEmpty() ? "" : "\n" + lib.treeName));
        if (!tagsOnly && crates != lib.crates) {
            Library.Crate cur = crateSel >= 0 && crateSel < crates.size() ? crates.get(crateSel) : null;
            String want = cur != null ? cur.id : lastCrateId;
            crates = lib.crates;
            crateSel = 0;
            for (int i = 0; i < crates.size(); ++i) if (crates.get(i).id.equals(want)) crateSel = i;
            crateAdapter.notifyDataSetChanged();
            refilter();
        } else if (tagsOnly) {
            trackAdapter.notifyDataSetChanged();
        }
    }

    private void pickCrate(int pos) {
        if (pos < 0 || pos >= crates.size()) return;
        crateSel = pos;
        crateAdapter.notifyDataSetChanged();
        refilter();
        trackList.setSelection(0);
        cb.crateChosen(crates.get(pos).id);
    }

    private void refilter() {
        List<Library.Track> src = crateSel >= 0 && crateSel < crates.size() ? crates.get(crateSel).tracks : Collections.emptyList();
        String q = search.getText().toString().trim().toLowerCase(Locale.ROOT);
        if (q.isEmpty()) shown = new ArrayList<>(src);
        else {
            String[] terms = q.split("\\s+");
            List<Library.Track> out = new ArrayList<>();
            for (Library.Track t : src) {
                String s = t.search();
                boolean all = true;
                for (String term : terms) if (!s.contains(term)) { all = false; break; }
                if (all) out.add(t);
            }
            shown = out;
        }
        trackAdapter.notifyDataSetChanged();
    }

    // ---- MIDI browsing (the MPC Sample's encoder): move / press / back
    void midiMove(int delta) {
        if (focusList == 0) {
            if (crates.isEmpty()) return;
            int p = Math.max(0, Math.min(crates.size() - 1, crateSel + delta));
            if (p != crateSel) pickCrate(p);
            crateList.smoothScrollToPosition(p);
        } else {
            if (shown.isEmpty()) return;
            int cur = selected != null ? shown.indexOf(selected) : -1;
            int p = Math.max(0, Math.min(shown.size() - 1, cur + delta));
            selected = shown.get(p);
            trackAdapter.notifyDataSetChanged();
            trackList.smoothScrollToPosition(p);
        }
    }
    /** Encoder press: in the crates, go into the tracks; in the tracks, load the selected one on the deck. */
    void midiSelect(int deck) {
        if (focusList == 0) {
            focusList = 1;
            if (selected == null && !shown.isEmpty()) selected = shown.get(0);
            trackAdapter.notifyDataSetChanged();
        } else if (selected != null) cb.load(deck, selected);
    }
    /** MixTrack Go browse push: in the crates, go into the tracks (first one selected); in the tracks, nothing. */
    void midiEnter() {
        if (focusList != 0) return;
        focusList = 1;
        if (selected == null && !shown.isEmpty()) selected = shown.get(0);
        trackAdapter.notifyDataSetChanged();
        crateAdapter.notifyDataSetChanged();
    }
    /** A track went on a deck from the controller (LOAD A / B): the A / B marks follow. */
    void markLoaded(int deck, String rel) {
        deckPaths[deck] = rel;
        trackAdapter.notifyDataSetChanged();
    }
    void midiBack() {
        if (focusList == 1) { focusList = 0; trackAdapter.notifyDataSetChanged(); crateAdapter.notifyDataSetChanged(); }
        else cb.closeLibrary();
    }

    // ---- rows
    private final class CrateAdapter extends BaseAdapter {
        @Override public int getCount() { return crates.size(); }
        @Override public Object getItem(int i) { return crates.get(i); }
        @Override public long getItemId(int i) { return i; }
        @Override public View getView(int i, View v, ViewGroup parent) {
            Row r = v instanceof Row ? (Row) v : new Row(getContext(), false);
            r.crate = crates.get(i);
            r.sel = i == crateSel;
            r.focus = focusList == 0;
            r.invalidate();
            return r;
        }
    }

    private final class TrackAdapter extends BaseAdapter {
        @Override public int getCount() { return shown.size(); }
        @Override public Object getItem(int i) { return shown.get(i); }
        @Override public long getItemId(int i) { return i; }
        @Override public View getView(int i, View v, ViewGroup parent) {
            Row r = v instanceof Row ? (Row) v : new Row(getContext(), true);
            r.track = shown.get(i);
            r.sel = r.track == selected;
            r.focus = focusList == 1;
            r.odd = (i & 1) == 1;
            r.invalidate();
            return r;
        }
    }

    private final class Row extends View {
        final boolean isTrack;
        Library.Crate crate;
        Library.Track track;
        boolean sel, focus, odd;
        final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

        Row(Context c, boolean isTrack) {
            super(c);
            this.isTrack = isTrack;
            p.setTypeface(Typeface.DEFAULT_BOLD);
            setLayoutParams(new AbsListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(isTrack ? 44 : 42)));
        }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight();
            int bg = sel ? (focus ? Pal.TEXT : 0xff2a3a4d) : (odd ? 0xff111316 : Pal.PANEL);
            c.drawColor(bg);
            int fg = sel && focus ? 0xff000000 : Pal.TEXT;
            int dim = sel && focus ? 0xff333333 : Pal.DIM;
            float ts = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 15, getResources().getDisplayMetrics());
            ts = Math.min(ts, h * .42f);
            float base = h / 2f + ts * .36f;
            if (!isTrack) {
                if (crate == null) return;
                float x = px(10) + crate.depth * px(16);
                String icon = crate.kind == Library.K_ALL ? "♫ " : crate.kind == Library.K_CRATE ? (crate.depth > 0 ? "└ " : "▸ ")
                        : crate.kind == Library.K_PLAYLIST ? "≡ " : "▫ ";
                String s = icon + crate.name;
                p.setTextSize(ts);
                p.setColor(crate.kind == Library.K_CRATE ? (sel && focus ? 0xff000000 : Pal.AMBER) : fg);
                String cnt = String.valueOf(crate.tracks.size());
                float cw = p.measureText(cnt) + px(12);
                c.drawText(fit(s, w - x - cw - px(6)), x, base, p);
                p.setColor(dim);
                p.setTextSize(ts * .8f);
                c.drawText(cnt, w - cw + px(4), base, p);
                if (sel) { p.setColor(Pal.DECK[0]); c.drawRect(0, 0, px(4), h, p); }
                return;
            }
            Library.Track t = track;
            if (t == null) return;
            // columns: mark | title | artist | length | BPM | key
            float mark = px(22);
            float keyW = px(48), bpmW = px(56), lenW = px(52);
            float rest = w - mark - keyW - bpmW - lenW - px(10);
            float titleW = rest * .56f, artistW = rest - titleW;
            String rel = t.rel;
            int onDeck = rel.equals(deckPaths[0]) ? 0 : rel.equals(deckPaths[1]) ? 1 : -1;
            if (onDeck >= 0) {
                p.setColor(Pal.DECK[onDeck]);
                c.drawRect(0, 0, px(4), h, p);
                p.setTextSize(ts * .9f);
                c.drawText(onDeck == 0 ? "A" : "B", px(8), base, p);
            }
            p.setTextSize(ts);
            p.setColor(fg);
            float x = mark;
            c.drawText(fit(t.label(), titleW - px(8)), x, base, p);
            x += titleW;
            p.setColor(dim);
            p.setTextSize(ts * .9f);
            c.drawText(fit(t.artist, artistW - px(8)), x, base, p);
            x += artistW;
            c.drawText(fit(Library.lengthText(t.seconds), lenW - px(6)), x, base, p);
            x += lenW;
            p.setColor(fg);
            c.drawText(fit(bpmText(t.bpm), bpmW - px(6)), x, base, p);
            x += bpmW;
            if (!t.key.isEmpty()) {
                p.setColor(Pal.keyColour(t.key));
                float kw = Math.min(keyW - px(6), p.measureText(t.key) + px(10));
                c.drawRoundRect(x, h * .22f, x + kw, h * .78f, px(3), px(3), p);
                p.setColor(0xff000000);
                p.setTextAlign(Paint.Align.CENTER);
                c.drawText(fit(t.key, kw), x + kw / 2, base, p);
                p.setTextAlign(Paint.Align.LEFT);
            } else if (!t.tagged) {
                p.setColor(Pal.FAINT);
                c.drawText("…", x, base, p);
            }
            p.setColor(0xff1d2024);
            c.drawRect(0, h - 1, w, h, p);
        }

        private String bpmText(String b) {
            if (b == null || b.isEmpty()) return "";
            try {
                double v = Double.parseDouble(b.trim());
                return v <= 0 ? "" : (Math.abs(v - Math.round(v)) < .05 ? String.valueOf(Math.round(v)) : String.format(Locale.US, "%.1f", v));
            } catch (NumberFormatException e) { return b; }
        }

        private String fit(String s, float maxW) {
            if (s == null) return "";
            if (maxW <= 0) return "";
            if (p.measureText(s) <= maxW) return s;
            int n = p.breakText(s, true, Math.max(0, maxW - p.measureText("…")), null);
            return n <= 0 ? "" : s.substring(0, n) + "…";
        }
    }
}
