#include "ExecutableFixtureIdentity.h"
#include "../hooks/TextLanguageCore.h"
#include "../hooks/TextLanguageGraphicsCatalog.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace FfxHooks::TextLanguage;
static unsigned checks=0, failures=0;
static void Check(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
static std::string Replace(std::string value,std::string_view from,std::string_view to){
    const auto at=value.find(from);if(at==std::string::npos)throw std::runtime_error("Test fixture substitution missing");
    value.replace(at,from.size(),to);return value;
}
static const char* Valid = R"json({
 "schema_version":1,"capability":"ffx.text-locale","hook_api":1,
 "locale":"pt-BR","display_name":"Português (Brasil)","pack_version":"1.0.0",
 "base_locale":1,"fallback":"native","activation":"restart",
 "executable_sha256":")json" FFXHOOKS_FIXTURE_SHA256 R"json(",
 "coverage":{"menu":"partial","battle":"unavailable","events":"unavailable","texture_text":"unavailable"},
 "resources":[
  {"id":"menu","family":"menu","request":"/FFX_Data/ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin","path":"text/menu.bin","font":"western","source_size":12,"size":12,"source_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
  {"id":"metrics","family":"font_metrics","request":"/FFX_Data/ffx_ps2/ffx/master/jppc/menu/us/base.ftc","path":"font/base.ftc","source_size":256,"size":288,"source_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
  {"id":"normal","family":"font_atlas","request":"/FFX_Data/GameData/PS3Data/menu_us/base_ftc/D3D11/font_0_0.dds.phyre","path":"font/normal.dds.phyre","source_size":128,"size":128,"source_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
  {"id":"shadow","family":"font_atlas","request":"/FFX_Data/GameData/PS3Data/menu_us/base_ftc/D3D11/shadow_0_0.dds.phyre","path":"font/shadow.dds.phyre","source_size":128,"size":128,"source_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}
 ],
 "fonts":[{"id":"western","encoding":"ffx-western-v1","preserve_native":true,"metrics":"metrics","atlases":["normal","shadow"],"glyphs":[{"unicode":227,"code":240,"width":8},{"unicode":245,"code":241,"width":8},{"unicode":195,"code":242,"width":9},{"unicode":213,"code":243,"width":9}]}]
})json";
int main(int argc,char** argv){
    Manifest m;std::string error;
    {
        const auto& graphic=GraphicProfiles[0];
        auto value=Replace(Replace(Valid,"\"schema_version\":1","\"schema_version\":4"),"\"hook_api\":1","\"hook_api\":4");
        value=Replace(value,"\"texture_text\":\"unavailable\"","\"subtitles\":\"unavailable\",\"texture_text\":\"partial\"");
        const std::string resource="{\"id\":\"ui-proof\",\"family\":\"ui_texture\",\"request\":\""+std::string(graphic.request)+"\",\"path\":\"graphics/proof.phyre\",\"source_size\":"+std::to_string(graphic.bytes)+",\"size\":"+std::to_string(graphic.bytes)+",\"source_sha256\":\""+std::string(graphic.source)+"\",\"sha256\":\""+std::string(graphic.output)+"\"},";
        value=Replace(value,"\"resources\":[","\"resources\":["+resource);
        Manifest examined;
        Check(ParseManifest(value,examined,error),"API 4 admits only the examined graphic binding");
        Check(!ParseManifest(Replace(value,graphic.output,std::string(64,'c')),examined,error),"self-declared graphic pixels are rejected");
        Check(!ParseManifest(Replace(Replace(value,"\"schema_version\":4","\"schema_version\":3"),"\"hook_api\":4","\"hook_api\":3"),examined,error),"API 3 cannot admit graphical replacements");
    }
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    Check(std::string(ExecutableSha256)=="0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d","Steam target uses its exact executable identity");
#else
    Check(std::string(ExecutableSha256)=="78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced","legacy target keeps its exact executable identity");
#endif
    std::vector<std::uint8_t> cedilla;
    Check(EncodeLiteral(Font{},u8"Çç",2,cedilla,error)&&cedilla==std::vector<std::uint8_t>({167,190}),"verified native uppercase and lowercase cedilla");
    Check(ParseManifest(Valid,m,error),"valid PT-BR manifest is admitted");
    if(!m.fonts.empty()){
        Check(m.locale=="pt-BR"&&m.displayName=="Português (Brasil)"&&m.baseLocale==1,"virtual locale keeps its own identity");
        Check(Resolve(m,"../../../FFX_Data/ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin",1,true)!=nullptr,"verified stream prefix matches");
        Check(Resolve(m,"\\FFX_Data\\ffx_ps2\\ffx\\master\\new_uspc\\battle\\kernel\\menu_txt.bin",1,true)!=nullptr,"slash and ASCII case normalization");
        Check(Resolve(m,"../../../ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin",1,true)!=nullptr,
              "actual native kernel requests omit the manifest-only FFX_Data namespace");
        Check(!Resolve(m,m.resources[0].request,0,true),"a Japanese native session retains original routing");
        Check(!Resolve(m,m.resources[0].request,1,false),"writes never enter the text pack");
        Check(!Resolve(m,"/FFX_Data/Sound/Voice/JP/voice.fsb",1,true),"voice path stays native");
        Check(!Resolve(m,"/FFX_Data/unknown.bin",1,true),"missing translation falls back to native");
        std::vector<std::uint8_t> encoded;
        Check(EncodeLiteral(m.fonts[0],"Português (Brasil): ações, órgãos!",100,encoded,error),"real PT-BR glyph codes encode");
        Check(std::find(encoded.begin(),encoded.end(),0xc1)!=encoded.end()&&std::find(encoded.begin(),encoded.end(),0xf0)!=encoded.end()&&std::find(encoded.begin(),encoded.end(),0xf1)!=encoded.end(),"circumflex and tilde glyph codes survive");
        std::string decoded="unchanged";
        Check(DecodeLiteral(m.fonts[0],encoded.data(),encoded.size(),decoded)&&decoded==u8"Português (Brasil): ações, órgãos!","plain native names round-trip through the published Portuguese font");
        const std::string previous=decoded;const std::uint8_t controls[]={1,2,3,4,0};
        for(auto control:controls)Check(!DecodeLiteral(m.fonts[0],&control,1,decoded)&&decoded==previous,"control streams cannot become Workshop names");
        const std::uint8_t missing=0xFF;
        Check(!DecodeLiteral(Font{},&missing,1,decoded)&&decoded==previous,"unknown glyph does not manufacture a localized name");
        std::vector<std::uint8_t> variant;
        Check(EncodeLiteral(m.fonts[0],"Break Limits",100,variant,error)&&DecodeLiteral(m.fonts[0],variant.data(),variant.size(),decoded)&&decoded=="Break Limits","modified ability identity is preserved instead of a vanilla ID-based name");
        const auto before=encoded;
        Check(!EncodeLiteral(m.fonts[0],"emoji 😀",100,encoded,error)&&encoded==before,"missing glyph rejects without destroying output");
        Check(!EncodeLiteral(m.fonts[0],"abc",2,encoded,error)&&encoded==before,"overflow never truncates a string");
        Check(!EncodeLiteral(m.fonts[0],std::string("a\x01",2),10,encoded,error),"raw control injection is rejected");
        Check(!EncodeLiteral(m.fonts[0],std::string("\xc0\xaf",2),10,encoded,error),"overlong UTF-8 is rejected");
        Manifest other=m;other.locale="es-MX";other.resources[0].path="other/menu.bin";
        Check(Resolve(other,m.resources[0].request,1,true)->path=="other/menu.bin"&&Resolve(m,m.resources[0].request,1,true)->path=="text/menu.bin","independent packs do not share a routing cache");
    }
    const std::pair<const char*,const char*> bad[]={
        {"\"schema_version\":1","\"schema_version\":2"},
        {"\"hook_api\":1","\"hook_api\":2"},
        {"\"base_locale\":1","\"base_locale\":0"},
        {"\"locale\":\"pt-BR\"","\"locale\":\"../pt-BR\""},
        {"\"locale\":\"pt-BR\"","\"locale\":\"es-MX\""},
        {"\"fallback\":\"native\"","\"fallback\":\"pt-BR\""},
        {"\"activation\":\"restart\"","\"activation\":\"live\""},
        {"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1"},
        {"\"schema_version\":1","\"schema_version\":1,\"surprise\":true"},
        {"text/menu.bin","../menu.bin"},{"text/menu.bin","C:/menu.bin"},
        {"text/menu.bin","text/CON.bin"},{"text/menu.bin","text/menu.bin:stream"},
        {"text/menu.bin","text/menu.bin."},{"text/menu.bin","text//menu.bin"},
        {"text/menu.bin","font/normal.dds.phyre"},
        {"\"id\":\"shadow\"","\"id\":\"normal\""},
        {"\"font\":\"western\"","\"font\":\"absent\""},
        {"\"metrics\":\"metrics\"","\"metrics\":\"menu\""},
        {"\"preserve_native\":true","\"preserve_native\":false"},
        {"\"code\":240","\"code\":112"},
        {"\"code\":241","\"code\":240"},
        {"\"unicode\":245","\"unicode\":227"},
        {"\"width\":8","\"width\":0"},
        {"\"size\":12","\"size\":67108865"},
        {"\"size\":12","\"size\":-1"},
        {"\"size\":12","\"size\":12.0"},
        {"\"events\":\"unavailable\"","\"events\":\"partial\""},
        {"\"menu\":\"partial\"","\"menu\":\"unavailable\""},
        {"menu_txt.bin","command.bin"},
        {"\"family\":\"menu\"","\"family\":\"voice\""},
        {"\"display_name\":\"Português (Brasil)\"","\"display_name\":\"bad\\u0000name\""},
        {"\"display_name\":\"Português (Brasil)\"","\"display_name\":\"bad\\ud800\""}
    };
    for(const auto& pair:bad){Manifest unchanged;unchanged.locale="preserved";Check(!ParseManifest(Replace(Valid,pair.first,pair.second),unchanged,error)&&unchanged.locale=="preserved",pair.first);}
    // Native FTC widths are HD advances: the proven PT-BR capitals occupy 42,
    // while the 14-pixel logical cell spans 56 pixels in the 512-pixel atlas.
    Manifest wide;
    Check(ParseManifest(Replace(Valid,"\"width\":9","\"width\":42"),wide,error),
          "real PT-BR capital advance is admitted without narrowing");
    Check(!ParseManifest(Replace(Valid,"\"width\":9","\"width\":57"),wide,error),
          "advance beyond the physical glyph cell is rejected");
    for(std::size_t n=0;n<std::string_view(Valid).size();++n){Manifest out;Check(!ParseManifest(std::string_view(Valid).substr(0,n),out,error),"truncated JSON rejected");}
    Check(!SafeRelativePath("..")&&!SafeRelativePath("foo/../bar")&&SafeRelativePath("text/menu.bin"),"portable resource paths");
    std::string canonical="preserved";
    Check(!CanonicalRequest("../../../../FFX_Data/secret",canonical)&&canonical=="preserved","unverified prefix rejected atomically");
    Check(!CanonicalRequest("/FFX_Data/../Sound/Voice/JP/x",canonical),"virtual traversal rejected");
    Check(NativeSourceRequest("/FFX_Data/ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin",canonical)&&
          canonical=="ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin","kernel source uses the exact archive member without the package namespace");
    Check(NativeSourceRequest("../../../ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_0.dds.phyre",canonical)&&
          canonical=="ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_0.dds.phyre","graphics retain their actual ffx_data directory");
    Check(NativeSourceRequest("../../../ffx_ps2/ffx/master/uspc/menu/base.ftc",canonical)&&
          canonical=="ffx_ps2/ffx/master/uspc/menu/base.ftc","native Western metrics resolve without a second virtual root");
    canonical="preserved";
    Check(!NativeSourceRequest("/FFX_Data/ffx_ps2/../secret",canonical)&&canonical=="preserved","source conversion cannot bypass traversal rejection");
    auto v2=Replace(Replace(Valid,"\"schema_version\":1","\"schema_version\":2"),"\"hook_api\":1","\"hook_api\":2");
    v2=Replace(v2,"\"texture_text\":\"unavailable\"","\"subtitles\":\"unavailable\",\"texture_text\":\"unavailable\"");
    Manifest version2;
    Check(ParseManifest(v2,version2,error)&&version2.schemaVersion==2,"v2 capabilities are admitted separately from v1");
    Check(!ParseManifest(Replace(v2,"\"locale\":\"pt-BR\"","\"locale\":\"es-MX\""),version2,error),
          "v2 cannot admit a locale the configured runtime cannot activate");
    auto event=Replace(v2,"\"family\":\"menu\"","\"family\":\"events\"");
    event=Replace(event,"battle/kernel/menu_txt.bin","event/obj_ps3/ss/ssbt0000/ssbt0000.bin");
    event=Replace(event,"\"menu\":\"partial\"","\"menu\":\"unavailable\"");
    event=Replace(event,"\"events\":\"unavailable\"","\"events\":\"partial\"");
    event=Replace(event,"\"subtitles\":\"unavailable\"","\"subtitles\":\"partial\"");
    Check(ParseManifest(event,version2,error)&&version2.resources[0].family==Family::Event,"event/subtitle family is explicit and versioned");
    Check(!ParseManifest(Replace(event,"obj_ps3","obj_unknown"),version2,error),"unknown event tree rejects");
    Check(!ParseManifest(Replace(event,"ssbt0000.bin","different.bin"),version2,error),"event identity and filename must agree");
    Check(!ParseManifest(Replace(event,"\"hook_api\":2","\"hook_api\":3"),version2,error),"future hook API rejects before activation");
    Check(!ParseManifest(Replace(event,"\"subtitles\":\"partial\"","\"subtitles\":\"complete\""),version2,error),"package cannot claim full subtitle coverage");
    auto v3=Replace(Replace(v2,"\"schema_version\":2","\"schema_version\":3"),"\"hook_api\":2","\"hook_api\":3");
    const std::string nativePrefix="/FFX_Data/ffx_ps2/ffx/master/new_uspc/";
    const std::string originalRequest=nativePrefix+"battle/kernel/menu_txt.bin";
    for(const char* file:{"a_ability.bin","command.bin","important.bin","item.bin","monmagic1.bin","monmagic2.bin",
                         "monster1.bin","monster2.bin","monster3.bin","panel.bin","sphere.bin","w_name.bin"}){
        auto extended=Replace(v3,originalRequest,nativePrefix+"battle/kernel/"+file);
        extended=Replace(extended,"\"family\":\"menu\"","\"family\":\"battle\"");
        extended=Replace(extended,"\"menu\":\"partial\"","\"menu\":\"unavailable\"");
        extended=Replace(extended,"\"battle\":\"unavailable\"","\"battle\":\"partial\"");
        Check(ParseManifest(extended,version2,error),"API 3 admits exact indexed text-prefix catalogue");
        auto old=Replace(Replace(extended,"\"schema_version\":3","\"schema_version\":2"),"\"hook_api\":3","\"hook_api\":2");
        Check(!ParseManifest(old,version2,error),"API 2 cannot opt into a new indexed layout");
    }
    for(const char* leaf:{"menu/menumain.bin","menu/macrodic.dcp"}){
        auto extended=Replace(v3,originalRequest,nativePrefix+leaf);
        Check(ParseManifest(extended,version2,error),"API 3 admits the exact additional menu containers");
    }
    Check(!ParseManifest(Replace(v3,originalRequest,"/FFX_Data/GameData/PS3Data/lockit/ffx_loc_kit_ps3_us.bin"),version2,error),
          "mixed-encoding lockit stays outside runtime admission");
    auto bank=Replace(v3,originalRequest,nativePrefix+"battle/btl/besa01_00/besa01_00.bin");
    bank=Replace(bank,"\"family\":\"menu\"","\"family\":\"battle\"");
    bank=Replace(bank,"\"menu\":\"partial\"","\"menu\":\"unavailable\"");
    bank=Replace(bank,"\"battle\":\"unavailable\"","\"battle\":\"partial\"");
    Check(ParseManifest(bank,version2,error),"API 3 admits bounded battle text-bank names");
    Check(!ParseManifest(Replace(bank,"besa01_00.bin","different.bin"),version2,error),"battle bank directory and leaf must agree");
    for(const char* leaf:{"battle/kernel/ply_save.bin","battle/kernel/ply_rom.bin","menu/albheddic.bin","menu/unknown.bin",
                         "battle/btl/besa01_00/voice.fsb","event/obj_ps3/ss/ssbt0000/ssbt0000.eb"})
        Check(!ParseManifest(Replace(v3,originalRequest,nativePrefix+leaf),version2,error),"API 3 keeps adjacent nontext data outside admission");
    Check(!ParseManifest(Replace(v3,"\"subtitles\":\"unavailable\"","\"subtitles\":\"complete\""),version2,error),
          "API 3 validates subtitle coverage as strictly as API 2");
    if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});Manifest external;Check(ParseManifest(data,external,error),"Python-generated manifest interoperability");if(!error.empty())std::cerr<<error<<'\n';}
    {
        // FFX_Kernel_LoadFileToTable keeps the 16-byte-rounded size of four tables in 16 bits.
        const std::string kernel="/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/";
        for(const char* leaf:{"command.bin","a_ability.bin","item.bin"}){
            Check(NativeKernelLimit(kernel+leaf,false)==0xFFF0u,"16-bit kernel tables stop at 0xFFF0 bytes");
            Check(NativeKernelLimit(kernel+leaf,true)==0x25800u,"an extended loader only keeps the staging buffer bound");
        }
        Check(NativeKernelLimit(kernel+"ply_rom.bin",false)==0xFFF0u&&NativeKernelLimit(kernel+"ply_rom.bin",true)==0xFFF0u,
              "ply_rom keeps its native 16-bit path even with the adapter");
        Check(sizeof(ExtendedKernelTables)/sizeof(ExtendedKernelTables[0])==3&&ExtendedKernelTables[0].kind==0&&
              ExtendedKernelTables[1].kind==2&&ExtendedKernelTables[2].kind==3,"the adapter owns loader cases 0, 2 and 3");
        {
            const auto fresh=PlanKernelBank(false,0,0x10010u),first=PlanKernelBank(false,0,0);
            Check(fresh.allocate&&!fresh.retainOld&&first.allocate,"the first load allocates the 32-bit extent, even when empty as natively");
            const auto reuse=PlanKernelBank(true,0x10010u,0x10010u),smaller=PlanKernelBank(true,0x10010u,0x200u);
            Check(!reuse.allocate&&!reuse.retainOld&&!smaller.allocate,"a reload that fits reuses the adapter bank like the native loader");
            const auto grow=PlanKernelBank(true,0x10010u,0x10020u);
            Check(grow.allocate&&grow.retainOld,"a larger reload gets a fresh bank and never frees the cached one");
            const auto unknown=PlanKernelBank(true,0,0x100u);
            Check(unknown.allocate&&unknown.retainOld,"a bank allocated before the adapter is never written past an unknown extent");
            const auto empty=PlanKernelBank(true,0x100u,0),missing=PlanKernelBank(true,0,0);
            Check(!empty.allocate&&!missing.allocate,"a missing table copies nothing and keeps the existing bank, as natively");
        }
        for(const char* leaf:{"monmagic1.bin","monmagic2.bin"})
            Check(NativeKernelLimit(kernel+leaf,false)==0x25800u,"32-bit tables are bounded by the staging buffer");
        Check(NativeKernelLimit(kernel+"btl_txt.bin",false)==0,"other kernel text has no loader bound");
        Check(NativeKernelLimit("/ffx_data/gamedata/ps3data/menu_us/d3d11/battle.dds.phyre",false)==0,"textures have no kernel bound");
        Check(NativeKernelLimit("/ffx_data/ffx_ps2/ffx/master/new_uspc/menu/command.bin",false)==0,"only the battle kernel folder is bounded");
    }
    std::cout<<"TextLanguageCore RT0: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
