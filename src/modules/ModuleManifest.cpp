#include <vix/cli/modules/ModuleManifest.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace vix::cli::modules
{
namespace {
std::string trim(std::string s) { auto p=[](unsigned char c){return std::isspace(c);}; while(!s.empty()&&p(s.front()))s.erase(s.begin()); while(!s.empty()&&p(s.back()))s.pop_back(); return s; }
std::string lower(std::string s) { for(char &c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string unquote(std::string s) { s=trim(std::move(s)); if(s.size()>=2&&((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\''))) return s.substr(1,s.size()-2); return s; }
bool valid_name(const std::string &s) { return !s.empty() && std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}); }
bool boolean(const std::string &raw,bool &out) { auto s=lower(unquote(raw)); if(s=="true"||s=="yes"||s=="on"||s=="1"){out=true;return true;} if(s=="false"||s=="no"||s=="off"||s=="0"){out=false;return true;} return false; }
bool array(const std::string &raw,std::vector<std::string> &out) { auto s=trim(raw); if(s.size()<2||s.front()!='['||s.back()!=']') return false; s=s.substr(1,s.size()-2); std::stringstream ss(s); std::string item; while(std::getline(ss,item,',')){item=unquote(item);if(!item.empty())out.push_back(item);} return true; }
}
ModuleManifestLoadResult load_module_manifest(const std::filesystem::path &path)
{
 ModuleManifestLoadResult r; std::ifstream in(path); if(!in){r.error="cannot open vix.module";return r;} std::string line,section, pendingKey,pendingValue; bool any=false;
 auto assign=[&](const std::string &sec,const std::string &key,const std::string &raw)->bool { auto k=lower(key); auto value=trim(raw); any=true;
   if(sec.empty()&&(k=="name"||k=="kind"||k=="workflow")){auto &dst=k=="name"?r.manifest.name:k=="kind"?r.manifest.kind:r.manifest.workflow; if(!dst.empty()){r.error="duplicate field: "+key;return false;} dst=unquote(value); return true;}
   if(sec.empty()&&k=="runtime") return boolean(value,r.manifest.runtime) || (r.error="invalid bool: runtime",false);
   if(sec=="routes"&&k=="prefix"){r.manifest.routePrefix=unquote(value);return true;}
   if(sec=="exports"&&k=="include"){r.manifest.exportInclude=unquote(value);return true;}
   if(sec=="tests"&&k=="enabled") return boolean(value,r.manifest.testsEnabled) || (r.error="invalid bool: tests.enabled",false);
   if(sec=="deps"&&(k=="registry"||k=="links")){auto &dst=k=="registry"?r.manifest.registryDependencies:r.manifest.links; if(!array(value,dst)){r.error="malformed array: deps."+key;return false;} return true;}
   if(sec=="websocket"){if(!r.manifest.websocket)r.manifest.websocket.emplace(); auto &w=*r.manifest.websocket; if(k=="workflow")w.workflow=unquote(value); else if(k=="path")w.path=unquote(value); else if(k=="host")w.host=unquote(value); else if(k=="long_polling") {if(!boolean(value,w.longPolling)){r.error="invalid bool: websocket.long_polling";return false;}} else if(k=="metrics"){if(!boolean(value,w.metrics)){r.error="invalid bool: websocket.metrics";return false;}} else if(k=="port"){try{auto n=std::stoul(value);if(n>65535)throw std::out_of_range("port");w.port=static_cast<unsigned short>(n);}catch(...){r.error="invalid port";return false;}} else {r.error="unsupported websocket field: "+key;return false;} return true;}
   r.error="unsupported vix.module field: "+(sec.empty()?key:sec+"."+key); return false; };
 while(std::getline(in,line)){auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);line=trim(line);if(line.empty())continue; if(!pendingKey.empty()){pendingValue+=" "+line;if(line.find(']')==std::string::npos)continue; if(!assign(section,pendingKey,pendingValue))return r;pendingKey.clear();continue;} if(line.front()=='['){if(line.back()!=']'){r.error="malformed section";return r;}section=lower(trim(line.substr(1,line.size()-2))); if(section!="routes"&&section!="deps"&&section!="tests"&&section!="websocket"&&section!="exports"){r.error="unsupported section: "+section;return r;}continue;}auto eq=line.find('=');if(eq==std::string::npos){r.error="malformed field";return r;}auto key=trim(line.substr(0,eq));auto value=trim(line.substr(eq+1));if(value.find('[')!=std::string::npos&&value.find(']')==std::string::npos){pendingKey=key;pendingValue=value;continue;}if(!assign(section,key,value))return r; }
 if(!pendingKey.empty()){r.error="truncated array";return r;} if(!any){r.error="empty vix.module";return r;} if(!r.manifest.name.empty()&&!valid_name(r.manifest.name)){r.error="invalid module name";} return r;
}
}
