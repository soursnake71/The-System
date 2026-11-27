// Omega XR10 as WebAssembly for the Android / web app (built by xr10/tools/build-wasm.sh, no libc).
#include "xr10.h"
extern "C" {
void* memset(void* d, int c, unsigned long n) { u8* p = (u8*)d; while (n--) *p++ = (u8)c; return d; }
void* memcpy(void* d, const void* s, unsigned long n) { u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d; }
void* memmove(void* d, const void* s, unsigned long n) {
  u8* a = (u8*)d; const u8* b = (const u8*)s;
  if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
  return d;
}
}
static xr::Machine M;
static float outL[4096], outR[4096];
static u8 scratch[12 * 1024 * 1024];   // state in / out and sample uploads
static u8 gridBuf[xr::Machine::GRID_ROWS * 33];

#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT void xr_init(double sr) { M.init(sr); }
EXPORT float* xr_out_l() { return outL; }
EXPORT float* xr_out_r() { return outR; }
EXPORT void xr_process(int n) { if (n > 4096) n = 4096; M.process(outL, outR, 0, 0, n); }
EXPORT void xr_button(int b, int down) { M.button(b, down); }
EXPORT void xr_pad(int i, int down, int vel) { M.pad(i, down, vel); }
EXPORT void xr_knob(int d) { M.knob(d); }
EXPORT void xr_midi(int s, int a, int b) { M.midi(s, a, b); }
EXPORT void xr_trigger(int s, int vel) { M.hitSound(s, vel); }
EXPORT xr::Status* xr_status() { return &M.status(); }
EXPORT int xr_get(int k) { return M.get(k); }
EXPORT void xr_set(int k, int v) { M.put(k, v); }
EXPORT int xr_param(int s, int p) { return M.vp[s][p]; }
EXPORT void xr_set_param(int s, int p, int v) { M.setParam(s, p, v); }
EXPORT u8* xr_grid(int bar) { M.grid(bar, gridBuf); return gridBuf; }
EXPORT void xr_grid_toggle(int row, int bar, int step) { M.gridToggle(row, bar, step); }
EXPORT void xr_grid_assign(int row, int s) { M.gridAssign(row, s); }
EXPORT int xr_grid_steps() { return M.gridSteps(); }
EXPORT u8* xr_scratch() { return scratch; }
EXPORT int xr_scratch_size() { return sizeof scratch; }
EXPORT int xr_save() { xr::Machine::Writer w{scratch, 0, sizeof scratch}; M.save(w); return w.n <= sizeof scratch ? (int)w.n : -1; }
EXPORT int xr_load(int len) { return M.load(scratch, (u32)len) ? 1 : 0; }
EXPORT int xr_load_sample(int s, int len, double rate) { return M.loadSample(s, (const float*)scratch, len, rate) ? 1 : 0; }
EXPORT void xr_clear_sample(int s) { M.clearSample(s); }
EXPORT const char* xr_sound_name(int i) { return xr::SOUND_NAMES[i & 63]; }
EXPORT const char* xr_pattern_name(int p) { return M.pat[p < 0 ? 0 : (p > 98 ? 98 : p)].name; }
EXPORT const char* xr_param_name(int p) { return xr::VPARAMS[p].name; }
EXPORT int xr_param_lo(int p) { return xr::VPARAMS[p].lo; }
EXPORT int xr_param_hi(int p) { return xr::VPARAMS[p].hi; }
EXPORT xr::Song* xr_song(int i) { return &M.songs[i < 0 ? 0 : (i >= xr::NSONG ? xr::NSONG - 1 : i)]; }
EXPORT void xr_song_set(int song, int step, int p, int part, int rep) { M.songSet(song, step, p, part, rep); }
EXPORT void xr_song_insert(int song, int step) { M.songInsert(song, step); }
EXPORT void xr_song_tempo(int song, int bpm) { M.songTempo(song, bpm); }
EXPORT void xr_factory_reset() { M.stop(); M.factoryReset(); }
EXPORT void xr_clear_part() { M.clearPart(); }
EXPORT void xr_song_delete(int song, int step) { M.songDelete(song, step); }
EXPORT void xr_set_pad(int bank, int pad, int s) { M.pads[bank & 3][pad % 15] = (u8)(s & 63); }
EXPORT int xr_status_size() { return sizeof(xr::Status); }
EXPORT int xr_has_user(int s) { return M.user[s & 63].len > 0; }
EXPORT int xr_render_sound(int s) {   // 16-bit sample of a sound into scratch (for the waveform view)
  int len; const s16* d = M.sampleOf(s & 63, len);
  if (len * 2 > (int)sizeof scratch) len = sizeof scratch / 2;
  for (int i = 0; i < len; i++) ((s16*)scratch)[i] = d[i];
  return len;
}
