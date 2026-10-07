#include "../hooks/UiNativeFont.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace N=FfxHooks::UiNativeFont;
namespace U=FfxHooks::UiLanguage;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static std::uint32_t Scalar(const unsigned char* p,std::size_t n){
    std::uint32_t cp=*p;if(n>1){cp&=(1u<<(7-n))-1u;for(std::size_t i=1;i<n;++i)cp=(cp<<6)|(p[i]&63);}return cp;
}
static unsigned Ink(const N::Glyph& g){unsigned total=0;for(auto p:g.pixels)total+=p>>24;return total;}
int main(int argc,char** argv){
    if(argc!=3)return 2;const std::filesystem::path fixture=argv[1],root=argv[2];
    for(unsigned locale=0;locale<U::LocaleCount;++locale){
        auto library=N::Library::Load(fixture,static_cast<U::Locale>(locale));
        Check(library->Find('A')&&library->Find('0')&&library->Size()<=1024,"native atlas supplies Latin digits/letters with a bounded cache");
        unsigned found=0,missing=0;
        for(std::size_t row=0;row<U::EntryCount();++row){
            const auto* p=reinterpret_cast<const unsigned char*>(U::Text(U::EntryKey(row),static_cast<U::Locale>(locale)));
            while(*p){const auto n=U::ScalarBytes(p);if(!n)return 3;const auto cp=Scalar(p,n);const auto* glyph=library->Find(cp);
                if(glyph){++found;Check(glyph->advance>0,"catalog native glyph keeps a real positive advance");}else ++missing;
                p+=n;}
        }
        if(locale<=static_cast<unsigned>(U::Locale::German))Check(missing==0,"every Western catalog scalar is rendered from native glyphs");
        else Check(found>missing,"Asian catalogs preserve available game glyphs and isolate missing scalars");
        std::printf("NATIVE_FONT locale=%s glyphs=%zu native=%u fallback=%u\n",U::Codes[locale],library->Size(),found,missing);
        if(locale==1){
            for(auto pair:{std::pair<std::uint32_t,std::uint32_t>{0xE3,'a'},{0xF5,'o'},{0xC3,'A'},{0xD5,'O'}}){
                const auto* accented=library->Find(pair.first);const auto* base=library->Find(pair.second);
                Check(accented&&base&&accented->advance==base->advance&&Ink(*accented)>Ink(*base),"Portuguese tilde retains the base advance and adds native accent pixels");
                bool preserved=true;for(std::size_t i=0;i<base->pixels.size();++i)if((base->pixels[i]>>24)>200&&accented->pixels[i]!=base->pixels[i])preserved=false;
                Check(preserved,"Portuguese accent composition preserves the original letter body");
            }
            const auto* zero=library->Find('0');std::ifstream stream(root/"us-0.rgba",std::ios::binary);
            const std::vector<unsigned char> reference((std::istreambuf_iterator<char>(stream)),{});
            Check(reference.size()==N::GlyphWidth*N::GlyphHeight*4,"independent Pillow reference has the expected sprite extent");
            if(reference.size()==zero->pixels.size()*4){double total=0;
                for(std::size_t i=0;i<zero->pixels.size();++i){const auto p=zero->pixels[i];const unsigned a=p>>24,ra=reference[4*i+3];
                    total+=std::abs(int(a)-int(ra));
                    for(unsigned c=0;c<3;++c){const unsigned shift=16-8*c;total+=std::abs(double((p>>shift)&255)*a/255.-double(reference[4*i+c])*ra/255.);}}
                const double error=total/double(reference.size());std::printf("NATIVE_SPRITE mean premultiplied error=%.5f\n",error);
                Check(error<2.0,"native digit orientation, coordinates and gradient match an independent atlas decoder");
            }
        }
    }
    for(const char* name:{"truncated.vbf","bad-extent.vbf","bad-block.vbf"}){
        bool rejected=false;try{(void)N::Library::Load(root/name,U::Locale::Portuguese);}catch(const std::exception&){rejected=true;}
        Check(rejected,"malformed or changed source font archives cannot publish glyphs");
    }
    std::printf("UiNativeFontRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
