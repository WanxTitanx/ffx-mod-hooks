#include "../hooks/ArenaMixLibrary.h"
#include "../hooks/ArenaBattleProgram.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
using namespace FfxHooks;
namespace L = ArenaMixLibrary;
namespace fs = std::filesystem;
static int checks = 0, failures = 0;
static void Check(bool ok, const char *text)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", text);
    }
}
static std::vector<unsigned char> Read(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static void Write(const fs::path &p, const std::string &s)
{
    std::ofstream f(p, std::ios::binary);
    f << s;
}
int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    std::string error;
    Check(ArenaBattleProgram::Load(argv[2],&error),"verified normal profiles load for authoring");
    if(!ArenaBattleProgram::Ready())return 2;
    Check(ArenaBattleProgram::Encounters().size()==863,"the complete installed battle catalog is loaded");
    unsigned supportedLineups=0;
    for(const auto& encounter:ArenaBattleProgram::Encounters()){
        CustomMixUltra::SelectionInput current{};current.musicTrack=49;CustomMixUltra::SelectionInput selected{};
        if(ArenaBattleProgram::UseEncounter(encounter,current,&selected)){
            ++supportedLineups;
            Check(CustomMixUltra::BuildSelection(selected).result==CustomMixUltra::SelectionResult::Ready && selected.musicTrack==49,
                  "catalog lineup imports remain bounded and preserve the chosen music");
        }
    }
    Check(supportedLineups>500,"ordinary battle lineups are authorable rather than just displayed");
    const auto original = Read(argv[1]);
    L::Preset p;
    p.name = "My Mix";
    p.requiredSlots = 5;
    p.selection.activationCount = 3;
    p.selection.activations[0] = CustomMixUltra::MonsterChoice::Magus;
    p.selection.activations[1] = CustomMixUltra::MonsterChoice::Yojimbo;
    p.selection.activations[2] = CustomMixUltra::MonsterChoice::Anima;
    p.selection.positions = ArenaPositions::Generate(5);
    p.selection.scenery = ArenaScenery::Choice::Bikanel;
    p.selection.camera = ArenaScenery::Camera::Tactical;
    ArenaBattleProgram::Geometry geometry{};
    Check(ArenaBattleProgram::Describe(p.selection.scenery,p.selection.camera,&geometry),"authoring uses the selected normal arena geometry");
    const auto slots=geometry.slotsOffset,points=geometry.positionsOffset;
    L::Preset parsed;
    auto json = L::Serialize(p);
    Check(L::Parse(json, &parsed, &error) && parsed.requiredSlots == 5 &&
              parsed.selection.positions.count == 5 && parsed.selection.scenery == p.selection.scenery && parsed.selection.camera==p.selection.camera,
          "JSON roundtrip preserves named mix, selected scenery and expanded positions");
    Check(L::Serialize(parsed) == json, "canonical preset serialization is stable");
    auto ultra=parsed;
    ultra.requiredSlots=0;ultra.selection.activationCount=3;
    ultra.selection.activations[0]=static_cast<CustomMixUltra::MonsterChoice>(0x109u);
    ultra.selection.activations[1]=static_cast<CustomMixUltra::MonsterChoice>(0x214u);
    ultra.selection.activations[2]=CustomMixUltra::MonsterChoice::Valefor;
    ultra.selection.positions=ArenaScenery::Generate(ultra.selection.scenery,3);
    ultra.selection.musicTrack=49;
    Check(L::Parse(L::Serialize(ultra),&parsed,&error) && parsed.selection.musicTrack==49 &&
          parsed.selection.activations==ultra.selection.activations,"Ultra v3 persists catalog symbols and soundtrack");
    std::vector<unsigned char> ultraBinary;
    Check(L::BuildBinary({},ultra,&ultraBinary,&error),"Ultra v3 exports a native catalog formation");
    auto roundtripUltra=ultra;
    Check(L::ImportBinary({},ultraBinary,&roundtripUltra,&error) &&
          roundtripUltra.selection.activations==ultra.selection.activations && roundtripUltra.selection.musicTrack==49,
          "native import retains catalog choices and the JSON soundtrack");
    if(argc>4 && argv[4][0]) {
        const auto previous=Read(argv[4]);
        auto u32=[](const std::vector<unsigned char>& data,size_t offset){uint32_t n=0;std::memcpy(&n,data.data()+offset,4);return n;};
        Check(previous.size()>32,"previous camera-profile fixture is available");
        if(previous.size()>32) {
            size_t at=16u+u32(previous,12);bool found=false;
            for(unsigned record=0;record<18 && at+48u<=previous.size();++record) {
                const auto scene=u32(previous,at),camera=u32(previous,at+4),size=u32(previous,at+8);
                if(at+48u+size>previous.size())break;
                if(scene==static_cast<unsigned>(p.selection.scenery)&&camera==1u) {
                    std::vector<unsigned char> older(previous.begin()+at+48u,previous.begin()+at+48u+size);
                    const size_t oldSlots=u32(older,12)+12u,area=u32(older,16),oldPoints=area+u32(older,area+0x20u);
                    const auto expanded=CustomMixUltra::BuildSelection(p.selection).expanded;
                    for(unsigned i=0;i<8;++i){const uint16_t id=i<expanded.monsterCount?expanded.monsterIds[i]:0xffffu;std::memcpy(older.data()+oldSlots+i*2u,&id,2);}
                    for(unsigned i=0;i<expanded.monsterCount;++i){float x,z;ArenaBattleProgram::ToWorld(geometry,p.selection.positions.points[i].x,p.selection.positions.points[i].z,&x,&z);std::memcpy(older.data()+oldPoints+i*16u,&x,4);std::memcpy(older.data()+oldPoints+i*16u+8u,&z,4);}
                    auto imported=p;
                    Check(L::ImportBinary(original,older,&imported,&error),"previous Tactical .bin imports after a camera framing update");
                    Check(imported.selection.activations==p.selection.activations,"older camera import preserves the roster");
                    older.back()^=1;
                    Check(!L::ImportBinary(original,older,&imported,&error),"legacy compatibility rejects unrelated native edits");
                    found=true;break;
                }
                at+=48u+size;
            }
            Check(found,"previous Tactical profile matched the selected arena");
        }
    }
    const std::string oldV1="{\"schema\":\"ffx-hooks.arena-mix\",\"version\":1,\"name\":\"Old Mix\",\"carrier\":\"dome02_00\",\"required_slots\":3,\"choices\":[\"valefor\",\"ifrit\",\"ixion\"],\"position_mode\":\"native\",\"positions\":[]}";
    Check(L::Parse(oldV1,&parsed,&error) && parsed.selection.scenery==ArenaScenery::Choice::Carrier,
          "older v1 presets without arena metadata retain their original carrier scene");
    Check(parsed.classicTemplate&&parsed.selection.camera==ArenaScenery::Camera::Arena,"old v1 imports use normal rules and arena camera by default");
    auto invalidArena = json;
    invalidArena.replace(invalidArena.find("\"bikanel\""),9,"\"unreviewed\"");
    Check(!L::Parse(invalidArena,&parsed,&error),"unknown arena names cannot authorize a scenery writer");
    auto mismatch = json;
    mismatch.replace(mismatch.find("1049"),4,"9999");
    Check(!L::Parse(mismatch,&parsed,&error),"arbitrary or mismatched numeric battlefield IDs are rejected");
    for (const auto &bad : std::vector<std::string>{
             "{}", json + "garbage", std::string(32769, ' '),
             "{\"schema\":\"ffx-hooks.arena-mix\",\"schema\":\"arena-layout-export-v1\"}",
             "{\"schema\":\"unknown\"}"})
        Check(!L::Parse(bad, &parsed, &error),
              "malformed/duplicate/oversized/unknown schema is rejected");
    auto future = json;
    auto close = future.rfind('}');
    future.insert(close, ",\"extension\":{\"future\":[true,null,123]}\n");
    Check(L::Parse(future, &parsed, &error),
          "unknown well-formed fields permit additive format evolution");
    auto newer = json;
    newer.replace(newer.find("\"version\": 3"), 12, "\"version\": 99");
    Check(!L::Parse(newer, &parsed, &error),
          "unknown major versions are rejected instead of misread");
    const std::string legacy = "{\"schema\":\"arena-layout-export-v1\",\"picks\":[\"valefor\","
                               "\"ifrit\",\"shiva\"],\"scenario\":\"cavern\",\"monsters\":[[0,0]]}";
    Check(L::Parse(legacy, &parsed, &error) && parsed.legacy &&
              parsed.selection.positions.automatic,
          "old exports become marked converted drafts with regenerated positions");
    std::vector<unsigned char> binary;
    Check(L::BuildBinary(original, p, &binary, &error) && binary.size()!=original.size()&&binary.size()<65536u,
          "native editor export uses a complete normal arena, not the boss template");
    parsed=p;
    Check(L::ImportBinary(original, binary, &parsed, &error) &&
              parsed.selection.positions.count == 5,
          "native binary is readable back into supported mix slots");
    uint16_t yojimbo = 0, anima = 0;
    std::memcpy(&yojimbo, binary.data() + slots+6u, 2);
    std::memcpy(&anima, binary.data() + slots+8u, 2);
    Check(yojimbo == 0x1154u && anima == 0x1153u,
          "editor-facing binary preserves corrected Yojimbo/Anima identities");
    auto edited = binary;
    float x = 0;
    std::memcpy(&x, edited.data() + points, 4);
    x += 4;
    std::memcpy(edited.data() + points, &x, 4);
    float worldZ=0,relativeX=0,relativeZ=0;
    std::memcpy(&worldZ,edited.data()+points+8u,4);
    ArenaBattleProgram::ToRelative(geometry,x,worldZ,&relativeX,&relativeZ);
    parsed = p;
    Check(L::ImportBinary(original, edited, &parsed, &error) &&
              std::fabs(parsed.selection.positions.points[0].x-relativeX)<0.001f && parsed.selection.scenery==p.selection.scenery && parsed.selection.camera==p.selection.camera,
          "an editor X/Z edit retains monster identity and JSON scenery metadata");
    for (size_t offset : std::vector<size_t>{0, geometry.areaOffset, points+4u, points+12u, 0x30u})
    {
        auto bad = binary;
        bad[offset] ^= 1;
        Check(!L::ImportBinary(original, bad, &parsed, &error),
              "structure/height/rotation/camera edits cannot escape the supported import boundary");
    }
    auto wrongTemplate = original;
    wrongTemplate[20] ^= 1;
    Check(!L::BuildBinary(wrongTemplate, p, &edited, &error),
          "an unknown template cannot be exported as a supported arena");
    auto oldBinary=original;
    const auto oldSlots=CustomMixUltra::BuildSelection(p.selection).expanded;
    for(unsigned i=0;i<8;++i){const uint16_t idOld=i<oldSlots.monsterCount?oldSlots.monsterIds[i]:0xFFFFu;std::memcpy(oldBinary.data()+0x3F78u+i*2u,&idOld,2);}
    for(unsigned i=0;i<5;++i){std::memcpy(oldBinary.data()+0x41B8u+i*16u,&p.selection.positions.points[i].x,4);std::memcpy(oldBinary.data()+0x41C0u+i*16u,&p.selection.positions.points[i].z,4);}
    parsed=p;parsed.classicTemplate=true;
    Check(L::ImportBinary(original,oldBinary,&parsed,&error)&&parsed.selection.positions.count==5,
          "old v1 editor binaries import only roster/layout, never their boss program");
    const fs::path root =
        fs::temp_directory_path() / ("JarvisArenaLibrary-" + std::to_string(GetCurrentProcessId()) +
                                     "-" + std::to_string(GetTickCount64()));
    const fs::path old = root / "legacy";
    fs::create_directories(old);
    {
        std::ofstream f(root / "_template.bin", std::ios::binary);
        f.write(reinterpret_cast<const char *>(original.data()), original.size());
    }
    Write(old / "formation_001.json", legacy);
    std::string id;
    Check(L::Save(root.string(), p, &id, &error), "export creates a new JSON/native bundle");
    const auto jsonPath = root / (id + ".json");
    const std::string carrierName=ArenaScenery::ExportCarrier(p.selection.scenery);
    const auto binPath = root / id / carrierName / (carrierName+".bin");
    const auto savedBinary = Read(binPath);
    const auto savedTextBytes = Read(jsonPath);
    std::string savedText(savedTextBytes.begin(), savedTextBytes.end());
    Check(savedBinary == binary &&
              savedText.find(id+"/"+carrierName+"/"+carrierName+".bin") != std::string::npos,
          "JSON editor path resolves to the native file actually exported");
    savedText.insert(savedText.rfind('}'), ",\"future_extension\":{\"keep\":42}\n");
    Write(jsonPath, savedText);
    auto entries = L::Scan(root.string(), old.string());
    Check(entries.size() == 5,
          "library discovers three preloaded presets, a saved mix and an old formation");
    L::Entry selected;
    for (const auto &e : entries)
        if (e.id == id)
            selected = e;
    Check(L::Load(root.string(), old.string(), selected, &parsed, &error),
          "library loads the saved bundle for review");
    auto external = Read(binPath);
    float externalX = 0;
    std::memcpy(&externalX, external.data() + points, 4);
    externalX += 4;
    std::memcpy(external.data() + points, &externalX, 4);
    ArenaBattleProgram::ToRelative(geometry,externalX,worldZ,&relativeX,&relativeZ);
    {
        std::ofstream out(binPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(external.data()), external.size());
    }
    Check(L::Load(root.string(), old.string(), selected, &parsed, &error) &&
              parsed.selection.positions.points[0].x == p.selection.positions.points[0].x,
          "loading JSON settings does not silently take edits from a companion binary");
    Check(L::Load(root.string(), old.string(), selected, &parsed, &error, true) &&
              std::fabs(parsed.selection.positions.points[0].x-relativeX)<0.001f,
          "the explicit editor import uses the edited native battle instead");
    {
        std::ofstream out(binPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(savedBinary.data()), savedBinary.size());
    }
    Check(L::Rename(root.string(), id, "Renamed Battle", &error), "saved battle can be renamed");
    auto renamed = Read(jsonPath);
    std::string renamedText(renamed.begin(), renamed.end());
    Check(renamedText.find("future_extension") != std::string::npos && Read(binPath) == savedBinary,
          "rename preserves future metadata, binary bytes and stable identifier");
    Check(L::Parse(renamedText, &parsed, &error) && parsed.name == "Renamed Battle",
          "renamed title reads back");
    Check(!L::Rename(root.string(), "../escape", "Bad", &error) &&
              !L::Rename(root.string(), id, "", &error),
          "rename rejects traversal identifiers and empty names");
    Check(Read(jsonPath) == renamed, "rejected rename preserves the saved JSON exactly");
    std::string second;
    Check(L::Save(root.string(), p, &second, &error) && second != id && Read(jsonPath) == renamed,
          "a second export cannot overwrite an existing battle");
    Write(root / "broken.json", "{broken");
    entries = L::Scan(root.string(), old.string());
    bool invalid = false;
    for (const auto &e : entries)
        if (e.id == "broken")
            invalid = !e.valid;
    Check(invalid,
          "invalid imports remain visible as invalid entries instead of crashing the menu");
    if (argc > 3)
    {
        const fs::path evidence = argv[3];
        fs::create_directories(evidence / id / carrierName);
        fs::copy_file(binPath, evidence / id / carrierName / (carrierName+".bin"),
                      fs::copy_options::overwrite_existing);
        Write(evidence / (id + ".json"), renamedText);
        std::printf("EDITOR_EVIDENCE=%s\n",
                    (evidence / id / carrierName / (carrierName+".bin")).string().c_str());
    }
    fs::remove_all(root); // This harness owns the fresh temporary tree above.
    std::printf("ArenaMixLibraryRt1: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
