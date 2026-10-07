#include "TextLanguageCore.h"
#include "TextLanguageGraphicsCatalog.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace FfxHooks::TextLanguage {
namespace {
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] void Reject(const char* reason) { throw Invalid(reason); }
bool Starts(std::string_view s,std::string_view p){return s.size()>=p.size()&&s.substr(0,p.size())==p;}
bool Ends(std::string_view s,std::string_view p){return s.size()>=p.size()&&s.substr(s.size()-p.size())==p;}
bool Alpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
bool Digit(char c){return c>='0'&&c<='9';}
std::string Lower(std::string_view s){std::string r(s);for(char& c:r)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return r;}
bool Scalar(std::uint32_t u){return u<=0x10ffffu&&!(u>=0xd800u&&u<=0xdfffu);}
bool Next(std::string_view s,std::size_t& at,std::uint32_t& u){
    if(at>=s.size())return false;
    const auto first=static_cast<unsigned char>(s[at++]);
    if(first<0x80){u=first;return true;}
    unsigned rest=0;std::uint32_t minimum=0;
    if(first>=0xc2&&first<=0xdf){rest=1;u=first&31u;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){rest=2;u=first&15u;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){rest=3;u=first&7u;minimum=0x10000;}
    else return false;
    if(rest>s.size()-at)return false;
    while(rest--){const auto c=static_cast<unsigned char>(s[at++]);if((c&0xc0u)!=0x80u)return false;u=(u<<6)|(c&63u);}
    return u>=minimum&&Scalar(u);
}
void Utf8(std::string& s,std::uint32_t u){
    if(u<0x80)s+=static_cast<char>(u);
    else if(u<0x800){s+=static_cast<char>(0xc0u|(u>>6));s+=static_cast<char>(0x80u|(u&63));}
    else if(u<0x10000){s+=static_cast<char>(0xe0u|(u>>12));s+=static_cast<char>(0x80u|((u>>6)&63));s+=static_cast<char>(0x80u|(u&63));}
    else {s+=static_cast<char>(0xf0u|(u>>18));s+=static_cast<char>(0x80u|((u>>12)&63));s+=static_cast<char>(0x80u|((u>>6)&63));s+=static_cast<char>(0x80u|(u&63));}
}
bool Printable(std::string_view s){
    std::size_t at=0;std::uint32_t u=0;
    while(at<s.size())if(!Next(s,at,u)||u<32u||(u>=127u&&u<160u))return false;
    return true;
}
struct Node {
    enum class Kind { Object, Array, String, Integer, Boolean } kind=Kind::Object;
    std::map<std::string,Node> object;
    std::vector<Node> array;
    std::string text;
    std::uint32_t integer=0;
    bool boolean=false;
};
// A bounded manifest reader, following the repository's closed-schema parsers.
// General JSON numbers/null are deliberately outside this integer-only contract.
class Reader {
    std::string_view input;std::size_t at=0,nodes=0;
    void Space(){while(at<input.size()&&(input[at]==' '||input[at]=='\t'||input[at]=='\r'||input[at]=='\n'))++at;}
    bool Take(char c){Space();if(at<input.size()&&input[at]==c){++at;return true;}return false;}
    void Need(char c){if(!Take(c))Reject("Malformed manifest JSON");}
    unsigned Hex(){
        unsigned u=0;
        for(unsigned n=0;n<4;++n){if(at==input.size())Reject("Truncated Unicode escape");const char c=input[at++];
            unsigned d=Digit(c)?static_cast<unsigned>(c-'0'):c>='a'&&c<='f'?static_cast<unsigned>(c-'a'+10):c>='A'&&c<='F'?static_cast<unsigned>(c-'A'+10):16u;
            if(d==16)Reject("Invalid Unicode escape");
            u=(u<<4)|d;}
        return u;
    }
    std::string String(){
        Need('"');std::string result;
        while(at<input.size()){
            const auto c=static_cast<unsigned char>(input[at++]);
            if(c=='"')return result;
            if(c<32)Reject("Unescaped control in JSON string");
            if(c=='\\'){
                if(at==input.size())Reject("Truncated JSON escape");
                const char e=input[at++];
                if(e=='"'||e=='\\'||e=='/')result+=e;
                else if(e=='b')result+='\b';else if(e=='f')result+='\f';else if(e=='n')result+='\n';else if(e=='r')result+='\r';else if(e=='t')result+='\t';
                else if(e=='u'){
                    std::uint32_t u=Hex();
                    if(u>=0xd800u&&u<=0xdbffu){if(at+2>input.size()||input[at]!='\\'||input[at+1]!='u')Reject("Unpaired Unicode surrogate");at+=2;const auto low=Hex();if(low<0xdc00u||low>0xdfffu)Reject("Invalid Unicode surrogate pair");u=0x10000u+((u-0xd800u)<<10)+(low-0xdc00u);}
                    if(!Scalar(u))Reject("Unpaired Unicode surrogate");
                    Utf8(result,u);
                } else Reject("Unknown JSON escape");
            } else if(c<0x80)result+=static_cast<char>(c);
            else {--at;const auto begin=at;std::uint32_t u=0;if(!Next(input,at,u))Reject("Invalid UTF-8");result.append(input.substr(begin,at-begin));}
            if(result.size()>4096)Reject("JSON string exceeds its bound");
        }
        Reject("Unterminated JSON string");
    }
    Node Value(unsigned depth){
        if(depth>8||++nodes>65536)Reject("Manifest nesting/node limit exceeded");
        Space();if(at==input.size())Reject("Truncated manifest");
        Node node;
        if(input[at]=='{'){
            ++at;node.kind=Node::Kind::Object;if(Take('}'))return node;
            do{const auto key=String();Need(':');auto value=Value(depth+1);if(!node.object.emplace(key,std::move(value)).second)Reject("Duplicate JSON key");}while(Take(','));Need('}');
        } else if(input[at]=='['){
            ++at;node.kind=Node::Kind::Array;if(Take(']'))return node;
            do{if(node.array.size()>=4096)Reject("Manifest array limit exceeded");node.array.push_back(Value(depth+1));}while(Take(','));Need(']');
        } else if(input[at]=='"'){node.kind=Node::Kind::String;node.text=String();}
        else if(Starts(input.substr(at),"true")){at+=4;node.kind=Node::Kind::Boolean;node.boolean=true;}
        else if(Starts(input.substr(at),"false")){at+=5;node.kind=Node::Kind::Boolean;}
        else if(Digit(input[at])){
            node.kind=Node::Kind::Integer;const bool zero=input[at]=='0';
            do{const unsigned digit=static_cast<unsigned>(input[at++]-'0');if(node.integer>(std::numeric_limits<std::uint32_t>::max()-digit)/10u)Reject("JSON integer overflow");node.integer=node.integer*10+digit;if(zero&&at<input.size()&&Digit(input[at]))Reject("Leading zero in JSON integer");}while(at<input.size()&&Digit(input[at]));
        } else Reject("Unsupported JSON value");
        return node;
    }
public:
    explicit Reader(std::string_view s):input(s){}
    Node Read(){if(input.empty()||input.size()>MaxManifestBytes)Reject("Manifest size exceeds its bound");auto root=Value(0);Space();if(at!=input.size())Reject("Trailing JSON data");return root;}
};
void Keys(const Node& n,std::initializer_list<const char*> required,std::initializer_list<const char*> optional={}){
    if(n.kind!=Node::Kind::Object)Reject("Expected JSON object");
    for(const char* key:required)if(!n.object.count(key))Reject("Missing manifest field");
    for(const auto& field:n.object){bool known=false;for(const char* key:required)known=known||field.first==key;for(const char* key:optional)known=known||field.first==key;if(!known)Reject("Unknown manifest field");}
}
const Node& Get(const Node& n,const char* key){const auto it=n.object.find(key);if(it==n.object.end())Reject("Missing manifest field");return it->second;}
std::string Text(const Node& n,std::size_t max=240){if(n.kind!=Node::Kind::String||n.text.empty()||n.text.size()>max||!Printable(n.text))Reject("Invalid manifest string");return n.text;}
std::string Text(const Node& n,const char* key,std::size_t max=240){return Text(Get(n,key),max);}
std::uint32_t Number(const Node& n,const char* key,std::uint32_t min,std::uint32_t max){const auto& v=Get(n,key);if(v.kind!=Node::Kind::Integer||v.integer<min||v.integer>max)Reject("Invalid manifest integer");return v.integer;}
const std::vector<Node>& Array(const Node& n,const char* key,std::size_t min,std::size_t max){const auto& v=Get(n,key);if(v.kind!=Node::Kind::Array||v.array.size()<min||v.array.size()>max)Reject("Invalid manifest array");return v.array;}
bool Identifier(std::string_view value){if(value.empty()||value.size()>64||!Alpha(value[0]))return false;for(char c:value)if(!Alpha(c)&&!Digit(c)&&c!='-'&&c!='_')return false;return true;}
bool Locale(std::string_view value){
    if(value.size()<2||value.size()>35)return false;
    std::size_t count=0,part=0;
    for(char c:value){if(c=='-'){if(count==0||count>8||(part==0&&count<2))return false;++part;count=0;}else{if(!Alpha(c)&&(part==0||!Digit(c)))return false;++count;}}
    return count>0&&count<=8&&(part>0||count>=2);
}
bool Version(std::string_view value){unsigned dots=0,part=0;for(char c:value){if(c=='.'){if(!part)return false;++dots;part=0;}else{if(!Digit(c)||++part>9)return false;}}return dots==2&&part>0;}
std::string Digest(const Node& n,const char* key){auto s=Text(n,key,64);if(s.size()!=64)Reject("Invalid SHA-256 length");for(char c:s)if(!Digit(c)&&!(c>='a'&&c<='f'))Reject("SHA-256 must be lowercase hexadecimal");if(s==std::string(64,'0'))Reject("Zero SHA-256 is not a fingerprint");return s;}
const Resource* ById(const Manifest& m,std::string_view id){for(const auto& r:m.resources)if(r.id==id)return &r;return nullptr;}
bool IsText(Family f){return f==Family::Menu||f==Family::Battle||f==Family::Event;}
bool Allowed(Family family,std::string_view request,std::uint32_t api){
    if(family==Family::Graphic)return api>=4&&FindGraphicProfile(request)!=nullptr;
    if(IsText(family)){
        TextLayout layout;
        return DescribeTextRequest(request,layout)&&layout.family==family&&api>=layout.minimumApi;
    }
    if(family==Family::Metrics)return Starts(request,"/ffx_data/ffx_ps2/ffx/master/")&&request.find("/menu/")!=std::string_view::npos&&Ends(request,"/base.ftc");
    constexpr std::string_view atlas="/ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/";
    if(!Starts(request,atlas))return false;
    const auto name=request.substr(atlas.size());
    return (Starts(name,"font_")||Starts(name,"shadow_"))&&name.find('/')==std::string_view::npos&&Ends(name,".dds.phyre");
}
std::uint8_t BaseCode(std::uint32_t u){
    constexpr std::string_view ascii="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    if(u<128){const auto at=ascii.find(static_cast<char>(u));if(at!=std::string_view::npos)return static_cast<std::uint8_t>(48u+at);}
    // Western table byte identities are independent of UTF-8 source encoding.
    constexpr std::pair<std::uint32_t,std::uint32_t> accents[]={
        {0x00c7,167},{0x00c0,163},{0x00c1,164},{0x00c2,165},{0x00c4,166},{0x00c8,168},{0x00c9,169},{0x00ca,170},{0x00cb,171},
        {0x00cc,172},{0x00cd,173},{0x00ce,174},{0x00cf,175},{0x00d1,176},{0x00d2,177},{0x00d3,178},{0x00d4,179},
        {0x00d6,180},{0x00d9,181},{0x00da,182},{0x00db,183},{0x00dc,184},{0x00df,185},{0x00e0,186},{0x00e1,187},
        {0x00e2,188},{0x00e4,189},{0x00e7,190},{0x00e8,191},{0x00e9,192},{0x00ea,193},{0x00eb,194},{0x00ec,195},
        {0x00ed,196},{0x00ee,197},{0x00ef,198},{0x00f1,199},{0x00f2,200},{0x00f3,201},{0x00f4,202},{0x00f6,203},
        {0x00f9,204},{0x00fa,205},{0x00fb,206},{0x00fc,207},{0x2026,211},{0x2019,213},{0x2014,150},{0x201c,148},{0x201d,149}
    };
    for(const auto& item:accents)if(item.first==u)return static_cast<std::uint8_t>(item.second);
    return 0;
}
} // namespace

