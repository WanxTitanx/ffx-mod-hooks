#include "ArenaMixLibrary.h"
#include "ArenaBattleProgram.h"
#include "ArenaMixPolicy.h"
#include "ArenaLegacyProgramHashes.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace FfxHooks::ArenaMixLibrary
{
namespace
{
namespace fs = std::filesystem;
constexpr size_t kMaxJson = 32768u;
constexpr size_t kSlots = 0x3F78u, kPositions = 0x41B8u;
struct Node
{
    enum Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    } type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Node> array;
    std::map<std::string, Node> object;
    size_t begin = 0, end = 0;
};
class Reader
{
    const std::string &text;
    size_t at = 0;
    unsigned nodes = 0;
    void Space()
    {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n'))
            ++at;
    }
    bool Take(char c)
    {
        Space();
        if (at >= text.size() || text[at] != c)
            return false;
        ++at;
        return true;
    }
    bool Hex(unsigned *out)
    {
        *out = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (at >= text.size())
                return false;
            char c = text[at++];
            unsigned n = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 99;
            if (n > 15)
                return false;
            *out = *out * 16 + n;
        }
        return true;
    }
    static void Utf8(std::string &s, unsigned c)
    {
        if (c < 0x80)
            s += char(c);
        else if (c < 0x800)
        {
            s += char(0xC0 | (c >> 6));
            s += char(0x80 | (c & 63));
        }
        else if (c < 0x10000)
        {
            s += char(0xE0 | (c >> 12));
            s += char(0x80 | ((c >> 6) & 63));
            s += char(0x80 | (c & 63));
        }
        else
        {
            s += char(0xF0 | (c >> 18));
            s += char(0x80 | ((c >> 12) & 63));
            s += char(0x80 | ((c >> 6) & 63));
            s += char(0x80 | (c & 63));
        }
    }
    bool String(std::string &s)
    {
        if (!Take('"'))
            return false;
        while (at < text.size())
        {
            unsigned char c = static_cast<unsigned char>(text[at++]);
            if (c == '"')
                return true;
            if (c < 32)
                return false;
            if (c == '\\')
            {
                if (at >= text.size())
                    return false;
                char e = text[at++];
                if (e == '"' || e == '\\' || e == '/')
                    s += e;
                else if (e == 'b')
                    s += '\b';
                else if (e == 'f')
                    s += '\f';
                else if (e == 'n')
                    s += '\n';
                else if (e == 'r')
                    s += '\r';
                else if (e == 't')
                    s += '\t';
                else if (e == 'u')
                {
                    unsigned u = 0;
                    if (!Hex(&u))
                        return false;
                    if (u >= 0xD800 && u <= 0xDBFF)
                    {
                        if (at + 2 > text.size() || text[at++] != '\\' || text[at++] != 'u')
                            return false;
                        unsigned low = 0;
                        if (!Hex(&low) || low < 0xDC00 || low > 0xDFFF)
                            return false;
                        u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (u >= 0xDC00 && u <= 0xDFFF)
                        return false;
                    Utf8(s, u);
                }
                else
                    return false;
            }
            else
                s += char(c);
            if (s.size() > 4096)
                return false;
        }
        return false;
    }
    bool Value(Node &n, unsigned depth)
    {
        Space();
        if (depth > 8 || ++nodes > 2048 || at >= text.size())
            return false;
        n.begin = at;
        if (text[at] == '{')
        {
            n.type = Node::Object;
            ++at;
            Space();
            if (Take('}'))
            {
                n.end = at;
                return true;
            }
            do
            {
                std::string key;
                if (!String(key) || key.size() > 96 || !Take(':'))
                    return false;
                Node value;
                if (!Value(value, depth + 1) || !n.object.emplace(key, std::move(value)).second)
                    return false;
                Space();
                if (Take('}'))
                {
                    n.end = at;
                    return true;
                }
            } while (Take(','));
            return false;
        }
        if (text[at] == '[')
        {
            n.type = Node::Array;
            ++at;
            if (Take(']'))
            {
                n.end = at;
                return true;
            }
            do
            {
                Node value;
                if (!Value(value, depth + 1))
                    return false;
                n.array.push_back(std::move(value));
                if (Take(']'))
                {
                    n.end = at;
                    return true;
                }
            } while (Take(','));
            return false;
        }
        if (text[at] == '"')
        {
            n.type = Node::String;
            if (!String(n.string))
                return false;
        }
        else if (text.compare(at, 4, "true") == 0)
        {
            n.type = Node::Bool;
            n.boolean = true;
            at += 4;
        }
        else if (text.compare(at, 5, "false") == 0)
        {
            n.type = Node::Bool;
            at += 5;
        }
        else if (text.compare(at, 4, "null") == 0)
        {
            at += 4;
        }
        else
        {
            n.type = Node::Number;
            const size_t start = at;
            if (text[at] == '-')
                ++at;
            if (at >= text.size())
                return false;
            if (text[at] == '0')
                ++at;
            else
            {
                if (text[at] < '1' || text[at] > '9')
                    return false;
                while (at < text.size() && text[at] >= '0' && text[at] <= '9')
                    ++at;
            }
            if (at < text.size() && text[at] == '.')
            {
                ++at;
                size_t first = at;
                while (at < text.size() && text[at] >= '0' && text[at] <= '9')
                    ++at;
                if (at == first)
                    return false;
            }
            if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
            {
                ++at;
                if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                    ++at;
                size_t first = at;
                while (at < text.size() && text[at] >= '0' && text[at] <= '9')
                    ++at;
                if (at == first)
                    return false;
            }
            if (at - start > 64)
                return false;
            std::istringstream number(text.substr(start, at - start));
            number.imbue(std::locale::classic());
            number >> n.number;
            if (number.fail() || !std::isfinite(n.number))
                return false;
        }
        n.end = at;
        return true;
    }

  public:
    explicit Reader(const std::string &input) : text(input)
    {
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            at = 3;
    }
    bool Read(Node &n)
    {
        if (text.size() > kMaxJson || !Value(n, 0))
            return false;
        Space();
        return at == text.size() && n.type == Node::Object;
    }
};
const Node *Get(const Node &n, const char *key, Node::Type type)
{
    auto i = n.object.find(key);
    return i != n.object.end() && i->second.type == type ? &i->second : nullptr;
}
bool Fail(std::string *e, const char *text)
{
    if (e)
        *e = text;
    return false;
}
std::string Quote(const std::string &text)
{
    std::string result = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            result += '\\';
            result += char(c);
        }
        else if (c < 32)
        {
            char hex[8] = {};
            std::snprintf(hex, sizeof(hex), "\\u%04X", c);
            result += hex;
        }
        else
            result += char(c);
    }
    return result + '"';
}
bool ValidPreset(const Preset &p)
{
    const auto expanded = CustomMixUltra::BuildSelection(p.selection);
    return ValidName(p.name) && ArenaScenery::Get(p.selection.scenery) && ArenaScenery::ValidCamera(p.selection.camera) &&
           expanded.result == CustomMixUltra::SelectionResult::Ready &&
           (p.requiredSlots == 0 || p.requiredSlots == 3 || p.requiredSlots == 4 ||
            p.requiredSlots == 5) &&
           (p.requiredSlots == 0 || p.requiredSlots == expanded.expanded.monsterCount) &&
           ArenaPositions::Validate(p.selection.positions, expanded.expanded.monsterCount) ==
               ArenaPositions::Issue::None;
}
bool Decode(const Node &root, Preset *p, std::string *error)
{
    const auto *schema = Get(root, "schema", Node::String);
    if (!schema)
        return Fail(error, "Missing schema");
    const bool legacy = schema->string == "arena-layout-export-v1";
    if (!legacy && schema->string != "ffx-hooks.arena-mix")
        return Fail(error, "Unsupported preset schema");
    Preset result{};
    result.legacy = legacy;
    if (legacy)
        result.name = "Legacy Mix";
    else
    {
        const auto *version = Get(root, "version", Node::Number);
        const auto *name = Get(root, "name", Node::String);
        const auto *mode = Get(root, "required_slots", Node::Number);
        const auto *carrier = Get(root, "carrier", Node::String);
        if (!version || (version->number != 1 && version->number != 2 && version->number != 3) || !name || !mode || !carrier)
            return Fail(error, "Unsupported version or arena");
        result.classicTemplate=version->number==1;
        if (mode->number != 0 && mode->number != 3 && mode->number != 4 && mode->number != 5)
            return Fail(error, "Invalid mix size");
        result.name = name->string;
        result.requiredSlots = static_cast<uint8_t>(mode->number);
        // Additive v1 metadata: older files preserve their original carrier scene.
        const auto arena = root.object.find("arena");
        if (arena != root.object.end() && (arena->second.type != Node::String ||
            !ArenaScenery::Parse(arena->second.string.c_str(), &result.selection.scenery)))
            return Fail(error, "Unknown arena; choose one of the supported battlefields");
        const auto battlefield = root.object.find("battlefield_id");
        if (battlefield != root.object.end() &&
            (battlefield->second.type != Node::Number ||
             battlefield->second.number != ArenaScenery::Get(result.selection.scenery)->battlefieldId))
            return Fail(error, "Arena and battlefield ID do not match");
        const auto camera=root.object.find("camera_mode");
        if(camera!=root.object.end()){
            if(camera->second.type!=Node::String || (camera->second.string!="arena"&&camera->second.string!="tactical"))
                return Fail(error,"Unknown camera mode");
            result.selection.camera=camera->second.string=="tactical"?ArenaScenery::Camera::Tactical:ArenaScenery::Camera::Arena;
        }
        if(result.classicTemplate){
            if(carrier->string!="dome02_00")return Fail(error,"Unsupported v1 carrier");
        }else{
            const auto* space=Get(root,"coordinate_space",Node::String);
            const auto* program=Get(root,"battle_program",Node::String);
            const auto* source=Get(root,"program_source",Node::String);
            if(!space||space->string!="party-relative"||!program||program->string!="normal-v1"||
                !source||source->string!=ArenaScenery::ProgramSource(result.selection.scenery)||
                carrier->string!=ArenaScenery::ExportCarrier(result.selection.scenery))return Fail(error,"Unsupported normal battle format");
        }
    }
    const auto *choices = Get(root, legacy ? "picks" : "choices", Node::Array);
    if (!choices || choices->array.empty() || choices->array.size() > 8)
        return Fail(error, "Invalid boss selection");
    for (const auto &item : choices->array)
    {
        if (item.type != Node::String)
            return Fail(error, "Invalid boss key");
        const auto* entry = ArenaMonsters::FindKey(item.string.c_str());
        if (!entry || !entry->count) return Fail(error, "Unsupported monster choice");
        result.selection.activations[result.selection.activationCount++] = entry->choice;
    }
    if (const auto* music=Get(root,"music_track",Node::Number)) {
        if(music->number < 0 || music->number > 181 || std::floor(music->number)!=music->number ||
           !ArenaSoundtrack::Get(static_cast<std::uint16_t>(music->number)))
            return Fail(error,"Unsupported soundtrack");
        result.selection.musicTrack=static_cast<std::uint16_t>(music->number);
    } else if(root.object.count("music_track")) return Fail(error,"Invalid soundtrack type");
    for(unsigned i=0;i<result.selection.activationCount;++i)
        if(static_cast<unsigned>(result.selection.activations[i])>=8u) result.requiredSlots=0;
    const auto expanded = CustomMixUltra::BuildSelection(result.selection);
    if (expanded.result != CustomMixUltra::SelectionResult::Ready)
        return Fail(error, "Too many monster positions");
    if (legacy)
    {
        result.requiredSlots =
            expanded.expanded.monsterCount >= 3 && expanded.expanded.monsterCount <= 5
                ? expanded.expanded.monsterCount
                : 0;
        result.selection.positions = ArenaPositions::Generate(expanded.expanded.monsterCount);
    }
    else
    {
        const auto *mode = Get(root, "position_mode", Node::String);
        const auto *points = Get(root, "positions", Node::Array);
        if (!mode || !points)
            return Fail(error, "Missing positions");
        if (mode->string == "native")
        {
            if (!points->array.empty())
                return Fail(error, "Native mode must have no overrides");
        }
        else
        {
            if (mode->string != "auto" && mode->string != "manual")
                return Fail(error, "Unknown position mode");
            if (points->array.size() != expanded.expanded.monsterCount)
                return Fail(error, "Position count does not match bosses");
            auto &layout = result.selection.positions;
            layout.enabled = true;
            layout.automatic = mode->string == "auto";
            layout.count = expanded.expanded.monsterCount;
            for (size_t i = 0; i < points->array.size(); ++i)
            {
                const auto &point = points->array[i];
                if (point.type != Node::Array || point.array.size() != 2 ||
                    point.array[0].type != Node::Number || point.array[1].type != Node::Number)
                    return Fail(error, "Invalid position pair");
                layout.points[i] = {static_cast<float>(point.array[0].number),
                                    static_cast<float>(point.array[1].number)};
            }
        }
    }
    if (!ValidPreset(result))
        return Fail(error, "Invalid name, count or position limits");
    *p = std::move(result);
    return true;
}
bool SafeId(const std::string &id)
{
    if (id.empty() || id.size() > 48)
        return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    return true;
}
bool Regular(const fs::path &p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           !(a & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
}
bool Directory(const fs::path &p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
           !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool ReadBytes(const fs::path &path, std::vector<unsigned char> *bytes, size_t limit)
{
    if (!Regular(path))
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    bytes->clear();
    std::array<char, 4096> block{};
    while (in)
    {
        in.read(block.data(), block.size());
        const auto n = in.gcount();
        if (n < 0 || bytes->size() + static_cast<size_t>(n) > limit)
            return false;
        bytes->insert(bytes->end(), block.data(), block.data() + n);
    }
    return in.eof();
}
bool ReadText(const fs::path &path, std::string *text)
{
    std::vector<unsigned char> b;
    if (!ReadBytes(path, &b, kMaxJson))
        return false;
    text->assign(b.begin(), b.end());
    return true;
}
bool KnownLegacyProgram(const std::vector<unsigned char>& bytes,const Preset& preset,
                        size_t* slots,size_t* points)
{
    if(bytes.size()<0x70u || bytes.size()>65535u)return false;
    const auto word=[&](size_t at){uint32_t value=0;std::memcpy(&value,bytes.data()+at,4);return value;};
    if(word(0)!=8u || word(32)!=bytes.size())return false;
    const size_t formation=word(12),area=word(16);
    if(formation>bytes.size()-28u || area>bytes.size()-0x70u || formation+28u>area)return false;
    const size_t relative=word(area+0x20u);
    if(relative<0x70u || relative>bytes.size()-area || bytes.size()-area-relative<128u)return false;
    const size_t actorSlots=formation+12u,actorPoints=area+relative;
    auto canonical=bytes;
    std::memset(canonical.data()+actorSlots,0,16u);
    for(unsigned i=0;i<8;++i){std::memset(canonical.data()+actorPoints+i*16u,0,4);std::memset(canonical.data()+actorPoints+i*16u+8u,0,4);}
    BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char digest[32]={};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    const bool ok=BCryptHash(algorithm,nullptr,0,canonical.data(),static_cast<ULONG>(canonical.size()),digest,32)>=0;
    BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)return false;
    char hex[65]={};const char* alphabet="0123456789abcdef";
    for(unsigned i=0;i<32;++i){hex[i*2]=alphabet[digest[i]>>4];hex[i*2+1]=alphabet[digest[i]&15];}
    for(const auto& known:kLegacyProgramHashes)
        if(known.scenery==static_cast<unsigned>(preset.selection.scenery) &&
           known.camera==static_cast<unsigned>(preset.selection.camera) && std::strcmp(known.digest,hex)==0){
            *slots=actorSlots;*points=actorPoints;return true;
        }
    return false;
}

bool ExactTemplate(const std::vector<unsigned char> &bytes)
{
    if (bytes.size() != 0x4428u)
        return false;
    static const unsigned char expected[32] = {0xDD, 0xF8, 0xD8, 0x93, 0x43, 0x19, 0x5D, 0x3D,
                                               0x01, 0x46, 0x30, 0xC2, 0x96, 0xA9, 0x58, 0x3E,
                                               0xE5, 0x56, 0xEF, 0xA8, 0x39, 0x91, 0x84, 0x35,
                                               0x80, 0x22, 0x49, 0xFE, 0x14, 0x85, 0x94, 0xD0};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    unsigned char hash[32] = {};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    const bool ok = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                               static_cast<ULONG>(bytes.size()), hash, sizeof(hash)) >= 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok && std::memcmp(hash, expected, 32) == 0;
}
bool ReadTemplate(const fs::path &root, std::vector<unsigned char> *bytes, std::string *error)
{
    if (!Directory(root) || !ReadBytes(root / "_template.bin", bytes, 0x4428u) ||
        !ExactTemplate(*bytes))
        return Fail(error, "Native export template missing or changed");
    return true;
}
bool WriteNew(const fs::path &file, const unsigned char *bytes, size_t count)
{
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = count <= MAXDWORD &&
                    WriteFile(h, bytes, static_cast<DWORD>(count), &written, nullptr) &&
                    written == count && FlushFileBuffers(h);
    const bool closed = CloseHandle(h) != FALSE;
    if (!ok || !closed)
    {
        DeleteFileW(file.c_str());
        return false;
    }
    return true;
}
std::string NewId()
{
    static std::atomic<unsigned> serial{0};
    char id[49] = {};
    std::snprintf(id, sizeof(id), "mix-%016llX-%08X",
                  static_cast<unsigned long long>(
                      std::chrono::system_clock::now().time_since_epoch().count()),
                  ++serial);
    return id;
}
} // namespace

