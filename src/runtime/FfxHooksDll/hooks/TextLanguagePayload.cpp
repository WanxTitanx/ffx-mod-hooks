// Jarvis-HOOK. Validate text before its bytes reach a native consumer.
#include "TextLanguagePayload.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace FfxHooks::TextLanguage {
namespace {
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };
void Require(bool condition,const char* error){if(!condition)throw Invalid(error);}
std::size_t Word(const Bytes& bytes,std::size_t at){
    Require(at<=bytes.size()&&bytes.size()-at>=2,"Truncated offset");
    return bytes[at]|(static_cast<std::size_t>(bytes[at+1])<<8);
}
std::size_t End(const Bytes& bytes,std::size_t at){
    Require(at<bytes.size(),"String offset is out of bounds");
    const auto end=std::find(bytes.begin()+at,bytes.end(),0);
    Require(end!=bytes.end(),"String terminator is missing");
    return static_cast<std::size_t>(end-bytes.begin());
}
bool Control(unsigned code,unsigned argument){
    switch(code){
    case 9:case 11:case 25:return argument>=48&&argument<=255;
    case 10:return argument==65||argument==67||argument==82||argument==177;
    case 18:return argument>=48&&argument<=57;
    case 19:return argument>=48&&argument<=65;
    default:return false;
    }
}
struct Shape {Bytes controls;std::vector<std::size_t> widths{0};};
Shape Measure(const Bytes& bytes,std::size_t begin,std::size_t end,
              const Font& font,const Advances& advances){
    Require(end>=begin&&end-begin<=2048,"Encoded string exceeds its bound");
    Shape result;
    for(auto at=begin;at<end;++at){
        const auto code=bytes[at];
        if(code==3){result.controls.push_back(code);result.widths.push_back(0);continue;}
        if(code==9||code==10||code==11||code==18||code==19||code==25){
            Require(at+1<end,"Truncated text control");
            const auto argument=bytes[++at];
            Require(Control(code,argument),"Unsupported text control argument");
            result.controls.insert(result.controls.end(),{code,argument});continue;
        }
        Require(code>=48,"Editing this native control or glyph bank is unavailable");
        if(code>=240)Require(std::any_of(font.glyphs.begin(),font.glyphs.end(),
            [code](const Glyph& glyph){return glyph.code==code;}),"Undeclared extended glyph");
        Require(advances[code]>0,"Glyph metrics are missing");
        result.widths.back()+=advances[code];
    }
    return result;
}
void CompareStrings(const Bytes& source,std::size_t before,const Bytes& target,
                    std::size_t after,const Font& font,const Advances& advances){
    const auto sourceEnd=End(source,before),targetEnd=End(target,after);
    if(sourceEnd-before==targetEnd-after&&std::equal(source.begin()+before,
       source.begin()+sourceEnd,target.begin()+after))return;
    Require(targetEnd-after<=sourceEnd-before,"Translated string exceeds its native encoded byte capacity");
    const auto original=Measure(source,before,sourceEnd,font,advances);
    const auto translated=Measure(target,after,targetEnd,font,advances);
    Require(original.controls==translated.controls,"Text controls or placeholder identity changed");
    Require(original.widths.size()==translated.widths.size(),"Line count changed");
    for(std::size_t line=0;line<original.widths.size();++line)
        Require(translated.widths[line]<=original.widths[line],"Translated line exceeds its native measured width");
}
void ValidateField(const Bytes& source,const Bytes& target,const Font& font,const Advances& advances){
    Require(source.size()>=9&&source.size()<=65536&&target.size()>=9&&target.size()<=65536,
            "Field table exceeds the native u16 address space");
    const auto header=Word(source,0);
    Require(header>=8&&header%8==0&&header<source.size()&&header<target.size(),"Invalid implicit field header");
    Require(Word(target,0)==header,"Field entry count or implicit first offset changed");
    if(source==target)return; // Preserve opaque native tables without interpreting their scripts.
    std::vector<std::pair<std::size_t,std::size_t>> spans;
    for(std::size_t at=0;at<header;at+=4){
        Require(source[at+2]==target[at+2]&&source[at+3]==target[at+3],"Field flags or choice count changed");
        const auto before=Word(source,at),after=Word(target,at);
        Require(before>=header&&after>=header,"Field string points into its header");
        CompareStrings(source,before,target,after,font,advances);
        spans.emplace_back(after,End(target,after)+1);
    }
    std::sort(spans.begin(),spans.end());
    auto cursor=header;
    for(const auto& span:spans){
        Require(span.first<=cursor,"Unreferenced gap in the field string pool");
        cursor=(std::max)(cursor,span.second);
    }
    Require(target.size()-cursor<64&&std::all_of(target.begin()+cursor,target.end(),
            [](std::uint8_t value){return value==0;}),"Unreferenced field text tail");
}
void ValidateKernel(std::string_view request,const Bytes& source,const Bytes& target,
                    const Font& font,const Advances& advances){
    constexpr std::string_view prefix="/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/";
    Require(request.substr(0,prefix.size())==prefix,"Text request is outside the supported families");
    const auto name=request.substr(prefix.size());
    const bool battle=name=="btl_txt.bin";
    const bool names=name=="menu_txt.bin"||name=="config_txt.bin"||name=="arms_txt.bin"||
        name=="item_txt.bin"||name=="name_txt.bin"||name=="status_txt.bin"||name=="summon_txt.bin"||
        name=="mmain_txt.bin"||name=="save_txt.bin"||name=="build_txt.bin"||name=="btlend_txt.bin";
    Require(battle||names,"This text table family is not admitted");
    Require(source.size()>=20&&target.size()>=source.size(),"Text header is truncated or original bytes were removed");
    Require(std::equal(source.begin(),source.begin()+20,target.begin()),"Native text header changed");
    const auto low=Word(source,8),high=Word(source,10),stride=Word(source,12),block=Word(source,14);
    Require(high>=low&&stride==(battle?8u:16u),"Native table layout is incompatible");
    Require(block>0&&(high-low+1)*stride<=block&&20+block<source.size(),"Native table bounds are invalid");
    const auto pool=20+block;
    Require(target.size()-pool<=65536,"Text pool exceeds the native u16 offset space");
    Bytes offsets(source.size(),0);std::map<std::size_t,std::size_t> appended;
    for(std::size_t row=0;row<=high-low;++row)for(std::size_t slot=0;slot<4;++slot){
        const auto at=20+row*stride+slot*(battle?2:4);
        const auto before=pool+Word(source,at),after=pool+Word(target,at);
        offsets[at]=1;offsets[at+1]=1;
        if(before==after)continue;
        // Unchanged secondary slots can be opaque native metadata. Interpret only an edited slot.
        Require(after>=source.size(),"Changed offset must point to the appended translation pool");
        CompareStrings(source,before,target,after,font,advances);
        appended.emplace(after,End(target,after)+1);
    }
    for(std::size_t at=0;at<source.size();++at)
        Require(offsets[at]||source[at]==target[at],"Native pool, keys or opaque table metadata changed");
    auto cursor=source.size();
    for(const auto& span:appended){Require(span.first==cursor,"Appended strings overlap or contain unreferenced bytes");cursor=span.second;}
    Require(cursor==target.size(),"Unreferenced appended text bytes");
}
}

bool ValidateTextReplacement(std::string_view request,const Bytes& source,const Bytes& replacement,
                             const Font& font,const Advances& advances,std::string& error){
    try{
        Require(source.size()<=MaxResourceBytes&&replacement.size()<=MaxResourceBytes,"Text resource exceeds its bound");
        std::string canonical;
        Require(CanonicalRequest(request,canonical),"Invalid text resource path");
        if(IsEventRequest(canonical))ValidateField(source,replacement,font,advances);
        else ValidateKernel(canonical,source,replacement,font,advances);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
} // namespace FfxHooks::TextLanguage