bool SafeRelativePath(std::string_view path){
    if(path.empty()||path.size()>240||path.front()=='/'||path.back()=='/')return false;
    std::size_t begin=0;
    while(begin<path.size()){
        auto end=path.find('/',begin);if(end==std::string_view::npos)end=path.size();const auto part=path.substr(begin,end-begin);
        if(part.empty()||part=="."||part==".."||part.back()=='.')return false;
        for(char c:part)if(!Alpha(c)&&!Digit(c)&&c!='_'&&c!='-'&&c!='.')return false;
        const auto name=Lower(part.substr(0,part.find('.')));
        if(name=="con"||name=="prn"||name=="aux"||name=="nul"||
           (name.size()==4&&(Starts(name,"com")||Starts(name,"lpt"))&&name[3]>='1'&&name[3]<='9'))return false;
        begin=end+1;
    }
    return true;
}
bool IsEventRequest(std::string_view request){
    constexpr std::string_view prefix="/ffx_data/ffx_ps2/ffx/master/new_uspc/event/";
    if(!Starts(request,prefix))return false;
    auto tail=request.substr(prefix.size());
    if(Starts(tail,"obj_ps3/"))tail.remove_prefix(8);
    else if(Starts(tail,"obj_psv/"))tail.remove_prefix(8);
    else return false;
    if(!SafeRelativePath(tail))return false;
    const auto slash=tail.find('/');
    if(slash!=2)return false;
    for(char c:tail.substr(0,slash))if(!Alpha(c)&&!Digit(c))return false;
    tail.remove_prefix(slash+1);
    const auto end=tail.find('/');
    if(end==std::string_view::npos)return false;
    const auto name=tail.substr(0,end),file=tail.substr(end+1);
    if(!Identifier(name)||name.size()>32)return false;
    return file.size()==name.size()+4&&Starts(file,name)&&Ends(file,".bin");
}
bool DescribeTextRequest(std::string_view request,TextLayout& output){
    constexpr std::string_view master="/ffx_data/ffx_ps2/ffx/master/new_uspc/";
    constexpr std::string_view kernel="/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/";
    struct IndexedProfile { std::string_view name; Family family; std::uint16_t stride; std::uint8_t slots; std::uint32_t api; };
    static constexpr IndexedProfile indexed[]={
        {"menu_txt.bin",Family::Menu,16,4,1},{"mmain_txt.bin",Family::Menu,16,4,1},
        {"config_txt.bin",Family::Menu,16,4,1},{"save_txt.bin",Family::Menu,16,2,1},
        {"arms_txt.bin",Family::Battle,16,4,1},{"btl_txt.bin",Family::Battle,8,2,1},
        {"btlend_txt.bin",Family::Battle,16,2,1},{"build_txt.bin",Family::Battle,16,2,1},
        {"item_txt.bin",Family::Battle,16,4,1},{"name_txt.bin",Family::Battle,16,2,1},
        {"status_txt.bin",Family::Battle,16,4,1},{"summon_txt.bin",Family::Battle,16,4,1},
        {"a_ability.bin",Family::Battle,108,4,3},{"command.bin",Family::Battle,96,4,3},
        {"important.bin",Family::Battle,20,4,3},{"item.bin",Family::Battle,96,4,3},
        {"monmagic1.bin",Family::Battle,92,4,3},{"monmagic2.bin",Family::Battle,92,4,3},
        {"monster1.bin",Family::Battle,128,4,3},{"monster2.bin",Family::Battle,128,4,3},
        {"monster3.bin",Family::Battle,128,4,3},{"panel.bin",Family::Battle,24,4,3},
        {"sphere.bin",Family::Battle,16,2,3},{"w_name.bin",Family::Battle,72,14,3}
    };
    if(Starts(request,kernel))for(const auto& profile:indexed){
        if(request.substr(kernel.size())==profile.name){
            // Indexed text references are offset/metadata pairs, including
            // the two pairs in the compact battle-message record.
            output={TextContainer::Indexed,profile.family,profile.api,profile.stride,profile.slots,4};
            return true;
        }
    }
    if(IsEventRequest(request)){output={TextContainer::Field,Family::Event,2,8,2,4};return true;}
    if(Starts(request,master)){
        const auto relative=request.substr(master.size());
        if(relative=="menu/menumain.bin"){output={TextContainer::Field,Family::Menu,3,8,2,4};return true;}
        if(relative=="menu/macrodic.dcp"){output={TextContainer::Macro,Family::Menu,3,0,2,2};return true;}
        constexpr std::string_view banks="battle/btl/";
        if(Starts(relative,banks)){
            const auto tail=relative.substr(banks.size());
            const auto slash=tail.find('/');
            if(slash!=std::string_view::npos){
                const auto name=tail.substr(0,slash),leaf=tail.substr(slash+1);
                if(name.size()<=32&&Identifier(name)&&leaf.size()==name.size()+4&&Starts(leaf,name)&&Ends(leaf,".bin")){
                    output={TextContainer::Field,Family::Battle,3,8,2,4};return true;
                }
            }
        }
    }
    // Lockit interleaves Western game bytes with Flash ASCII/UTF-8 rows. A
    // line-only parser does not prove which native consumer owns each row.
    return false;
}
bool CanonicalRequest(std::string_view path,std::string& output){
    if(path.empty()||path.size()>255)return false;
    std::string value(path);std::replace(value.begin(),value.end(),'\\','/');
    if(Starts(value,"../../../"))value.erase(0,9);
    if(!value.empty()&&value.front()=='/')value.erase(0,1);
    if(!SafeRelativePath(value))return false;
    value=Lower(value);
    if(!Starts(value,"ffx_data/"))return false;
    output="/"+value;return true;
}
bool ParseManifest(std::string_view json,Manifest& output,std::string& error){
    try{
        const Node root=Reader(json).Read();
        Keys(root,{"schema_version","capability","hook_api","locale","display_name","pack_version","base_locale","fallback","activation","executable_sha256","coverage","resources","fonts"});
        const auto schema=Number(root,"schema_version",1,HookApi);
        const auto api=Number(root,"hook_api",1,HookApi);
        if(schema!=api)Reject("Schema and hook API versions must match");
        if(Text(root,"capability")!="ffx.text-locale"||Text(root,"fallback")!="native"||Text(root,"activation")!="restart")Reject("Unsupported text-locale capability/fallback/activation");
        if(Digest(root,"executable_sha256")!=ExecutableSha256)Reject("Executable fingerprint is incompatible");
        Manifest candidate;candidate.schemaVersion=schema;candidate.hookApi=api;candidate.locale=Text(root,"locale",35);candidate.displayName=Text(root,"display_name",96);candidate.packVersion=Text(root,"pack_version",32);
        if(!Locale(candidate.locale)||candidate.locale!="pt-BR"||!Version(candidate.packVersion))Reject("Unsupported locale or invalid package version");
        // v1 never coerces the global language manager. Only the demonstrated Western base is admitted.
        candidate.baseLocale=Number(root,"base_locale",1,1);
        std::set<std::string> ids,requests,paths;std::uint64_t total=0;std::size_t menuCount=0,battleCount=0,eventCount=0,graphicCount=0;
        for(const auto& node:Array(root,"resources",1,4096)){
            Keys(node,{"id","family","request","path","source_size","size","source_sha256","sha256"},{"font"});
            Resource r;r.id=Text(node,"id",64);r.path=Text(node,"path");
            if(!Identifier(r.id)||!ids.insert(r.id).second||!SafeRelativePath(r.path)||!paths.insert(Lower(r.path)).second)Reject("Invalid/duplicate resource ID or physical path");
            const auto request=Text(node,"request");if(!CanonicalRequest(request,r.request)||!requests.insert(r.request).second)Reject("Invalid/duplicate virtual request");
            const auto family=Text(node,"family",32);
            if(family=="menu"){r.family=Family::Menu;++menuCount;}else if(family=="battle"){r.family=Family::Battle;++battleCount;}else if(family=="events"&&schema>=2){r.family=Family::Event;++eventCount;}else if(family=="ui_texture"&&schema>=4){r.family=Family::Graphic;++graphicCount;}else if(family=="font_metrics")r.family=Family::Metrics;else if(family=="font_atlas")r.family=Family::Atlas;else Reject("Unsupported resource family");
            if(!Allowed(r.family,r.request,api))Reject("Resource path or API is outside the demonstrated text/font families");
            r.sourceSize=Number(node,"source_size",1,MaxResourceBytes);r.size=Number(node,"size",1,MaxResourceBytes);
            total+=r.size;if(total>MaxPackBytes)Reject("Pack exceeds total resource size bound");
            r.sourceSha256=Digest(node,"source_sha256");r.sha256=Digest(node,"sha256");
            if(r.family==Family::Graphic){
                const auto* profile=FindGraphicProfile(r.request);
                if(!profile||r.sourceSha256!=profile->source||r.sha256!=profile->output||r.sourceSize!=profile->bytes||r.size!=profile->bytes)
                    Reject("UI texture differs from its examined native compilation");
            }
            if(IsText(r.family)){r.font=Text(node,"font",64);if(!Identifier(r.font))Reject("Invalid font binding");}
            else if(node.object.count("font"))Reject("Font resources cannot recursively bind a font");
            candidate.resources.push_back(std::move(r));
        }
        if(!menuCount&&!battleCount&&!eventCount)Reject("A text pack must contain a supported text resource");
        const auto& coverage=Get(root,"coverage");
        if(schema==1)Keys(coverage,{"menu","battle","events","texture_text"});
        else Keys(coverage,{"menu","battle","events","subtitles","texture_text"});
        if(Text(coverage,"menu")!=(menuCount?"partial":"unavailable")||
           Text(coverage,"battle")!=(battleCount?"partial":"unavailable")||
           Text(coverage,"events")!=(eventCount?"partial":"unavailable")||
           (schema>=2&&Text(coverage,"subtitles")!=(eventCount?"partial":"unavailable"))||
           Text(coverage,"texture_text")!=(graphicCount?"partial":"unavailable"))Reject("Coverage does not match supported resources");
        std::set<std::string> fontIds,usedFontResources;
        for(const auto& node:Array(root,"fonts",1,16)){
            Keys(node,{"id","encoding","preserve_native","metrics","atlases","glyphs"});Font font;font.id=Text(node,"id",64);font.metrics=Text(node,"metrics",64);
            const auto encoding=Text(node,"encoding");
            if(encoding=="ffx-western-v2"&&api>=4)font.profile=2;
            else if(encoding!="ffx-western-v1")Reject("Invalid font encoding or API");
            if(!Identifier(font.id)||!fontIds.insert(font.id).second)Reject("Invalid font identity");
            const auto& preserve=Get(node,"preserve_native");if(preserve.kind!=Node::Kind::Boolean||!preserve.boolean)Reject("Original glyphs must be preserved");
            const auto* metrics=ById(candidate,font.metrics);if(!metrics||metrics->family!=Family::Metrics||!usedFontResources.insert(font.metrics).second)Reject("Missing, duplicated or mistyped font metrics");
            for(const auto& atlas:Array(node,"atlases",2,32)){const auto id=Text(atlas,64);const auto* resource=ById(candidate,id);if(!resource||resource->family!=Family::Atlas||!usedFontResources.insert(id).second)Reject("Missing, duplicated or mistyped font atlas");font.atlases.push_back(id);}
            std::set<std::uint32_t> unicode,codes;
            for(const auto& glyph:Array(node,"glyphs",0,16)){
                Keys(glyph,{"unicode","code","width"});Glyph g;g.unicode=Number(glyph,"unicode",160,0x10ffff);g.code=static_cast<std::uint8_t>(Number(glyph,"code",240,255));g.width=static_cast<std::uint8_t>(Number(glyph,"width",1,56));
                if(!Scalar(g.unicode)||BaseCode(g.unicode)||!unicode.insert(g.unicode).second||!codes.insert(g.code).second)Reject("Custom glyph collides with native mapping or another glyph");
                font.glyphs.push_back(g);
            }
            candidate.fonts.push_back(std::move(font));
        }
        std::set<std::string> referencedFonts;
        for(const auto& r:candidate.resources){if(IsText(r.family)){if(!fontIds.count(r.font))Reject("Text refers to a missing font");referencedFonts.insert(r.font);}else if(r.family!=Family::Graphic&&!usedFontResources.count(r.id))Reject("Unbound font resource");}
        if(referencedFonts!=fontIds)Reject("Unreferenced font binding");
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
const Resource* Resolve(const Manifest& manifest,std::string_view request,std::uint32_t nativeLocale,bool readOnly){
    if(!readOnly||nativeLocale!=manifest.baseLocale)return nullptr;
    std::string key;if(!CanonicalRequest(request,key))return nullptr;
    for(const auto& r:manifest.resources)if(r.request==key)return &r;
    return nullptr;
}
bool EncodeLiteral(const Font& font,std::string_view utf8,std::size_t capacity,std::vector<std::uint8_t>& output,std::string& error){
    try{
        std::vector<std::uint8_t> bytes;std::size_t at=0;std::uint32_t u=0;
        while(at<utf8.size()){
            if(!Next(utf8,at,u)||u<32||(u>=127&&u<160))Reject("Invalid UTF-8 or control code in literal text");
            auto code=BaseCode(u);if(!code)for(const auto& g:font.glyphs)if(g.unicode==u){code=g.code;break;}
            if(!code)Reject("The font has no glyph for this Unicode character");
            if(bytes.size()>=capacity)Reject("Encoded text exceeds its slot");
            bytes.push_back(code);
        }
        output=std::move(bytes);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
} // namespace FfxHooks::TextLanguage
