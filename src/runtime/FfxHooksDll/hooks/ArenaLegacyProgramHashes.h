#pragma once
// Jarvis-HOOK: metadata hashes only, no proprietary battle payload.
// Prior exact profile bundles; zero only formation IDs and monster X/Z before hashing.
// See docs/reverse/ARENA_ULTRA_NATIVE_2026-09-23.md for provenance and boundary.
namespace FfxHooks::ArenaMixLibrary {
struct LegacyProgramHash { unsigned scenery; unsigned camera; const char* digest; };
inline constexpr LegacyProgramHash kLegacyProgramHashes[]={
    {0, 1, "3d93c6d28e0598a9503e04099b9725704ae0f477f4765ff98f8ccc8e78d870a9"},
    {1, 1, "2d5932bfd218eb0f0e3615e86ded3ba76af0ca3420b57aeaf42e4be4ba07d4ac"},
    {2, 1, "3b0d4ca8e5c8341008ff6b2bf48c3e3fba1b3f72137603cd2a4a11e7f4cf3e0b"},
    {3, 1, "2b40a792acabfff8c952db08a348b7a9427faeb016282a3f557fc5c9cf9af62e"},
    {4, 1, "955b4cc616809d28326e7d9bc64bbe90c82208f7a3e64840a72c9e9554988bb0"},
    {5, 1, "955b4cc616809d28326e7d9bc64bbe90c82208f7a3e64840a72c9e9554988bb0"},
    {6, 1, "955b4cc616809d28326e7d9bc64bbe90c82208f7a3e64840a72c9e9554988bb0"},
    {7, 1, "205c654a1825f0f85fd330e471fd71bf69b2c2434e13e18750190631ed9d00ff"},
    {8, 1, "3d93c6d28e0598a9503e04099b9725704ae0f477f4765ff98f8ccc8e78d870a9"},
    {0, 1, "043a0f39a76b4bb695f8f496235de8e65069b91555325fa5e2358cd33b078614"},
    {1, 1, "fcf1b5282d32ca38547bc2449fcb97ccefe23eeb38484c47312f88026a828702"},
    {2, 1, "128cddc81f53e74e5ad373253519e931738803b6667f54bab5590c17e585076a"},
    {3, 1, "56d4d73f746eae6e1c9c3bc5413628a9fbf2fa90f8fdfcda69dbe26280e620da"},
    {4, 1, "e81ac742a83bf9ff7c381adbc9263498030866a5186e0511c520dcbb656eeddd"},
    {5, 1, "e81ac742a83bf9ff7c381adbc9263498030866a5186e0511c520dcbb656eeddd"},
    {6, 1, "e81ac742a83bf9ff7c381adbc9263498030866a5186e0511c520dcbb656eeddd"},
    {7, 1, "86f264028e235909f4bc304c942f22fa8b55a68bf596e20e41144e1de9a7a9a6"},
    {8, 1, "043a0f39a76b4bb695f8f496235de8e65069b91555325fa5e2358cd33b078614"},
};
}
