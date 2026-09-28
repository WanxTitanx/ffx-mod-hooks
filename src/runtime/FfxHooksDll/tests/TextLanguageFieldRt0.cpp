// Jarvis-HOOK: field-table consumer contracts use synthetic text only.
#include "../hooks/TextLanguagePayload.h"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace FfxHooks::TextLanguage;
namespace {
unsigned checks=0,failures=0;
std::string error;
Font font{"western","metrics",{},{{227,242,31},{245,243,33},{195,244,42},{213,245,42}}};
Advances advances{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",label,error.c_str());}}
void Word(Bytes& bytes,std::size_t offset,std::size_t value){bytes[offset]=static_cast<std::uint8_t>(value);bytes[offset+1]=static_cast<std::uint8_t>(value>>8);}
Bytes Literal(const char* text){Bytes bytes;Check(EncodeLiteral(font,text,2048,bytes,error),"fixture literal encodes");return bytes;}
Bytes Field(const Bytes& first,const Bytes& second){
 Bytes bytes(16,0);Word(bytes,0,16);Word(bytes,4,16);
 // Flags and choice counts belong to the event interpreter, not the translator.
 bytes[2]=0x40;bytes[3]=1;bytes[6]=0x40;bytes[7]=1;
 bytes.insert(bytes.end(),first.begin(),first.end());bytes.push_back(0);
 Word(bytes,8,bytes.size());Word(bytes,12,bytes.size());
 bytes.insert(bytes.end(),second.begin(),second.end());bytes.push_back(0);return bytes;
}
bool Valid(const Bytes& source,const Bytes& target,const char* tree="obj_ps3"){
 return ValidateTextReplacement(std::string("/ffx_data/ffx_ps2/ffx/master/new_uspc/event/")+tree+"/ss/ssbt0000/ssbt0000.bin",source,target,font,advances,error);
}
}
int main(){
 advances.fill(24);advances[242]=31;advances[243]=33;advances[244]=42;advances[245]=42;
 auto first=Literal("Formation configuration"),second=Literal("Character information");
 auto source=Field(first,second);
 auto target=Field(Literal("Forma\xC3\xA7\xC3\xA3o"),Literal("A\xC3\xA7\xC3\xA3o"));
 Check(Valid(source,source),"field identity is byte-faithful");
 Check(Valid(source,target),"PS3 event text can replace the first implicit-header string");
 Check(Valid(source,target,"obj_psv"),"PSV event text uses the same verified table layout");
 Check(!Valid(source,target,"obj_unknown"),"unmapped event resource tree rejects");
 auto bad=target;bad[2]^=1;Check(!Valid(source,bad),"event flags cannot change");
 bad=target;bad[7]^=1;Check(!Valid(source,bad),"event choice count cannot change");
 bad=target;Word(bad,0,24);Check(!Valid(source,bad),"implicit entry count cannot change");
 bad=target;Word(bad,8,3);Check(!Valid(source,bad),"field pointer cannot target the header");
 bad=target;Word(bad,12,65535);Check(!Valid(source,bad),"field pointer cannot target past EOF");
 bad=target;bad.pop_back();Check(!Valid(source,bad),"field strings require terminators");
 bad=target;bad.resize(65537,0);Check(!Valid(source,bad),"field table stays inside u16 address space");
 Check(!Valid(source,Field(Literal("Formation configuration Formation configuration"),second)),"field line width is bounded by the native line");
 first=Literal("Character information: ");first.insert(first.end(),{19,48,3});
 auto tail=Literal("Formation configuration");first.insert(first.end(),tail.begin(),tail.end());
 source=Field(first,second);
 first=Literal("Personagem: ");first.insert(first.end(),{19,48,3});tail=Literal("Forma\xC3\xA7\xC3\xA3o");first.insert(first.end(),tail.begin(),tail.end());
 target=Field(first,second);Check(Valid(source,target),"subtitle keeps its named character and line break");
 for(std::size_t n=16;n+1<target.size();++n)if(target[n]==19){target[n+1]=49;break;}
 Check(!Valid(source,target),"subtitle cannot replace a character placeholder");
 target=Field(Literal("Personagem"),second);Check(!Valid(source,target),"subtitle cannot remove controls or placeholders");
 // Unknown native controls remain usable through unchanged byte ranges only.
 first={4,90,91};source=Field(first,second);target=Field(first,Literal("Info"));
 Check(Valid(source,target),"opaque unedited field string survives another row translation");
 Check(!Valid(source,Field(Literal("Edit"),second)),"opaque field controls cannot be rewritten");
 for(std::size_t n=0;n<16;++n){Bytes truncated(source.begin(),source.begin()+n);Check(!Valid(truncated,truncated),"truncated implicit field header rejects");}
 // Use encoded identities rather than assuming ASCII equals the game's bytes.
 auto wide=Literal("WW"),thin=Literal("iiiiiiii");advances[wide[0]]=56;advances[thin[0]]=1;
 Check(!Valid(Field(wide,second),Field(thin,second)),"narrow glyphs cannot expand an unproven native character buffer");
 std::printf("TextLanguageField RT0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
