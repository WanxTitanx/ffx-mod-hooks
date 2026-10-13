#include "../hooks/ArcanaUiCore.h"
#include "../hooks/ArcanaPresentation.h"
#include <cstdio>

using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
}
int main(){
    State state;Award(state,36);Equip(state,state.revision,0,0,36);
    const State original=state;
    Ui::View view;Ui::Observe(view,state,0x1234,1,0,true);view.category=2;
    Ui::Input(view,state,Ui::Key::Confirm);view.cursor=Ui::kGuideRow;
    Check(Ui::PickerCard(Ui::kGuideRow)==kEmpty,"guide entry is not an inventory card");
    const auto open=Ui::Input(view,state,Ui::Key::Confirm);
    Check(open.action==Ui::Action::None&&view.page==Ui::Page::Guide&&view.guidePage==0,"guide opens without an equipment command");
    for(unsigned page=0;page<Presentation::kGuidePageCount;++page){
        Check(view.page==Ui::Page::Guide&&view.guidePage==page,"all guide sections can be read in order");
        const auto before=view;const auto next=Ui::Input(view,state,Ui::Key::Confirm);
        Check(next.action==Ui::Action::None,"reading never generates equipment or mode writes");
        Check(Ui::Feedback(before,view,Ui::Key::Confirm,next)==Ui::Sound::MoveConfirm,"page confirmation has one native feedback cue");
    }
    Check(view.page==Ui::Page::Picker&&view.cursor==Ui::kGuideRow,"finishing returns to the same guide entry");
    Ui::Input(view,state,Ui::Key::Confirm);Ui::Input(view,state,Ui::Key::PageDown);
    Check(view.guidePage==1,"optional review supports page navigation");
    Ui::Input(view,state,Ui::Key::PageUp);Check(view.guidePage==0,"review can go back without leaving the guide");
    Ui::Input(view,state,Ui::Key::Cancel);Check(view.page==Ui::Page::Picker,"manual guide can be closed without replaying mandatory intro");
    Check(state.revision==original.revision&&state.acquired==original.acquired&&state.slots==original.slots&&state.mode==original.mode,"guide reading preserves collection, slots and mode");
    Ui::Input(view,state,Ui::Key::Confirm);Ui::Observe(view,state,0x1234,2,0,true);
    Check(view.page==Ui::Page::Root,"new native session retires an old guide view");
    std::printf("ArcanaGuideUiRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
