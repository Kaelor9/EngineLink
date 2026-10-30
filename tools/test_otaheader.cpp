#include "../firmware/EngineLink/otaheader.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
  uint8_t image[96]={};image[0]=0xE9;
  image[0x20]=0x32;image[0x21]=0x54;image[0x22]=0xCD;image[0x23]=0xAB;
  for(size_t cut=0;cut<=48;cut++){
    OtaImageHeader h;h.feed(image,cut);assert(h.complete()==(cut==48));
    h.feed(image+cut,sizeof(image)-cut);
    assert(h.complete()&&h.valid());
  }
  OtaImageHeader single;for(size_t i=0;i<sizeof(image);i++)single.feed(image+i,1);
  assert(single.valid());
  uint8_t bad[96];memcpy(bad,image,sizeof(bad));bad[0x22]=0x00;
  OtaImageHeader invalid;invalid.feed(bad,sizeof(bad));assert(!invalid.valid());
  invalid.reset();assert(!invalid.complete());invalid.feed(image,sizeof(image));assert(invalid.valid());
  std::puts("OTA image header: fragmented, bytewise and invalid data OK");
}
