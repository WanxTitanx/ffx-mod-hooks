using System.Collections.Generic;

namespace SinCoreLib.Ai
{
    // Friendly names for AI TARGET sentinels — the literal pushed before a performCommand id (#8). These are the
    // FFX `btlActor` enum (cross-referenced from the public FFX ATEL RE, Karifean/FFXDataParser ScriptConstants —
    // same provenance as the func-name + chr-property tables). Self (0xFFF3) is independently byte/RT2-proven; the
    // rest are corpus-consistent (offensive spells target Character#1-3/Self, monster heals target AllMonsters; see
    // docs/reverse/FFX_AI_TARGET_ENCODING_AND_CONDITION_GETTERS_2026-06-07.md).
    //
    // "enemy/ally" is from the MONSTER's point of view: a monster's enemies are the player party (FrontlineChars /
    // Character#N), its allies are the other monsters (AllMonsters). Labels keep the canonical name in parens.
    public static class AiTargetNames
    {
        /// <summary>The standard, useful target sentinels offered in the "🎯 Mudar alvo" dropdown, in a sensible
        /// order. Operand -> friendly label.</summary>
        public static readonly IReadOnlyList<(ushort Operand, string Label)> Standard = new (ushort, string)[]
        {
            (0xFFF3, "Si mesmo (Self) — provado"),
            (0xFFF2, "Inimigos: todos os personagens (FrontlineChars)"),
            (0xFFFA, "Inimigo: Personagem #1"),
            (0xFFF9, "Inimigo: Personagem #2"),
            (0xFFF8, "Inimigo: Personagem #3"),
            (0xFFF1, "Aliados: todos os aeons (AllAeons)"),
            (0xFFEC, "Aliados: todos os Aeons (AllAeons)"),
            (0xFFE9, "Todos personagens + Aeons (AllPlayer)"),
            (0xFFEF, "Último atacante (LastAttacker)"),
            (0xFFFD, "Alvo atual da ação (TargetActors)"),
            (0xFFFC, "Alvo imediato (TargetActorsNow)"),
            (0xFFFB, "Todos os atores, ambos os lados (AllActors)"),
            (0xFFFE, "LAB: ator ativo/owner (ActiveActors) — RT2: monstro se bateu; não é inimigo"),
            (0xFFF0, "Random Enemies / grupo predefinido (PredefinedGroup · RT2)"),
            (0xFFFF, "Nenhum / limpar (Null)"),
        };

        // Full btlActor name map (for labelling a target value seen in a script, incl. non-standard ones).
        static readonly Dictionary<ushort, string> _names = new()
        {
            [0xFFE9] = "AllCharsAndAeons", [0xFFEB] = "AllActors?", [0xFFEC] = "AllAeons",
            [0xFFEF] = "LastAttacker", [0xFFF0] = "PredefinedGroup", [0xFFF1] = "AllAeons",
            [0xFFF2] = "FrontlineChars", [0xFFF3] = "Self", [0xFFF4] = "CharReserve#4",
            [0xFFF5] = "CharReserve#3", [0xFFF6] = "CharReserve#2", [0xFFF7] = "CharReserve#1",
            [0xFFF8] = "Character#3", [0xFFF9] = "Character#2", [0xFFFA] = "Character#1",
            [0xFFFB] = "AllActors", [0xFFFC] = "TargetActorsNow", [0xFFFD] = "TargetActors",
            [0xFFFE] = "ActiveActors", [0xFFFF] = "Null", [0x00FF] = "None",
        };

        /// <summary>Canonical btlActor name for a target sentinel, or null. For an actor-id ref (0x1000+n) returns
        /// "MonsterType=NNNN"; for a small positive (0x13+) "Monster#n".</summary>
        public static string? Get(ushort operand)
        {
            if (_names.TryGetValue(operand, out string? n)) return n;
            if (operand >= 0x1000 && operand < 0x1FFF) return $"MonsterType={(operand - 0x1000):X4}";
            if (operand >= 0x0013 && operand < 0x00FF) return $"Monster#{operand - 0x0013}";
            return null;
        }
    }
}
