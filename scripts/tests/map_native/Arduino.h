#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <chrono>
#include <cctype>
#include <cstdio>
using std::min;using std::max;
class String {
 std::string s;
 public:
 String(){} String(const char *v):s(v?v:""){} String(const std::string &v):s(v){} String(char c):s(1,c){}
 String(int v):s(std::to_string(v)){} String(unsigned v):s(std::to_string(v)){} String(long v):s(std::to_string(v)){} String(unsigned long v):s(std::to_string(v)){}
 String(double v,int places=2){char b[100];snprintf(b,sizeof(b),"%.*f",places,v);s=b;}
 size_t length()const{return s.length();} const char *c_str()const{return s.c_str();} char operator[](size_t n)const{return s[n];} char &operator[](size_t n){return s[n];}
 void reserve(size_t n){s.reserve(n);} bool startsWith(const String &x)const{return s.rfind(x.s,0)==0;} bool endsWith(const String &x)const{return s.size()>=x.s.size()&&s.compare(s.size()-x.s.size(),x.s.size(),x.s)==0;}
 String substring(size_t a)const{return a>=s.size()?String():String(s.substr(a));} String substring(size_t a,size_t b)const{return a>=s.size()?String():String(s.substr(a,b-a));}
 int indexOf(char c,int start=0)const{auto n=s.find(c,start<0?0:start);return n==std::string::npos?-1:(int)n;} int indexOf(const String &v,int start=0)const{auto n=s.find(v.s,start<0?0:start);return n==std::string::npos?-1:(int)n;}
 int lastIndexOf(char c)const{auto n=s.rfind(c);return n==std::string::npos?-1:(int)n;}
 void trim(){size_t a=0,b=s.size();while(a<b&&isspace((unsigned char)s[a]))a++;while(b>a&&isspace((unsigned char)s[b-1]))b--;s=s.substr(a,b-a);}
 void toLowerCase(){for(char &c:s)c=(char)tolower((unsigned char)c);} void replace(char a,char b){std::replace(s.begin(),s.end(),a,b);}
 String &operator+=(const String &v){s+=v.s;return *this;} String &operator+=(char c){s+=c;return *this;}
 friend bool operator==(const String&a,const String&b){return a.s==b.s;}friend bool operator!=(const String&a,const String&b){return !(a==b);}friend String operator+(const String&a,const String&b){return a.s+b.s;}
};
inline size_t strlcpy(char *dst,const char *src,size_t cap){size_t n=strlen(src);if(cap){memcpy(dst,src,std::min(n,cap-1));dst[std::min(n,cap-1)]=0;}return n;}
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)0)
#define portEXIT_CRITICAL(m) ((void)0)
inline uint32_t millis(){static auto start=std::chrono::steady_clock::now();return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
inline void yield(){}
