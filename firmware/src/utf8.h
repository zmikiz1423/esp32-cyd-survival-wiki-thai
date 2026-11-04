#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Strict, bounded decoding. Invalid/partial UTF-8 becomes one replacement
// character; a caller can never read past a packet or file buffer.
inline uint32_t utf8Decode(const char *s, int end, int &p) {
  if (p >= end) return 0;
  uint8_t c = (uint8_t)s[p++];
  if (c < 0x80) return c;
  int n = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
  if (n < 0 || p + n > end) return 0xFFFD;
  uint32_t cp = c & ((1 << (6-n))-1);
  for (int i=0;i<n;i++) {
    uint8_t t=(uint8_t)s[p];
    if ((t&0xC0)!=0x80) return 0xFFFD;
    cp=(cp<<6)|(t&63); p++;
  }
  if ((n==1&&cp<0x80)||(n==2&&cp<0x800)||(n==3&&cp<0x10000)||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)) return 0xFFFD;
  return cp;
}
inline bool thaiMark(uint32_t c) {
  return c==0xE31 || (c>=0xE34&&c<=0xE3A) || (c>=0xE47&&c<=0xE4E);
}
inline bool thaiLeadingVowel(uint32_t c) { return c>=0xE40&&c<=0xE44; }
inline int utf8ClusterEnd(const char *s,int end,int start) {
  int p=start; uint32_t c=utf8Decode(s,end,p);
  // A leading vowel stays with the following Thai consonant when wrapping.
  if (thaiLeadingVowel(c)&&p<end) { int q=p; uint32_t d=utf8Decode(s,end,q); if(d>=0xE01&&d<=0xE2E) p=q; }
  while(p<end) { int q=p; uint32_t d=utf8Decode(s,end,q); if(!thaiMark(d)&&d!=0xE33&&d!=0x200D) break; p=q; }
  return p;
}
inline int utf8PreviousCluster(const char *s,int len) {
  int p=0,last=0;
  while(p<len) { last=p; p=utf8ClusterEnd(s,len,p); }
  return last;
}
inline size_t utf8Copy(char *dst,const char *src,size_t cap) {
  if(!cap) return 0;
  int len=strlen(src),p=0,fit=0;
  while(p<len) { int e=utf8ClusterEnd(src,len,p); if(e>=(int)cap) break; fit=e; p=e; }
  memmove(dst,src,fit); dst[fit]=0; return fit;
}
