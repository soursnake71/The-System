// Omega SID 64's reverb is the Omega REV7 (plugins/dsp/Rev7.h, its programs and presets in plugins/src/Rev7.cpp and
// plugins/src/Rev7Library.inc). This file compiles that plugin's spec (parameters, 30 factory programs, artist
// presets and the library) once, under its own name, for the SID engine. Build it into every Omega SID target.
#define makeSpec omegaRev7MakeSpec
#include "../src/Rev7.cpp"
#undef makeSpec

namespace sid {
const omega::Spec& rev7Spec() { static const omega::Spec s = omega::omegaRev7MakeSpec(); return s; }
}  // namespace sid
