#include "wav.h"
#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<uint8_t>;
void put16(Bytes &b, uint16_t v) { b.push_back(v); b.push_back(v >> 8); }
void put32(Bytes &b, uint32_t v) { put16(b, v); put16(b, v >> 16); }
void tag(Bytes &b, const char *s) { b.insert(b.end(), s, s + 4); }
void sizeHeader(Bytes &b) {
  const uint32_t n = b.size() - 8;
  for (int i = 0; i < 4; ++i) b[4+i] = n >> (8*i);
}
Bytes makeWav(uint16_t channels = 2, bool junk = false, bool dataFirst = false) {
  Bytes b; tag(b,"RIFF"); put32(b,0); tag(b,"WAVE");
  if (junk) { tag(b,"JUNK"); put32(b,1); b.push_back(99); b.push_back(0); }
  auto data = [&] { tag(b,"data"); put32(b,channels*4); for(int i=0;i<channels*2;++i) put16(b,0x8000); };
  if(dataFirst) data();
  tag(b,"fmt "); put32(b,16); put16(b,1); put16(b,channels);
  put32(b,44100); put32(b,44100*channels*2); put16(b,channels*2); put16(b,16);
  if(!dataFirst) data();
  sizeHeader(b); return b;
}
bool parse(const Bytes &b, WavInfo &info) {
  return parseWav(b.size(), [&](uint32_t off, uint8_t *out, size_t n) {
    if(uint64_t(off)+n > b.size()) return false;
    memcpy(out,b.data()+off,n); return true;
  }, info);
}
int main() {
  WavInfo info;
  auto stereo=makeWav(); assert(parse(stereo,info)); assert(info.channels==2 && info.bytes==8 && info.offset==44);
  assert(parse(makeWav(1),info) && info.channels==1);
  assert(parse(makeWav(2,true),info) && info.offset==54);
  assert(parse(makeWav(2,false,true),info) && info.offset==20);
  // Every truncated prefix must be rejected, including incomplete PCM data.
  for(size_t n=0;n<stereo.size();++n) assert(!parse(Bytes(stereo.begin(),stereo.begin()+n),info));
  auto bad=stereo; bad[20]=3; assert(!parse(bad,info)); // IEEE float, not PCM
  bad=stereo; bad[24]=0; assert(!parse(bad,info)); // Unsupported sample rate
  bad=stereo; bad[34]=24; assert(!parse(bad,info)); // Unsupported bit depth
  bad=stereo; bad[32]=2; assert(!parse(bad,info)); // Wrong frame alignment
  bad=stereo; bad[40]=7; bad.pop_back(); sizeHeader(bad); assert(!parse(bad,info));
  bad=stereo; for(int i=40;i<44;++i) bad[i]=255; assert(!parse(bad,info)); // Oversized chunk
  bad=stereo; bad[0]='X'; assert(!parse(bad,info));
  std::puts("WAV parser: valid formats, padding, reordered chunks and malformed files passed.");
}
