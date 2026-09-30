using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using SinCoreLib.Ai;
using SinCoreLib.Monster;
using SinCoreLib.Files;
using SinCoreLib.Encoding;
using Xe.BinaryMapper;

namespace SinScaleInject;

class Program
{
    // ── Paths (defaults, override via --mod-base / --clean-base / --roster-dir) ─
    static string MOD_BASE   = BuildDefaultModBase();
    static string CLEAN_BASE = BuildDefaultCleanBase();
    static string ROSTER_DIR = BuildDefaultRosterDir();
    static string KERNEL_DIR = BuildDefaultKernelDir();

    // ── Preset metadata ────────────────────────────────────────────────────────
    static readonly Dictionary<int, string> UNI_NAMES = new()
    {
        {1, "Veil"}, {2, "March"}, {3, "Rush"}, {4, "Ward"},
        {5, "Frost"}, {6, "Tide"}, {7, "Salve"}, {8, "Mist"},
    };

    static readonly Dictionary<int, string> UNI_FULL = new()
    {
        {1, "Opening Veil"}, {2, "Forced March"}, {3, "Low HP Rush"}, {4, "Ward Stack"},
        {5, "Frost-Flood Weave"}, {6, "Shoreline Break"}, {7, "Sin Salve"}, {8, "Mist Chorus"},
    };

    // ── CLI ────────────────────────────────────────────────────────────────────
    static int Main(string[] args)
    {
        if (args.Length == 0) { PrintUsage(); return 1; }

        bool dryRun = args.Contains("--dry-run");
        bool verbose = args.Contains("--verbose") || args.Contains("-v");

        // Override paths via CLI args
        if (GetArg(args, "--mod-base")   is string mb)   MOD_BASE   = mb;
        if (GetArg(args, "--clean-base") is string cb)   CLEAN_BASE = cb;
        if (GetArg(args, "--roster-dir") is string rd)   ROSTER_DIR = rd;
        if (GetArg(args, "--kernel-dir") is string kd)   KERNEL_DIR = kd;

        // --save-clean
        if (args.Contains("--save-clean"))   return SaveClean(dryRun);
        // --restore
        if (args.Contains("--restore"))      return RestoreAll(dryRun);
        if (args.Contains("--restore-area"))
        {
            string? restoreAreaId = GetArg(args, "--restore-area");
            if (restoreAreaId == null) { Console.Error.WriteLine("--restore-area requires an id"); return 1; }
            return RestoreArea(restoreAreaId, dryRun);
        }

        // --area
        string? areaId  = GetArg(args, "--area");
        string? seedStr = GetArg(args, "--seed");
        string? tStr    = GetArg(args, "--t");
        string? intStr  = GetArg(args, "--intensity");

        if (areaId == null) { Console.Error.WriteLine("--area required"); return 1; }
        if (tStr == null) { Console.Error.WriteLine("--t required in --area mode"); return 1; }
        uint seed = 0;
        if (seedStr != null)
        {
            // Hook passes decimal; support 0x prefix for manual use
            if (seedStr.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                if (!uint.TryParse(seedStr.Substring(2), System.Globalization.NumberStyles.HexNumber, null, out seed))
                {
                    Console.Error.WriteLine($"invalid --seed hex value: {seedStr}");
                    return 1;
                }
            }
            else
            {
                if (!uint.TryParse(seedStr, System.Globalization.NumberStyles.Number, null, out seed))
                {
                    Console.Error.WriteLine($"invalid --seed decimal value: {seedStr}");
                    return 1;
                }
            }
        }
        if (seed == 0) seed = 0xDEADBEEF; // XorShift32(0) stays 0 — use default

        // T is now authoritative from the hook (--t); no reroll
        if (!int.TryParse(tStr, out int t))
        {
            Console.Error.WriteLine($"invalid --t value: {tStr}");
            return 1;
        }
        if (t < 0)
        {
            Console.Error.WriteLine($"--t must be >= 0 (got {t})");
            return 1;
        }

        int intensity = 60;
        if (intStr != null && !int.TryParse(intStr, out intensity))
        {
            Console.Error.WriteLine($"invalid --intensity value: {intStr}");
            return 1;
        }
        if (intensity < 0 || intensity > 100)
        {
            Console.Error.WriteLine($"--intensity must be between 0 and 100 (got {intensity})");
            return 1;
        }

        return ProcessArea(areaId, seed, t, intensity, dryRun, verbose);
    }

