#pragma once
// Jarvis-HOOK: identities of original, user-owned font resources; no font pixels are shipped.
namespace FfxHooks::UiNativeFont::Evidence {
struct Asset {const char* path;const char* sha256;};
inline constexpr Asset Assets[]={
    {"ffx_ps2/ffx/master/uspc/menu/base.ftc","309ba121c26b693a8bea16f10891c997a0363a72307bc7af80f25d11cf21f8c2"},
    {"ffx_ps2/ffx/master/jppc/ffx_encoding/ffxsjistbl_us.bin","9f96e3b904c0ed332bb6859ec74f36b5a440c9c10c00fad24099afd42bd39af8"},
    {"ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_0.dds.phyre","d4ccdc03c461238b664e960a2efca12888eddc210ec67479151d7957e336d926"},
    {"ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_1.dds.phyre","15ad660bda441c061832e542e69b57d31e3a529f2f4ce7d177988d7b225246b6"},
    {"ffx_ps2/ffx/master/new_jppc/menu/base.ftc","0bc93b42cc38845cc338ac45d02ac7ea4f448818ca53fd51365982ead732ae85"},
    {"ffx_ps2/ffx/master/jppc/ffx_encoding/ffxsjistbl_jp.bin","40a4369a11fdf0584797522f7a635933418f4b20908b2d7b9b35a8a2ae12c2d5"},
    {"ffx_data/gamedata/ps3data/menu/base_ftc/d3d11/font_0_0.dds.phyre","dfdcd8416f17a458214779c8948729084314ef99dd825e2572ef5021aa0c2a59"},
    {"ffx_data/gamedata/ps3data/menu/base_ftc/d3d11/font_0_1.dds.phyre","03198f7d4215fba90f4b483d8a4c91c72eaf6bbef0ee1a2dee41465546afc6a3"},
    {"ffx_ps2/ffx/master/new_krpc/menu/base.ftc","58fed1461ae15c30233520527d9aeb42512198b35d900e67ab19ee41e7ff6225"},
    {"ffx_ps2/ffx/master/jppc/ffx_encoding/ffxsjistbl_kr.bin","cb3c63318e4791a15d64eca1ea197fee929dbc244159349a63ec8948fe363235"},
    {"ffx_data/gamedata/ps3data/menu_kr/base_ftc/d3d11/font_0_0.dds.phyre","f3a14943143e288f52e110482c2ebcc2a62dc235260ef5fc2bd06dcc61a5333a"},
    {"ffx_data/gamedata/ps3data/menu_kr/base_ftc/d3d11/font_0_1.dds.phyre","eaa95b91807cc15ad429bbe56ce395a1cfbd574cf9c92dada2d985f66af0ac7e"},
    {"ffx_ps2/ffx/master/new_chpc/menu/base.ftc","887644ed0178284c5e1673cf93e280956be750115479a2b861c595200dfda6fc"},
    {"ffx_ps2/ffx/master/jppc/ffx_encoding/ffxsjistbl_ch.bin","33fc00ffa5bd80b5a8d08b614c0f1527e4095db160e187b1e945bbe6ebfa4488"},
    {"ffx_data/gamedata/ps3data/menu_ch/base_ftc/d3d11/font_0_0.dds.phyre","6a36f2a408a3a444d0094533596226553ad4044f36400d41b1be3eebdab06aa1"},
    {"ffx_data/gamedata/ps3data/menu_ch/base_ftc/d3d11/font_0_1.dds.phyre","f1eb51cb93b31173e717dc920309dee7d89c204feb69d5980892200afa95a31d"},
};
}
