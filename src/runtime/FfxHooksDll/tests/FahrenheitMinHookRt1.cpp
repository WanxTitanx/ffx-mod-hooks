// Jarvis-HOOK: real x86 code and two independent MinHook providers.
// No game binary, save, injection or live process is involved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

static_assert(sizeof(void*) == 4, "This harness exercises the FFX x86 ABI");
namespace {
using Fn = int (__cdecl*)();
Fn originalA = nullptr;
Fn originalB = nullptr;
int checks = 0, failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
int __cdecl HookA() { return originalA ? originalA() + 10 : -1000; }
int __cdecl HookB() { return originalB ? originalB() + 100 : -2000; }

struct Provider {
    using NoArg = int (WINAPI*)();
    using Target = int (WINAPI*)(void*);
    using Create = int (WINAPI*)(void*, void*, void**);
    HMODULE module;
    NoArg init, apply;
    Target enable, disable, remove, queue;
    Create create;
    template<class T> T Resolve(const char* name) {
        auto address = GetProcAddress(module, name);
        if (!address) throw std::runtime_error(std::string("Missing export: ") + name);
        return reinterpret_cast<T>(address);
    }
    explicit Provider(const char* path) : module(LoadLibraryA(path)) {
        if (!module) throw std::runtime_error(std::string("LoadLibrary failed: ") + path);
        init = Resolve<NoArg>("MH_Initialize");
        apply = Resolve<NoArg>("MH_ApplyQueued");
        enable = Resolve<Target>("MH_EnableHook");
        disable = Resolve<Target>("MH_DisableHook");
        remove = Resolve<Target>("MH_RemoveHook");
        queue = Resolve<Target>("MH_QueueEnableHook");
        create = Resolve<Create>("MH_CreateHook");
    }
};
struct Fixture {
    unsigned char* page = nullptr;
    unsigned char* target = nullptr;
    explicit Fixture(bool hotpatch = false) {
        page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!page) throw std::runtime_error("VirtualAlloc failed");
        std::memset(page, 0x90, 4096);
        target = page + 32;
        if (hotpatch) {
            const unsigned char code[] = {0x33, 0xC0, 0xC3, 0x42, 0x43};
            std::memcpy(target, code, sizeof(code));
        } else {
            const unsigned char code[] = {0xB8, 7, 0, 0, 0, 0xC3};
            std::memcpy(target, code, sizeof(code));
        }
        FlushInstructionCache(GetCurrentProcess(), page, 4096);
    }
    std::array<unsigned char, 16> Bytes() const {
        std::array<unsigned char, 16> bytes{};
        std::memcpy(bytes.data(), target - 5, bytes.size());
        return bytes;
    }
    int Call() const { return reinterpret_cast<Fn>(target)(); }
    // Each scenario runs in a separate child process. Executable storage lives
    // to process exit, including when the test intentionally exposes a bug.
};
void CreateA(Provider& a, Fixture& f) {
    Check(a.create(f.target, reinterpret_cast<void*>(&HookA),
                   reinterpret_cast<void**>(&originalA)) == 0, "create A");
}
void CreateB(Provider& b, void* target) {
    Check(b.create(target, reinterpret_cast<void*>(&HookB),
                   reinterpret_cast<void**>(&originalB)) == 0, "create B");
    Check(b.enable(target) == 0, "enable B");
}
void Run(const std::string& scenario, Provider& a, Provider& b) {
    if (scenario == "shared-provider") {
        using Bind = int(WINAPI*)(HMODULE);
        auto bind = reinterpret_cast<Bind>(GetProcAddress(a.module, "MH_BindSharedProvider"));
        Check(bind != nullptr, "shared provider binding is implemented");
        if (!bind) return;
        Check(b.init() == 0, "Fahrenheit initializes the shared provider first");
        Fixture foreign, owned, queued;
        CreateB(b, foreign.target);
        Check(bind(b.module) == 0 && a.init() == 0, "native runtime borrows an initialized provider");
        Check(a.init() == 1, "repeat initialization retains the provider");
        Check(a.disable(foreign.target) != 0 && a.remove(foreign.target) != 0, "native cleanup cannot address an unowned peer hook");
        CreateA(a, owned);
        Check(a.enable(owned.target) == 0 && owned.Call() == 17, "owned shared-provider hook calls original once");
        void* ignored=nullptr;
        Check(b.create(owned.target,reinterpret_cast<void*>(&HookB),&ignored) == 3, "peer sees the same registry and cannot duplicate our hook");
        Check(b.create(queued.target,reinterpret_cast<void*>(&HookB),&ignored)==0 && b.queue(queued.target)==0, "peer has unrelated pending queue intent");
        Check(a.disable(owned.target)==0 && a.queue(owned.target)==0 && a.apply()==0, "native queued intent applies only its own targets");
        Check(queued.Call()==7, "native apply does not flush the peer queue");
        Check(foreign.Call()==107 && owned.Call()==17, "both disjoint hooks remain functional");
        auto uninitialize=a.Resolve<Provider::NoArg>("MH_Uninitialize");
        Check(uninitialize()==0 && owned.Call()==7 && foreign.Call()==107, "native shutdown leaves the borrowed provider and peer hooks alive");
        Check(b.apply()==0, "peer retains its queued transaction after native shutdown");
        return;
    }
    Check(a.init() == 0, "first provider initializes");
    Check(a.init() == 1, "same instance reports ALREADY_INITIALIZED");
    Check(b.init() == 0, "separate provider has independent initialization");
    Fixture f(scenario == "hotpatch-conflict");
    const auto vanilla = f.Bytes();
    if (scenario == "normal") {
        CreateA(a, f);
        Check(a.enable(f.target) == 0 && f.Call() == 17, "owned hook executes original once");
        Check(a.disable(f.target) == 0 && f.Bytes() == vanilla, "owned disable restores bytes");
        Check(a.queue(f.target) == 0 && a.apply() == 0 && f.Call() == 17, "owned queued re-enable");
        Check(a.remove(f.target) == 0 && f.Call() == 7, "owned remove restores behavior");
    } else if (scenario == "foreign-first") {
        CreateB(b, f.target);
        const auto foreign = f.Bytes();
        Check(a.create(f.target, reinterpret_cast<void*>(&HookA),
                       reinterpret_cast<void**>(&originalA)) != 0, "reject pre-existing foreign detour");
        Check(f.Bytes() == foreign && f.Call() == 107, "foreign-first owner remains callable");
    } else if (scenario == "create-enable" || scenario == "queued-conflict") {
        CreateA(a, f);
        CreateB(b, f.target);
        const auto foreign = f.Bytes();
        int status;
        if (scenario == "queued-conflict") {
            Check(a.queue(f.target) == 0, "queue is intent only");
            status = a.apply();
        } else status = a.enable(f.target);
        Check(status != 0, "enable rejects target changed since create");
        Check(f.Bytes() == foreign, "enable preserves intervening foreign bytes");
        if (f.Bytes() == foreign) Check(f.Call() == 107, "intervening owner remains callable");
    } else if (scenario == "remove-foreign") {
        CreateA(a, f);
        Check(a.enable(f.target) == 0, "enable A before foreign owner");
        CreateB(b, f.target);
        Check(f.Call() == 117, "independent foreign provider chains through A");
        const auto foreign = f.Bytes();
        const auto disabled = a.disable(f.target);
        const auto removed = a.remove(f.target);
        Check(disabled != 0, "disable refuses a foreign-owned entry");
        Check(removed != 0, "remove retains a still-reachable original");
        Check(f.Bytes() == foreign, "teardown leaves foreign bytes intact");
        if (disabled != 0 && removed != 0 && f.Bytes() == foreign) {
            Check(f.Call() == 117, "foreign chain survives rejected teardown");
            Check(b.disable(f.target) == 0, "foreign provider restores our entry");
            Check(a.disable(f.target) == 0, "our owner may now restore its entry");
            Check(f.Bytes() == vanilla, "restoration after foreign retirement is exact");
        }
    } else if (scenario == "hotpatch-conflict") {
        CreateA(a, f);
        Check(a.enable(f.target) == 0, "enable hotpatch A");
        Check(f.target[0] == 0xEB && f.target[-5] == 0xE9, "fixture uses both hotpatch regions");
        f.target[-1] ^= 1;
        const auto foreign = f.Bytes();
        Check(a.disable(f.target) != 0, "hotpatch ownership covers the preceding five bytes");
        Check(f.Bytes() == foreign, "foreign hotpatch bytes remain unchanged");
    } else if (scenario == "trampoline-conflict") {
        CreateA(a, f);
        Check(a.enable(f.target) == 0, "enable A before trampoline borrower");
        CreateB(b, reinterpret_cast<void*>(originalA));
        const auto owned = f.Bytes();
        Check(a.remove(f.target) != 0, "remove cannot free a foreign-hooked trampoline");
        Check(f.Bytes() == owned, "trampoline conflict keeps the parent entry intact");
    } else throw std::runtime_error("Unknown scenario");
}
}
int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: harness scenario provider-A.dll provider-B.dll\n"); return 2; }
    try {
        Provider a(argv[2]), b(argv[3]);
        Run(argv[1], a, b);
        std::printf("%s: %d checks, %d failures\n", argv[1], checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "HARNESS ERROR: %s\n", e.what());
        return 2;
    }
}
