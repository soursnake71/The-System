// Omega XR10 as WebAssembly for the Android / web app (built by xr10/tools/build-wasm.sh, no libc).
#include "xr10.h"
#include "../rack/rack.h"
#include <cstdlib>
#include <new>
// built with the C++ library (wasm32-wasi) for the rack; no exceptions: allocation failure stops the module
void* operator new(size_t n) { void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
extern "C" void* __cxa_allocate_exception(size_t) { std::abort(); }
extern "C" void __cxa_throw(void*, void*, void (*)(void*)) { std::abort(); }
static xr::Machine M;
static float outL[4096], outR[4096], auxL[4096], auxR[4096], outPeak[2];
static xrack::Rack R;
static std::string rackText;
static u8 scratch[12 * 1024 * 1024];   // state in / out and sample uploads
static u8 gridBuf[xr::Machine::GRID_ROWS * 33];

#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT void xr_init(double sr) { M.init(sr); R.prepare(sr, 4096); R.defaults(); }
EXPORT void xr_init_pack(double sr, int len) { M.init(sr, scratch, (u32)len); R.prepare(sr, 4096); R.defaults(); }   // the sample ROM (rom.bin) in scratch
EXPORT int xr_from_pack(int s) { return M.fromPack[s & 63] ? 1 : 0; }
EXPORT int xr_export_midi(int song) { u32 n = M.exportMidi(scratch, sizeof scratch, song); return n <= sizeof scratch ? (int)n : -1; }
EXPORT int xr_import_midi(int len) { return M.importMidi(scratch, (u32)len); }
EXPORT int xr_match_name(int len) { if (len > 200) len = 200; char n[201]; for (int i = 0; i < len; i++) n[i] = (char)scratch[i]; n[len] = 0; return xr::Machine::matchSoundName(n); }
EXPORT float* xr_out_l() { return outL; }
EXPORT float* xr_out_r() { return outR; }
EXPORT void xr_process(int n) {
  if (n > 4096) n = 4096;
  M.process(outL, outR, auxL, auxR, n);   // aux: the EFFECT jack
  R.bpm = M.set[xr::K_TEMPO] / 10.0;
  R.process(auxL, outL, outR, n);
  for (int i = 0; i < 2; i++) outPeak[i] *= 0.9f;
  for (int i = 0; i < n; i++) { float a = outL[i] < 0 ? -outL[i] : outL[i], b = outR[i] < 0 ? -outR[i] : outR[i]; if (a > outPeak[0]) outPeak[0] = a; if (b > outPeak[1]) outPeak[1] = b; }
}
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
// the memory file: the machine's state, then the rack's settings as text
EXPORT int xr_save() {
  xr::Machine::Writer w{scratch, 0, sizeof scratch}; M.save(w);
  std::string t = R.save();
  if (w.n + t.size() > sizeof scratch) return -1;
  for (size_t i = 0; i < t.size(); i++) scratch[w.n + i] = (u8)t[i];
  return (int)(w.n + t.size());
}
EXPORT int xr_load(int len) {
  if (!M.load(scratch, (u32)len)) return 0;
  u32 at = M.loadedBytes;
  if (at < (u32)len && !R.load((const char*)scratch + at, len - at)) R.defaults();
  if (at >= (u32)len) R.defaults();
  return 1;
}
// ---- the fold-out: mixer ----
EXPORT u8* xr_mutes() { return M.mute; }
EXPORT u8* xr_solos() { return M.solo; }
EXPORT float* xr_meters() { return M.meter; }
EXPORT float* xr_out_peaks() { return outPeak; }
EXPORT void xr_mute(int s, int on) { M.mute[s & 63] = on ? 1 : 0; }
EXPORT void xr_solo(int s, int on) { M.solo[s & 63] = on ? 1 : 0; }
// ---- the fold-out: rack ----
EXPORT int xr_rack_units() { int n; xrack::unitTypes(n); return n; }
EXPORT const char* xr_rack_unit_name(int i) { int n; const xrack::UnitType* T = xrack::unitTypes(n); return i >= 0 && i < n ? T[i].name : ""; }
EXPORT void xr_rack_set_type(int slot, int t) { R.setType(slot, t); }
EXPORT int xr_rack_type(int slot) { return slot >= 0 && slot < xrack::Rack::NSLOT ? R.slot[slot].type : -1; }
EXPORT int xr_rack_describe(int slot) { rackText = R.describe(slot); return (int)rackText.size(); }
EXPORT const char* xr_rack_text() { return rackText.c_str(); }
EXPORT void xr_rack_set_param(int slot, int p, double v) { R.setParam(slot, p, v); }
EXPORT void xr_rack_preset(int slot, int k) { R.applyPreset(slot, k); }
EXPORT void xr_rack_bypass(int slot, int on) { if (slot >= 0 && slot < xrack::Rack::NSLOT) R.slot[slot].bypass = on != 0; }
EXPORT void xr_rack_parallel(int slot, int on) { if (slot >= 0 && slot < xrack::Rack::NSLOT) R.slot[slot].parallel = on != 0; }
EXPORT int xr_rack_flags(int slot) { return slot >= 0 && slot < xrack::Rack::NSLOT ? (R.slot[slot].bypass ? 1 : 0) | (R.slot[slot].parallel ? 2 : 0) : 0; }
EXPORT void xr_rack_set_return(double v) { R.ret = (float)(v < 0 ? 0 : (v > 1.5 ? 1.5 : v)); }
EXPORT double xr_rack_return() { return R.ret; }
EXPORT float xr_rack_peak(int i) { return i == 0 ? R.peakIn : (i == 1 ? R.peakOut : (i - 2 >= 0 && i - 2 < xrack::Rack::NSLOT ? R.slot[i - 2].peak : 0)); }
EXPORT int xr_load_sample(int s, int len, double rate) { return M.loadSample(s, (const float*)scratch, len, rate) ? 1 : 0; }
EXPORT void xr_clear_sample(int s) { M.clearSample(s); }
EXPORT const char* xr_sound_name(int i) { return xr::SOUND_NAMES[i & 63]; }
EXPORT const char* xr_pattern_name(int p) { return M.pat[p < 0 ? 0 : (p > 98 ? 98 : p)].name; }
EXPORT const char* xr_param_name(int p) { return xr::VPARAMS[p].name; }
EXPORT int xr_param_lo(int p) { return xr::VPARAMS[p].lo; }
EXPORT int xr_param_hi(int p) { return xr::VPARAMS[p].hi; }
EXPORT int xr_random_beat(int seed) { return M.randomBeat((u32)seed) ? 1 : 0; }
EXPORT void xr_random_sound(int seed) { M.randomSound((u32)seed); }
EXPORT int xr_nvp() { return xr::NVP; }
EXPORT int xr_nvp_xr10() { return xr::NVP_XR10; }
EXPORT int xr_nkeys() { return xr::NKEYS; }
EXPORT void xr_default_voice(int s) { M.defaultVoice(s & 63); }
EXPORT xr::Song* xr_song(int i) { return &M.songs[i < 0 ? 0 : (i >= xr::NSONG ? xr::NSONG - 1 : i)]; }
EXPORT void xr_song_set(int song, int step, int p, int part, int rep) { M.songSet(song, step, p, part, rep); }
EXPORT void xr_song_insert(int song, int step) { M.songInsert(song, step); }
EXPORT void xr_song_tempo(int song, int bpm) { M.songTempo(song, bpm); }
EXPORT void xr_factory_reset() { M.stop(); M.factoryReset(); R.defaults(); }
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