    static void PrintUsage()
    {
        Console.WriteLine("SinScaleInject v2 — .bin-first SIN injection");
        Console.WriteLine();
        Console.WriteLine("Modes:");
        Console.WriteLine("  --area <id> --seed <n> --t <n> [--intensity <pct>] [--dry-run] [-v]");
        Console.WriteLine("  --save-clean [--dry-run]");
        Console.WriteLine("  --restore [--dry-run]");
        Console.WriteLine("  --restore-area <id> [--dry-run]");
        Console.WriteLine();
        Console.WriteLine("Path overrides (defaults are Halyson's machine):");
        Console.WriteLine("  --mod-base <dir>     monster .bin root (default: jppc/battle/mon)");
        Console.WriteLine("  --kernel-dir <dir>   kernel name files dir (default: new_uspc/battle/kernel)");
        Console.WriteLine("  --clean-base <dir>   clean bin backup dir");
        Console.WriteLine("  --roster-dir <dir>   area roster CSV dir");
    }

    static string? GetArg(string[] args, string key)
    {
        int idx = Array.IndexOf(args, key);
        return idx >= 0 && idx + 1 < args.Length ? args[idx + 1] : null;
    }

    static string BuildDefaultModBase()
    {
        string? gameRoot = TryGetGameRootFromExeLocation();
        if (!string.IsNullOrEmpty(gameRoot))
            return Path.Combine(gameRoot, @"data\mods\ffx_ps2\ffx\master\jppc\battle\mon");
        return @"D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\data\mods\ffx_ps2\ffx\master\jppc\battle\mon";
    }

    static string BuildDefaultKernelDir()
    {
        string? gameRoot = TryGetGameRootFromExeLocation();
        if (!string.IsNullOrEmpty(gameRoot))
            return Path.Combine(gameRoot, @"data\mods\ffx_ps2\ffx\master\new_uspc\battle\kernel");
        return @"D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\data\mods\ffx_ps2\ffx\master\new_uspc\battle\kernel";
    }

    static string BuildDefaultCleanBase()
    {
        string? gameRoot = TryGetGameRootFromExeLocation();
        if (!string.IsNullOrEmpty(gameRoot))
            return Path.Combine(gameRoot, @"modules\tools\SinScaleInject\data\sin-clean-bins");
        return @"C:\Users\wande\Documents\ffx-editor-main\mods\Spira Reforge\sin-clean-bins";
    }

    static string BuildDefaultRosterDir()
    {
        string? gameRoot = TryGetGameRootFromExeLocation();
        if (!string.IsNullOrEmpty(gameRoot))
        if (Directory.Exists(Path.Combine(gameRoot, "data\\modules")))
            return Path.Combine(gameRoot, @"data\modules\tools\SinScaleInject\data\rosters");   // padrao tools do usuario (2026-08-02)
        return @"C:\Users\wande\Documents\ffx-editor-main\mods\Spira Reforge\arena\spira-sin-area-rosters";
    }

    static string? TryGetGameRootFromExeLocation()
    {
        string baseDir = AppContext.BaseDirectory;
        if (string.IsNullOrWhiteSpace(baseDir))
            return null;

        var dir = new DirectoryInfo(baseDir);
        if (dir.Name.Equals("SinScaleInject", StringComparison.OrdinalIgnoreCase) &&
            dir.Parent != null &&
            dir.Parent.Name.Equals("tools", StringComparison.OrdinalIgnoreCase) &&
            dir.Parent.Parent != null &&
            dir.Parent.Parent.Name.Equals("modules", StringComparison.OrdinalIgnoreCase) &&
            dir.Parent.Parent.Parent != null)
        {
            return dir.Parent.Parent.Parent.FullName;
        }

        return null;
    }

