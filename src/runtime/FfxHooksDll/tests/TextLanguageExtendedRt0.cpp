// Jarvis-HOOK: API 3 container invariants, synthetic assets only.
#include "../hooks/TextLanguagePayload.h"
#include <algorithm>
#include <cstdio>
using namespace FfxHooks::TextLanguage;
namespace {
unsigned checks=0,failures=0;
std::string error;
Font font{"western","metrics",{},{{227,242,31},{245,243,33},{195,244,42},{213,245,42}}};
Advances advances{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",label,error.c_str());}}
void Word(Bytes& b,std::size_t at,std::size_t value){b.at(at)=static_cast<std::uint8_t>(value);b.at(at+1)=static_cast<std::uint8_t>(value>>8);}
void Dword(Bytes& b,std::size_t at,std::size_t value){Word(b,at,value);Word(b,at+2,value>>16);}
Bytes Literal(const char* text){Bytes bytes;Check(EncodeLiteral(font,text,2048,bytes,error),"fixture encodes");return bytes;}
void Add(Bytes& output,const Bytes& text){output.insert(output.end(),text.begin(),text.end());output.push_back(0);}
Bytes Indexed(unsigned stride,unsigned slots,unsigned step){
    Bytes bytes(20+2*stride,0xa5);std::fill(bytes.begin(),bytes.begin()+20,std::uint8_t{0});
    Word(bytes,8,101);Word(bytes,10,102);Word(bytes,12,stride);Word(bytes,14,2*stride);
    for(unsigned row=0;row<2;++row)for(unsigned slot=0;slot<slots;++slot){
        const auto at=20+row*stride+slot*step;Word(bytes,at,0);
        if(step==4)Word(bytes,at+2,0x100+slot);
    }
    Add(bytes,Literal("Formation configuration"));return bytes;
}
Bytes Append(const Bytes& source,const Bytes& text,unsigned pool,unsigned pointer=20){
    auto target=source;Word(target,pointer,target.size()-pool);Add(target,text);return target;
}
bool Valid(const std::string& path,const Bytes& source,const Bytes& target){
    return ValidateTextReplacement(path,source,target,font,advances,error);
}
const std::string master="/ffx_data/ffx_ps2/ffx/master/new_uspc/";
Bytes Field(const Bytes& first,const Bytes& second,bool nullVariant=false){
    Bytes b(16,0);Word(b,0,16);Word(b,4,nullVariant?0:16);Word(b,2,0x140);Word(b,6,0x141);Add(b,first);
    Word(b,8,b.size());Word(b,12,b.size());Add(b,second);return b;
}
Bytes Chunk(const Bytes& first,const Bytes& second){
    Bytes b(8,0);Word(b,0,8);Word(b,2,8);Add(b,first);Word(b,4,b.size());Word(b,6,b.size());Add(b,second);return b;
}
Bytes Macro(const Bytes& first,const Bytes& second){
    Bytes b(64,0);Dword(b,24,64);b.insert(b.end(),first.begin(),first.end());Dword(b,28,b.size());
    b.insert(b.end(),second.begin(),second.end());return b;
}
Bytes Lines(const Bytes& first,const Bytes& second){
    auto bytes=first;bytes.insert(bytes.end(),{13,10});bytes.insert(bytes.end(),second.begin(),second.end());
    bytes.insert(bytes.end(),{13,10});return bytes;
}
}
int main(){
    advances.fill(24);advances[242]=31;advances[243]=33;advances[244]=42;advances[245]=42;
    {
        const auto path=master+"battle/kernel/command.bin";
        auto original=Indexed(96,4,4);
        auto expanded=Append(original,Literal("A longer localized description of the same native resource"),212);
        Check(!ValidateTextReplacement(path,original,expanded,font,advances,error,3),"API 3 retains original byte/width limits");
        Check(ValidateTextReplacement(path,original,expanded,font,advances,error,4),"API 4 accepts bounded container-backed text growth");
        Bytes tooLong(2049,80);auto excessive=Append(original,tooLong,212);
        Check(!ValidateTextReplacement(path,original,excessive,font,advances,error,4),"API 4 retains the absolute script bound");
        Bytes control={7,84,14,64,18,69,11,32};auto before=control,after=control;
        const auto oldText=Literal("Original label"),newText=Literal("Updated label");
        before.insert(before.end(),oldText.begin(),oldText.end());after.insert(after.end(),newText.begin(),newText.end());
        const auto oldField=Field(before,Literal("Other")),newField=Field(after,Literal("Other"));
        const auto fieldPath=master+"menu/menumain.bin";
        Check(!ValidateTextReplacement(fieldPath,oldField,newField,font,advances,error,3),"older APIs reject newly examined control forms");
        Check(ValidateTextReplacement(fieldPath,oldField,newField,font,advances,error,4),"API 4 preserves spacing, style, large variables and native button argument");
        auto altered=after;altered[1]^=1;
        Check(!ValidateTextReplacement(fieldPath,oldField,Field(altered,Literal("Other")),font,advances,error,4),"position arguments cannot change");
        const Bytes a={80,19,48,3,81},b={80,3,19,48,81};
        Check(ValidateTextReplacement(fieldPath,Field(a,Literal("Other")),Field(b,Literal("Other")),font,advances,error,4),"content macro reflow keeps identity and line count");
        const Bytes choicesA={16,48,80,3,16,49,81},choicesB={16,48,80,16,49,3,81};
        Check(!ValidateTextReplacement(fieldPath,Field(choicesA,Literal("Other")),Field(choicesB,Literal("Other")),font,advances,error,4),"choice wire layout remains exact");
    }
    struct Profile{const char* name;unsigned stride,slots,step;};
    for(const auto& profile:{Profile{"a_ability.bin",108,4,4},Profile{"command.bin",96,4,4},
            Profile{"important.bin",20,4,4},Profile{"item.bin",96,4,4},Profile{"monmagic1.bin",92,4,4},
            Profile{"monmagic2.bin",92,4,4},Profile{"monster1.bin",128,4,4},Profile{"monster2.bin",128,4,4},
            Profile{"monster3.bin",128,4,4},Profile{"panel.bin",24,4,4},Profile{"sphere.bin",16,2,4},
            Profile{"btlend_txt.bin",16,2,4},Profile{"build_txt.bin",16,2,4},
            Profile{"name_txt.bin",16,2,4},Profile{"save_txt.bin",16,2,4},
            Profile{"w_name.bin",72,14,4},Profile{"btl_txt.bin",8,2,4}}){
        const auto path=master+"battle/kernel/"+profile.name;
        const auto source=Indexed(profile.stride,profile.slots,profile.step);
        const unsigned pool=20+2*profile.stride;
        auto target=Append(source,Literal("Forma\xC3\xA7\xC3\xA3o"),pool);
        Check(Valid(path,source,target),"new indexed catalogue preserves zero-based pool strings and nonzero row IDs");
        auto bad=target;bad[22]^=1;Check(!Valid(path,source,bad),"indexed reference flags cannot change");
        bad=target;bad[pool]^=1;Check(!Valid(path,source,bad),"original indexed pool remains intact");
        bad=source;Word(bad,12,profile.stride+4);Check(!Valid(path,bad,bad),"exact source stride is required");
        bad=target;bad.push_back(0);Check(!Valid(path,source,bad),"unreferenced appended tail rejects");
        if(profile.stride>profile.slots*profile.step){bad=target;bad[20+profile.slots*profile.step]^=1;
            Check(!Valid(path,source,bad),"gameplay or weapon model fields cannot change");}
        auto opaque=source;Word(opaque,24,65535);target=Append(opaque,Literal("Info"),pool);
        Check(Valid(path,opaque,target),"unchanged invalid optional reference survives another slot edit");
        auto inactive=source;Word(inactive,24,source.size()-pool+1);target=Append(inactive,Literal("Info"),pool);
        Check(!Valid(path,inactive,target),"growth cannot activate a previously out-of-range reference");
    }
    {
        const auto path=master+"battle/kernel/btl_txt.bin";
        TextLayout layout{};
        Check(DescribeTextRequest(path,layout)&&layout.slots==2&&layout.offsetStep==4,
              "compact battle records expose two offset/metadata pairs");
        auto compact=Indexed(8,2,4);
        // Metadata also falls inside the string pool, so a mistaken offset
        // interpretation could pass text/control/width checks.
        Word(compact,22,2);Word(compact,26,3);
        auto changed=Append(compact,Literal("Info"),36,24);
        Check(Valid(path,compact,changed),"second compact text slot can be edited without changing metadata");
        changed=Append(compact,Literal("Info"),36,22);
        Check(!Valid(path,compact,changed),"compact metadata cannot be redirected into the appended pool");
    }
    auto source=Field(Literal("Formation configuration"),Literal("Character information"),true);
    auto target=Field(Literal("Info"),Literal("Character information"),true);
    for(const auto& path:{master+"menu/menumain.bin",master+"battle/btl/besa01_00/besa01_00.bin"}){
        Check(Valid(path,source,target),"new field routes retain null variants");
        auto bad=target;Word(bad,4,16);Check(!Valid(path,source,bad),"null field variant cannot be activated");
        bad=target;bad[6]^=1;Check(!Valid(path,source,bad),"null variant flags remain immutable");
    }
    const auto originalChunk=Chunk(Literal("Formation configuration"),Literal("Character information"));
    const auto translatedChunk=Chunk(Literal("Info"),Literal("Character information"));
    source=Macro(originalChunk,originalChunk);target=Macro(translatedChunk,originalChunk);
    const auto macroPath=master+"menu/macrodic.dcp";
    Check(Valid(macroPath,source,target),"macro relocation preserves chunk identity and unchanged chunk bytes");
    auto bad=target;Dword(bad,20,64);Check(!Valid(macroPath,source,bad),"missing macro chunk cannot be added");
    bad=target;Word(bad,64,12);Check(!Valid(macroPath,source,bad),"macro implicit row count cannot change");
    bad=target;Word(bad,66,3);Check(!Valid(macroPath,source,bad),"macro references cannot point into the header");
    bad=target;bad.push_back(42);Check(!Valid(macroPath,source,bad),"macro opaque tail injection rejects");
    bad=target;Dword(bad,28,64);Check(!Valid(macroPath,source,bad),"macro chunks cannot alias each other");
    auto longChunk=Bytes(1044,0);for(std::size_t at=0;at<1044;at+=2)Word(longChunk,at,1044);
    Add(longChunk,Literal("Formation configuration"));
    auto shortChunk=Bytes(1044,0);for(std::size_t at=0;at<1044;at+=2)Word(shortChunk,at,1044);Add(shortChunk,Literal("Info"));
    Check(Valid(macroPath,Macro(longChunk,originalChunk),Macro(shortChunk,originalChunk)),"macro chunks may contain more than 256 entries");
    const std::string linePath="/ffx_data/gamedata/ps3data/lockit/ffx_loc_kit_ps3_us.bin";
    source=Lines(Literal("Formation configuration"),Literal("Character information"));
    target=Lines(Literal("Info"),Literal("Character information"));
    Check(!Valid(linePath,source,target),"mixed-encoding lockit has no admitted runtime contract");
    auto before=Literal("Formation configuration"),after=Literal("Info");
    const Bytes commands={1,16,48,32,49,35,255,10,136};
    before.insert(before.begin(),commands.begin(),commands.end());after.insert(after.begin(),commands.begin(),commands.end());
    source=Field(before,Literal("Other"));target=Field(after,Literal("Other"));
    const auto eventPath=master+"event/obj_ps3/ss/ssbt0000/ssbt0000.bin";
    Check(Valid(eventPath,source,target),"API 3 preserves pause choice macro and key-item escapes");
    auto altered=target;altered[18]=49;Check(!Valid(eventPath,source,altered),"choice identity cannot change");
    for(const auto& escape:{Bytes{1},Bytes{16,48},Bytes{32,49},Bytes{35,255},Bytes{10,136},Bytes{19,66}}){
        before=Literal("Formation configuration");after=Literal("Info");
        before.insert(before.begin(),escape.begin(),escape.end());after.insert(after.begin(),escape.begin(),escape.end());
        source=Field(before,Literal("Other"));target=Field(after,Literal("Other"));
        Check(Valid(eventPath,source,target),"each additional preserved escape is admitted in API 3");
        Check(!ValidateTextReplacement(eventPath,source,target,font,advances,error,2),"API 2 cannot claim additional escape support");
    }
    std::printf("TextLanguageExtended RT0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
