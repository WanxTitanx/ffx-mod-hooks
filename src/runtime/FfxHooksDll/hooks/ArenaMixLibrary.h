#pragma once
#include "CustomMixUltraCore.h"
#include <string>
#include <vector>

namespace FfxHooks::ArenaMixLibrary
{
struct Preset
{
    std::string name;
    std::uint8_t requiredSlots = 0;
    CustomMixUltra::SelectionInput selection{};
    bool legacy = false;
    bool classicTemplate = false; // Read-only compatibility with old v1 companion binaries.
};
struct Entry
{
    std::string id;
    Preset preset{};
    bool builtin = false;
    bool legacySource = false;
    bool valid = false;
    std::string error;
};
bool Parse(const std::string &text, Preset *preset, std::string *error);
std::string Serialize(const Preset &preset);
bool ValidName(const std::string &name);
bool BuildBinary(const std::vector<unsigned char> &original, const Preset &preset,
                 std::vector<unsigned char> *output, std::string *error);
bool ImportBinary(const std::vector<unsigned char> &original,
                  const std::vector<unsigned char> &edited, Preset *preset, std::string *error);
std::vector<Entry> Scan(const std::string &root, const std::string &legacyRoot);
bool Load(const std::string &root, const std::string &legacyRoot, const Entry &entry,
          Preset *preset, std::string *error, bool nativeEdits = false);
bool Save(const std::string &root, const Preset &preset, std::string *id, std::string *error);
bool Rename(const std::string &root, const std::string &id, const std::string &name,
            std::string *error);
} // namespace FfxHooks::ArenaMixLibrary
