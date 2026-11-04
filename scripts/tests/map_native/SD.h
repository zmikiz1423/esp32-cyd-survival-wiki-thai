#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <functional>
namespace fs=std::filesystem;
#define FILE_READ "r"
#define FILE_WRITE "w"
class File {
 struct Node {fs::path path;std::string logical;std::fstream file;std::vector<fs::path> entries;size_t cursor=0;bool directory=false,open=false;};
 std::shared_ptr<Node> node;
 public:
 File(){} File(const fs::path &path,const std::string &logical,const char *mode){
  if(!fs::exists(path)&&std::string(mode)=="r")return;
  node=std::make_shared<Node>();node->path=path;node->logical=logical;node->directory=fs::is_directory(path);
  if(node->directory){for(auto &e:fs::directory_iterator(path))node->entries.push_back(e.path());std::sort(node->entries.begin(),node->entries.end());node->open=true;}
  else{node->file.open(path,std::ios::binary|(std::string(mode)=="r"?std::ios::in:(std::ios::out|std::ios::trunc)));node->open=node->file.is_open();}
 }
 explicit operator bool()const{return node&&node->open;}bool isDirectory()const{return node&&node->directory;}
 const char *name()const{return node?node->logical.c_str():"";}size_t size()const{return node&&!node->directory?fs::file_size(node->path):0;}
 int available(){return node&&node->open&&!node->directory&&node->file.peek()!=EOF;}int read(){return node->file.get();}
 void close(){if(node){if(node->file.is_open())node->file.close();node->open=false;}}
 File openNextFile(){if(!node||!node->directory||node->cursor==node->entries.size())return {};auto p=node->entries[node->cursor++];return File(p,node->logical+(node->logical=="/"?"":"/")+p.filename().string(),"r");}
 uint32_t position(){return (uint32_t)node->file.tellg();}bool seek(uint32_t n){node->file.clear();node->file.seekg(n);return !node->file.fail();}
 size_t print(const String &s){if(!node||!node->open)return 0;node->file<<s.c_str();node->file.flush();return node->file.fail()?0:s.length();}
};
struct MockSD {
 fs::path root;std::function<void()> beforeOpen;bool failWrites=false;int failRenameAt=0,renameCalls=0;
 fs::path path(const String &logical){std::string p=logical.c_str();return root/(p.size()&&p[0]=='/'?p.substr(1):p);}
 File open(const String &p,const char *mode=FILE_READ){if(beforeOpen){auto hook=beforeOpen;beforeOpen={};hook();}if(failWrites&&std::string(mode)!=FILE_READ)return {};return File(path(p),p.c_str(),mode);}
 bool exists(const String &p){return fs::exists(path(p));}bool remove(const String &p){return fs::remove(path(p));}
 bool rename(const String &a,const String &b){if(++renameCalls==failRenameAt)return false;std::error_code ec;fs::rename(path(a),path(b),ec);return !ec;}
 void put(const String &p,const std::string &v){auto target=path(p);fs::create_directories(target.parent_path());std::ofstream(target,std::ios::binary)<<v;}
};
extern MockSD SD;
