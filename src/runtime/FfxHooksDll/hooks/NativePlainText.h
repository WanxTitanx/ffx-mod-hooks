#pragma once

// Jarvis-HOOK: extra Scan labels share the native glyph body, but must not
// multiply each glyph into the eight-neighbor outline batch as well.
namespace FfxHooks::NativeText {
inline thread_local bool plainTextActive=false;
inline bool PlainTextActive() noexcept {return plainTextActive;}
#ifdef _MSC_VER
using DrawFn=int(__cdecl*)(unsigned,const unsigned char*,float,float,unsigned,float,float);
#else
using DrawFn=int(*)(unsigned,const unsigned char*,float,float,unsigned,float,float);
#endif
inline int DrawPlain(DrawFn draw,unsigned font,const unsigned char* text,float x,float y,unsigned style,float sx,float sy){
    const bool previous=plainTextActive;plainTextActive=true;
#ifdef _MSC_VER
    // Native callers may unwind with SEH; a C++ destructor under /EHsc would
    // leave suppression latched after a caught native drawing exception.
    __try {return draw(font,text,x,y,style,sx,sy);}
    __finally {plainTextActive=previous;}
#else
    try{const int result=draw(font,text,x,y,style,sx,sy);plainTextActive=previous;return result;}
    catch(...){plainTextActive=previous;throw;}
#endif
}
}
