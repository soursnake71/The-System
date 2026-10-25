// Runs a test script through the C++ port and writes interleaved float32 stereo.
#include "../Source/dsp/Core.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
int main(int argc,char**argv){
  std::ifstream f(argv[1],std::ios::binary);std::stringstream ss;ss<<f.rdbuf();farp::Val S=farp::Val::parse(ss.str());
  const double fs=S["fs"].num();const int blocks=(int)S["blocks"].num();farp::FarpDSP d(fs);
  std::vector<float> voice;if(S["voice"].truthy()){std::string dir=argv[1];dir=dir.substr(0,dir.find_last_of('/'));std::ifstream vf(dir+"/voice.f32",std::ios::binary);vf.seekg(0,std::ios::end);size_t n=vf.tellg();vf.seekg(0);voice.resize(n/4);vf.read((char*)voice.data(),n);}
  const int N=128;std::vector<float> res((size_t)blocks*N*2);float L[N],R[N];const farp::Val& ev=S["ev"];size_t ei=0;
  auto t0=std::chrono::steady_clock::now();
  for(int b=0;b<blocks;b++){while(ei<ev.size()&&ev[ei]["b"].num()<=b)d.msg(ev[ei++]["m"]);
    d.process(L,R,N,0,voice.empty()?nullptr:voice.data()+(size_t)b*N);
    for(int i=0;i<N;i++){res[((size_t)b*N+i)*2]=L[i];res[((size_t)b*N+i)*2+1]=R[i];}}
  auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  std::ofstream o(argv[2],std::ios::binary);o.write((char*)res.data(),res.size()*4);std::cout<<"cpp ms "<<ms<<"\n";}
