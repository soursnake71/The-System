#include "../Source/dsp/Rack.h"
#include <fstream>
#include <sstream>
int main(int argc,char**argv){
  std::ifstream c(std::string(argv[3])+".cfg.json");std::stringstream ss;ss<<c.rdbuf();farp::Val cfg=farp::Val::parse(ss.str());
  auto U=farp::rack::make(argv[1],48000,cfg);
  std::ifstream f(argv[2],std::ios::binary);f.seekg(0,std::ios::end);size_t bytes=f.tellg();f.seekg(0);std::vector<float> in(bytes/4);f.read((char*)in.data(),bytes);
  const int B=128;size_t n=in.size()/B;std::vector<float> res(n*B*2);float L[B],R[B];
  for(size_t k=0;k<n;k++){const float* a=in.data()+k*B;U->process(a,a,L,R,B);for(int i=0;i<B;i++){res[(k*B+i)*2]=L[i];res[(k*B+i)*2+1]=R[i];}}
  std::ofstream o(argv[4],std::ios::binary);o.write((char*)res.data(),res.size()*4);}
