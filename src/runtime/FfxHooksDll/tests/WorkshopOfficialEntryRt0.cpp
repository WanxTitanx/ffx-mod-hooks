#include "../hooks/NativeBindingsCore.h"
#include <cstdio>
#include <cstring>
namespace B=FfxHooks::NativeBindings;
int main(){unsigned checks=0,failures=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}};
 const auto defaults=B::Defaults();
 check(static_cast<unsigned>(B::Action::Count)==16,"only the16 surviving named shortcuts are exposed");
 for(unsigned i=0;i<static_cast<unsigned>(B::Action::Count);++i){const auto action=static_cast<B::Action>(i);
  check(std::strstr(B::Name(action),"Workshop")==nullptr,"Workshop is absent from keyboard/gamepad binding labels");
  const auto* key=B::Key(action);check(key&&std::strcmp(key,"bindings.workshop"),"no binding key resolves to Workshop");}
 check(B::Key(static_cast<B::Action>(16))==nullptr,"retired binding key is ignored without renumbering other actions");
 check(B::Validate(defaults,static_cast<B::Action>(16),{0x79,0})==B::BindResult::Invalid,"retired slot cannot be rebound to F10");
 check(B::Resolve(defaults,0x79,0)==B::Action::None,"F10 has no Workshop default action");
 check(static_cast<unsigned>(B::Action::RefreshWindow)==15&&B::Key(B::Action::MenuF7)&&B::Key(B::Action::MenuF8),"all surviving IDs retain their meanings");
 std::printf("WorkshopOfficialEntryRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
