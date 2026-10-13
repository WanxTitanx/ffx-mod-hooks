#include "ArcanaFieldDialogCore.h"
#include "UiLanguage.h"
#include <cstring>

namespace FfxHooks::Arcana::FieldDialog {
bool Wrap(const char* input,char* output,std::size_t capacity,unsigned columns) noexcept {
    if(!input||!output||!capacity||!columns||!UiLanguage::ValidUtf8(input))return false;
    std::size_t used=0;unsigned column=0;
    const auto put=[&](char c){if(used+1>=capacity)return false;output[used++]=c;return true;};
    while(*input){
        if(*input=='\n'){if(!put(*input++))return false;column=0;continue;}
        if(*input==' '){++input;if(column&&column<columns){if(!put(' '))return false;++column;}continue;}
        std::size_t length=0;unsigned characters=0;
        while(input[length]&&input[length]!=' '&&input[length]!='\n'){
            length+=UiLanguage::ScalarBytes(reinterpret_cast<const unsigned char*>(input+length));++characters;
        }
        if(column&&column+characters>columns){if(!put('\n'))return false;column=0;}
        for(std::size_t consumed=0;consumed<length;){
            if(column==columns){if(!put('\n'))return false;column=0;}
            const auto bytes=UiLanguage::ScalarBytes(reinterpret_cast<const unsigned char*>(input));
            for(std::size_t i=0;i<bytes;++i)if(!put(*input++))return false;
            consumed+=bytes;++column;
        }
    }
    output[used]=0;return true;
}
void Controller::Reset() noexcept {
    active_=closing_=cancelArmed_=false;generation_=0;scene_=0;view_={};
    completion_=Presentation::Navigation::None;input_.Reset();
}
bool Controller::Begin(std::uint64_t generation,std::uint16_t scene,const Presentation::View& view) noexcept {
    if(active_||!generation||view.card>=Presentation::kCardCount||view.page>=Presentation::kGuidePageCount||
       (view.kind!=Presentation::Kind::Notice&&view.kind!=Presentation::Kind::Introduction&&view.kind!=Presentation::Kind::Objective&&view.kind!=Presentation::Kind::System))return false;
    Reset();active_=true;generation_=generation;scene_=scene;view_=view;return true;
}
Decision Controller::Poll(const Sample& sample) noexcept {
    if(!active_)return {};
    Decision result;result.view=view_;result.generation=generation_;
    if(sample.scene!=scene_){result.action=Action::Abandon;Reset();return result;}
    const bool stale=sample.generation!=generation_;
    if(stale)completion_=Presentation::Navigation::None;
    // Another native text bank can be used by a menu while our field bank is
    // hidden. Do not close its windows or discard the still-live field lease.
    if(sample.suspended){input_.Reset();cancelArmed_=false;return result;}
    if(!sample.owned){result.action=Action::Abandon;Reset();return result;}
    if(stale||sample.stopping||sample.foreign||!sample.available)completion_=Presentation::Navigation::None;
    if(sample.window==0||sample.window==4){
        result.action=Action::Release;
        if(closing_)result.navigation=completion_;
        Reset();return result;
    }
    if(closing_)return result;
    if(stale||sample.stopping||sample.foreign||!sample.available){
        closing_=true;result.action=Action::Close;input_.Reset();cancelArmed_=false;return result;
    }
    if(!sample.focused||sample.window!=2)cancelArmed_=false;
    else if(!sample.back&&!sample.confirm&&!sample.previous)cancelArmed_=true;
    else if(sample.back&&cancelArmed_){
        // Consume only a fresh, normalized native Cancel action. Keyboard and
        // controller remapping stay owned by the game, even during text reveal.
        cancelArmed_=false;completion_=view_.kind==Presentation::Kind::Notice?
            Presentation::Navigation::Next:Presentation::Navigation::Skip;
        closing_=true;result.action=Action::Close;result.navigation=completion_;return result;
    }
    const auto input=input_.Poll(sample.focused,sample.window==2&&sample.text==3&&!sample.back,sample.confirm,sample.previous);
    if(input==Presentation::Navigation::None)return result;
    completion_=view_.kind==Presentation::Kind::Notice?Presentation::Navigation::Next:input;
    closing_=true;result.action=Action::Close;result.navigation=completion_;return result;
}
bool Encode(const char* literal,EncodedText& output) noexcept {
    if(!literal||!*literal)return false;
    constexpr char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    EncodedText encoded;
    for(std::size_t i=0;literal[i];++i){
        if(encoded.size+1>=encoded.bytes.size())return false;
        const char c=literal[i];
        if(c=='\n')encoded.bytes[encoded.size++]=3;
        else {
            const char* glyph=std::strchr(alphabet,c);if(!glyph)return false;
            encoded.bytes[encoded.size++]=static_cast<unsigned char>(0x30+(glyph-alphabet));
        }
    }
    encoded.bytes[encoded.size++]=0;output=encoded;return true;
}
}
