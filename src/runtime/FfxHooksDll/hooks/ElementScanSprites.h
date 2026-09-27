#pragma once
namespace FfxHooks::ElementScan {
struct SpriteUv {float u0,v0,u1,v1;};
// 78CE... native strip: (125,2,225,35.7), UV (230,972,477,1012)/1024.
// Design-space cropping keeps the original sphere shading, alpha and border.
inline constexpr SpriteUv StripCrop(float x,float y,float width,float height){
    return {(230.f+(x-125.f)*247.f/225.f)/1024.f,
            (972.f+(y-2.f)*40.f/35.7f)/1024.f,
            (230.f+(x+width-125.f)*247.f/225.f)/1024.f,
            (972.f+(y+height-2.f)*40.f/35.7f)/1024.f};
}
inline constexpr SpriteUv SilverSphere=StripCrop(316.f,4.f,32.7f,32.7f);
inline constexpr SpriteUv Connector=StripCrop(290.f,4.f,16.f,32.7f);
inline constexpr SpriteUv InactiveSphere={557.f/1024.f,974.f/1024.f,594.f/1024.f,1010.f/1024.f};
}
