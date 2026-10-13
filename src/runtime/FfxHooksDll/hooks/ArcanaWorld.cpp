#include "ArcanaWorld.h"
#include "ArcanaRuntime.h"
#include "ArcanaAssets.h"
#include <algorithm>
#include <cmath>

namespace FfxHooks::Arcana::World {
namespace {
Presentation::InputLatch input;
std::uint64_t epoch=0,imageGeneration=0;
std::uint16_t scene=0;
}
void Tick(const FieldPresentation::FieldContext& field,FieldPresentation::FieldPrompt& prompt) noexcept {
    NativeUi::Images images;images.generation=++imageGeneration;
    images.scene=field.scene;images.session=field.generation;
    if(epoch!=field.generation||scene!=field.scene){input.Reset();epoch=field.generation;scene=field.scene;}
    State state;Acquisition::Progress progress;Acquisition::Facts facts;std::uint64_t generation=0;
    if(!field.available||!Assets::Decoded(kTarotIconResource)||!Runtime::CaptureWorld(state,progress,facts,generation,field.scene)||generation!=field.generation){
        input.Reset();Assets::PublishWorld(images);return;
    }
    const auto targets=Acquisition::Discover(state,progress,facts,field.scene);
    struct Visible {unsigned index=0;float distance=0,x=0,y=0;};
    std::array<Visible,16> visible{};unsigned count=0;
    for(unsigned i=0;i<targets.count;++i){
        const auto& location=targets.values[i].location;
        const float dx=field.x-location.x,dz=field.z-location.z,distance=dx*dx+dz*dz;
        if(distance>Acquisition::kDisplayRadius*Acquisition::kDisplayRadius)continue;
        float x=0,y=0;
        // The marker uses the controlled actor's local floor elevation. Only
        // nearby ground-plane anchors are drawn; no authored world Y is guessed.
        if(!FieldPresentation::Project(location.x,field.y,location.z,x,y))continue;
        if(x<.025f||x>.975f||y<.075f||y>.98f)continue;
        visible[count++]={i,distance,x,y};
    }
    std::sort(visible.begin(),visible.begin()+count,[](const Visible& a,const Visible& b){return a.distance<b.distance;});
    for(unsigned i=0;i<count&&images.count<images.quads.size();++i){
        const auto& point=visible[i];const bool near=i==0&&point.distance<=Acquisition::kInteractRadius*Acquisition::kInteractRadius;
        const float width=near?.04f:.03f,height=width*16.f/9.f;
        images.quads[images.count++]= {kTarotIconResource,point.x-width/2,point.y-height,width,height};
    }
    Assets::PublishWorld(images);
    const bool canInteract=count&&visible[0].distance<=Acquisition::kInteractRadius*Acquisition::kInteractRadius;
    const auto navigation=input.Poll(true,canInteract,(field.held&0x20)!=0,(field.held&0x40)!=0);
    if(navigation!=Presentation::Navigation::Next)return;
    const auto result=Runtime::InteractWorld(generation,targets.values[visible[0].index],field.scene,field.x,field.z);
    prompt.text=result.message;prompt.card=result.card;
    if(result.kind!=Acquisition::ResultKind::None||result.message){images.count=0;Assets::PublishWorld(images);input.Reset();}
}
}