bool ValidName(const std::string &name)
{
    if (name.empty() || name.size() > 40 || name.front() == ' ' || name.back() == ' ')
        return false;
    for (unsigned char c : name)
        if (c < 32 || c > 126)
            return false;
    return true;
}
bool Parse(const std::string &text, Preset *preset, std::string *error)
{
    if (!preset)
        return false;
    Node node;
    if (!Reader(text).Read(node))
        return Fail(error, "Malformed or oversized preset JSON");
    return Decode(node, preset, error);
}
std::string Serialize(const Preset &p)
{
    if (!ValidPreset(p))
        return {};
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s.precision(9);
    s << "{\n  \"schema\": \"ffx-hooks.arena-mix\",\n  \"version\": 3,\n  \"name\": "
      << Quote(p.name)
      << ",\n  \"carrier\": " << Quote(ArenaScenery::ExportCarrier(p.selection.scenery))
      << ",\n  \"battle_program\": \"normal-v1\",\n  \"program_source\": " << Quote(ArenaScenery::ProgramSource(p.selection.scenery))
      << ",\n  \"coordinate_space\": \"party-relative\",\n  \"camera_mode\": " << Quote(p.selection.camera==ArenaScenery::Camera::Tactical?"tactical":"arena")
      << ",\n  \"required_slots\": " << unsigned(p.requiredSlots)
      << ",\n  \"arena\": " << Quote(ArenaScenery::Get(p.selection.scenery)->key)
      << ",\n  \"battlefield_id\": " << ArenaScenery::Get(p.selection.scenery)->battlefieldId
      << ",\n  \"music_track\": " << p.selection.musicTrack
      << ",\n  \"choices\": [";
    for (unsigned i = 0; i < p.selection.activationCount; ++i)
    {
        if (i)
            s << ", ";
        s << Quote(ArenaMonsters::Get(p.selection.activations[i])->key);
    }
    s << "],\n  \"position_mode\": "
      << Quote(!p.selection.positions.enabled    ? "native"
               : p.selection.positions.automatic ? "auto"
                                                 : "manual")
      << ",\n  \"positions\": [";
    if (p.selection.positions.enabled)
        for (unsigned i = 0; i < p.selection.positions.count; ++i)
        {
            if (i)
                s << ", ";
            s << '[' << p.selection.positions.points[i].x << ", "
              << p.selection.positions.points[i].z << ']';
        }
    const std::string carrier=ArenaScenery::ExportCarrier(p.selection.scenery);
    s << "],\n  \"editor_binary\": " << Quote(carrier+"/"+carrier+".bin") << "\n}\n";
    return s.str();
}
bool BuildBinary(const std::vector<unsigned char> &original, const Preset &preset,
                 std::vector<unsigned char> *out, std::string *error)
{
    if (!out || (!original.empty()&&!ExactTemplate(original)) || !ValidPreset(preset))
        return Fail(error, "Unsupported template or preset");
    return ArenaBattleProgram::Build(preset.selection,out,error);
}
bool ImportBinary(const std::vector<unsigned char> &original,
                  const std::vector<unsigned char> &edited, Preset *p, std::string *error)
{
    if (!p)
        return Fail(error, "Unsupported native battle template");
    const bool classic=edited.size()==0x4428u;
    std::vector<unsigned char> baseline;
    size_t slots=kSlots,points=kPositions;
    ArenaBattleProgram::Geometry geometry{};
    if(classic){if(!ExactTemplate(original))return Fail(error,"Legacy template missing");baseline=original;}
    else {
        if(!ArenaBattleProgram::Build(p->selection,&baseline,error)||!ArenaBattleProgram::Describe(p->selection.scenery,p->selection.camera,&geometry))return false;
        slots=geometry.slotsOffset;points=geometry.positionsOffset;
    }
    const size_t baselinePoints=points;
    bool matches=edited.size()==baseline.size();
    for(size_t i=0;matches && i<edited.size();++i)
        if(!((i>=slots&&i<slots+16u)||(i>=points&&i<points+128u&&((i-points)%16u<4u||((i-points)%16u>=8u&&(i-points)%16u<12u)))) && edited[i]!=baseline[i])
            matches=false;
    // Accept old camera revisions by exact normalized identity, importing only
    // roster and X/Z. Their obsolete script/camera never reaches the live runtime.
    if(!matches && (classic || !KnownLegacyProgram(edited,*p,&slots,&points)))
        return Fail(error,"Native edit changes scenery, camera, height or battle structure");
    Preset result = *p;
    result.selection = {};
    result.selection.scenery = p->selection.scenery;
    result.selection.camera = p->selection.camera;
    result.selection.musicTrack = p->selection.musicTrack;
    std::array<uint16_t, 8> ids{};
    unsigned count = 0;
    bool ended = false;
    bool positions = !classic && p->selection.positions.enabled;
    for (unsigned i = 0; i < 8; ++i)
    {
        std::memcpy(&ids[i], edited.data() + slots + i * 2u, 2u);
        if (ids[i] == 0xFFFFu)
            ended = true;
        else
        {
            if (ended)
                return Fail(error, "Sparse native monster slots are unsupported");
            ++count;
        }
        if (std::memcmp(edited.data() + points + i * 16u,
                        baseline.data() + baselinePoints + i * 16u, 4u) ||
            std::memcmp(edited.data() + points + i * 16u + 8u,
                        baseline.data() + baselinePoints + i * 16u + 8u, 4u))
        {
            if (ended)
                return Fail(error, "Edited empty monster position");
            positions = true;
        }
    }
    for (unsigned i = 0; i < count;)
    {
        const auto* entry = ArenaMonsters::FromNative(ids[i]);
        if(!entry || i+entry->count>count) return Fail(error,"Unsupported or incomplete native monster group");
        for(unsigned member=0;member<entry->count;++member)
            if(ids[i+member]!=entry->monsterIds[member]) return Fail(error,"Monster group order changed");
        result.selection.activations[result.selection.activationCount++]=entry->choice;
        if(static_cast<unsigned>(entry->choice)>=8u) result.requiredSlots=0;
        i+=entry->count;
    }
    auto expanded = CustomMixUltra::BuildSelection(result.selection);
    if (expanded.result != CustomMixUltra::SelectionResult::Ready)
        return Fail(error, "Empty or invalid native selection");
    if (result.requiredSlots && result.requiredSlots != count)
        result.requiredSlots = count >= 3 && count <= 5 ? static_cast<uint8_t>(count) : 0;
    if (positions)
    {
        auto &layout = result.selection.positions;
        layout.enabled = true;
        layout.count = static_cast<uint8_t>(count);
        std::array<bool, 8> used{};
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned j = 0;
            while (j < count && (used[j] || ids[j] != expanded.expanded.monsterIds[i]))
                ++j;
            if (j == count)
                return Fail(error, "Cannot match native slots");
            used[j] = true;
            float x,z;
            std::memcpy(&x, edited.data() + points + j * 16u, 4u);
            std::memcpy(&z, edited.data() + points + j * 16u + 8u, 4u);
            if(classic)layout.points[i]={x,z};
            else ArenaBattleProgram::ToRelative(geometry,x,z,&layout.points[i].x,&layout.points[i].z);
        }
    }
    if (!ValidPreset(result))
        return Fail(error, "Native positions exceed supported bounds or spacing");
    *p = std::move(result);
    return true;
}
std::vector<Entry> Scan(const std::string &root, const std::string &legacyRoot)
{
    std::vector<Entry> result;
    try
    {
        for (unsigned n = 0; n < 3; ++n)
        {
            Entry e;
            e.builtin = e.valid = true;
            e.id = "builtin-" + std::to_string(n);
            e.preset.name = n == 0 ? "Elemental Trio" : n == 1 ? "Elemental Five" : "Magus Sisters";
            e.preset.requiredSlots = n == 1 ? 5 : 3;
            e.preset.selection.activationCount = n == 2 ? 1 : e.preset.requiredSlots;
            for (unsigned i = 0; i < e.preset.selection.activationCount; ++i)
                e.preset.selection.activations[i] =
                    static_cast<CustomMixUltra::MonsterChoice>(n == 2 ? 7 : i);
            e.preset.selection.positions = ArenaPositions::Generate(e.preset.requiredSlots);
            result.push_back(e);
        }
        for (unsigned source = 0; source < 2; ++source)
        {
            const fs::path directory = source ? legacyRoot : root;
            if (!Directory(directory))
                continue;
            std::error_code ec;
            fs::directory_iterator it(directory, ec), end;
            for (; !ec && it != end && result.size() < 67; it.increment(ec))
            {
                const auto file = it->path();
                if (file.extension() != ".json" || !SafeId(file.stem().string()) || !Regular(file))
                    continue;
                Entry e;
                e.id = file.stem().string();
                std::string text;
                if (ReadText(file, &text))
                    e.valid = Parse(text, &e.preset, &e.error);
                else
                    e.error = "Unreadable preset";
                e.legacySource = source != 0;
                e.preset.legacy = source != 0 || e.preset.legacy;
                if (e.preset.name.empty() || e.preset.legacy)
                    e.preset.name = e.id;
                result.push_back(std::move(e));
            }
        }
        std::sort(result.begin() + 3, result.end(),
                  [](const Entry &a, const Entry &b) { return a.preset.name < b.preset.name; });
    }
    catch (...)
    {
    }
    return result;
}
bool Load(const std::string &root, const std::string &legacyRoot, const Entry &e, Preset *preset,
          std::string *error, bool nativeEdits)
{
    try
    {
        if (!preset || !e.valid)
            return Fail(error, "Preset is invalid; refresh the library");
        if (e.builtin)
        {
            *preset = e.preset;
            return true;
        }
        if (!SafeId(e.id))
            return Fail(error, "Invalid preset identifier");
        const fs::path folder = e.legacySource ? legacyRoot : root;
        if (!Directory(folder))
            return Fail(error, "Preset folder unavailable");
        std::string text;
        Preset p;
        if (!ReadText(folder / (e.id + ".json"), &text) || !Parse(text, &p, error))
            return false;
        if (p.legacy)
        {
            p.name = e.preset.name;
            *preset = std::move(p);
            return true;
        }
        const std::string carrier=p.classicTemplate?"dome02_00":ArenaScenery::ExportCarrier(p.selection.scenery);
        const fs::path native = fs::path(root) / e.id / carrier / (carrier+".bin");
        if (nativeEdits)
        {
            if (!fs::exists(native))
                return Fail(error, "No native editor binary in this bundle");
            if (!Directory(fs::path(root) / e.id) || !Directory(native.parent_path()))
                return Fail(error, "Native bundle path is not a regular directory");
            std::vector<unsigned char> original, edited;
            if ((p.classicTemplate&&!ReadTemplate(root, &original, error)) || !ReadBytes(native, &edited, 65535u) ||
                !ImportBinary(original, edited, &p, error))
                return false;
        }
        *preset = std::move(p);
        return true;
    }
    catch (...)
    {
        return Fail(error, "Cannot read battle bundle");
    }
}
bool Save(const std::string &root, const Preset &preset, std::string *id, std::string *error)
{
    try
    {
        std::vector<unsigned char> original, native;
        if (!BuildBinary(original, preset, &native, error))
            return false;
        auto json = Serialize(preset);
        if (json.empty())
            return Fail(error, "Preset is invalid");
        const std::string key = NewId();
        Node document;
        if (!Reader(json).Read(document))
            return Fail(error, "Cannot serialize preset");
        const auto *binary = Get(document, "editor_binary", Node::String);
        if (!binary)
            return Fail(error, "Missing editor binary path");
        const std::string carrier=ArenaScenery::ExportCarrier(preset.selection.scenery);
        json.replace(binary->begin, binary->end - binary->begin,
                     Quote(key+"/"+carrier+"/"+carrier+".bin"));
        if (fs::exists(fs::path(root) / (key + ".json")))
            return Fail(error, "Export identifier collision; try again");
        const fs::path bundle = fs::path(root) / key;
        if (!fs::create_directory(bundle))
            return Fail(error, "Cannot reserve export folder");
        bool success = false;
        try
        {
            fs::create_directory(bundle / carrier);
            success =
                WriteNew(bundle / carrier / (carrier+".bin"), native.data(), native.size()) &&
                WriteNew(fs::path(root) / (key + ".json"),
                         reinterpret_cast<const unsigned char *>(json.data()), json.size());
        }
        catch (...)
        {
            success = false;
        }
        if (!success)
        {
            std::error_code ec;
            fs::remove(bundle / carrier / (carrier+".bin"), ec);
            fs::remove(bundle / carrier, ec);
            fs::remove(bundle, ec);
            return Fail(error, "Export failed; existing battles were preserved");
        }
        if (id)
            *id = key;
        return true;
    }
    catch (...)
    {
        return Fail(error, "Export folder is unavailable");
    }
}
bool Rename(const std::string &root, const std::string &id, const std::string &name,
            std::string *error)
{
    if (!SafeId(id) || !ValidName(name) || !Directory(root))
        return Fail(error, "Name must use 1-40 supported characters");
    const fs::path target = fs::path(root) / (id + ".json");
    std::string before;
    Node doc;
    Preset p;
    if (!ReadText(target, &before) || !Reader(before).Read(doc) || !Decode(doc, &p, error) ||
        p.legacy)
        return Fail(error, "Export a copy before renaming this preset");
    const auto *field = Get(doc, "name", Node::String);
    if (!field)
        return Fail(error, "Missing battle name");
    std::string after = before;
    after.replace(field->begin, field->end - field->begin, Quote(name));
    const fs::path temp = fs::path(root) / (id + "-" + NewId() + ".tmp");
    if (!WriteNew(temp, reinterpret_cast<const unsigned char *>(after.data()), after.size()))
        return Fail(error, "Could not stage the new name");
    std::string latest;
    const bool unchanged = ReadText(target, &latest) && latest == before;
    const bool ok =
        unchanged && MoveFileExW(temp.c_str(), target.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok)
    {
        DeleteFileW(temp.c_str());
        return Fail(error, "Battle changed or rename failed; refresh the library");
    }
    return true;
}
} // namespace FfxHooks::ArenaMixLibrary
