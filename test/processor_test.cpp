// Drives FarpProcessor like a host would: page messages, MIDI, state save and restore.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <fstream>
#include <sstream>

static std::string slurp(const char* f){std::ifstream i(f,std::ios::binary);std::stringstream s;s<<i.rdbuf();return s.str();}
static double run(FarpProcessor& p,int blocks,const std::vector<int>& notes,int onBlock,int offBlock){
  juce::AudioBuffer<float> b(2,512);double acc=0;size_t n=0;
  for(int k=0;k<blocks;k++){b.clear();juce::MidiBuffer m;
    if(k==onBlock)for(int nn:notes)m.addEvent(juce::MidiMessage::noteOn(1,nn,(juce::uint8)110),17);
    if(k==offBlock)for(int nn:notes)m.addEvent(juce::MidiMessage::noteOff(1,nn),5);
    p.processBlock(b,m);for(int c=0;c<2;c++)for(int i=0;i<512;i++){double v=b.getSample(c,i);acc+=v*v;n++;}}
  return std::sqrt(acc/n);}
static int pageLog(const char* file){
  // messages exactly as the plugin page sent them (recorded in the browser), then MIDI to each instrument
  farp::Val arr=farp::Val::parse(slurp(file));FarpProcessor p;p.setPlayConfigDetails(2,2,48000,512);p.prepareToPlay(48000,512);
  for(size_t i=0;i<arr.size();i++)p.fromPage(juce::String(arr[i].str()));
  run(p,4,{},-1,-1);
  for(const char* inst:{"","p5","e2","dx","cz","sd1","cm","p5r3","six"}){
    p.fromPage("{\"t\":\"route\",\"keys\":\""+juce::String(inst)+"\",\"split\":{\"on\":false},\"vr\":null}");
    p.fromPage("{\"t\":\"eng\",\"i\":\"p5r3\",\"op\":\"run\",\"v\":1}");p.fromPage("{\"t\":\"eng\",\"i\":\"six\",\"op\":\"run\",\"v\":1}");
    p.fromPage("{\"t\":\"voc\",\"on\":0,\"eng\":\"clear\",\"ch\":-1}");
    int boot=(std::string(inst)=="p5r3"||std::string(inst)=="six")?180:2;run(p,boot,{},-1,-1);
    double r=run(p,120,{48,55,60,64},2,80);printf("page log -> %-5s rms %.4f\n",inst[0]?inst:"omni",r);}
  juce::MemoryBlock mb;p.getStateInformation(mb);printf("state %d bytes\n",(int)mb.getSize());
  return 0;}
int main(int argc,char**argv){
  juce::ScopedJuceInitialiser_GUI init;
  if(std::string(argv[1]).find("plugsent")!=std::string::npos)return pageLog(argv[1]);
  // a script from the engine tests: its messages are what the page sends
  farp::Val S=farp::Val::parse(slurp(argv[1]));
  FarpProcessor p;p.setPlayConfigDetails(2,2,48000,512);p.prepareToPlay(48000,512);
  const farp::Val& ev=S["ev"];std::string text=slurp(argv[1]);
  // send each setting message as the page would (JSON text)
  for(size_t i=0;i<ev.size();i++){const farp::Val& m=ev[i]["m"];if(m["t"].str()=="on"||m["t"].str()=="off")continue;
    // re-serialise via JUCE for the test: take the text of the message from the file
  }
  // easier: the page's messages are in the file verbatim; pull each "m":{...} object by bracket walking
  size_t pos=0;int sent=0;std::string inst=argc>2?argv[2]:"";
  while((pos=text.find("\"m\": {",pos))!=std::string::npos){size_t st=pos+5;int d=0;bool q=false;size_t i=st;for(;i<text.size();i++){char c=text[i];if(q){if(c=='\\')i++;else if(c=='"')q=false;continue;}if(c=='"')q=true;else if(c=='{')d++;else if(c=='}'){d--;if(!d){i++;break;}}}
    std::string m=text.substr(st,i-st);pos=i;if(m.find("\"t\": \"on\"")!=std::string::npos||m.find("\"t\": \"off\"")!=std::string::npos)continue;p.fromPage(juce::String(m));sent++;}
  p.fromPage("{\"t\":\"route\",\"keys\":\""+juce::String(inst)+"\",\"split\":{\"on\":false,\"point\":60,\"lo\":{},\"hi\":{}},\"vr\":null}");
  if(inst=="p5r3"||inst=="six")p.fromPage("{\"t\":\"eng\",\"i\":\""+juce::String(inst)+"\",\"op\":\"run\",\"v\":1}");
  int boot=inst=="p5r3"?500:inst=="six"?700:2;
  run(p,boot*128/512+2,{},-1,-1);
  double r1=run(p,200,{48,55,60,64},2,120);
  juce::MemoryBlock mb;p.getStateInformation(mb);
  FarpProcessor q;q.setPlayConfigDetails(2,2,48000,512);q.prepareToPlay(48000,512);q.setStateInformation(mb.getData(),(int)mb.getSize());
  run(q,boot*128/512+2,{},-1,-1);
  double r2=run(q,200,{48,55,60,64},2,120);
  printf("%s msgs %d  rms %.4f  after state restore %.4f  state %d bytes\n",argv[1],sent,r1,r2,(int)mb.getSize());
  return 0;}
