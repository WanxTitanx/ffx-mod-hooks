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
bool Control(unsigned code,unsigned argument,std::uint32_t api){
    // New escapes preserve existing parameters; styles and glyph banks remain opaque.
    if(api>=4){
        if(code==7||code==14)return true;
        if(code==18&&argument>=48)return true;
        if(code==11&&argument==32)return true;
    }
    if(api>=3){
        if((code==16||(code>=20&&code<=35))&&argument>=48)return true;
        if(code==19&&(argument==66||argument==67))return true;
        if(code==10&&(argument==136||argument==148||argument==151||argument==161))return true;
    }
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
              const Font& font,const Advances& advances,std::uint32_t api){
    Require(end>=begin&&end-begin<=2048,"Encoded string exceeds its bound");
    Shape result;
    for(auto at=begin;at<end;++at){
        const auto code=bytes[at];
        if(code==3){result.controls.push_back(code);result.widths.push_back(0);continue;}
        if(code==1){Require(api>=3,"Pause-preserving edits require API 3");result.controls.push_back(code);continue;}
        if(code==7||code==9||code==10||code==11||code==14||code==16||code==18||code==19||(code>=20&&code<=35)){
            Require(at+1<end,"Truncated text control");
            const auto argument=bytes[++at];
            Require(Control(code,argument,api),"Unsupported text control argument");
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
using Tokens=std::vector<Bytes>;
Tokens ControlTokens(const Bytes& wire){
    Tokens result;
    for(std::size_t at=0;at<wire.size();){
        const auto size=wire[at]==1||wire[at]==3?1u:2u;
        Require(at+size<=wire.size(),"Truncated control fingerprint");
        result.emplace_back(wire.begin()+at,wire.begin()+at+size);at+=size;
    }
    return result;
}
bool CompatibleControls(const Bytes& a,const Bytes& b,std::uint32_t api){
    if(api<4)return a==b;
    const auto x=ControlTokens(a),y=ControlTokens(b);
    if(std::any_of(x.begin(),x.end(),[](const Bytes& t){return t[0]==7||t[0]==16;}))return x==y;
    using Page=std::pair<std::size_t,Tokens>;
    const auto pages=[](const Tokens& tokens){
        std::vector<Page> out(1);
        for(const auto& token:tokens){
            if(token[0]==1)out.emplace_back();
            else if(token[0]==3)++out.back().first;
            else out.back().second.push_back(token);
        }
        return out;
    };
    return pages(x)==pages(y);
}
void CompareSpans(const Bytes& source,std::size_t before,std::size_t sourceEnd,
                  const Bytes& target,std::size_t after,std::size_t targetEnd,
                  const Font& font,const Advances& advances,std::uint32_t api){
    Require(before<=sourceEnd&&sourceEnd<=source.size()&&after<=targetEnd&&targetEnd<=target.size(),"Invalid string span");
    if(sourceEnd-before==targetEnd-after&&std::equal(source.begin()+before,
       source.begin()+sourceEnd,target.begin()+after))return;
    if(api<4)Require(targetEnd-after<=sourceEnd-before,"Translated string exceeds its native encoded byte capacity");
    const auto original=Measure(source,before,sourceEnd,font,advances,api);
    const auto translated=Measure(target,after,targetEnd,font,advances,api);
    Require(CompatibleControls(original.controls,translated.controls,api),"Text controls or placeholder identity changed");
    if(api>=4&&sourceEnd-before>=3&&(source[before]==19||source[before]==25)&&source[before+2]==3)
        Require(targetEnd-after>=3&&std::equal(source.begin()+before,source.begin()+before+3,target.begin()+after),"Leading speaker/name line changed");
    Require(original.widths.size()==translated.widths.size(),"Line count changed");
    if(api<4)for(std::size_t line=0;line<original.widths.size();++line)
        Require(translated.widths[line]<=original.widths[line],"Translated line exceeds its native measured width");
}
void CompareStrings(const Bytes& source,std::size_t before,const Bytes& target,
                    std::size_t after,const Font& font,const Advances& advances,std::uint32_t api){
    CompareSpans(source,before,End(source,before),target,after,End(target,after),font,advances,api);
}
void ValidatePool(const Bytes& target,std::size_t header,std::vector<std::pair<std::size_t,std::size_t>> spans){
    std::sort(spans.begin(),spans.end());auto cursor=header;
    for(const auto& span:spans){Require(span.first<=cursor,"Unreferenced gap in string pool");cursor=(std::max)(cursor,span.second);}
    Require(cursor<=target.size()&&target.size()-cursor<64&&std::all_of(target.begin()+cursor,target.end(),
            [](std::uint8_t value){return value==0;}),"Unreferenced text tail");
}
void ValidateField(const Bytes& source,const Bytes& target,const Font& font,const Advances& advances,std::uint32_t api){
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
        if(!before||!after){Require(api>=3&&before==after,"Null field variant changed or requires API 3");continue;}
        Require(before>=header&&after>=header,"Field string points into its header");
        CompareStrings(source,before,target,after,font,advances,api);
        spans.emplace_back(after,End(target,after)+1);
    }
    ValidatePool(target,header,std::move(spans));
}
void ValidateKernel(const TextLayout& layout,const Bytes& source,const Bytes& target,
                    const Font& font,const Advances& advances,std::uint32_t api){
    Require(source.size()>=20&&target.size()>=source.size(),"Text header is truncated or original bytes were removed");
    Require(std::equal(source.begin(),source.begin()+20,target.begin()),"Native text header changed");
    const auto low=Word(source,8),high=Word(source,10),stride=Word(source,12),block=Word(source,14);
    Require(high>=low&&stride==layout.stride,"Native table layout is incompatible");
    Require(block>0&&(high-low+1)*stride<=block&&20+block<source.size(),"Native table bounds are invalid");
    const auto pool=20+block;
    Require(target.size()-pool<=65536,"Text pool exceeds the native u16 offset space");
    Bytes offsets(source.size(),0);std::map<std::size_t,std::size_t> appended;
    for(std::size_t row=0;row<=high-low;++row)for(std::size_t slot=0;slot<layout.slots;++slot){
        const auto at=20+row*stride+slot*layout.offsetStep;
        const auto before=pool+Word(source,at),after=pool+Word(target,at);
        offsets[at]=1;offsets[at+1]=1;
        if(before==after){
            Require(before<source.size()||before>=target.size(),"Appended pool activates an invalid native reference");
            continue;
        }
        // Unchanged secondary slots can be opaque native metadata. Interpret only an edited slot.
        Require(after>=source.size(),"Changed offset must point to the appended translation pool");
        CompareStrings(source,before,target,after,font,advances,api);
        appended.emplace(after,End(target,after)+1);
    }
    for(std::size_t at=0;at<source.size();++at)
        Require(offsets[at]||source[at]==target[at],"Native pool, keys or opaque table metadata changed");
    auto cursor=source.size();
    for(const auto& span:appended){Require(span.first==cursor,"Appended strings overlap or contain unreferenced bytes");cursor=span.second;}
    Require(cursor==target.size(),"Unreferenced appended text bytes");
}
struct Chunk {std::size_t id,begin,end;};
std::vector<Chunk> Chunks(const Bytes& data){
    Require(data.size()>=64,"Macro dictionary header is truncated");
    std::vector<Chunk> result;
    for(std::size_t id=0;id<16;++id){
        const auto at=id*4;
        const auto offset=Word(data,at)|(Word(data,at+2)<<16);
        if(!offset)continue;
        Require(offset>=64&&offset<data.size(),"Macro chunk offset is out of bounds");
        result.push_back({id,offset,data.size()});
    }
    Require(!result.empty(),"Macro dictionary has no text chunks");
    std::sort(result.begin(),result.end(),[](const Chunk& a,const Chunk& b){return a.begin<b.begin;});
    Require(result.front().begin==64,"Unreferenced macro header gap");
    for(std::size_t i=0;i+1<result.size();++i){
        Require(result[i].begin<result[i+1].begin,"Macro chunks overlap or alias");
        result[i].end=result[i+1].begin;
    }
    for(const auto& chunk:result)Require(chunk.end-chunk.begin<=65536,"Macro chunk exceeds u16 offset space");
    return result;
}
void ValidateMacroChunk(const Bytes& source,const Bytes& target,const Font& font,const Advances& advances,std::uint32_t api){
    const auto header=Word(source,0);
    Require(header>=4&&header%4==0&&header<source.size()&&header<target.size(),"Invalid macro row table");
    Require(Word(target,0)==header,"Macro row count or implicit first offset changed");
    if(source==target)return;
    std::vector<std::pair<std::size_t,std::size_t>> spans;
    for(std::size_t at=0;at<header;at+=2){
        const auto before=Word(source,at),after=Word(target,at);
        if(!before||before>=source.size()){
            Require(after==before&&(!after||after>=target.size()),"Inactive macro reference changed or became active");
            continue;
        }
        Require(before>=header&&after>=header,"Macro string points into its header");
        CompareStrings(source,before,target,after,font,advances,api);
        spans.emplace_back(after,End(target,after)+1);
    }
    ValidatePool(target,header,std::move(spans));
}
void ValidateMacro(const Bytes& source,const Bytes& target,const Font& font,const Advances& advances,std::uint32_t api){
    const auto original=Chunks(source),translated=Chunks(target);
    Require(original.size()==translated.size(),"Macro chunk presence changed");
    for(std::size_t i=0;i<original.size();++i){
        const auto& before=original[i];const auto& after=translated[i];
        Require(before.id==after.id,"Macro chunk identity or order changed");
        const Bytes a(source.begin()+before.begin,source.begin()+before.end);
        const Bytes b(target.begin()+after.begin,target.begin()+after.end);
        ValidateMacroChunk(a,b,font,advances,api);
    }
}

}

bool ValidateTextReplacement(std::string_view request,const Bytes& source,const Bytes& replacement,
                             const Font& font,const Advances& advances,std::string& error,std::uint32_t api){
    try{
        Require(source.size()<=MaxResourceBytes&&replacement.size()<=MaxResourceBytes,"Text resource exceeds its bound");
        std::string canonical;
        Require(CanonicalRequest(request,canonical),"Invalid text resource path");
        TextLayout layout;
        Require(DescribeTextRequest(canonical,layout),"Text resource has no admitted container layout");
        Require(api>=layout.minimumApi&&api<=HookApi,"Text container requires a different API");
        switch(layout.container){
        case TextContainer::Indexed:ValidateKernel(layout,source,replacement,font,advances,api);break;
        case TextContainer::Field:ValidateField(source,replacement,font,advances,api);break;
        case TextContainer::Macro:ValidateMacro(source,replacement,font,advances,api);break;
        }
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
} // namespace FfxHooks::TextLanguage
