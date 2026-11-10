#pragma once
// ESP32 application-image descriptor checker for arbitrarily fragmented TCP
// and multipart upload streams. Never assumes a minimum packet size.
// Keeps the first 0x70 bytes: the image header (0xE9), the app descriptor
// magic at 0x20 and its project_name at 0x50 (esp_app_desc_t, 32 bytes), so
// the project can be compared before Update.end() makes the image bootable.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct OtaImageHeader {
  uint8_t bytes[0x70]={};
  size_t count=0;
  void reset(){count=0;memset(bytes,0,sizeof(bytes));}
  void feed(const uint8_t *data,size_t length){
    if(count>=sizeof(bytes)||!data)return;
    const size_t n=length<(sizeof(bytes)-count)?length:(sizeof(bytes)-count);
    memcpy(bytes+count,data,n);count+=n;
  }
  bool complete()const{return count==sizeof(bytes);}
  bool valid()const{return complete()&&bytes[0]==0xE9&&bytes[0x20]==0x32
    &&bytes[0x21]==0x54&&bytes[0x22]==0xCD&&bytes[0x23]==0xAB;}
  // Same esp_app_desc_t.project_name as `name` (the running firmware's).
  bool sameProject(const char *name)const{return valid()&&name&&strncmp((const char*)bytes+0x50,name,32)==0;}
};
