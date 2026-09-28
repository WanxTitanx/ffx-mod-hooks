// Real F8 page; only external runtime status is substituted by the parent harness.
#include "../hooks/TextLanguageSettings.h"
static void TextLanguageMenuCases(){
 namespace C=FfxHooks::Config;namespace S=FfxHooks::TextLanguage::Settings;
 EquipmentMenu::StopReady();F8NativeSettingsReset();pointerSample={};pointerState={};NativeMenu::padEdge=NativeMenu::padDirection=0;
 C::ResetForTests();C::LoadTextForTests("[language]\nvoice=2\nsfx=1\nvideo=0\n","C:\\private-mod006-f8.ini");
 C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;f8Saved.clear();
 const int obj=NativeMenu::Alloc();Check(obj!=0,"text page fixture allocates");if(!obj)return;
 NativeMenu::WrW(obj,NativeMenu::O_SELECTED,6);NativeMenu::WrW(obj,NativeMenu::O_TOP,2);
 F8NativeSettingsPush(obj,NativeSettingsPage::TextLanguages);
 const auto count=F8NativeSettingsCount(NativeSettingsPage::TextLanguages);
 Check(count==4,"text page exposes original, PT-BR, status and Back");
 if(count==4){
  char label[128]{};F8NativeSettingsLabel(NativeSettingsPage::TextLanguages,1,label,sizeof(label));
  Check(std::strstr(label,S::BrazilianName)!=nullptr,"PT-BR has its own named selection");
  F8NativeSettingsActivate(obj,2);Check(F8NativeSettingsActive()&&f8Writes==0,"status row does not change configuration");
  F8NativeSettingsActivate(obj,1);
  Check(!F8NativeSettingsActive()&&S::Selection()==1&&f8Writes==1,"F8 saves the virtual locale");
  Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==6&&NativeMenu::RdW(obj,NativeMenu::O_TOP)==2,"parent cursor and scroll survive selection");
  Check(C::GetInt("language.voice",-1)==2&&C::GetInt("language.sfx",-1)==1&&C::GetInt("language.video",-1)==0,"text selection preserves all audio choices");
  Check(std::strstr(g_nativeSettingsNotice,"Restart")!=nullptr,"restart boundary is visible");
  F8NativeSettingsPush(obj,NativeSettingsPage::TextLanguages);
  Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==1,"reopen highlights saved PT-BR");
  F8NativeSettingsActivate(obj,3);Check(f8Writes==1&&S::Selection()==1,"Back changes no language");
  F8NativeSettingsPush(obj,NativeSettingsPage::TextLanguages);f8AllowWrite=false;
  F8NativeSettingsActivate(obj,0);Check(F8NativeSettingsActive()&&S::Selection()==1,"write failure retains PT-BR and its page");
  f8AllowWrite=true;F8NativeSettingsActivate(obj,0);Check(S::Selection()==0&&!F8NativeSettingsActive(),"F8 restores original after restart");
 }
 F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
