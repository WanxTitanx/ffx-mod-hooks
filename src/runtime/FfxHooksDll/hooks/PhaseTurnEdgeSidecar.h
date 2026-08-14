// PhaseTurnEdgeSidecar — leitor minimalista do sidecar de config.
//
// Carrega mapeamentos de config\monster_ai_phase_turn_edge.cfg com formato:
//   monsterId skillId targetMask guardVar onlyOnce
//   m020      0x3049  0xFFF2     2        1
//
// A versao JSON (config\monster_ai_phase_turn_edge.json) e consumida pelo editor.
// Este .cfg e equivalente e mantido em sync.
//
// Cada entrada significa: quando um turno CTB passar para este monstro,
// dispatche a skillId via forcePerformCommand.

#pragma once

#include <stdint.h>

namespace FfxHooks {

struct PhaseTurnEdgeEntry {
    char     monsterId[8];   // e.g. "m020"
    uint16_t skillId;        // e.g. 0x3049 = Firaga
    uint16_t targetMask;     // e.g. 0xFFF2 = FrontlineChars
    int      guardVar;       // AI variable index for guard (-1 = no guard)
    bool     onlyOnce;       // fire only once per battle
};

struct PhaseTurnEdgeSidecar {
    PhaseTurnEdgeEntry entries[64];
    int                count;
};

// Load sidecar from config\monster_ai_phase_turn_edge.cfg.
// Returns empty sidecar if file not found or parse error.
PhaseTurnEdgeSidecar LoadPhaseTurnEdgeSidecar(const char* configDir);

// Find entry for a monster ID. Returns -1 if not found.
int FindPhaseTurnEdgeEntry(const PhaseTurnEdgeSidecar& sidecar, const char* monsterId);

} // namespace FfxHooks
