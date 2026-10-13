#include "../hooks/OmegaT7Text.h"
#include "../hooks/ArcanaFieldDialogCore.h"
#include <cstdio>
#include <cstring>
int main(){unsigned checks=0,failures=0;char text[2048]{};
    for(bool pt:{false,true})for(unsigned kind=0;kind<3;++kind)for(unsigned page=0;page<(kind?1:FfxHooks::OmegaT7::Text::Pages);++page){
        const bool ok=FfxHooks::OmegaT7::Text::Format(kind,page,pt,text,sizeof(text));++checks;if(!ok||std::strstr(text,"farm")||std::strstr(text,"ESC")||!std::strstr(text,pt?"Cancelar":"Cancel"))++failures;
        if(!pt){FfxHooks::Arcana::FieldDialog::EncodedText bytes;++checks;if(!FfxHooks::Arcana::FieldDialog::Encode(text,bytes)||!bytes.size||bytes.size>2048)++failures;}}
    ++checks;if(FfxHooks::OmegaT7::Text::Format(0,99,false,text,sizeof(text)))++failures;
    ++checks;if(FfxHooks::OmegaT7::Text::Format(0,0,false,text,4))++failures;
    std::printf("OmegaT7TextRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;}
