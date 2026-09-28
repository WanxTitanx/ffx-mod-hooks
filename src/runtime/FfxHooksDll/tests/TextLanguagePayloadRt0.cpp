#include "../hooks/TextLanguagePayload.h"
#include <cstdio>
using namespace FfxHooks::TextLanguage;
namespace {
unsigned checks=0,failures=0;
Font font{"western","metrics",{},{{227,242,31},{245,243,33},{195,244,42},{213,245,42}}};
Advances advances;
std::string error;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",name,error.c_str());}}
void Word(Bytes& b,size_t i,size_t n){b[i]=static_cast<std::uint8_t>(n);b[i+1]=static_cast<std::uint8_t>(n>>8);}
Bytes Literal(const char* text){Bytes b;Check(EncodeLiteral(font,text,2048,b,error),"fixture literal");return b;}
Bytes Table(const Bytes& text){Bytes b(37,0);Word(b,12,16);Word(b,14,16);Word(b,20,1);Word(b,24,1);b.insert(b.end(),text.begin(),text.end());b.push_back(0);return b;}
Bytes Append(const Bytes& b,const Bytes& text){auto r=b;Word(r,20,r.size()-36);r.insert(r.end(),text.begin(),text.end());r.push_back(0);return r;}
bool Valid(const Bytes& a,const Bytes& b){return ValidateTextReplacement("/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin",a,b,font,advances,error);}
}
int main(){
 advances.fill(24);advances[242]=31;advances[243]=33;advances[244]=42;advances[245]=42;
 auto a=Table(Literal("Formation")),b=Append(a,Literal("Forma\xC3\xA7\xC3\xA3o"));
 Check(Valid(a,a),"identity");Check(Valid(a,b),"real PT-BR append");
 auto opaque=a;Word(opaque,32,65535);
 Check(Valid(opaque,opaque),"unconsumed opaque native slots survive identity");
 auto variable=Literal("Item gained: ");variable.insert(variable.end(),{18,48});auto sourceVariable=Table(variable);
 variable=Literal("Item: ");variable.insert(variable.end(),{18,48});
 Check(Valid(sourceVariable,Append(sourceVariable,variable)),"native numeric placeholder preserved");
 variable.back()=49;Check(!Valid(sourceVariable,Append(sourceVariable,variable)),"numeric placeholder index cannot change");
 for(const char* file:{"mmain_txt.bin","save_txt.bin","build_txt.bin","btlend_txt.bin"})
  Check(ValidateTextReplacement(std::string("/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/")+file,a,b,font,advances,error),"verified additional kernel layout");
 auto bad=b;bad[22]^=1;Check(!Valid(a,bad),"opaque key");
 bad=b;bad[8]^=1;Check(!Valid(a,bad),"header");
 bad=b;bad[37]^=1;Check(!Valid(a,bad),"native pool");
 bad=b;bad.pop_back();Check(!Valid(a,bad),"terminator");
 bad=b;bad.push_back(42);Check(!Valid(a,bad),"unreferenced suffix");
 bad=b;Word(bad,20,a.size()-37);Check(!Valid(a,bad),"alias into old pool");
 bad=b;Word(bad,20,65535);Check(!Valid(a,bad),"offset bounds");
 Check(!Valid(a,Append(a,Literal("Formation Formation Formation"))),"width overflow");
 auto text=Literal("Short");text.push_back(3);Check(!Valid(a,Append(a,text)),"control injection");
 text=Literal("Character information: ");text.insert(text.end(),{19,48,3});auto tail=Literal("Formation configuration");text.insert(text.end(),tail.begin(),tail.end());auto c=Table(text);
 text=Literal("Personagem: ");text.insert(text.end(),{19,48,3});tail=Literal("A\xC3\xA7\xC3\xA3o");text.insert(text.end(),tail.begin(),tail.end());
 Check(Valid(c,Append(c,text)),"placeholder and newline survive");
 for(size_t i=0;i+1<text.size();++i)if(text[i]==19){text[i+1]=49;break;}
 Check(!Valid(c,Append(c,text)),"placeholder identity");
 text=Literal("Unknown");text.insert(text.begin(),9);c=Table(text);
 Check(Valid(c,c),"opaque identity");Check(!Valid(c,Append(c,Literal("Known"))),"opaque control edit");
 text=Literal("A");text.push_back(240);Check(!Valid(a,Append(a,text)),"unmapped glyph");
 bad=b;bad.resize(70000,0);Check(!Valid(a,bad),"u16 pool overflow");
 for(size_t n=0;n<36;++n){Bytes shortFile(a.begin(),a.begin()+n);Check(!Valid(shortFile,shortFile),"truncated table");}
 std::printf("TextLanguagePayload RT0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
