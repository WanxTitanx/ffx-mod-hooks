#include "TextLanguagePack.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace FfxHooks::TextLanguage {
namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct FontAsset {const char* name;const char* source;const char* output;};
// A self-declared matching hash does not certify arbitrary glyph pixels.
// v1 admits only the examined, original-preserving Western PT-BR profile.
constexpr FontAsset FontAssets[]={
 {"base.ftc","309ba121c26b693a8bea16f10891c997a0363a72307bc7af80f25d11cf21f8c2","934dd27b6615ef10e940635a1acf7e814785c533da6f8a8426cb40a20aa01fa2"},
 {"font_0_0.dds.phyre","d4ccdc03c461238b664e960a2efca12888eddc210ec67479151d7957e336d926","4bce810435e33eb645422c6b95bb10d3c9bd23a0b0f6ccfb30d0fef2d3e14317"},
 {"font_0_1.dds.phyre","15ad660bda441c061832e542e69b57d31e3a529f2f4ce7d177988d7b225246b6","5acc4235bef724cf839dbaba1a73bfcfbc6c9f4695ab7d17e43f635ca889705a"},
 {"shadow_0_0.dds.phyre","5688e69d337bf0ed7b96730b92748002bd8e22590e46ab1b85fa4da8dd6e6e59","680209161e6958450c204296dfa3a68abb76e91c9feb00e0d810d30ca58f98a3"},
 {"shadow_0_1.dds.phyre","2f1e8667334d2f554c8dbab90e6f76d146c559a6dfca8f220c2cd786e0cdf47e","410d0d4f4c2790b2aab2d81feacdda34b3e30737adadff93dcaa6ff0568a2cb4"}
};
constexpr Glyph ExpectedGlyphs[]={{227,242,31},{245,243,33},{195,244,42},{213,245,42}};
std::size_t Index(const Manifest& m,std::string_view id){
 for(std::size_t n=0;n<m.resources.size();++n)if(m.resources[n].id==id)return n;
 throw std::runtime_error("Missing resource binding");
}
void ValidateFont(const PreparedPack& p,const std::vector<Bytes>& sources,Advances& advances){
 Require(p.manifest.fonts.size()==1,"Version 1 requires one examined Western font");
 const auto& font=p.manifest.fonts[0];
 Require(font.atlases.size()==4&&font.glyphs.size()==4,"Font requires four atlas pages and all four PT-BR glyphs");
 for(const auto& expected:ExpectedGlyphs)
  Require(std::any_of(font.glyphs.begin(),font.glyphs.end(),[&](const Glyph& g){return g.unicode==expected.unicode&&g.code==expected.code&&g.width==expected.width;}),"Glyph mapping or advance differs from the examined profile");
 std::set<std::string> names;
 for(const auto& r:p.manifest.resources){
  if(r.family!=Family::Metrics&&r.family!=Family::Atlas)continue;
  const auto name=r.request.substr(r.request.find_last_of('/')+1);
  const auto found=std::find_if(std::begin(FontAssets),std::end(FontAssets),[&](const FontAsset& f){return name==f.name;});
  Require(found!=std::end(FontAssets)&&names.insert(name).second,"Unexamined or duplicated font page");
  Require(r.sourceSha256==found->source&&r.sha256==found->output,"Font asset is outside the examined profile");
 }
 Require(names.size()==5,"Complete metric/font/shadow group is required");
 const auto index=Index(p.manifest,font.metrics);const auto& metrics=p.resources[index];
 Require(metrics.size()==304&&sources[index].size()==304,"Western metrics extent changed");
 advances.fill(0);
 for(unsigned code=48;code<256;++code)advances[code]=metrics[64+code-48];
 for(const auto& g:font.glyphs)Require(advances[g.code]==g.width,"Manifest advance differs from actual metrics");
}
}

bool AdmitPack(std::string_view json,const PackIo& io,PreparedPack& output,std::string& error){
 try {
  Require(io.read&&io.sha256,"Pack IO is incomplete");PreparedPack candidate;
  if(!ParseManifest(json,candidate.manifest,error))return false;
  std::uint64_t combined=0;
  for(const auto& r:candidate.manifest.resources){combined+=r.size;combined+=r.sourceSize;}
  Require(combined<=MaxPackBytes,"Source and output working set exceeds pack limit");
  std::vector<Bytes> sources;std::string hash;
  for(const auto& r:candidate.manifest.resources){
   Bytes source,target;
   Require(io.read(io.context,r,true,source)&&source.size()==r.sourceSize,"Native source is missing or has a different size");
   Require(io.sha256(io.context,source,hash)&&hash==r.sourceSha256,"Native source fingerprint changed");
   Require(io.read(io.context,r,false,target)&&target.size()==r.size,"Pack resource is missing or has a different size");
   Require(io.sha256(io.context,target,hash)&&hash==r.sha256,"Pack resource fingerprint changed");
   sources.push_back(std::move(source));candidate.resources.push_back(std::move(target));
  }
  Advances advances{};ValidateFont(candidate,sources,advances);
  for(std::size_t n=0;n<candidate.resources.size();++n){
   const auto& r=candidate.manifest.resources[n];
   if(r.family==Family::Menu||r.family==Family::Battle||r.family==Family::Event)
    if(!ValidateTextReplacement(r.request,sources[n],candidate.resources[n],candidate.manifest.fonts[0],advances,error))return false;
  }
  output=std::move(candidate);error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
}