    // ── XorShift32 ─────────────────────────────────────────────────────────────
    static uint XorShift32(ref uint state)
    {
        uint x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    // ── Area processing ────────────────────────────────────────────────────────
    static int ProcessArea(string areaId, uint seed, int t, int intensity, bool dryRun, bool verbose)
    {
        // 1. Find roster CSV for this area
        string csvPath = Path.Combine(ROSTER_DIR, $"{areaId}.csv");

        if (!File.Exists(csvPath))
        {
            // Fallback: try macalania_all.csv for any macalania_* area
            if (areaId.StartsWith("macalania_"))
                csvPath = Path.Combine(ROSTER_DIR, "macalania_all.csv");
            else if (areaId.StartsWith("thunder_"))
                csvPath = Path.Combine(ROSTER_DIR, "thunder_all.csv");
        }

        if (!File.Exists(csvPath))
        {
            Console.WriteLine($"[SKIP] No roster CSV for '{areaId}' at {csvPath}");
            return 1;
        }

        // 2. Parse CSV
        var roster = ParseRosterCsv(csvPath);
        if (roster.Count == 0) { Console.WriteLine($"[SKIP] Empty roster: {csvPath}"); return 1; }

        Console.WriteLine($"=== SinScaleInject v2 — area={areaId} seed=0x{seed:X8} t={t} intensity={intensity}% ===");
        Console.WriteLine($"  Roster: {roster.Count} monsters from {csvPath}");

        bool restoreOnly = (t == 0);
        if (restoreOnly)
            Console.WriteLine($"  [RESTORE-ONLY] T=0 — restoring kernel + bins for this roster, no SIN applied");

        // Alinhar com C++: g_rngState = h ^ (h << 16)
        seed = seed ^ (seed << 16);

        // 3. Restore kernel names to clean before applying SIN names
        string kernelCleanDir = Path.Combine(CLEAN_BASE, "kernel");
        if (!Directory.Exists(kernelCleanDir))
        {
            Console.WriteLine($"[ERRO] Clean kernel backup not found at {kernelCleanDir}. Run --save-clean first.");
            return 1;
        }
        foreach (var file in Directory.GetFiles(kernelCleanDir, "monster*.bin"))
        {
            var dst = Path.Combine(KERNEL_DIR, Path.GetFileName(file));
            if (!dryRun) File.Copy(file, dst, overwrite: true);
            Console.WriteLine($"  [KERNEL-RESTORE] {Path.GetFileName(file)}");
        }

        // In restore-only mode (T=0): restore every monster in the roster to clean bins, then exit.
        if (restoreOnly)
        {
            int restoredCount = 0;
            foreach (var entry in roster)
            {
                if (!dryRun && RestoreSingle(entry.MonsterId, dryRun))
                    restoredCount++;
                else if (dryRun)
                    Console.WriteLine($"  [RESTORE] {entry.MonsterId} (dry-run)");
            }
            Console.WriteLine($"  [RESTORE-ONLY] Restored {restoredCount}/{roster.Count} monster bins to clean");
            return 0;
        }

        int selected = 0;
        int succeeded = 0;
        int failed = 0;
        foreach (var entry in roster)
        {
            // Roll: does this monster get SIN?
            seed = XorShift32(ref seed);
            int roll = (int)(seed % 100);

            if (roll >= intensity)
            {
                if (verbose) Console.WriteLine($"  [  ] {entry.MonsterId} (roll={roll} >= {intensity})");
                if (!dryRun) RestoreSingle(entry.MonsterId, dryRun);
                continue;
            }

            // Roll preset
            seed = XorShift32(ref seed);
            int preset = entry.Presets[seed % entry.Presets.Length];

            Console.WriteLine($"  [SIN] {entry.MonsterId} T={t} preset={preset} ({UNI_NAMES[preset]})");

            if (!dryRun)
            {
                bool ok = ApplySinToMonster(entry.MonsterId, preset, t, verbose);
                if (ok) succeeded++; else failed++;
            }
            else
                Console.WriteLine($"        (dry-run, would apply)");

            selected++;
        }
        Console.WriteLine($"  Summary: {selected} selected, {succeeded} succeeded, {failed} failed");
        Console.WriteLine($"  Done: {selected}/{roster.Count} monsters selected");

        if (failed > 0 && succeeded == 0) return 2;  // total failure
        if (failed > 0) return 1;                     // partial failure
        return 0;                                      // clean success
    }

    // T is now passed from the hook; AREA_T table removed.

    // ── CSV roster ─────────────────────────────────────────────────────────────
    class RosterEntry
    {
        public string MonsterId { get; set; } = "";
        public int[] Presets { get; set; } = Array.Empty<int>();
    }

    static List<RosterEntry> ParseRosterCsv(string path)
    {
        var result = new List<RosterEntry>();
        bool firstLine = true;
        foreach (var line in File.ReadAllLines(path))
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith('#')) continue;
            if (firstLine) { firstLine = false; continue; } // skip header
            var parts = line.Split(',');
            if (parts.Length < 4) continue;

            var entry = new RosterEntry
            {
                MonsterId = parts[0].Trim(),
                // allowed_presets is in column 3 (0-indexed), format: "UNI-001|UNI-003|..."
                Presets = parts[3].Trim()
                    .Split('|')
                    .Select(p => int.Parse(p.Replace("UNI-", "")))
                    .ToArray(),
            };
            result.Add(entry);
        }
        return result;
    }

    // ── Apply SIN to .bin ─────────────────────────────────────────────────────
    static bool ApplySinToMonster(string monsterId, int preset, int t, bool verbose)
    {
        try
        {
            string srcDir  = Path.Combine(MOD_BASE, $"_{monsterId}");
            string srcBin  = Path.Combine(srcDir, $"{monsterId}.bin");
            string cleanBin = Path.Combine(CLEAN_BASE, $"_{monsterId}", $"{monsterId}.bin");

            if (!File.Exists(cleanBin))
            {
                Console.WriteLine($"    [ERR] No clean bin for {monsterId} at {cleanBin}");
                return false;
            }

            // 1. Copy clean → target
            byte[] bin = File.ReadAllBytes(cleanBin);
            if (verbose) Console.WriteLine($"    Read {bin.Length} bytes from clean copy");

            // 2. Section pointers — find stat sheet end (next non-zero section or FileSize)
            uint statOff    = ReadU32(bin, 0x0C);
            uint workerOff  = ReadU32(bin, 0x08);
            uint fileSize   = ReadU32(bin, 0x20);
            uint nextSec    = fileSize;
            foreach (int off in new[] { 0x10, 0x14, 0x18, 0x1C }) // spoils, loot, audio, text
            {
                uint v = ReadU32(bin, off);
                if (v > statOff && v < nextSec) nextSec = v;
            }
            int statLen = (int)(nextSec - statOff);

            if (verbose) Console.WriteLine($"    Sections: stat=0x{statOff:X4}({statLen}B) worker=0x{workerOff:X4}");

            // 3. Scale HP and stats via Monster_StatSheet (BinaryMapping layout)
            float hpMult = T_HpMult(t);
            float statMult = T_StatMult(t);
            int statFlat = T_StatFlat(t);

            // Extract just the stat sheet section from the .bin
            byte[] statSection = new byte[statLen];
            Array.Copy(bin, statOff, statSection, 0, statLen);

            // Parse with editor's official method
            var statSheet = Monster_StatSheet.ReadSingle(statSection);
            if (verbose)
            {
                Console.WriteLine($"    Read: HP={statSheet.Hp} STR={statSheet.Strength} DEF={statSheet.Defense}");
            }

            // Scale
            uint oldHp = statSheet.Hp;
            statSheet.Hp = (uint)(statSheet.Hp * hpMult);
            statSheet.HpOverkill = (uint)(statSheet.HpOverkill * hpMult);
            statSheet.Strength = (byte)Math.Min(255, (int)(statSheet.Strength * statMult) + statFlat);
            statSheet.Defense = (byte)Math.Min(255, (int)(statSheet.Defense * statMult) + statFlat);
            statSheet.Magic = (byte)Math.Min(255, (int)(statSheet.Magic * statMult) + statFlat);
            statSheet.MagicDefense = (byte)Math.Min(255, (int)(statSheet.MagicDefense * statMult) + statFlat);
            statSheet.Agility = (byte)Math.Min(255, (int)(statSheet.Agility * statMult) + statFlat);
            statSheet.Luck = (byte)Math.Min(255, (int)(statSheet.Luck * statMult) + statFlat);
            statSheet.Evasion = (byte)Math.Min(255, (int)(statSheet.Evasion * statMult) + statFlat);
            statSheet.Accuracy = (byte)Math.Min(255, (int)(statSheet.Accuracy * statMult) + statFlat);

            // Write back using editor's official method (preserves text pool)
            byte[] newSection = statSheet.WriteSingle();

            // Splice updated section back into .bin
            Array.Copy(newSection, 0, bin, statOff, Math.Min(newSection.Length, statLen));

            if (verbose) Console.WriteLine($"    HP: {oldHp} -> {statSheet.Hp} (x{hpMult:F2})");

            // 4. Modify monster name in kernel localization files (monster1/2/3.bin)
            try
            {
                SetMonsterNameInKernel(monsterId, preset);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"    [WARN] Kernel name change failed: {ex.Message}");
            }

            // 5. ATEL injection — inject UNI preset handler via AppendGuardedAction
            try
            {
                bin = InjectAtelHandler(bin, preset, verbose);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"    [WARN] ATEL injection failed: {ex.Message}");
            }

            // 6. Write back
            string dstBin = Path.Combine(MOD_BASE, $"_{monsterId}", $"{monsterId}.bin");
            File.WriteAllBytes(dstBin, bin);
            Console.WriteLine($"    Written {bin.Length} bytes -> {dstBin}");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"    [FAIL] ApplySinToMonster failed for {monsterId}: {ex.Message}");
            return false;
        }
    }

    // ── Monster name ──────────────────────────────────────────────────────────
    static void SetMonsterName(byte[] bin, uint statOff, int statLen, string monsterId, int preset)
    {
        // Parse the TSInfo at the start of the stat section to find the name offset
        // Section layout: 5×TextScriptInfo (4 bytes each) + MonsterStatSheetStruct + text pool
        // NameTSInfo is at section offset 0x00: [u16 nameOffsetInTextPool, u16 scriptId]
        int tsInfoNameOff = (int)(statOff + 0); // first TSInfo
        if (tsInfoNameOff + 4 > bin.Length) return;

        ushort nameOffInPool = (ushort)(bin[tsInfoNameOff] | (bin[tsInfoNameOff + 1] << 8));

        // Find struct size by parsing the section with BinaryMapping
        byte[] section = new byte[statLen];
        Array.Copy(bin, statOff, section, 0, statLen);

        using var ms = new MemoryStream(section);
        var structObj = BinaryMapping.ReadObject<MonsterStatSheetStruct>(ms);
        int structSize = (int)ms.Position;

        int nameAbsPos = (int)(statOff + structSize + nameOffInPool);
        if (nameAbsPos <= 0 || nameAbsPos >= bin.Length) return;

        string newName = $"{GetShortName(monsterId)} [{UNI_NAMES[preset]}]";
        byte[] nameBytes = Encoding.ASCII.GetBytes(newName);

        // Overwrite, null-terminated, max 32 bytes
        int maxLen = Math.Min(nameBytes.Length + 1, 32);
        if (nameAbsPos + maxLen > bin.Length)
            maxLen = bin.Length - nameAbsPos;

        for (int i = 0; i < maxLen; i++)
            bin[nameAbsPos + i] = i < nameBytes.Length ? nameBytes[i] : (byte)0;
    }

    // MonsterStatSheetStruct for BinaryMapping
    class MonsterStatSheetStruct
    {
        [Data] public ushort NameTSInfo_Offset { get; set; }
        [Data] public ushort NameTSInfo_ScriptId { get; set; }
        [Data] public ushort SensorTSInfo_Offset { get; set; }
        [Data] public ushort SensorTSInfo_ScriptId { get; set; }
        [Data] public ushort Unused1TSInfo_Offset { get; set; }
        [Data] public ushort Unused1TSInfo_ScriptId { get; set; }
        [Data] public ushort ScanTSInfo_Offset { get; set; }
        [Data] public ushort ScanTSInfo_ScriptId { get; set; }
        [Data] public ushort Unused2TSInfo_Offset { get; set; }
        [Data] public ushort Unused2TSInfo_ScriptId { get; set; }
        [Data] public Monster_StatSheet StatSheet { get; set; } = new();
    }

    // Get short display name from monster ID
    static string GetShortName(string monsterId)
    {
        var names = new Dictionary<string, string>
        {
            {"m003", "Murussu"}, {"m004", "Mafdet"}, {"m012", "Snow Wolf"},
            {"m019", "Ice Flan"}, {"m026", "Iguion"}, {"m033", "Wasp"},
            {"m037", "Evil Eye"}, {"m081", "Blue Element"}, {"m087", "Chimera"},
            {"m217", "Xiphos"},
        };
        return names.TryGetValue(monsterId, out var n) ? n : monsterId;
    }

    // ── Kernel monster name (monster1/2/3.bin) ────────────────────────────────
    static readonly string[] MONSTER_BINS = { "monster1.bin", "monster2.bin", "monster3.bin" };
    static readonly int[] MONSTER_OFFSETS = { 0, 185, 293 };

    static void SetMonsterNameInKernel(string monsterId, int preset)
    {
        if (!int.TryParse(monsterId.AsSpan(1), out int id)) return;
        string newName = $"{GetShortName(monsterId)} [{UNI_NAMES[preset]}]";

        for (int fi = 0; fi < MONSTER_BINS.Length; fi++)
        {
            int firstId = MONSTER_OFFSETS[fi];
            int nextFirst = (fi + 1 < MONSTER_BINS.Length) ? MONSTER_OFFSETS[fi + 1] : int.MaxValue;
            if (id < firstId || id >= nextFirst) continue;

            string path = Path.Combine(KERNEL_DIR, MONSTER_BINS[fi]);
            if (!File.Exists(path)) { Console.WriteLine($"    [WARN] {MONSTER_BINS[fi]} not found at {path}"); return; }

            byte[] data = File.ReadAllBytes(path);
            var file = MonX_File.Read(data);
            int entryIdx = id - firstId;
            if (entryIdx < 0 || entryIdx >= file.Entries.Count) return;

            file.Entries[entryIdx].Name = newName;
            byte[] newData = file.Write();
            File.WriteAllBytes(path, newData);
            Console.WriteLine($"    Name: \"{newName}\" (in {MONSTER_BINS[fi]}[{entryIdx}])");
            return;
        }
    }

    // ── ATEL injection ─────────────────────────────────────────────────────────
    static byte[] InjectAtelHandler(byte[] bin, int preset, bool verbose)
    {
        byte[] currentBin = bin;

        // 1. Slice + parse AiFile
        byte[]? aiBytes = AiScript_File.SliceAiFileFromMonster(currentBin);
        if (aiBytes == null) throw new Exception("SliceAiFileFromMonster returned null");
        var aiScript = AiScript_File.Read(aiBytes);
        if (aiScript == null || aiScript.Workers.Count == 0)
            throw new Exception("AiScript parse failed or empty");

        int workerIdx = 0;
        ushort? varIndex = null;

        // 2. If preset needs 1-time execution (UNI-001, 004), create a private VAR
        bool needsOneTime = (preset == 1 || preset == 4);
        if (needsOneTime)
        {
            // Find free slot in private storage
            if (!AiScript_File.TryFindFreePrivateVariableSlot(aiScript, out int slot, out string reason))
                throw new Exception($"Cannot find free VAR slot: {reason}");

            if (verbose) Console.WriteLine($"    VAR: slot={slot}");

            // Append descriptor — returns new AiFile bytes with the variable
            byte[] newAi = AiScript_File.AppendPrivateVariableDescriptor(aiScript, slot, typeId: 1);

            // Splice into bin (grow if needed)
            currentBin = AiScript_File.SpliceAiFileIntoMonsterGrow(currentBin, newAi);

            // Re-slice + re-parse to get the variable index
            byte[]? reAi = AiScript_File.SliceAiFileFromMonster(currentBin);
            if (reAi == null) throw new Exception("Re-slice failed after VAR add");
            var reScript = AiScript_File.Read(reAi);
            if (reScript == null) throw new Exception("Re-parse failed after VAR add");

            varIndex = (ushort)(reScript.Variables.Count - 1);
            aiScript = reScript;

            if (verbose) Console.WriteLine($"    VAR: index={varIndex} (total vars={reScript.Variables.Count})");

            // Init VAR = 0 at entrypoint 0 (init/battle start)
            if (0 < aiScript.Workers[workerIdx].Entrypoints.Count)
            {
                var initGuard = new List<AiInstruction> { I(0xAE, 1) };  // always
                var initAction = new List<AiInstruction>
                {
                    I(0xAE, 0),              // PUSHII 0
                    I(0xA0, varIndex.Value),  // POPV var[index]
                };
                byte[] initAi = AiScript_File.AppendGuardedAction(
                    aiScript, workerIdx, 0, initGuard, initAction, stopAfterAction: false);

                currentBin = AiScript_File.SpliceAiFileIntoMonsterGrow(currentBin, initAi);

                // Re-slice + re-parse for the preset injection
                byte[]? afterInit = AiScript_File.SliceAiFileFromMonster(currentBin);
                if (afterInit != null)
                {
                    var afterScript = AiScript_File.Read(afterInit);
                    if (afterScript != null) aiScript = afterScript;
                }

                if (verbose) Console.WriteLine($"    VAR: init at entrypoint 0 (PUSHII 0 · POPV {varIndex.Value})");
            }
        }

        // 3. Build guard + action for this preset
        var (guard, action) = BuildPresetInstructions(preset, varIndex);
        if (guard.Count == 0 && action.Count == 0) return currentBin; // no-op

        // 4. Determine entrypoint: UNI-001 and UNI-002 use onHit (3), others use onTurn (2)
        int entrypointIdx = (preset == 1 || preset == 2) ? 3 : 2;

        if (entrypointIdx >= aiScript.Workers[workerIdx].Entrypoints.Count)
        {
            if (verbose) Console.WriteLine($"    [WARN] Entrypoint {entrypointIdx} not available (max {aiScript.Workers[workerIdx].Entrypoints.Count - 1})");
            return currentBin;
        }

        byte[] finalAi = AiScript_File.AppendGuardedAction(
            aiScript, workerIdx, entrypointIdx, guard, action, stopAfterAction: false);

        // 5. Splice back into .bin (may grow)
        byte[] result = AiScript_File.SpliceAiFileIntoMonsterGrow(currentBin, finalAi);
        if (verbose) Console.WriteLine($"    ATEL injected: preset={preset} entrypoint={entrypointIdx} ({currentBin.Length} -> {result.Length} bytes)");
        return result;
    }

    // Helper: build AiInstruction quickly (local usage only)
    static AiInstruction I(byte opcode, ushort operand = 0, bool hasOperand = true)
    {
        return new AiInstruction
        {
            Offset = -1,
            Opcode = opcode,
            HasOperand = hasOperand,
            Operand = operand,
            OperandKind = hasOperand ? AiScript_File.OperandKindOf(opcode) : AiOperandKind.None,
        };
    }
    static AiInstruction I1(byte opcode) => I(opcode, 0, false);

    static (List<AiInstruction> guard, List<AiInstruction> action) BuildPresetInstructions(int preset, ushort? varIndex = null)
    {
        switch (preset)
        {
            // UNI-001: Opening Veil — Haste + Protect + Shell (self, onHit, 1x only)
            case 1:
            {
                // Guard: var == 0 (executa só na 1ª vez)
                var g = varIndex.HasValue
                    ? new List<AiInstruction> { I(0x9F, varIndex.Value), I(0xAE, 0), I1(0x06) } // PUSHV · PUSHII 0 · EQ
                    : new List<AiInstruction> { I(0xAE, 1) }; // fallback: always
                var a = new List<AiInstruction>
                {
                    // Shell (0x303A) → Protect (0x303B) → Haste (0x3036) — ordem do editor
                    I(0xAE, 0xFFF3), I(0xAE, 0x303A), I(0xD8, 0x700B), // performCommand(Shell)
                    I(0xAE, 0xFFF3), I(0xAE, 0x303B), I(0xD8, 0x700B), // performCommand(Protect)
                    I(0xAE, 0xFFF3), I(0xAE, 0x3036), I(0xD8, 0x700B), // performCommand(Haste)
                };
                // var++ depois da última ação
                if (varIndex.HasValue)
                {
                    a.Add(I(0x9F, varIndex.Value)); // PUSHV var
                    a.Add(I(0xAE, 1));               // PUSHII 1
                    a.Add(I1(0x14));                  // ADD
                    a.Add(I(0xA0, varIndex.Value));  // POPV var
                }
                return (g, a);
            }

            // UNI-002: Counter March — CounterAttack onHit
            case 2:
            {
                var g = new List<AiInstruction> { I(0xAE, 1) }; // always
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x4000), I(0xD8, 0x705A), // forcePerformCommand(MonsterAttack)
                };
                return (g, a);
            }

            // UNI-003: Low HP Rush — Haste self + Slow random frontline when NearDeath (HP < 50%)
            case 3:
            {
                var g = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x0119), I(0xB5, 0x700F), // readChrProperty(self, NearDeath)
                };
                var a = new List<AiInstruction>
                {
                    // Haste on self
                    I(0xAE, 0xFFF3), I(0xAE, 0x3036), I(0xD8, 0x705A),
                    // Slow on random living frontline
                    I(0xAE, 0xFFF2), I(0xAE, 0x0004), I(0xAE, 0), I(0xAE, 0),
                    I(0xB5, 0x7010), I(0xAE, 0x3038), I(0xD8, 0x705A),
                };
                return (g, a);
            }

            // UNI-004: Ward Stack — Shell + Regen + NulBlaze + NulShock (1x only)
            case 4:
            {
                // Guard: var == 0 (executa só na 1ª vez)
                var g = varIndex.HasValue
                    ? new List<AiInstruction> { I(0x9F, varIndex.Value), I(0xAE, 0), I1(0x06) } // PUSHV · PUSHII 0 · EQ
                    : new List<AiInstruction> { I(0xAE, 1) }; // fallback: always
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x303A), I(0xD8, 0x705A), // forcePerformCommand(Shell)
                    I(0xAE, 0xFFF3), I(0xAE, 0x303E), I(0xD8, 0x705A), // forcePerformCommand(Regen)
                    I(0xAE, 0xFFF3), I(0xAE, 0x302F), I(0xD8, 0x705A), // forcePerformCommand(NulBlaze)
                    I(0xAE, 0xFFF3), I(0xAE, 0x3030), I(0xD8, 0x705A), // forcePerformCommand(NulShock)
                };
                // Se tem VAR, incrementa depois da ação (var++)
                if (varIndex.HasValue)
                {
                    a.Add(I(0x9F, varIndex.Value)); // PUSHV var
                    a.Add(I(0xAE, 1));               // PUSHII 1
                    a.Add(I1(0x14));                  // ADD
                    a.Add(I(0xA0, varIndex.Value));  // POPV var
                }
                return (g, a);
            }

            // UNI-005: Frost-Flood Weave — Blizzara + Watera (frontline)
            case 5:
            {
                var g = new List<AiInstruction> { I(0xAE, 1) }; // always
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF2), I(0xAE, 0x3046), I(0xD8, 0x705A), // Blizzara
                    I(0xAE, 0xFFF2), I(0xAE, 0x3048), I(0xD8, 0x705A), // Watera
                };
                return (g, a);
            }

            // UNI-006: Shoreline Break — Watera (frontline)
            case 6:
            {
                var g = new List<AiInstruction> { I(0xAE, 1) }; // always
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF2), I(0xAE, 0x3048), I(0xD8, 0x705A), // Watera
                };
                return (g, a);
            }

            // UNI-007: Sin Salve — Cure when HP < 70%
            case 7:
            {
                var g = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x0000), I(0xB5, 0x700F),
                    I(0xAE, 0x0064), I1(0x17),
                    I(0xAE, 0xFFF3), I(0xAE, 0x0002), I(0xB5, 0x700F),
                    I(0xAE, 0x0046), I1(0x17),
                    I1(0x0B),
                };
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x302B), I(0xD8, 0x705A), // Cure (force)
                };
                return (g, a);
            }

            // UNI-008: Mist Chorus — White Wind when HP < 50%
            case 8:
            {
                var g = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF3), I(0xAE, 0x0000), I(0xB5, 0x700F),
                    I(0xAE, 0x0064), I1(0x17),
                    I(0xAE, 0xFFF3), I(0xAE, 0x0002), I(0xB5, 0x700F),
                    I(0xAE, 0x0032), I1(0x17),
                    I1(0x0B),
                };
                var a = new List<AiInstruction>
                {
                    I(0xAE, 0xFFF1), I(0xAE, 0x405A), I(0xD8, 0x705A), // White Wind (force)
                };
                return (g, a);
            }

            default:
                return (new List<AiInstruction>(), new List<AiInstruction>());
        }
    }

    // ── Clean / Restore ────────────────────────────────────────────────────────
    static int SaveClean(bool dryRun)
    {
        Console.WriteLine("=== SinScaleInject — save-clean ===");
        if (!Directory.Exists(CLEAN_BASE))
        {
            if (!dryRun) Directory.CreateDirectory(CLEAN_BASE);
            Console.WriteLine($"  Created {CLEAN_BASE}");
        }

        int count = 0;
        foreach (var dir in Directory.GetDirectories(MOD_BASE, "_m*"))
        {
            string mid = Path.GetFileName(dir).TrimStart('_');
            string srcBin = Path.Combine(dir, $"{mid}.bin");
            if (!File.Exists(srcBin)) continue;

            string dstDir = Path.Combine(CLEAN_BASE, $"_{mid}");
            if (!dryRun) Directory.CreateDirectory(dstDir);

            string dstBin = Path.Combine(dstDir, $"{mid}.bin");
            if (!dryRun) File.Copy(srcBin, dstBin, overwrite: true);
            Console.WriteLine($"  {mid}.bin ({new FileInfo(srcBin).Length} bytes)");
            count++;
        }

        // Save kernel monster name files backup
        string kernelBackup = Path.Combine(CLEAN_BASE, "kernel");
        if (!Directory.Exists(kernelBackup))
        {
            if (!dryRun) Directory.CreateDirectory(kernelBackup);
        }
        foreach (var file in Directory.GetFiles(KERNEL_DIR, "monster*.bin"))
        {
            string name = Path.GetFileName(file);
            string dst = Path.Combine(kernelBackup, name);
            if (!dryRun) File.Copy(file, dst, overwrite: true);
            Console.WriteLine($"  Kernel: {name} backed up");
        }

        Console.WriteLine($"  Saved {count} clean bins + kernel files to {CLEAN_BASE}");
        return 0;
    }

    static int RestoreAll(bool dryRunArg)
    {
        Console.WriteLine("=== SinScaleInject — restore ===");
        int count = 0;
        foreach (var dir in Directory.GetDirectories(CLEAN_BASE, "_m*"))
        {
            string mid = Path.GetFileName(dir).TrimStart('_');
            if (RestoreSingle(mid, dryRunArg)) count++;
        }
        // Restore kernel monster name files
        string kernelCleanDir = Path.Combine(CLEAN_BASE, "kernel");
        if (Directory.Exists(kernelCleanDir))
        {
            int kernelCount = 0;
            foreach (var file in Directory.GetFiles(kernelCleanDir, "monster*.bin"))
            {
                var dst = Path.Combine(KERNEL_DIR, Path.GetFileName(file));
                if (!dryRunArg) File.Copy(file, dst, overwrite: true);
                Console.WriteLine($"  Kernel: {Path.GetFileName(file)} restored");
                kernelCount++;
            }
            Console.WriteLine($"  Restored {kernelCount} kernel files");
        }
        else
        {
            Console.WriteLine($"  [WARN] Clean kernel backup not found at {kernelCleanDir}. Run --save-clean first.");
        }

        Console.WriteLine($"  Restored {count} bins");
        return 0;
    }

    static int RestoreArea(string areaId, bool dryRunArg)
    {
        // TODO: lookup roster CSV, restore only monsters listed there
        Console.WriteLine($"  --restore-area: {areaId} (NYI, use --restore for all)");
        return 0;
    }

    static bool RestoreSingle(string monsterId, bool dryRunArg = false)
    {
        string cleanBin = Path.Combine(CLEAN_BASE, $"_{monsterId}", $"{monsterId}.bin");
        string dstBin   = Path.Combine(MOD_BASE, $"_{monsterId}", $"{monsterId}.bin");

        if (!File.Exists(cleanBin)) return false;

        if (!dryRunArg)
        {
            if (!Directory.Exists(Path.GetDirectoryName(dstBin)))
                Directory.CreateDirectory(Path.GetDirectoryName(dstBin)!);
            File.Copy(cleanBin, dstBin, overwrite: true);
        }
        return true;
    }

    // ── T formulas (same as SinCurseHook.cpp) ──────────────────────────────────
    static float T_HpMult(int t)
    {
        if (t <= 0) return 1.0f;
        if (t >= 10) return 2.0f;
        return 1.0f + t * 0.10f;
    }

    static float T_StatMult(int t)
    {
        if (t <= 0) return 1.0f;
        if (t >= 10) return 1.50f;
        return 1.0f + t * 0.05f;
    }

    static int T_StatFlat(int t)
    {
        if (t <= 0) return 0;
        if (t >= 10) return 10;
        return t;
    }

    // ── Binary helpers ─────────────────────────────────────────────────────────
    static uint ReadU32(byte[] data, int offset)
    {
        return (uint)(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
    }

    static void WriteU32(byte[] data, int offset, uint value)
    {
        data[offset]     = (byte)(value & 0xFF);
        data[offset + 1] = (byte)((value >> 8) & 0xFF);
        data[offset + 2] = (byte)((value >> 16) & 0xFF);
        data[offset + 3] = (byte)((value >> 24) & 0xFF);
    }
}
