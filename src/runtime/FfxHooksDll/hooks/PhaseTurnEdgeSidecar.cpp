// PhaseTurnEdgeSidecar — leitor minimalista do sidecar de config.

#include "PhaseTurnEdgeSidecar.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace FfxHooks {

PhaseTurnEdgeSidecar LoadPhaseTurnEdgeSidecar(const char* configDir) {
    PhaseTurnEdgeSidecar sidecar = {};
    sidecar.count = 0;

    char path[512] = {};
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\monster_ai_phase_turn_edge.cfg", configDir);

    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || f == nullptr) {
        return sidecar; // empty — file not found
    }

    char line[256];
    while (fgets(line, sizeof(line), f) && sidecar.count < 64) {
        // Skip comments and empty lines
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        char     monsterId[8] = {};
        unsigned skillId = 0;
        unsigned targetMask = 0;
        int      guardVar = -1;
        int      onlyOnce = 0;

        int parsed = sscanf_s(p, "%7s %x %x %d %d",
            monsterId, (unsigned)sizeof(monsterId),
            &skillId, &targetMask, &guardVar, &onlyOnce);

        if (parsed >= 2 && monsterId[0] == 'm') {
            auto& entry = sidecar.entries[sidecar.count++];
            strncpy_s(entry.monsterId, sizeof(entry.monsterId), monsterId, _TRUNCATE);
            entry.skillId    = static_cast<uint16_t>(skillId & 0xFFFF);
            entry.targetMask = static_cast<uint16_t>(targetMask & 0xFFFF);
            entry.guardVar   = (parsed >= 4) ? guardVar : -1;
            entry.onlyOnce   = (parsed >= 5) ? (onlyOnce != 0) : false;
        }
    }

    fclose(f);
    return sidecar;
}

int FindPhaseTurnEdgeEntry(const PhaseTurnEdgeSidecar& sidecar, const char* monsterId) {
    for (int i = 0; i < sidecar.count; i++) {
        if (_stricmp(sidecar.entries[i].monsterId, monsterId) == 0)
            return i;
    }
    return -1;
}

} // namespace FfxHooks
