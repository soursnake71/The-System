#include "../Source/dsp/Core.h"
int main(){farp::FarpDSP d(48000);float L[128],R[128];d.process(L,R,128,0,nullptr);return 0;}
