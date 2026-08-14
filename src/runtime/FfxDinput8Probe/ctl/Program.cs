// ffxprobectl — external control/reader for the ffx-probe.dll Command Block (MMF).
// Proves the DINPUT8 main-thread seam: monitor heartbeat, arm READ/CALL, read results.
using System.Globalization;
using System.IO.MemoryMappedFiles;

const string MMF = "Local\\FFXProbeBlock_v1";
const int SIZE = 580;
// field offsets (match ffx_probe_block.h, #pragma pack(1))
const int O_MAGIC=0,O_VER=4,O_BASE=8,O_HB=12,O_HOOKED=16,O_SEQ=20,O_ACK=24,O_OP=28,
          O_STATUS=32,O_ADDR=36,O_LEN=40,O_ABI=44,O_A0=48,O_A1=52,O_A2=56,O_RET=60,O_ERR=64,O_BUF=68;
const uint MAGIC=0x46585042;

if (args.Length == 0) { Console.WriteLine("uso: mon | read <rvaHex> <len> | write <rvaHex> <hexbytes> | call <rvaHex> <abi> <a0> <a1> <a2> | soundcmd <cmd> <param0> [param1] | forcebattle <f> <g> <fo> | arena-flags [--all] | open-popup [textVaHex] | open-popup-text \"<ascii>\" | list-read [handleHex] | scan-instances | dump-bones <out.json> [idFilterHex] [frames] [intervalMs] | battle-actors | set-field-model <nome|c0NN> [--dispose-old] | aurora-calib | aurora-calib-v2 [--route <f> <g> <fo>] [--battle <id>] [--out <json>] [--csv <csv>] [--max-slots <N>]"); Console.WriteLine("abi: 1=cdecl(int) 2=cdecl(int,int) 3=cdecl(int,int,int) 4=cdecl(int,int,float) 5=stdcall(int,int)"); return 1; }

MemoryMappedViewAccessor a;
try {
    var mmf = MemoryMappedFile.OpenExisting(MMF, MemoryMappedFileRights.ReadWrite);
    a = mmf.CreateViewAccessor(0, SIZE, MemoryMappedFileAccess.ReadWrite);
} catch (Exception ex) {
    Console.WriteLine($"MMF '{MMF}' nao encontrado — o jogo esta rodando com o ffx-probe.dll carregado? ({ex.GetType().Name})");
    return 2;
}

uint U(int o) => a.ReadUInt32(o);
void W(int o, uint v) => a.Write(o, v);

if (U(O_MAGIC) != MAGIC) Console.WriteLine($"[aviso] magic=0x{U(O_MAGIC):X8} (esperado 0x{MAGIC:X8}) — modulo pode nao ter anexado ainda");

string mode = args[0].ToLowerInvariant();
uint baseAddr = U(O_BASE);

if (mode == "mon") {
    Console.WriteLine($"magic=0x{U(O_MAGIC):X8} ver={U(O_VER)} moduleBase=0x{baseAddr:X8} hooked={U(O_HOOKED)}");
    Console.WriteLine("monitorando heartbeat (Ctrl+C pra sair)...");
    uint last = U(O_HB); int still = 0;
    for (int i = 0; i < 100000; i++) {
        System.Threading.Thread.Sleep(250);
        uint hb = U(O_HB), hooked = U(O_HOOKED);
        string tick = hb != last ? $"+{hb-last}/250ms" : (++still % 4 == 0 ? "(parado)" : "");
        Console.WriteLine($"heartbeat={hb} hooked={hooked} {tick}");
        if (hb != last) still = 0;
        last = hb;
    }
    return 0;
}

uint Arm(uint op, uint addr, uint len, uint abi, uint a0, uint a1, uint a2) {
    W(O_OP, op); W(O_ADDR, addr); W(O_LEN, len); W(O_ABI, abi);
    W(O_A0, a0); W(O_A1, a1); W(O_A2, a2); W(O_STATUS, 0); W(O_ERR, 0);
    uint seq = U(O_SEQ) + 1; W(O_SEQ, seq);           // arm (seq last)
    for (int i = 0; i < 400; i++) { if (U(O_ACK) == seq) return U(O_STATUS); System.Threading.Thread.Sleep(5); }
    Console.WriteLine("[timeout] sem ack — heartbeat vivo? hook instalado?"); return 0xFFFFFFFF;
}

uint ParseHex(string s) => uint.Parse(s.Replace("0x",""), NumberStyles.HexNumber);
uint ParseUInt(string s) {
    if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) {
        return uint.Parse(s.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
    }
    return uint.Parse(s, NumberStyles.Integer, CultureInfo.InvariantCulture);
}

if (mode == "read") {
    uint rva = ParseHex(args[1]); uint len = uint.Parse(args[2]);
    uint addr = baseAddr + rva;
    uint st = Arm(1, addr, len, 0, 0, 0, 0);
    Console.WriteLine($"READ addr=0x{addr:X8} (base+0x{rva:X}) len={len} status={st} err=0x{U(O_ERR):X8}");
    if (st == 1) {
        var sb = new System.Text.StringBuilder();
        for (int i = 0; i < len && i < 512; i++) sb.Append(a.ReadByte(O_BUF + i).ToString("X2")).Append(' ');
        Console.WriteLine("  " + sb);
    }
    return 0;
}

if (mode == "u1") {
    const int U1RecordSize = 52;
    string sub = args.Length > 1 ? args[1].ToLowerInvariant() : "";
    if (sub == "start" && args.Length == 3) {
        uint family = ParseUInt(args[2]);
        uint st = Arm(17, 0, 0, 0, family, 0, 0);
        Console.WriteLine($"U1 START family={family} status={st} hook=0x{U(O_ADDR):X8}");
        return st == 1 ? 0 : 1;
    }
    if (sub == "stop") {
        uint st = Arm(18, 0, 0, 0, 0, 0, 0);
        Console.WriteLine($"U1 STOP status={st} records={(int)U(O_RET)}");
        return st == 1 ? 0 : 1;
    }
    if (sub == "drain") {
        string outPath = args.Length > 2 ? args[2] : "u1-capture.json";
        var records = new System.Collections.Generic.List<object>();
        uint start = 0;
        uint total = 0;
        while (true) {
            uint st = Arm(19, 0, 0, 0, start, 0, 0);
            if (st != 1) break;
            uint copied = U(O_RET);
            total = U(O_LEN);
            for (uint index = 0; index < copied; index++) {
                int offset = O_BUF + (int)index * U1RecordSize;
                byte[] programBytes = new byte[32];
                for (int byteIndex = 0; byteIndex < programBytes.Length; byteIndex++) {
                    programBytes[byteIndex] = a.ReadByte(offset + 20 + byteIndex);
                }
                records.Add(new {
                    frame = a.ReadUInt32(offset),
                    family = a.ReadUInt32(offset + 4),
                    context = $"0x{a.ReadUInt32(offset + 8):X8}",
                    program = $"0x{a.ReadUInt32(offset + 12):X8}",
                    slot = $"0x{a.ReadUInt32(offset + 16):X8}",
                    programBytesHex = Convert.ToHexString(programBytes)
                });
            }
            start += copied;
            if (copied == 0 || start >= total) break;
        }
        File.WriteAllText(outPath, System.Text.Json.JsonSerializer.Serialize(
            new { total, records },
            new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"U1 DRAIN records={records.Count} total={total} -> {outPath}");
        return 0;
    }
    Console.WriteLine("uso: u1 start <1=SclMove|2=SclAccele|3=AngMove|4=Move|5=Accele|6=AngAccele|7=Point|8=Angle|9=Scale> | u1 stop | u1 drain <out.json>");
    return 1;
}

if (mode == "texlog") {
    // texture-LOAD auto-logger (inline-hook on FFX_Ps3Data_BuildTextureSlotRecord_LoadTime @ RVA 0x2451F0)
    string sub = args.Length > 1 ? args[1].ToLowerInvariant() : "";
    if (sub == "start") {
        uint st = Arm(6, 0, 0, 0, 0, 0, 0);
        Console.WriteLine($"TEXLOG START -> status={st} installed={(int)U(O_RET)} (hook em 0x{baseAddr + 0x2451F0:X8})");
        Console.WriteLine(st == 1 ? "  ARMADO: carrega/troca de area; cada textura carregada (load-time) e logada com o path completo." : "  FALHOU: o hook nao instalou (status!=OK).");
        return st == 1 ? 0 : 1;
    }
    if (sub == "stop") {
        uint st = Arm(7, 0, 0, 0, 0, 0, 0);
        Console.WriteLine($"TEXLOG STOP -> status={st} records={(int)U(O_RET)}");
        return 0;
    }
    if (sub == "drain") {
        string outPath = args.Length > 2 ? args[2] : "texlog.csv";
        var lines = new System.Collections.Generic.List<string>();
        lines.Add("frame,name");
        uint start = 0, total = 0; int safety = 0;
        while (safety++ < 500000) {
            uint st = Arm(8, 0, 0, 0, start, 0, 0);
            if (st != 1) { Console.WriteLine($"  drain status={st} (parou)"); break; }
            uint copied = U(O_RET); total = U(O_LEN);
            if (copied == 0) break;
            for (uint k = 0; k < copied; k++) {
                int bo = O_BUF + (int)k * 128;
                uint frame = a.ReadUInt32(bo + 0);
                var sb = new System.Text.StringBuilder();
                for (int j = 0; j < 124; j++) { byte ch = a.ReadByte(bo + 4 + j); if (ch == 0) break; sb.Append((char)ch); }
                lines.Add($"{frame},{sb}");
            }
            start += copied;
            if (start >= total) break;
        }
        System.IO.File.WriteAllLines(outPath, lines);
        var uniq = new System.Collections.Generic.HashSet<string>();
        for (int li = 1; li < lines.Count; li++) { int c = lines[li].IndexOf(','); if (c >= 0) uniq.Add(lines[li].Substring(c + 1)); }
        Console.WriteLine($"TEXLOG DRAIN -> {lines.Count - 1} records (total reportado={total}), {uniq.Count} texturas unicas -> {outPath}");
        return 0;
    }
    Console.WriteLine("uso: texlog start | texlog stop | texlog drain <out.csv>");
    return 1;
}

if (mode == "call") {
    uint rva = ParseHex(args[1]); uint abi = uint.Parse(args[2]);
    uint a0 = args.Length>3 ? uint.Parse(args[3]) : 0;
    uint a1 = args.Length>4 ? uint.Parse(args[4]) : 0;
    uint a2;
    if (abi == 4 /*IIF*/ && args.Length>5) a2 = BitConverter.ToUInt32(BitConverter.GetBytes(float.Parse(args[5], CultureInfo.InvariantCulture)));
    else a2 = args.Length>5 ? uint.Parse(args[5]) : 0;
    uint addr = baseAddr + rva;
    uint st = Arm(3, addr, 0, abi, a0, a1, a2);
    Console.WriteLine($"CALL addr=0x{addr:X8} (base+0x{rva:X}) abi={abi} -> status={st} ret={(int)U(O_RET)} (0x{U(O_RET):X8}) err=0x{U(O_ERR):X8}");
    return 0;
}

if (mode == "soundcmd") {
    if (args.Length < 3) {
        Console.WriteLine("uso: soundcmd <cmd> <param0> [param1]  # ex: soundcmd 23 2 0");
        return 1;
    }
    uint cmd = ParseUInt(args[1]);
    uint param0 = ParseUInt(args[2]);
    uint param1 = args.Length > 3 ? ParseUInt(args[3]) : 0;
    uint st = Arm(9, 0, 0, 0, cmd, param0, param1);
    Console.WriteLine($"SOUNDCMD cmd={cmd} param0={param0} param1={param1} -> status={st} ret={(int)U(O_RET)} (0x{U(O_RET):X8}) err=0x{U(O_ERR):X8}");
    return st == 1 ? 0 : 1;
}

if (mode == "write") {
    uint rva = ParseHex(args[1]);
    string hx = args[2].Replace(" ", "").Replace("0x", "");
    int len = hx.Length / 2;
    for (int i = 0; i < len && i < 512; i++) a.Write(O_BUF + i, Convert.ToByte(hx.Substring(i * 2, 2), 16));
    uint addr = baseAddr + rva;
    uint st = Arm(2, addr, (uint)len, 0, 0, 0, 0);
    Console.WriteLine($"WRITE addr=0x{addr:X8} (base+0x{rva:X}) len={len} bytes={hx} status={st} err=0x{U(O_ERR):X8}");
    return 0;
}

if (mode == "forcebattle") {
    // ATOMIC opcode (op=4): the module sets the scripted-encounter flags + calls MsBattleEncountExe
    // in ONE main-thread frame (the game clears the flag every frame, so it must be same-frame).
    uint field = args.Length>1 ? uint.Parse(args[1]) : 0;
    uint group = args.Length>2 ? uint.Parse(args[2]) : 0;
    uint formation = args.Length>3 ? uint.Parse(args[3]) : 0;
    uint st = Arm(4 /*FORCEBATTLE*/, 0, 0, 0, field, group, formation);
    int ret = (int)U(O_RET);
    Console.WriteLine($"FORCEBATTLE(field={field}, group={group}, formation={formation}) -> status={st} ret={ret} (0x{U(O_RET):X8}) err=0x{U(O_ERR):X8}");
    Console.WriteLine(ret == -1 ? "  ret=-1 => ENCONTRO ENFILEIRADO (batalha deve comecar!)" : "  ret=0 => nao disparou (field/group invalido?)");
    return 0;
}

// ---- live skeleton capture (ground-truth mocap from the running game) ----
// Anchors proved in IDA (FFX.exe base 0x400000), ver docs/reverse/FFX_IDA_SKINPALETTE_CAPTURE_RENAME_QUEUE_2026-06-04.md:
//   FFX_ActiveChrInstanceCount @ VA 0x23C44E0 (u32 count/capacity)
//   FFX_ActiveChrInstanceTable @ VA 0x23C44E4 (in-place array, stride 0x880=2176)
//   per inst: id=u16@+0  active=byte@+2  skel=ptr@+448 (boneCount=u16@skel+10)  bonePoseArray=ptr@+808
//   per bone i: world 4x4 (64B, row-major) = [bonePoseArray + 352*i + 136]
const uint IMAGE_BASE = 0x400000;
const uint VA_COUNT   = 0x23C44E0;
const uint VA_TABLE   = 0x23C44E4;
const uint RVA_BATTLE_PLAYER_LIST = 0xD334CC;
const uint RVA_BATTLE_ENEMY_LIST  = 0xD34460;
const int  BATTLE_CHR_STRIDE = 0xF90;
const int  INST_STRIDE= 2176;
// --- mocap harness anchors (workflow ffx-mocap-automation-re, 2026-06-04) ---
const uint VA_AllocInstance   = 0x825010; // cdecl(packedId)->inst
const uint VA_DisposeInstance = 0x826770; // (esi dead) cdecl-callable(inst)
const uint VA_ResetPlayback   = 0x82B100; // cdecl(inst)
const uint VA_SelectMotion    = 0x837D80; // cdecl(inst, layer, clipIndex) -> starts clip (auto rig)
const uint VA_MseqGroupPool   = 0x1301190;// u32 head of clip group pool
const uint VA_BindMseq        = 0x837B40; // cdecl(inst, packedId=(rig<<16)|clipId)
const uint VA_InitChannels    = 0x839A00; // cdecl(inst, mseqBlock) <- DEVE receber *(record+12), NAO o script-ptr
const uint VA_ResolveClipRecord = 0x8378E0; // FFX_Mseq_ResolveActiveClipRecordFromScript: cdecl(inst)->clipRecord; anda o script ate opcode PLAY e *(record+12)=channel-mode block REAL (PART A fix 2026-06-04)
const uint VA_EnsureMotion    = 0x8368F0; // cdecl(resourceId, variant) -> carrega+registra o .mgrp residente
const uint VA_UpdateMseq      = 0x838D10; // cdecl(inst, evalFlag) -> avanca/avalia canais no cursor
const uint VA_RebuildPose     = 0x8327E0; // cdecl(inst) -> reconstroi matrizes + m_currentPose
const uint VA_GetPartySlot    = 0x794030; // cdecl(slot) -> ptr slotData (+0=inst cache, +4=charId)
// --- field-model swap anchors (docs/reverse/FFX_FIELD_PLAYER_SPAWN_RENAME_QUEUE_2026-06-04.md) ---
const uint VA_SetControlled    = 0x82DB50; // FFX_Chr_SetControlledInstance: cdecl(inst) -> writes g_FFX_ControlledChrInstance(0x1300740), zera acumuladores de input
const uint VA_GetFieldActor    = 0x86A7C0; // FFX_Field_GetActorByIndex: cdecl(index) -> ptr actor (faz a walk segmentada strides 2904/1368/1464/744/48)
const uint VA_FieldActorMgr    = 0x1326AE8;// g_FFX_FieldActorManager (+10=u16 controlled key, +12=u16 actor count, +28=ptr table base)
const int  OFF_ACTOR_MATCHKEY  = 46;       // actor+46 (u16): casa contra g_FFX_FieldActorManager+10 -> identifica o ator controlado
const int  OFF_ACTOR_INST      = 0x9C;     // actor+156: ptr da FFX active instance daquele ator -> ALIMENTA o seletor por-frame 0x871A50
const int  OFF_MSEQBLK        = 0x714;    // inst+1812 = ptr mseq channel block (set by BindMseq)
const int  OFF_FRAMERATE      = 2016;     // =7680
const int  OFF_LOOPCNT        = 1828;     // u16 loops
const int  OFF_LOOPMODE       = 1830;     // u16 (-1 reset)
const int  OFF_LOOPSPAN       = 1824;
const int  OFF_SMOOTH         = 1874;
// clock/playback instance field byte-offsets
const int  OFF_CURSOR   = 1856; // u32 fixed-point .8 (intFrame = >>8)
const int  OFF_LASTCUR  = 1820; // u32 (totalFrames<<8)-1
const int  OFF_STARTCUR = 1816; // u32
const int  OFF_PLAYING  = 1832; // u16 (1 playing, 0 ended this frame)
const int  OFF_LOOPS    = 1828; // u16 remaining loops
const int  OFF_SPEEDB   = 1876; // i16 (WRITE 0 = freeze, no smear)
const int  OFF_CURCLIP  = 1836; // u32 current packed mseq id

// READ helper: arms an absolute-address READ and returns the bytes, or null on SEH/AV (status!=OK).
// The probe SEH-guards the read; a bad pointer yields status=2/err=0xC0000005 -> we skip, never crash.
byte[]? ReadAbs(uint absAddr, int len) {
    if (len > 512) len = 512;
    uint st = Arm(1, absAddr, (uint)len, 0, 0, 0, 0);
    if (st != 1) return null;
    var b = new byte[len];
    for (int i = 0; i < len; i++) b[i] = a.ReadByte(O_BUF + i);
    return b;
}
uint? Ru32(uint absAddr) { var b = ReadAbs(absAddr, 4); return b == null ? (uint?)null : BitConverter.ToUInt32(b, 0); }
ushort? Ru16(uint absAddr) { var b = ReadAbs(absAddr, 2); return b == null ? (ushort?)null : BitConverter.ToUInt16(b, 0); }
// le count matrizes de 64B em blocos de 512B (8 matrizes/READ) -> ~8x menos round-trips que 1-por-vez.
byte[] ReadMatrices(uint dataPtr, int count) {
    int total = count * 64; var all = new byte[total];
    for (int off = 0; off < total; off += 512) { int len = Math.Min(512, total - off); var ch = ReadAbs(dataPtr + (uint)off, len); if (ch != null) Array.Copy(ch, 0, all, off, ch.Length); }
    return all;
}
uint AbsOf(uint va) => baseAddr + (va - IMAGE_BASE);   // static global -> runtime (ASLR-relocated)
// plausible heap/data pointer (excludes 0, low pages, and uninitialized fills like 0x8B8B8B8B/0xCDCDCDCD/0xDDDDDDDD)
bool PtrOk(uint p) => p >= 0x10000 && p < 0x7FFF0000 && (p & 0xFF) != 0x8B && p != 0xCDCDCDCD && p != 0xDDDDDDDD && p != 0xFEEEFEEE;
// WRITE helper: arms an absolute-address WRITE of raw bytes (len<=512). Returns status.
uint WriteAbs(uint absAddr, byte[] bytes) {
    int len = Math.Min(bytes.Length, 512);
    for (int i = 0; i < len; i++) a.Write(O_BUF + i, bytes[i]);
    return Arm(2, absAddr, (uint)len, 0, 0, 0, 0);
}
uint WriteU32(uint absAddr, uint v) => WriteAbs(absAddr, BitConverter.GetBytes(v));
uint WriteI16(uint absAddr, short v) => WriteAbs(absAddr, BitConverter.GetBytes(v));
// CALL helper: arms a main-thread CALL of a function VA with up to 3 int args. Returns (status, ret).
(uint st, int ret) CallVA(uint funcVA, uint abi, uint a0, uint a1, uint a2) {
    uint st = Arm(3, AbsOf(funcVA), 0, abi, a0, a1, a2);
    return (st, (int)U(O_RET));
}
// resolve a target: "@<hexAddr>" = absolute instance address; senao = id (acha a 1a instancia com esse id).
// Por seguranca, ResolveInst(id) PULA a instancia controlada (g_FFX_ControlledChrInstance @0x1300740)
// se houver outra com o mesmo id (assim captura na scratch, nao no personagem que o jogador controla).
uint ResolveTarget(string s) {
    if (s.StartsWith("@")) { try { return ParseHex(s.Substring(1)); } catch { return 0; } }
    return ResolveInst((int)ParseHex(s));
}
uint ControlledInst() { var c = Ru32(AbsOf(0x1300740)); return (c != null && PtrOk(c.Value)) ? c.Value : 0; }
// resolve the absolute address of the first active skinned instance with id==idFilter (or first skinned if null)
uint ResolveInst(int? idFilter) {
    var count = Ru32(AbsOf(VA_COUNT)); var tb = Ru32(AbsOf(VA_TABLE));
    if (count == null || tb == null || !PtrOk(tb.Value)) return 0;
    uint ctrl = ControlledInst();
    uint firstMatch = 0;
    for (uint idx = 0; idx < count.Value && idx < 4096; idx++) {
        uint inst = tb.Value + (uint)(INST_STRIDE * idx);
        var info = InstInfo(inst);
        if (info == null) continue;
        if (idFilter.HasValue && info.Value.id != idFilter.Value) continue;
        if (firstMatch == 0) firstMatch = inst;
        if (inst != ctrl) return inst;   // prefere NAO-controlada (scratch) p/ nao mexer no player
    }
    return firstMatch;   // so a controlada existe -> retorna ela (ex.: leituras read-only)
}

// reads one instance's summary; returns null if the slot isn't a valid skinned character.
(ushort id, ushort bones, uint poseArr, uint skel)? InstInfo(uint inst) {
    var act = ReadAbs(inst + 2, 1); if (act == null || act[0] == 0) return null;
    var id = Ru16(inst); var skel = Ru32(inst + 448); var poseArr = Ru32(inst + 808);
    if (id == null || skel == null || poseArr == null) return null;
    if (!PtrOk(skel.Value) || !PtrOk(poseArr.Value)) return null;
    var bones = Ru16(skel.Value + 10);
    if (bones == null || bones.Value == 0 || bones.Value > 512) return null;
    return (id.Value, bones.Value, poseArr.Value, skel.Value);
}

float Rf32(uint absAddr) { var b = ReadAbs(absAddr, 4); return b == null ? float.NaN : BitConverter.ToSingle(b, 0); }
byte? Ru8(uint absAddr) { var b = ReadAbs(absAddr, 1); return b == null ? (byte?)null : b[0]; }
string F(float v) => float.IsFinite(v) ? v.ToString("0.###", CultureInfo.InvariantCulture) : "NaN";

if (mode == "arena-flags") {
    // READ-ONLY Arena+ pre-RT2 snapshot. Offsets came from the nagi0700/Arena research pass:
    // captures[104], conquest/special unlocks[35], dark-aeon defeat flags[9].
    // Use before/after around a disposable save to prove which bytes move in-game.
    bool showAll = args.Any(x => x.Equals("--all", StringComparison.OrdinalIgnoreCase));
    const uint RVA_ARENA_CAPTURE_COUNTS = 0x00D30C9C;
    const uint RVA_ARENA_UNLOCK_FLAGS = 0x00D30D04;
    const uint RVA_DARK_AEON_FLAGS_BASE = 0x00D2D759; // FFXED save+3273, bit7 per boss
    const uint RVA_PENANCE_LEGACY_FLAG = 0x00D2E38C; // save+0x18FC
    const int CAPTURE_COUNT_LEN = 104;
    const int UNLOCK_FLAG_LEN = 35;
    const int DARK_FLAG_LEN = 9;
    string[] darkNames =
    {
        "Dark Valefor", "Dark Ifrit", "Dark Ixion", "Dark Shiva", "Dark Bahamut",
        "Dark Yojimbo", "Dark Anima", "Dark Magus Sisters", "Penance"
    };

    if (baseAddr == 0) {
        Console.WriteLine("arena-flags: moduleBase=0; aguarde o probe anexar e rode `ffxprobectl mon` primeiro.");
        return 2;
    }

    string HexLine(byte[] bytes) => BitConverter.ToString(bytes).Replace("-", " ");

    byte[]? ReadArenaBlock(string label, uint rvaValue, int len) {
        uint abs = baseAddr + rvaValue;
        var bytes = ReadAbs(abs, len);
        if (bytes == null) {
            Console.WriteLine($"{label}: READ falhou rva=0x{rvaValue:X8} abs=0x{abs:X8} len={len} err=0x{U(O_ERR):X8}");
            return null;
        }
        Console.WriteLine($"{label}: rva=0x{rvaValue:X8} abs=0x{abs:X8} len={bytes.Length}");
        Console.WriteLine($"  raw: {HexLine(bytes)}");
        return bytes;
    }

    void DumpIndexed(string label, byte[]? bytes, bool all) {
        if (bytes == null) return;
        int nonzero = bytes.Count(x => x != 0);
        int total = bytes.Sum(x => (int)x);
        Console.WriteLine($"  {label}: nonzero={nonzero} sum={total}");
        for (int i = 0; i < bytes.Length; i++) {
            if (!all && bytes[i] == 0) continue;
            Console.WriteLine($"    [{i:000}] = {bytes[i]} (0x{bytes[i]:X2})");
        }
    }

    Console.WriteLine($"arena-flags base=0x{baseAddr:X8} showAll={showAll} readOnly=1");
    var captures = ReadArenaBlock("arena.captureCounts", RVA_ARENA_CAPTURE_COUNTS, CAPTURE_COUNT_LEN);
    DumpIndexed("captureCounts", captures, showAll);

    var unlocks = ReadArenaBlock("arena.unlockFlags", RVA_ARENA_UNLOCK_FLAGS, UNLOCK_FLAG_LEN);
    DumpIndexed("unlockFlags", unlocks, showAll);

    var darkBytes = ReadArenaBlock("arena.darkAeonRaw", RVA_DARK_AEON_FLAGS_BASE, 8);
    if (darkBytes != null) {
        int mask = 0;
        for (int i = 0; i < darkBytes.Length && i < darkNames.Length - 1; i++) {
            bool defeated = ((darkBytes[i] >> 7) & 1) != 0;
            if (defeated) mask |= (1 << i);
            Console.WriteLine($"    [{i}] {darkNames[i],-18} raw=0x{darkBytes[i]:X2} defeated(bit7)={defeated}");
        }
        var pen = ReadAbs(baseAddr + RVA_PENANCE_LEGACY_FLAG, 1);
        if (pen != null) {
            bool penDef = pen[0] != 0;
            if (penDef) mask |= (1 << 8);
            Console.WriteLine($"    [8] {darkNames[8],-18} raw=0x{pen[0]:X2} defeated(nonzero)={penDef}");
        }
        Console.WriteLine($"  darkAeons: mask=0x{mask:X3} (FFXED bit7 @ save+3273..3280)");
    }
    Console.WriteLine("arena-flags: nenhuma escrita feita; salve os snapshots before/after para RT2.");
    return 0;
}

if (mode == "battle-actors") {
    // READ-ONLY battle actor dump for Aurora camera/overlay work.
    // Proven list layout: docs/reverse/FFX_BATTLE_TRACKER_LIVE_READ_PROVEN_2026-06-07.md.
    uint? playerBaseN = Ru32(baseAddr + RVA_BATTLE_PLAYER_LIST);
    uint? enemyBaseN = Ru32(baseAddr + RVA_BATTLE_ENEMY_LIST);
    Console.WriteLine($"battle-actors base=0x{baseAddr:X8}");
    Console.WriteLine($"  playerList[0x{RVA_BATTLE_PLAYER_LIST:X}] -> 0x{playerBaseN?.ToString("X8") ?? "????????"}");
    Console.WriteLine($"  enemyList [0x{RVA_BATTLE_ENEMY_LIST:X}] -> 0x{enemyBaseN?.ToString("X8") ?? "????????"}");

    void DumpList(string tag, uint? baseN, int count) {
        if (baseN == null || !PtrOk(baseN.Value)) { Console.WriteLine($"  {tag}: ponteiro invalido"); return; }
        for (int i = 0; i < count; i++) {
            uint chr = baseN.Value + (uint)(i * BATTLE_CHR_STRIDE);
            ushort id = Ru16(chr + 0x0E) ?? 0;
            uint maxHp = Ru32(chr + 0x594) ?? 0;
            uint hp = Ru32(chr + 0x5D0) ?? 0;
            uint currentHp = Ru32(chr + 0x6E4) ?? 0;
            byte inBattle = Ru8(chr + 0xDC8) ?? 0;
            byte placed = Ru8(chr + 0x6D4) ?? 0;
            float height = Rf32(chr + 0x534);
            float x = Rf32(chr + 0x3B0), y = Rf32(chr + 0x3B4), z = Rf32(chr + 0x3B8), w = Rf32(chr + 0x3BC);
            float cx = Rf32(chr + 0x3C0), cy = Rf32(chr + 0x3C4), cz = Rf32(chr + 0x3C8), cw = Rf32(chr + 0x3CC);
            bool plausible = inBattle != 0 || id > 0 && id != 0xFFFF || (float.IsFinite(x) && (Math.Abs(x) + Math.Abs(y) + Math.Abs(z)) > 0.01f);
            if (!plausible && args.Length <= 1) continue;
            Console.WriteLine(
                $"  {tag}[{i:00}] chr=0x{chr:X8} id=0x{id:X4} inBattle={inBattle} hp={currentHp}/{maxHp} rawHp=0x{hp:X8} " +
                $"placed=0x{placed:X2} height={F(height)} pos3B0=({F(x)},{F(y)},{F(z)},{F(w)}) pos3C0=({F(cx)},{F(cy)},{F(cz)},{F(cw)})");
        }
    }

    DumpList("P", playerBaseN, 18);
    DumpList("E", enemyBaseN, 11);
    Console.WriteLine("  offsets: id@0xE, maxHp@0x594, hp@0x5D0, currentHp@0x6E4, placed@0x6D4, inBattle@0xDC8, world/cache candidates @0x3B0/@0x3C0.");
    return 0;
}

if (mode == "battle-actor-indexes") {
    // READ-ONLY: calls FFX_Battle_GetActorByIndex(idx) and dumps the returned actor row.
    // Useful because the visual actor accessor is the IDA-proven entrypoint; list pointers alone can be misleading.
    const uint VA_BattleGetActorByIndex = 0x794030;
    int first = args.Length > 1 ? int.Parse(args[1], CultureInfo.InvariantCulture) : 0;
    int count = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 32;
    if (count < 1) count = 1;
    if (count > 96) count = 96;
    Console.WriteLine($"battle-actor-indexes first={first} count={count} base=0x{baseAddr:X8}");
    for (int i = first; i < first + count; i++) {
        var r = CallVA(VA_BattleGetActorByIndex, 1, (uint)i, 0, 0);
        uint chr = (uint)r.ret;
        if (r.st != 1 || !PtrOk(chr)) {
            Console.WriteLine($"  idx[{i:00}] st={r.st} ret=0x{chr:X8} invalido");
            continue;
        }
        ushort id = Ru16(chr + 0x0E) ?? 0;
        byte inBattle = Ru8(chr + 0xDC8) ?? 0;
        byte placed = Ru8(chr + 0x6D4) ?? 0;
        uint maxHp = Ru32(chr + 0x594) ?? 0;
        uint currentHp = Ru32(chr + 0x6E4) ?? 0;
        float height = Rf32(chr + 0x534);
        float x = Rf32(chr + 0x3B0), y = Rf32(chr + 0x3B4), z = Rf32(chr + 0x3B8), w = Rf32(chr + 0x3BC);
        float cx = Rf32(chr + 0x3C0), cy = Rf32(chr + 0x3C4), cz = Rf32(chr + 0x3C8), cw = Rf32(chr + 0x3CC);
        Console.WriteLine(
            $"  idx[{i:00}] chr=0x{chr:X8} id=0x{id:X4} inBattle={inBattle} hp={currentHp}/{maxHp} " +
            $"placed=0x{placed:X2} height={F(height)} pos3B0=({F(x)},{F(y)},{F(z)},{F(w)}) pos3C0=({F(cx)},{F(cy)},{F(cz)},{F(cw)})");
    }
    return 0;
}

if (mode == "open-popup") {
    // LAB POC (native UI via main-thread CALL): spawn a native FFX yes/no popup.
    //   FFX_Menu_OpenYesNoPopup @ VA 0x8E33A0, int __cdecl(int textPtr, int yesText, int noText) -> 0x8E33D0(...,0).
    //   Self-contained: Alloc(0x8AA150)+Reset+set update(0x8E2E20)/draw(0x8E2F80)/aux callbacks+RegisterAndEnter(0x8AAAB0).
    //   PRECONDITION: must run on the main thread WHILE a menu/field screen has the menu subsystem live
    //   (g_FFX_MenuSubsystemActive @ VA 0x13407E4 set). It will NOT draw from the title or in battle.
    //   yes==0 -> 1-button OK popup (the builder sets +62 = (yes==0)+2 = 3, a pumped layer).
    //   Default textVA 0xB66924 = "scene7\0" (clean FFX string, no control bytes). Pass a hex VA to use another string.
    //   Evidence: docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md.
    uint textVa = args.Length > 1 ? ParseHex(args[1]) : 0xB66924;
    uint absText = AbsOf(textVa);
    var r = CallVA(0x8E33A0, 3 /*cdecl(int,int,int)*/, absText, 0, 0);
    Console.WriteLine($"OPEN-POPUP fn=0x{AbsOf(0x8E33A0):X8} textVA=0x{textVa:X} absText=0x{absText:X8} -> status={r.st} ret={r.ret} (0x{(uint)r.ret:X8}) err=0x{U(O_ERR):X8}");
    Console.WriteLine(r.st == 1
        ? "  status=OK (ret = menu-object handle). Se o pump de menu estiver vivo, um popup NATIVO deve aparecer na tela."
        : "  status!=OK: SEH pegou — provavel pool cheio ou contexto de menu inativo. Abra a tela de menu do jogo e tente de novo.");
    return r.st == 1 ? 0 : 1;
}

if (mode == "open-popup-text") {
    // LAB POC (degrau 2 - texto nosso): native FFX popup showing OUR custom ASCII text.
    //   Writes the string to a VERIFIED-SAFE scratch buffer (VA 0x25D6E00 / RVA 0x21D6E00: writable .data tail,
    //   1536 zero bytes, 0 xrefs; HARD CAP <512 bytes before read-only .rodata @0x25D7000) via OP_WRITE,
    //   then CALLs FFX_Menu_OpenYesNoPopup(0x8E33A0)(textPtr,0,0). FFX renderer: bytes 0x24-0x7E = normal glyph,
    //   stops at 0x00; we map anything else (incl. space) to '_' so it always renders clean.
    //   Run on the main thread WHILE inside a game menu/field screen (menu subsystem live).
    //   Evidence: docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md + workflow find-safe-scratch-buffer.
    const uint SCRATCH_VA = 0x25D6E00;
    string text = args.Length > 1 ? string.Join(" ", args[1..]) : "JARVIS";
    if (text.Length > 120) text = text.Substring(0, 120);   // stay well under the 512-byte .rodata boundary
    // FFX font glyph encoding FULLY MAPPED (IDA + live sweep 2026-06-09): glyphIndex = byte - 0x30 into a
    //   9-col runtime atlas (FFX_EventText_LayoutString @0x8B71F0). The atlas is ASCII-ordered but DIGITS FIRST:
    //   idx 0..9='0'..'9', 10=space, 11..31=punct !"#$%&'()*+,-./:;<=>?, 32..57='A'..'Z', 58..63=[\]^_`,
    //   64..89='a'..'z'. So char->byte = 0x30 + index in FFX_ATLAS. Confirmed live: digits+space+!"#$%&'()
    //   (bytes 0x30-0x43), / : = ?, and A-Z (0x50-0x69); lowercase/brackets = strong ASCII-order inference.
    const string FFX_ATLAS = "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    var glyphs = new System.Collections.Generic.List<byte>();
    foreach (char c in text) {
        int gi = FFX_ATLAS.IndexOf(c);
        if (gi < 0) gi = 10;           // unknown char -> space (atlas index 10 = byte 0x3A)
        glyphs.Add((byte)(0x30 + gi));
    }
    glyphs.Add(0);
    var bytes = glyphs.ToArray();
    uint absText = AbsOf(SCRATCH_VA);
    uint wst = WriteAbs(absText, bytes);
    if (wst != 1) { Console.WriteLine($"OPEN-POPUP-TEXT write FALHOU status={wst} err=0x{U(O_ERR):X8} (jogo aberto? probe vivo?)"); return 1; }
    var r = CallVA(0x8E33A0, 3 /*cdecl(int,int,int)*/, absText, 0, 0);
    Console.WriteLine($"OPEN-POPUP-TEXT text=\"{text}\" scratch=0x{absText:X8} fn=0x{AbsOf(0x8E33A0):X8} -> status={r.st} ret={r.ret} (0x{(uint)r.ret:X8}) err=0x{U(O_ERR):X8}");
    Console.WriteLine(r.st == 1
        ? "  status=OK: com a tela de menu do jogo aberta, um popup NATIVO deve aparecer com o SEU texto."
        : "  status!=OK: SEH pegou (pool cheio/contexto inativo). Abra a tela de menu do jogo e tente de novo.");
    return r.st == 1 ? 0 : 1;
}

if (mode == "inst-roots") {
    // READ-ONLY: dump active skinned chr instance placement roots.
    // Instance offsets: docs/reverse/FFX_NPC_ANATOMY_MASTER_2026-06-05.md.
    var count = Ru32(AbsOf(VA_COUNT));
    var tableBaseN = Ru32(AbsOf(VA_TABLE));
    Console.WriteLine($"inst-roots count={count} table=0x{tableBaseN?.ToString("X8") ?? "????????"} base=0x{baseAddr:X8}");
    if (count == null || count == 0 || count > 4096 || tableBaseN == null || !PtrOk(tableBaseN.Value)) {
        Console.WriteLine("inst-roots: tabela invalida");
        return 2;
    }

    string MatT(uint abs) {
        var b = ReadAbs(abs, 64);
        if (b == null) return "read-fail";
        float[] m = new float[16];
        for (int i = 0; i < 16; i++) m[i] = BitConverter.ToSingle(b, i * 4);
        return $"rowT=({F(m[12])},{F(m[13])},{F(m[14])}) colT=({F(m[3])},{F(m[7])},{F(m[11])})";
    }

    for (uint idx = 0; idx < count.Value; idx++) {
        uint inst = tableBaseN.Value + (uint)(INST_STRIDE * idx);
        var info = InstInfo(inst);
        if (info == null) continue;
        float px = Rf32(inst + 0x0C), py = Rf32(inst + 0x10), pz = Rf32(inst + 0x14), pw = Rf32(inst + 0x18);
        float mx = Rf32(inst + 0x1C), my = Rf32(inst + 0x20), mz = Rf32(inst + 0x24), mw = Rf32(inst + 0x28);
        float sx = Rf32(inst + 0x5C), sy = Rf32(inst + 0x60), sz = Rf32(inst + 0x64);
        float ox = Rf32(inst + 0x6C), oy = Rf32(inst + 0x70), oz = Rf32(inst + 0x74);
        float yaw = Rf32(inst + 0x158);
        uint flags = Ru32(inst + 0x194) ?? 0;
        Console.WriteLine($"  inst[{idx:000}] @0x{inst:X8} id=0x{info.Value.id:X4} bones={info.Value.bones}");
        Console.WriteLine($"    pos0C=({F(px)},{F(py)},{F(pz)},{F(pw)}) prev1C=({F(mx)},{F(my)},{F(mz)},{F(mw)}) scale5C=({F(sx)},{F(sy)},{F(sz)}) off6C=({F(ox)},{F(oy)},{F(oz)}) yaw158={F(yaw)} flags194=0x{flags:X8}");
        Console.WriteLine($"    mtx1D0 {MatT(inst + 0x1D0)}");
        Console.WriteLine($"    mtx210 {MatT(inst + 0x210)}");
        Console.WriteLine($"    mtx250 {MatT(inst + 0x250)}");
        var pose0 = ReadAbs(info.Value.poseArr + 136, 64);
        if (pose0 != null) {
            float b0x = BitConverter.ToSingle(pose0, 48), b0y = BitConverter.ToSingle(pose0, 52), b0z = BitConverter.ToSingle(pose0, 56);
            Console.WriteLine($"    bone0@pose+136 rowT=({F(b0x)},{F(b0y)},{F(b0z)}) poseArr=0x{info.Value.poseArr:X8}");
        }
    }
    return 0;
}

if (mode == "scan-instances") {
    var count = Ru32(AbsOf(VA_COUNT));
    Console.WriteLine($"FFX_ActiveChrInstanceCount={count} (base=0x{baseAddr:X8})");
    if (count == null || count == 0 || count > 4096) { Console.WriteLine("contagem implausivel — jogo carregado? em campo/batalha com personagens?"); return 0; }
    var tableBaseN = Ru32(AbsOf(VA_TABLE));   // VA_TABLE holds a POINTER to the heap table
    if (tableBaseN == null || !PtrOk(tableBaseN.Value)) { Console.WriteLine($"tablePtr invalido (0x{tableBaseN?.ToString("X8")??"?"})"); return 2; }
    uint tableBase = tableBaseN.Value;
    Console.WriteLine($"tablePtr=0x{tableBase:X8}");
    bool verbose = args.Length > 1 && args[1] == "-v";
    int shown = 0, activeSlots = 0;
    for (uint idx = 0; idx < count.Value; idx++) {
        uint inst = tableBase + (uint)(INST_STRIDE * idx);
        var act = ReadAbs(inst + 2, 1);
        if (act == null || act[0] == 0) continue;
        activeSlots++;
        var id = Ru16(inst); var skel = Ru32(inst + 448); var poseArr = Ru32(inst + 808);
        var info = InstInfo(inst);
        if (info != null) {
            shown++;
            Console.WriteLine($"  inst[{idx}] @0x{inst:X8} id=0x{info.Value.id:X4}({info.Value.id}) boneCount={info.Value.bones} poseArr=0x{info.Value.poseArr:X8} skel=0x{info.Value.skel:X8}  <-- VALIDO");
        } else if (verbose) {
            Console.WriteLine($"  inst[{idx}] @0x{inst:X8} active id=0x{(id?.ToString("X4")??"??")} skel=0x{(skel?.ToString("X8")??"????????")} poseArr=0x{(poseArr?.ToString("X8")??"????????")}  (rejeitado)");
        }
    }
    Console.WriteLine($"-> {activeSlots} slot(s) com active!=0; {shown} skinado(s) valido(s). (Tidus c001 = boneCount 136)  [use -v p/ ver rejeitados]");
    return 0;
}

if (mode == "dump-bones") {
    if (args.Length < 2) { Console.WriteLine("uso: dump-bones <out.json> [idFilterHex] [frames=1] [intervalMs=33]"); return 1; }
    string outPath = args[1];
    int? idFilter = args.Length > 2 ? (int)ParseHex(args[2]) : (int?)null;
    int frames = args.Length > 3 ? int.Parse(args[3]) : 1;
    int intervalMs = args.Length > 4 ? int.Parse(args[4]) : 33;
    var count = Ru32(AbsOf(VA_COUNT));
    if (count == null || count == 0 || count > 4096) { Console.WriteLine($"contagem implausivel ({count})"); return 2; }

    var tableBaseN2 = Ru32(AbsOf(VA_TABLE));
    if (tableBaseN2 == null || !PtrOk(tableBaseN2.Value)) { Console.WriteLine("tablePtr invalido"); return 2; }
    uint tableBase = tableBaseN2.Value;
    // pick the target instance (first active matching idFilter, else first active skinned)
    uint target = 0; ushort tid = 0, tbones = 0;
    for (uint idx = 0; idx < count.Value; idx++) {
        uint inst = tableBase + (uint)(INST_STRIDE * idx);
        var info = InstInfo(inst);
        if (info == null) continue;
        if (idFilter.HasValue && info.Value.id != idFilter.Value) continue;
        target = inst; tid = info.Value.id; tbones = info.Value.bones; break;
    }
    if (target == 0) { Console.WriteLine("nenhuma instancia ativa skinada" + (idFilter.HasValue?$" com id=0x{idFilter:X4}":"") + " — rode scan-instances"); return 3; }
    Console.WriteLine($"capturando inst @0x{target:X8} id=0x{tid:X4} boneCount={tbones} frames={frames}");

    var sb = new System.Text.StringBuilder();
    sb.Append("{\n");
    sb.Append($"  \"moduleBase\": \"0x{baseAddr:X8}\",\n  \"instAddr\": \"0x{target:X8}\",\n  \"id\": {tid},\n  \"boneCount\": {tbones},\n");
    sb.Append("  \"frames\": [\n");
    for (int f = 0; f < frames; f++) {
        var poseArrN = Ru32(target + 808);   // re-read each frame (stable pointer)
        if (poseArrN == null || !PtrOk(poseArrN.Value)) { Console.WriteLine($"[frame {f}] poseArr invalido — abortando"); break; }
        uint poseArr = poseArrN.Value;
        sb.Append("    [");
        for (int bi = 0; bi < tbones; bi++) {
            var m = ReadAbs(poseArr + (uint)(352 * bi) + 136, 64);   // 16 floats row-major
            sb.Append(bi == 0 ? "\n      [" : ",\n      [");
            for (int k = 0; k < 16; k++) {
                float v = m == null ? 0f : BitConverter.ToSingle(m, k * 4);
                sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture));
            }
            sb.Append("]");
        }
        sb.Append("\n    ]" + (f < frames - 1 ? "," : "") + "\n");
        if (f < frames - 1) System.Threading.Thread.Sleep(intervalMs);
    }
    sb.Append("  ]\n}\n");
    System.IO.File.WriteAllText(outPath, sb.ToString());
    Console.WriteLine($"OK -> {outPath} ({frames} frame(s) x {tbones} bones)");
    return 0;
}

if (mode == "list-clips") {
    var head = Ru32(AbsOf(VA_MseqGroupPool));
    if (head == null || !PtrOk(head.Value)) { Console.WriteLine($"pool vazio/invalido (head=0x{head?.ToString("X8")??"?"}) — jogo carregado numa cena?"); return 2; }
    uint g = head.Value; int gi = 0; int total = 0;
    while (PtrOk(g) && gi < 200) {
        var rig = Ru16(g + 4); var tbl = Ru32(g + 0); var next = Ru32(g + 0x0C);
        if (rig == null || tbl == null) break;
        if (PtrOk(tbl.Value)) {
            var cnt = Ru16(tbl.Value + 8); var recs = Ru32(tbl.Value + 0x0C);
            if (cnt != null && recs != null && PtrOk(recs.Value) && cnt.Value <= 2048) {
                var ids = new System.Text.StringBuilder();
                for (int k = 0; k < cnt.Value; k++) { var ci = Ru16(recs.Value + (uint)(0x10 * k)); if (ci != null) ids.Append(ci.Value).Append(' '); }
                Console.WriteLine($"  group[{gi}] rig=0x{rig.Value:X4}({rig.Value}) clips={cnt.Value}: {ids}");
                total += cnt.Value;
            }
        }
        if (next == null) break; g = next.Value; gi++;
    }
    Console.WriteLine($"-> {gi+1} grupo(s), {total} clipe(s) no pool.");
    return 0;
}

if (mode == "anim-info") {
    int? idf = args.Length > 1 ? (int)ParseHex(args[1]) : (int?)null;
    uint inst = ResolveInst(idf);
    if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    var cursor = Ru32(inst + (uint)OFF_CURSOR); var last = Ru32(inst + (uint)OFF_LASTCUR);
    var playing = Ru16(inst + (uint)OFF_PLAYING); var speed = Ru16(inst + (uint)OFF_SPEEDB);
    var curclip = Ru32(inst + (uint)OFF_CURCLIP); var loops = Ru16(inst + (uint)OFF_LOOPS);
    int total = last != null ? (int)((last.Value + 1) >> 8) : -1;
    Console.WriteLine($"inst @0x{inst:X8} id=0x{Ru16(inst):X4}");
    Console.WriteLine($"  curFrame={(cursor!=null?(cursor.Value>>8):0)} totalFrames={total} playing={playing} loops={loops} speedB={(short?)speed} curClip=0x{curclip?.ToString("X8")}");
    return 0;
}

if (mode == "freeze") {
    int? idf = args.Length > 1 ? (int)ParseHex(args[1]) : (int?)null;
    short val = args.Length > 2 ? short.Parse(args[2]) : (short)0;
    uint inst = ResolveInst(idf); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    uint st = WriteI16(inst + (uint)OFF_SPEEDB, val);
    Console.WriteLine($"freeze inst @0x{inst:X8} speedB={val} status={st} (0=congelado/sem smear; restaure tocando o clipe de novo)");
    return 0;
}

if (mode == "step") {
    if (args.Length < 3) { Console.WriteLine("uso: step <idHex> <frame>"); return 1; }
    uint inst = ResolveInst((int)ParseHex(args[1])); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    uint frame = uint.Parse(args[2]);
    uint st = WriteU32(inst + (uint)OFF_CURSOR, frame << 8);
    Console.WriteLine($"step inst @0x{inst:X8} -> frame {frame} (cursor=0x{frame<<8:X}) status={st}");
    return 0;
}

if (mode == "play-clip") {
    if (args.Length < 3) { Console.WriteLine("uso: play-clip <idHex> <clipIndex> [layer=1]"); return 1; }
    uint inst = ResolveInst((int)ParseHex(args[1])); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    uint clip = uint.Parse(args[2]); uint layer = args.Length > 3 ? uint.Parse(args[3]) : 1;
    var r0 = CallVA(VA_ResetPlayback, 1 /*CDECL_I*/, inst, 0, 0);
    var r1 = CallVA(VA_SelectMotion, 3 /*CDECL_III*/, inst, layer, clip);
    System.Threading.Thread.Sleep(60); // let a couple game ticks bind+start
    var curclip = Ru32(inst + (uint)OFF_CURCLIP); var playing = Ru16(inst + (uint)OFF_PLAYING); var total = Ru32(inst + (uint)OFF_LASTCUR);
    Console.WriteLine($"play-clip inst @0x{inst:X8} clip={clip} layer={layer}: reset(st={r0.st}) select(st={r1.st} ret={r1.ret})");
    Console.WriteLine($"  -> curClip=0x{curclip?.ToString("X8")} playing={playing} totalFrames={(total!=null?((total.Value+1)>>8):0)}");
    return 0;
}

if (mode == "capture-clip") {
    if (args.Length < 4) { Console.WriteLine("uso: capture-clip <idHex> <clipIndex> <outdir> [maxFrames=60] [layer=1]"); return 1; }
    int idv = (int)ParseHex(args[1]); uint clip = uint.Parse(args[2]); string outdir = args[3];
    int maxFrames = args.Length > 4 ? int.Parse(args[4]) : 60; uint layer = args.Length > 5 ? uint.Parse(args[5]) : 1;
    uint inst = ResolveInst(idv); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    var binfo = InstInfo(inst); if (binfo == null) { Console.WriteLine("instancia invalida"); return 3; }
    int bones = binfo.Value.bones;
    System.IO.Directory.CreateDirectory(outdir);
    // 1) start clip
    CallVA(VA_ResetPlayback, 1, inst, 0, 0);
    CallVA(VA_SelectMotion, 3, inst, layer, clip);
    System.Threading.Thread.Sleep(80);
    var lastN = Ru32(inst + (uint)OFF_LASTCUR);
    int total = lastN != null ? (int)((lastN.Value + 1) >> 8) : 0;
    if (total <= 0 || total > 2000) { Console.WriteLine($"clip {clip}: totalFrames invalido ({total}) — clipe inexistente? abortando"); return 4; }
    int nF = Math.Min(total, maxFrames);
    Console.WriteLine($"capture-clip id=0x{idv:X} clip={clip} bones={bones} totalFrames={total} capturando={nF}");
    // 2) freeze (kill smear)
    WriteI16(inst + (uint)OFF_SPEEDB, 0);
    var sb = new System.Text.StringBuilder();
    sb.Append("{\n").Append($"  \"id\": {idv}, \"clip\": {clip}, \"boneCount\": {bones}, \"totalFrames\": {total},\n  \"frames\": [\n");
    for (int f = 0; f < nF; f++) {
        WriteU32(inst + (uint)OFF_CURSOR, (uint)(f << 8));
        System.Threading.Thread.Sleep(40); // let the game tick rebuild the pose at this cursor
        var poseArrN = Ru32(inst + 808);
        if (poseArrN == null || !PtrOk(poseArrN.Value)) { Console.WriteLine($"[frame {f}] poseArr invalido"); break; }
        uint poseArr = poseArrN.Value;
        sb.Append(f == 0 ? "    [" : ",\n    [");
        for (int bi = 0; bi < bones; bi++) {
            var m = ReadAbs(poseArr + (uint)(352 * bi) + 136, 64);
            sb.Append(bi == 0 ? "" : ",").Append("[");
            for (int k = 0; k < 16; k++) { float v = m == null ? 0f : BitConverter.ToSingle(m, k * 4); sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture)); }
            sb.Append("]");
        }
        sb.Append("]");
    }
    sb.Append("\n  ]\n}\n");
    string outPath = System.IO.Path.Combine(outdir, $"id{idv}_clip{clip}.json");
    System.IO.File.WriteAllText(outPath, sb.ToString());
    Console.WriteLine($"OK -> {outPath} ({nF} frames x {bones} bones). (speedB deixado 0; re-toque o clipe p/ retomar animacao normal)");
    return 0;
}

// resolve PMeshInstance (HD m_currentPose) from an FFX instance via the proven chain:
//   render = *(inst+0x830)  [= *(inst+524)] ; b = *(render+0x4) ; P = *(b+0x0) ; validate count@(P+8) in range.
uint ResolvePMeshInstance(uint inst, out uint count, out uint dataPtr) {
    count = 0; dataPtr = 0;
    var render = Ru32(inst + 0x830); if (render == null || !PtrOk(render.Value)) return 0;
    var b = Ru32(render.Value + 0x4); if (b == null || !PtrOk(b.Value)) return 0;
    var P = Ru32(b.Value + 0x0); if (P == null || !PtrOk(P.Value)) return 0;
    var c = Ru32(P.Value + 0x8); var d = Ru32(P.Value + 0xC);
    if (c == null || d == null) return 0;
    uint cnt = c.Value & 0x7FFFFFFF;
    if (cnt < 8 || cnt > 512 || !PtrOk(d.Value)) return 0;
    count = cnt; dataPtr = d.Value; return P.Value;
}

if (mode == "dump-pose") {
    int idv = args.Length > 1 ? (int)ParseHex(args[1]) : 1;
    string? outPath = args.Length > 2 ? args[2] : null;
    uint inst = ResolveInst(idv); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    uint P = ResolvePMeshInstance(inst, out uint count, out uint dataPtr);
    if (P == 0) { Console.WriteLine("PMeshInstance nao resolvido pela cadeia inst+0x830->+4->+0 — rode find-pose"); return 4; }
    Console.WriteLine($"PMeshInstance @0x{P:X8} m_currentPose.count={count} data@0x{dataPtr:X8}");
    var sb = new System.Text.StringBuilder();
    sb.Append($"{{\n  \"id\": {idv}, \"pmesh\": \"0x{P:X8}\", \"count\": {count},\n  \"matrices\": [\n");
    for (int i = 0; i < count; i++) {
        var m = ReadAbs(dataPtr + (uint)(64 * i), 64);
        sb.Append(i == 0 ? "    [" : ",\n    [");
        for (int k = 0; k < 16; k++) { float v = m == null ? 0f : BitConverter.ToSingle(m, k * 4); sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture)); }
        sb.Append("]");
        if (i < 3) { float t0=BitConverter.ToSingle(m!,48),t1=BitConverter.ToSingle(m,52),t2=BitConverter.ToSingle(m,56),c0=BitConverter.ToSingle(m,12),c1=BitConverter.ToSingle(m,28),c2=BitConverter.ToSingle(m,44);
            Console.WriteLine($"  bone{i}: trans row=({t0:F2},{t1:F2},{t2:F2}) col=({c0:F2},{c1:F2},{c2:F2})"); }
    }
    sb.Append("\n  ]\n}\n");
    if (outPath != null) { System.IO.File.WriteAllText(outPath, sb.ToString()); Console.WriteLine($"OK -> {outPath} ({count} matrices)"); }
    return 0;
}

if (mode == "capture-cur") {
    // captura o clipe que esta TOCANDO AGORA (input-driven, ex.: caminhada) — sem SelectResidentMotion.
    if (args.Length < 3) { Console.WriteLine("uso: capture-cur <id|@instAddr> <outdir> [maxFrames=60] [tag]"); return 1; }
    string outdir = args[2];
    int maxFrames = args.Length > 3 ? int.Parse(args[3]) : 60; string tag = args.Length > 4 ? args[4] : "cur";
    uint inst = ResolveTarget(args[1]); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    int idv = Ru16(inst) ?? 0;
    uint P0 = ResolvePMeshInstance(inst, out uint count, out _);
    if (P0 == 0) { Console.WriteLine("PMeshInstance nao resolvido"); return 4; }
    var curclip = Ru32(inst + (uint)OFF_CURCLIP);
    var lastN = Ru32(inst + (uint)OFF_LASTCUR); int total = lastN != null ? (int)((lastN.Value + 1) >> 8) : 0;
    if (total <= 0 || total > 2000) { Console.WriteLine($"totalFrames invalido ({total})"); return 4; }
    int nF = Math.Min(total, maxFrames);
    System.IO.Directory.CreateDirectory(outdir);
    Console.WriteLine($"capture-cur id=0x{idv:X} curClip=0x{curclip?.ToString("X8")} joints={count} totalFrames={total} capturando={nF}");
    WriteI16(inst + (uint)OFF_SPEEDB, 0);  // freeze clock (input de movimento segue latcheado p/ manter o clipe)
    var sb = new System.Text.StringBuilder();
    sb.Append($"{{\n  \"id\": {idv}, \"curClip\": \"0x{curclip?.ToString("X8")}\", \"count\": {count}, \"totalFrames\": {total}, \"space\": \"phyre_joint_local\",\n  \"frames\": [\n");
    bool rebuild = args.Contains("--rebuild");  // p/ scratch: forca eval+rebuild da pose a cada frame
    for (int f = 0; f < nF; f++) {
        WriteU32(inst + (uint)OFF_CURSOR, (uint)(f << 8));
        if (rebuild) { CallVA(VA_UpdateMseq, 2, inst, 1, 0); CallVA(VA_RebuildPose, 1, inst, 0, 0); }
        System.Threading.Thread.Sleep(rebuild ? 5 : 40);
        uint P = ResolvePMeshInstance(inst, out uint cnt, out uint dataPtr);
        if (P == 0 || cnt != count) { Console.WriteLine($"[frame {f}] PMeshInstance instavel"); break; }
        sb.Append(f == 0 ? "    [" : ",\n    [");
        var mats = ReadMatrices(dataPtr, (int)count);
        for (int i = 0; i < count; i++) {
            sb.Append(i == 0 ? "" : ",").Append("[");
            for (int k = 0; k < 16; k++) { float v = BitConverter.ToSingle(mats, i * 64 + k * 4); sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture)); }
            sb.Append("]");
        }
        sb.Append("]");
    }
    sb.Append("\n  ]\n}\n");
    string outPath = System.IO.Path.Combine(outdir, $"id{idv}_{tag}.json");
    System.IO.File.WriteAllText(outPath, sb.ToString());
    Console.WriteLine($"OK -> {outPath} ({nF} frames x {count} joints)");
    return 0;
}

if (mode == "capture-hd-clip") {
    // Like capture-clip but dumps the HD Phyre m_currentPose (count joints) per frame = ground-truth HD skin.
    if (args.Length < 4) { Console.WriteLine("uso: capture-hd-clip <idHex> <motionIndex> <outdir> [maxFrames=60] [layer=1]"); return 1; }
    int idv = (int)ParseHex(args[1]); uint clip = uint.Parse(args[2]); string outdir = args[3];
    int maxFrames = args.Length > 4 ? int.Parse(args[4]) : 60; uint layer = args.Length > 5 ? uint.Parse(args[5]) : 1;
    uint inst = ResolveInst(idv); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    System.IO.Directory.CreateDirectory(outdir);
    CallVA(VA_ResetPlayback, 1, inst, 0, 0); CallVA(VA_SelectMotion, 3, inst, layer, clip);
    System.Threading.Thread.Sleep(80);
    uint P0 = ResolvePMeshInstance(inst, out uint count, out uint _);
    if (P0 == 0) { Console.WriteLine("PMeshInstance nao resolvido — rode find-pose"); return 4; }
    var lastN = Ru32(inst + (uint)OFF_LASTCUR); int total = lastN != null ? (int)((lastN.Value + 1) >> 8) : 0;
    if (total <= 0 || total > 2000) { Console.WriteLine($"clip {clip}: totalFrames invalido ({total})"); return 4; }
    int nF = Math.Min(total, maxFrames);
    Console.WriteLine($"capture-hd-clip id=0x{idv:X} clip={clip} joints={count} totalFrames={total} capturando={nF}");
    WriteI16(inst + (uint)OFF_SPEEDB, 0);
    var sb = new System.Text.StringBuilder();
    sb.Append($"{{\n  \"id\": {idv}, \"clip\": {clip}, \"count\": {count}, \"totalFrames\": {total}, \"space\": \"phyre_joint_world_rowmajor\",\n  \"frames\": [\n");
    bool rebuild = args.Contains("--rebuild");  // p/ scratch: forca eval+rebuild da pose a cada frame
    for (int f = 0; f < nF; f++) {
        WriteU32(inst + (uint)OFF_CURSOR, (uint)(f << 8));
        if (rebuild) { CallVA(VA_UpdateMseq, 2, inst, 1, 0); CallVA(VA_RebuildPose, 1, inst, 0, 0); }
        System.Threading.Thread.Sleep(rebuild ? 5 : 40);
        uint P = ResolvePMeshInstance(inst, out uint cnt, out uint dataPtr);
        if (P == 0 || cnt != count) { Console.WriteLine($"[frame {f}] PMeshInstance instavel"); break; }
        sb.Append(f == 0 ? "    [" : ",\n    [");
        var mats = ReadMatrices(dataPtr, (int)count);
        for (int i = 0; i < count; i++) {
            sb.Append(i == 0 ? "" : ",").Append("[");
            for (int k = 0; k < 16; k++) { float v = BitConverter.ToSingle(mats, i * 64 + k * 4); sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture)); }
            sb.Append("]");
        }
        sb.Append("]");
    }
    sb.Append("\n  ]\n}\n");
    string outPath = System.IO.Path.Combine(outdir, $"id{idv}_clip{clip}_hd.json");
    System.IO.File.WriteAllText(outPath, sb.ToString());
    Console.WriteLine($"OK -> {outPath} ({nF} frames x {count} joints). re-toque o clipe p/ retomar.");
    return 0;
}

if (mode == "setinput") {
    // setinput <maskHex> [key1Hex] [key2Hex] — input forjado PERSISTENTE (FFXIN_*: up=1 down=2 left=4 right=8 confirm=10 cancel=20 menu=40 joy=100)
    uint mask = args.Length > 1 ? ParseHex(args[1]) : 0;
    uint k1 = args.Length > 2 ? ParseHex(args[2]) : 0;
    uint k2 = args.Length > 3 ? ParseHex(args[3]) : 0;
    uint st = Arm(5 /*OP_SETINPUT*/, 0, 0, 0, mask, k1, k2);
    Console.WriteLine($"setinput mask=0x{mask:X} k1=0x{k1:X} k2=0x{k2:X} status={st} (zere tudo p/ soltar)");
    return 0;
}

if (mode == "walk") {
    // walk <up|down|left|right|stop> — atalho (liga teclado+joystick na direcao)
    string d = args.Length > 1 ? args[1].ToLowerInvariant() : "stop";
    uint mask = 0x100; // FFXIN_JOY
    if (d == "up" || d == "forward") mask |= 1; else if (d == "down" || d == "back") mask |= 2;
    else if (d == "left") mask |= 4; else if (d == "right") mask |= 8; else mask = 0;
    uint st = Arm(5, 0, 0, 0, mask, 0, 0);
    Console.WriteLine($"walk {d} -> mask=0x{mask:X} status={st}");
    return 0;
}

if (mode == "load-motions") {
    // CALL FFX_Mgrp_EnsureResidentMotionLoadedAndRegistered(resourceId, variant) p/ carregar o set completo do .mgrp.
    if (args.Length < 2) { Console.WriteLine("uso: load-motions <resourceIdHex> [variant0,1...]  (ex: 1=Tidus)"); return 1; }
    uint rid = ParseHex(args[1]);
    string[] vs = args.Length > 2 ? args[2].Split(',') : new[] { "0", "1" };
    foreach (var v in vs) { var r = CallVA(VA_EnsureMotion, 2, rid, uint.Parse(v), 0); Console.WriteLine($"load-motions(rid=0x{rid:X}, variant={v}) -> status={r.st} ret={r.ret}"); System.Threading.Thread.Sleep(150); }
    return 0;
}

if (mode == "play-id") {
    // toca um clipe por ID REAL deterministicamente (BindMseq + InitChannelTracks + setup manual de range).
    // Forca QUALQUER clipe residente (ataque, etc.), ignorando o sistema de movimento/IA.
    if (args.Length < 3) { Console.WriteLine("uso: play-id <id|@instAddr> <clipIdDec> [rigHex]  (clipId da list-clips, ex 4243)"); return 1; }
    uint clipId = uint.Parse(args[2]);
    uint inst = ResolveTarget(args[1]); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    uint rig = args.Length > 3 ? ParseHex(args[3]) : (Ru16(inst) ?? (ushort)0);
    uint packed = (rig << 16) | (clipId & 0xFFFF);
    CallVA(VA_ResetPlayback, 1, inst, 0, 0);
    var bind = CallVA(VA_BindMseq, 2 /*CDECL_II*/, inst, packed, 0);
    var scriptN = Ru32(inst + (uint)OFF_MSEQBLK);   // *(inst+1812) = SCRIPT-ptr (bytecode de opcodes), NAO o mode-block
    if (scriptN == null || !PtrOk(scriptN.Value)) { Console.WriteLine($"bind ret={bind.ret} mseqBlock invalido (clipe nao residente? rode list-clips)"); return 4; }
    // PART A FIX (RE 2026-06-04, confirmado 2x byte-level): InitChannelTracks PRECISA do channel-mode block = *(record+12),
    // onde record = ResolveActiveClipRecordFromScript(inst). Passar o script-ptr cru fazia o tool ler bytes de CONTROLE
    // do script como header de canal -> canais lixo -> malha voa. idle/corrida (script comeca com opcode 1) sobreviviam
    // por sorte; ataque/cast (4197/4198, opcode != 1) corrompiam. Resolver = o passo que o harness PULAVA.
    var rRec = CallVA(VA_ResolveClipRecord, 1 /*CDECL_I*/, inst, 0, 0);
    uint record = (uint)rRec.ret;
    if (Environment.GetEnvironmentVariable("PLAYID_DBG") == "1")
        Console.WriteLine($"  [dbg] bind.ret={bind.ret} f1804={Ru32(inst+1804)?.ToString("X8")} f1808={Ru32(inst+1808)?.ToString("X8")} f1812={Ru32(inst+1812)?.ToString("X8")} rRec.st={rRec.st} rRec.ret={record:X8} PtrOk={PtrOk(record)}");
    uint mseq;
    // record vem do ResolveClipRecord (st=1 = call ok). NAO usar o PtrOk estrito: o filtro 0x8B/0xCD pode
    // rejeitar um record HEAP valido cujo low-byte calha em 0x8B -> fallback errado -> corrompe acao. Checagem de range simples.
    bool recOk = record >= 0x10000u && record < 0x7FFF0000u;
    if (rRec.st == 1 && recOk) {
        var modeN = Ru32(record + 12);
        if (modeN != null && PtrOk(modeN.Value)) { mseq = modeN.Value; }
        else { mseq = scriptN.Value; Console.WriteLine("  AVISO: *(record+12) invalido — caindo pro script-ptr (corrupcao possivel)."); }
    } else {
        mseq = scriptN.Value;
        Console.WriteLine($"  AVISO: ResolveClipRecord falhou (st={rRec.st}) — usando script-ptr cru (corrupcao possivel).");
    }
    CallVA(VA_InitChannels, 2, inst, mseq, 0);
    // total: o BindMseq ja seta o range (inst+1820); usa ele (mseq[0] nem sempre e o total).
    var lcN = Ru32(inst + (uint)OFF_LASTCUR); int total = (lcN != null && lcN.Value > 0) ? (int)((lcN.Value + 1) >> 8) : (Ru16(mseq) ?? 0);
    if (total <= 0 || total > 2000) { Console.WriteLine($"totalFrames invalido ({total})"); return 4; }
    uint endC = (uint)(total << 8);
    // replica FFX_Mseq_SetupPlaybackRangeAndChannels (0x839C00)
    WriteU32(inst + (uint)OFF_FRAMERATE, 7680);
    WriteU32(inst + (uint)OFF_STARTCUR, 0);
    WriteU32(inst + (uint)OFF_LOOPSPAN, endC);
    WriteU32(inst + (uint)OFF_LASTCUR, endC - 1);
    WriteI16(inst + (uint)OFF_LOOPCNT, 0x3FFF);   // loop "infinito" p/ ficar tocando
    WriteI16(inst + (uint)OFF_LOOPMODE, -1);
    WriteI16(inst + (uint)OFF_SPEEDB, 256);
    WriteU32(inst + (uint)OFF_CURSOR, 0);
    WriteI16(inst + (uint)OFF_SMOOTH, 1);
    var f404 = Ru32(inst + 404); if (f404 != null) WriteU32(inst + 404, f404.Value & ~0x10000u);
    WriteI16(inst + (uint)OFF_PLAYING, 1);
    System.Threading.Thread.Sleep(60);
    var cc = Ru32(inst + (uint)OFF_CURCLIP); var pl = Ru16(inst + (uint)OFF_PLAYING); var tot = Ru32(inst + (uint)OFF_LASTCUR);
    Console.WriteLine($"play-id inst=0x{inst:X8} clip={clipId} packed=0x{packed:X8} bind.ret={bind.ret} -> curClip=0x{cc?.ToString("X8")} playing={pl} totalFrames={(tot!=null?((tot.Value+1)>>8):0)}");
    return 0;
}

if (mode == "swap-model") {
    // MOD: troca o MODELO do personagem que voce controla (ou qualquer slot da party).
    // slotData=FFX_Party_GetSlotData(slot); WRITE charId@+4; limpa cache@+0 -> re-spawn no proximo frame.
    if (args.Length < 3) { Console.WriteLine("uso: swap-model <slot> <charIdHex>  (slot 0=lider; charId packed: c001=1, c002=2, n016=2010, s000=3000...)"); return 1; }
    uint slot = ParseHex(args[1]); uint newId = ParseHex(args[2]);
    var r = CallVA(VA_GetPartySlot, 1, slot, 0, 0);
    uint slotData = (uint)r.ret;
    if (!PtrOk(slotData)) { Console.WriteLine($"slotData invalido (0x{slotData:X8}) — em campo? slot valido?"); return 3; }
    var oldInst = Ru32(slotData + 0); var oldId = Ru32(slotData + 4);
    WriteU32(slotData + 4, newId);   // novo char-id (modelo)
    WriteU32(slotData + 0, 0);        // limpa o instance cacheado -> FFX_Party_SpawnMemberInstance re-aloca
    Console.WriteLine($"swap-model slot={slot}: charId 0x{oldId?.ToString("X")} -> 0x{newId:X}  (slotData=0x{slotData:X8} oldInst=0x{oldInst?.ToString("X8")})");
    Console.WriteLine($"  -> o player vira o modelo 0x{newId:X} no proximo frame. Reverter: swap-model {slot} 0x{oldId?.ToString("X")}");
    return 0;
}

if (mode == "whereami" || mode == "pos" || mode == "nudge" || mode == "backup-pos" || mode == "restore-pos") {
    // === Jarvis-MAP FIELD WARP verbs ===
    // whereami = READ-ONLY. pos/nudge/restore-pos = WRITE (game-open + Halyson OK; recipe replicates
    // FFX_Field_WarpActorToPosition: clear suppress latch -> WRITE actor cache (authoritative, survives the
    // per-frame sync 0x862D10) -> WRITE instance floats -> CALL ReseatInstanceTransform). read-first by design.
    const uint W_X=0x0C, W_Y=0x10, W_Z=0x14, W_MX=0x1C, W_MY=0x20, W_MZ=0x24;
    const uint W_FLAGS=0x194, W_CELL=0x824, W_FACING=0x158;
    const uint W_A_CACHE_N=0x284, W_A_CACHE56=0x558;
    const uint W_VA_RESEAT=0x8246E0, W_VA_SUPPRESS=0x12FFA90;
    string backupPath = Path.Combine(Path.GetTempPath(), "ffx_warp_backup.json");
    float Rf(uint abs){ var b=ReadAbs(abs,4); return b==null?float.NaN:BitConverter.ToSingle(b,0); }
    void Wf(uint abs,float v)=>WriteAbs(abs,BitConverter.GetBytes(v));
    string Inv(float v)=>v.ToString("R",CultureInfo.InvariantCulture);

    uint inst = ControlledInst();
    if (inst==0){ Console.WriteLine("ERRO: controlled inst nulo/invalido (esta em campo? nao em cutscene/battle?)"); return 3; }

    // resolve controlled field actor (same pattern as set-field-model): prefer actor+0x9C==inst, else matchkey
    uint actor=0, cacheOff=W_A_CACHE_N, actorType=0;
    var wMgr=AbsOf(VA_FieldActorMgr); var wKey=Ru16(wMgr+10); var wCnt=Ru16(wMgr+12);
    if (wKey!=null && wCnt!=null && wCnt.Value>0 && wCnt.Value<=4096){
        for (uint i=0;i<wCnt.Value;i++){ var rg=CallVA(VA_GetFieldActor,1,i,0,0); if(rg.st!=1)continue; uint fa=(uint)rg.ret; if(!PtrOk(fa))continue;
            var ai=Ru32(fa+(uint)OFF_ACTOR_INST); if(ai!=null && ai.Value==inst){ actor=fa; break; }
            var k=Ru16(fa+(uint)OFF_ACTOR_MATCHKEY); if(k!=null && k.Value==wKey.Value){ actor=fa; } }
    }
    if (actor!=0){ var t=Ru32(actor); if(t!=null){ actorType=t.Value & 0xFF; if(actorType==5||actorType==6) cacheOff=W_A_CACHE56; } }

    float cx=Rf(inst+W_X), cy=Rf(inst+W_Y), cz=Rf(inst+W_Z), cf=Rf(inst+W_FACING);
    Console.WriteLine($"inst=0x{inst:X8} actor=0x{actor:X8} type={actorType} cacheOff=0x{cacheOff:X}");
    Console.WriteLine($"POS x={cx:F3} y={cy:F3} z={cz:F3} facing={cf:F4}");
    if (mode=="whereami") return 0;
    if (mode=="backup-pos"){ File.WriteAllText(backupPath, $"{{\"inst\":{inst},\"x\":{Inv(cx)},\"y\":{Inv(cy)},\"z\":{Inv(cz)},\"facing\":{Inv(cf)}}}"); Console.WriteLine($"backup salvo: {backupPath}"); return 0; }

    float tx, ty, tz, tf=cf; bool setFacing=false;
    if (mode=="restore-pos"){
        if(!File.Exists(backupPath)){ Console.WriteLine($"ERRO: sem backup ({backupPath}) — rode backup-pos antes"); return 3; }
        string j=File.ReadAllText(backupPath);
        float G(string k){ var m=System.Text.RegularExpressions.Regex.Match(j,"\""+k+"\":(-?[0-9.eE+]+)"); return float.Parse(m.Groups[1].Value,CultureInfo.InvariantCulture); }
        tx=G("x"); ty=G("y"); tz=G("z"); tf=G("facing"); setFacing=true;
    } else if (mode=="nudge"){
        if(args.Length<4){ Console.WriteLine("uso: nudge <dx> <dy> <dz> [--dry-run]"); return 1; }
        tx=cx+float.Parse(args[1],CultureInfo.InvariantCulture); ty=cy+float.Parse(args[2],CultureInfo.InvariantCulture); tz=cz+float.Parse(args[3],CultureInfo.InvariantCulture);
    } else {
        if(args.Length<4){ Console.WriteLine("uso: pos <x> <y> <z> [facingRad] [--dry-run]"); return 1; }
        tx=float.Parse(args[1],CultureInfo.InvariantCulture); ty=float.Parse(args[2],CultureInfo.InvariantCulture); tz=float.Parse(args[3],CultureInfo.InvariantCulture);
        if(args.Length>=5 && !args[4].StartsWith("--")){ tf=float.Parse(args[4],CultureInfo.InvariantCulture); setFacing=true; }
    }
    bool dry=args.Contains("--dry-run");
    Console.WriteLine($"PLANO -> x={tx:F3} y={ty:F3} z={tz:F3}{(setFacing?$" facing={tf:F4}":"")} actor={(actor!=0?"ok":"NAO-RESOLVIDO(pode reverter)")} dry={dry}");
    if (dry) return 0;
    var lat=AbsOf(W_VA_SUPPRESS); var lv=ReadAbs(lat,1); if(lv!=null && lv[0]==1) WriteAbs(lat,new byte[]{0});
    if (actor!=0){ Wf(actor+cacheOff,tx); Wf(actor+cacheOff+4,ty); Wf(actor+cacheOff+8,tz); }
    Wf(inst+W_X,tx); Wf(inst+W_Y,ty); Wf(inst+W_Z,tz);
    Wf(inst+W_MX,tx); Wf(inst+W_MY,ty); Wf(inst+W_MZ,tz);
    var fl=Ru32(inst+W_FLAGS); if(fl!=null) WriteU32(inst+W_FLAGS, fl.Value | 0x2000000u);
    WriteI16(inst+W_CELL, unchecked((short)0xFFFF));
    if (setFacing) Wf(inst+W_FACING,tf);
    var rR=CallVA(W_VA_RESEAT,1,inst,0,0);
    System.Threading.Thread.Sleep(150);
    Console.WriteLine($"reseat st={rR.st}  APOS x={Rf(inst+W_X):F3} y={Rf(inst+W_Y):F3} z={Rf(inst+W_Z):F3}");
    return 0;
}

if (mode == "scenes") {
    // READ-ONLY: enumerate sceneId -> area via the live cdrom.fnd group-12 path table.
    // Recipe (RE'd this session): T=*0x2310C70 (FND blob); g12=*0x2310C60; per id: strOff=u32[T+4+4*(g12+18*id)]; path=cstr(T+strOff).
    // usage: scenes [find <substr>] [--limit N]
    const uint SC_VA_FND=0x2310C70, SC_VA_FID=0x2310C6C;
    uint? T = Ru32(AbsOf(SC_VA_FND));
    uint? fid = Ru32(AbsOf(SC_VA_FID));
    if (T==null || !PtrOk(T.Value) || fid==null || !PtrOk(fid.Value)) { Console.WriteLine($"FND/FID invalido (T=0x{T?.ToString("X8")??"?"} fid=0x{fid?.ToString("X8")??"?"}) — provavel cutscene/load; precisa um campo ANDAVEL (controlledInst != 0)."); return 3; }
    int g12 = (int)(short)(Ru16(fid.Value + 24) ?? 0);  // group-12 base = i16 @ cdrom.fid+24 (0x2310C60 e VOLATIL, nao usar)
    uint count = Ru32(T.Value) ?? 0;
    string? find=null; long limit=6000;
    for (int i=1;i<args.Length;i++){ if(args[i]=="find" && i+1<args.Length){ find=args[i+1]; i++; } else if(args[i]=="--limit" && i+1<args.Length){ long.TryParse(args[i+1],out limit); i++; } }
    long maxId = ((long)count - 1 - g12)/18;
    if (maxId<0) maxId=0;
    if (maxId>limit) maxId=limit;
    Console.WriteLine($"FND T=0x{T.Value:X8} g12base={g12} count={count} sweep id 0..{maxId}{(find!=null?$" find='{find}'":"")}");
    string ReadCstr(uint abs,int max){ var b=ReadAbs(abs,max); if(b==null) return ""; int n=0; while(n<b.Length && b[n]!=0) n++; return System.Text.Encoding.ASCII.GetString(b,0,n); }
    int shown=0;
    for (long id=0; id<=maxId; id++){
        long index=(long)g12 + 18L*id; if(index<0) continue;
        uint? strOff=Ru32(T.Value + 4u + 4u*(uint)index);
        if(strOff==null || strOff.Value==0 || strOff.Value>0x8000000u) continue;
        string path=ReadCstr(T.Value + strOff.Value, 96);
        if(string.IsNullOrEmpty(path) || path.Length<3) continue;
        if(find!=null && path.IndexOf(find, StringComparison.OrdinalIgnoreCase)<0) continue;
        Console.WriteLine($"  id={id} (0x{id:X}) -> {path}");
        shown++;
        if(find==null && shown>=400) { Console.WriteLine("  ...(corte em 400; use find/--limit)"); break; }
    }
    Console.WriteLine($"({shown} entradas)");
    return 0;
}

if (mode == "warp") {
    // CROSS-AREA: engine's own CALLable idiom. HIGHER RISK (loads a map). game-open + OK + discardable save.
    if (args.Length<2){ Console.WriteLine("uso: warp <sceneIdDec> [flag=0] [--pump-only]"); return 1; }
    const uint W_VA_REQ=0x88E9D0, W_VA_INIT=0x88DFE0;
    int sceneId=int.Parse(args[1],CultureInfo.InvariantCulture);
    uint flag = (args.Length>=3 && !args[2].StartsWith("--")) ? uint.Parse(args[2],CultureInfo.InvariantCulture) : 0;
    bool pumpOnly=args.Contains("--pump-only");
    Console.WriteLine($"warp sceneId={sceneId} flag={flag} pumpOnly={pumpOnly}");
    var r1=CallVA(W_VA_REQ,2,(uint)sceneId,flag,0);
    Console.WriteLine($"RequestTransition({sceneId},{flag}) st={r1.st} ret={r1.ret}");
    if(!pumpOnly){ var r2=CallVA(W_VA_INIT,1,(uint)sceneId,0,0); Console.WriteLine($"InitScene({sceneId}) st={r2.st} ret={r2.ret}"); }
    else Console.WriteLine("pump-only: deixando o per-frame pump disparar InitScene");
    return 0;
}

if (mode == "set-field-model") {
    // LAB — NAO TESTADO AO VIVO. Troca o MODELO do player de CAMPO (field actor manager, NAO party-slot).
    // Caminho ESCOLHIDO: fallback packedId puro (sem string) — recon recomenda PREFERIR sobre o caminho por nome.
    //   Motivo: 0x6B6810 (SetControlledModelByName) e thiscall-hibrido (__userpurge, retn 4) — INCOMPATIVEL com
    //   a ABI cdecl-only do probe (nao da p/ setar ECX/EBX nem deixar o callee limpar a pilha), E NAO sobrevive
    //   ao seletor por-frame 0x871A50 (nao grava actor+156). O fallback int e ABI-safe e PERSISTENTE.
    // Passos: packedId=(catNibble<<12)|atoi(nome+1) -> Allocate(packedId) -> SetControlled(inst)
    //         -> achar o ator controlado (GetActorByIndex(i) ate actor+46==mgr+10) -> WRITE actor+156=inst (persiste)
    //         -> (opcional --dispose-old) Dispose(oldInst).
    if (args.Length < 2) { Console.WriteLine("uso: set-field-model <nome|c0NN> [--dispose-old]\n  nome: c=char m=monstro n=npc s=summon w=weapon f=field k=key (ex: c002, m001, n016)\n  packedId = (catNibble<<12)|atoi(nome+1); c=0 m=1 n=2 s=3 w=4 f=5 k=6"); return 1; }
    string name = args[1].Trim();
    bool disposeOld = args.Contains("--dispose-old");
    // --- parse nome -> packedId (int puro, sem montar string; evita o marcador '>'/' ' do browser) ---
    if (name.Length < 2) { Console.WriteLine($"nome invalido '{name}' (esperado letra+numero, ex c002)"); return 1; }
    char cat = char.ToLowerInvariant(name[0]);
    int catNibble = cat switch { 'c'=>0, 'm'=>1, 'n'=>2, 's'=>3, 'w'=>4, 'f'=>5, 'k'=>6, _=>-1 };
    if (catNibble < 0) { Console.WriteLine($"categoria '{name[0]}' invalida (use c/m/n/s/w/f/k)"); return 1; }
    string numStr = name.Substring(1);
    // rejeita sinal/overflow explicitamente: 'c-5' nao pode virar packedId mascarado silencioso; 0..4095 e o limite real do esquema.
    if (!int.TryParse(numStr, NumberStyles.None, CultureInfo.InvariantCulture, out int num) || num < 0 || num > 0xFFF) {
        Console.WriteLine($"numero invalido '{numStr}' em '{name}' (esperado decimal 0..4095 sem sinal, ex c002 -> 2)"); return 1;
    }
    uint packedId = (uint)((catNibble << 12) | (num & 0xFFF));

    // --- PLANO (imprime ANTES de qualquer escrita; e LAB) ---
    Console.WriteLine("=== set-field-model (LAB — NAO testado ao vivo; recon estatica) ===");
    Console.WriteLine($"  nome='{name}' cat='{cat}'(nibble={catNibble}) num={num} -> packedId=0x{packedId:X4}");
    Console.WriteLine("  caminho: packedId puro (ABI-safe, persistente). 0x6B6810/por-nome DESCARTADO (thiscall retn4, nao sobrevive ao seletor).");
    Console.WriteLine($"  1) Allocate inst:    CALL 0x{VA_AllocInstance:X} (FFX_Chr_AllocateActiveInstance) abi=CDECL_I a0=packedId=0x{packedId:X4}");
    Console.WriteLine($"  2) SetControlled:    CALL 0x{VA_SetControlled:X} (FFX_Chr_SetControlledInstance) abi=CDECL_I a0=inst");
    Console.WriteLine($"  3) Persistir +156:   scan GetActorByIndex 0x{VA_GetFieldActor:X} ate actor+{OFF_ACTOR_MATCHKEY}==mgr(0x{VA_FieldActorMgr:X})+10; WRITE actor+0x{OFF_ACTOR_INST:X}=inst");
    if (disposeOld) Console.WriteLine($"  4) Dispose antigo:   CALL 0x{VA_DisposeInstance:X} (FFX_Chr_DisposeActiveInstance) abi=CDECL_I a0=oldInst");
    Console.WriteLine($"  base runtime=0x{baseAddr:X8} (enderecos absolutos = base + (VA - 0x{IMAGE_BASE:X}))");

    // --- 1) Allocate a active instance from packedId (carga sincrona na main thread) ---
    var rAlloc = CallVA(VA_AllocInstance, 1 /*CDECL_I*/, packedId, 0, 0);
    uint inst = (uint)rAlloc.ret;
    Console.WriteLine($"[1] Allocate(packedId=0x{packedId:X4}) -> status={rAlloc.st} inst=0x{inst:X8}");
    if (rAlloc.st != 1 || !PtrOk(inst)) { Console.WriteLine("  FALHA: Allocate nao retornou inst plausivel (em campo? id valido?). Abortando antes de SetControlled."); return 3; }
    var idChk = Ru16(inst); var actChk = ReadAbs(inst + 2, 1);
    Console.WriteLine($"    inst id=0x{idChk?.ToString("X4")??"????"} active={(actChk!=null?actChk[0]:0)}");

    // --- 2) SetControlled(inst): aponta g_FFX_ControlledChrInstance p/ a nova inst (writer unico 0x82DB50) ---
    var rSet = CallVA(VA_SetControlled, 1 /*CDECL_I*/, inst, 0, 0);
    Console.WriteLine($"[2] SetControlled(0x{inst:X8}) -> status={rSet.st} ret={rSet.ret}");

    // --- 3) Persistir: gravar actor+156 no ator CONTROLADO, senao o seletor por-frame 0x871A50 reverte ---
    var mgrAbs = AbsOf(VA_FieldActorMgr);
    var ctrlKey = Ru16(mgrAbs + 10);   // chave do ator controlado
    var actorCount = Ru16(mgrAbs + 12);
    Console.WriteLine($"[3] manager@0x{mgrAbs:X8} controlledKey=0x{ctrlKey?.ToString("X4")??"????"} actorCount={actorCount?.ToString()??"?"}");
    if (ctrlKey == null || actorCount == null || actorCount.Value == 0 || actorCount.Value > 4096) {
        Console.WriteLine("  AVISO: manager implausivel — pulando WRITE actor+156. O swap pode PISCAR e reverter (seletor por-frame).");
    } else {
        uint controlledActor = 0;
        for (uint i = 0; i < actorCount.Value; i++) {
            var rGet = CallVA(VA_GetFieldActor, 1 /*CDECL_I*/, i, 0, 0);
            if (rGet.st != 1) continue;
            uint actor = (uint)rGet.ret;
            if (!PtrOk(actor)) continue;
            var key = Ru16(actor + (uint)OFF_ACTOR_MATCHKEY);
            if (key != null && key.Value == ctrlKey.Value) { controlledActor = actor; break; }
        }
        if (controlledActor == 0) {
            Console.WriteLine("  AVISO: ator controlado (actor+46==mgr+10) nao encontrado — pulando WRITE actor+156 (pode reverter).");
        } else {
            var oldActorInst = Ru32(controlledActor + (uint)OFF_ACTOR_INST);
            uint stW = WriteU32(controlledActor + (uint)OFF_ACTOR_INST, inst);
            Console.WriteLine($"    ator controlado @0x{controlledActor:X8}: WRITE +0x{OFF_ACTOR_INST:X} (oldInst=0x{oldActorInst?.ToString("X8")??"????????"} -> 0x{inst:X8}) status={stW}");
            // 4) dispose do antigo (opcional) — so depois de re-apontar +156 p/ a inst nova
            if (disposeOld && oldActorInst != null && PtrOk(oldActorInst.Value) && oldActorInst.Value != inst) {
                var rDisp = CallVA(VA_DisposeInstance, 1 /*CDECL_I*/, oldActorInst.Value, 0, 0);
                Console.WriteLine($"[4] Dispose(oldInst=0x{oldActorInst.Value:X8}) -> status={rDisp.st}");
            }
        }
    }
    Console.WriteLine($"OK (LAB): player de campo deve virar o modelo '{name}' (packedId=0x{packedId:X4}). Se piscar/reverter, o WRITE actor+156 falhou — verifique manager/ator controlado.");
    return 0;
}

if (mode == "spawn") {
    if (args.Length < 2) { Console.WriteLine("uso: spawn <packedIdHex>  (ex: 1=Tidus, 2=Yuna, 2051=monstro)"); return 1; }
    uint pid = ParseHex(args[1]);
    var r = CallVA(VA_AllocInstance, 1 /*CDECL_I*/, pid, 0, 0);
    uint inst = (uint)r.ret;
    Console.WriteLine($"spawn(packedId=0x{pid:X4}) -> status={r.st} EAX=inst=0x{inst:X8}");
    if (!PtrOk(inst)) { Console.WriteLine("  (EAX nao parece instancia valida)"); return 0; }
    var idv = Ru16(inst); var skel = Ru32(inst + 448); var act = ReadAbs(inst + 2, 1);
    ushort bones = (skel != null && PtrOk(skel.Value)) ? (Ru16(skel.Value + 10) ?? 0) : (ushort)0;
    Console.WriteLine($"  inst id=0x{idv:X4} active={(act!=null?act[0]:0)} skel=0x{skel?.ToString("X8")} boneCount={bones}  (despawn com: despawn 0x{inst:X8})");
    return 0;
}

if (mode == "despawn") {
    if (args.Length < 2) { Console.WriteLine("uso: despawn <instAbsHex>"); return 1; }
    uint inst = ParseHex(args[1]);
    var r = CallVA(VA_DisposeInstance, 1 /*CDECL_I; ESI morto*/, inst, 0, 0);
    var act = ReadAbs(inst + 2, 1);
    Console.WriteLine($"despawn(0x{inst:X8}) -> status={r.st}  active-apos={(act!=null?act[0].ToString():"?")} (0=liberado)");
    return 0;
}

if (mode == "dump-skel") {
    // m_skeletonMatrices (inverse-bind autorado) do PMesh vivo: PMeshInstance+0 = PMesh; PMesh+8=count, PMesh+12=data (64B/joint).
    string? outPath = args.Length > 2 ? args[2] : null;
    uint inst = ResolveTarget(args.Length > 1 ? args[1] : "1"); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    int idv = Ru16(inst) ?? 0;
    uint P = ResolvePMeshInstance(inst, out _, out _); if (P == 0) { Console.WriteLine("PMeshInstance nao resolvido"); return 4; }
    var pmeshN = Ru32(P + 0); if (pmeshN == null || !PtrOk(pmeshN.Value)) { Console.WriteLine("PMesh ptr invalido"); return 4; }
    uint pmesh = pmeshN.Value;
    var cN = Ru32(pmesh + 8); var dN = Ru32(pmesh + 12);
    if (cN == null || dN == null || !PtrOk(dN.Value)) { Console.WriteLine("m_skeletonMatrices invalido"); return 4; }
    uint count = cN.Value & 0x7FFFFFFF; uint data = dN.Value;
    if (count == 0 || count > 512) { Console.WriteLine($"count implausivel ({count})"); return 4; }
    Console.WriteLine($"PMesh @0x{pmesh:X8} m_skeletonMatrices.count={count} data@0x{data:X8}");
    // meta: m_defaultPose@24 (PMatrix4), m_matrixNames@32 (PString), m_matrixParents@40 (int)
    var dpC=Ru32(pmesh+24); var dpD=Ru32(pmesh+28); var mpC=Ru32(pmesh+40); var mpD=Ru32(pmesh+44); var mnC=Ru32(pmesh+32);
    Console.WriteLine($"  m_defaultPose.count={(dpC.HasValue?(dpC.Value&0x7FFFFFFF):0)} m_matrixNames.count={(mnC.HasValue?(mnC.Value&0x7FFFFFFF):0)} m_matrixParents.count={(mpC.HasValue?(mpC.Value&0x7FFFFFFF):0)}");
    // dump m_defaultPose (bind pose, 79 matrices) -> <out>.defpose.json
    if(outPath!=null && dpC.HasValue && dpD.HasValue && PtrOk(dpD.Value)){ uint dpc=dpC.Value&0x7FFFFFFF; if(dpc>0&&dpc<=512){
        var dsb=new System.Text.StringBuilder(); dsb.Append($"{{\n  \"count\": {dpc}, \"space\": \"phyre_default_pose\",\n  \"matrices\": [\n");
        for(int i=0;i<dpc;i++){ var m=ReadAbs(dpD.Value+(uint)(64*i),64); dsb.Append(i==0?"    [":",\n    [");
            for(int k=0;k<16;k++){ float v=m==null?0f:BitConverter.ToSingle(m,k*4); dsb.Append(k==0?"":",").Append(v.ToString("G9",CultureInfo.InvariantCulture)); } dsb.Append("]"); }
        dsb.Append("\n  ]\n}\n"); System.IO.File.WriteAllText(outPath+".defpose.json", dsb.ToString()); Console.WriteLine($"  -> {outPath}.defpose.json ({dpc} matrices)"); } }
    if(mpC.HasValue && mpD.HasValue && PtrOk(mpD.Value)){ uint mpc=mpC.Value&0x7FFFFFFF; if(mpc>0&&mpc<=512){ var par=new List<int>();
        for(int i=0;i<mpc;i++){ var pv=Ru32(mpD.Value+(uint)(4*i)); par.Add(pv.HasValue?(int)pv.Value:-99); }
        Console.WriteLine($"  m_matrixParents = [{string.Join(",",par)}]");
        if(outPath!=null) System.IO.File.WriteAllText(outPath+".parents.json", "["+string.Join(",",par)+"]"); } }
    var sb = new System.Text.StringBuilder();
    sb.Append($"{{\n  \"id\": {idv}, \"count\": {count}, \"space\": \"phyre_inverse_bind\",\n  \"matrices\": [\n");
    for (int i = 0; i < count; i++) {
        var m = ReadAbs(data + (uint)(64 * i), 64);
        sb.Append(i == 0 ? "    [" : ",\n    [");
        for (int k = 0; k < 16; k++) { float v = m == null ? 0f : BitConverter.ToSingle(m, k * 4); sb.Append(k == 0 ? "" : ",").Append(v.ToString("G9", CultureInfo.InvariantCulture)); }
        sb.Append("]");
    }
    sb.Append("\n  ]\n}\n");
    if (outPath != null) { System.IO.File.WriteAllText(outPath, sb.ToString()); Console.WriteLine($"OK -> {outPath} ({count} matrices)"); }
    return 0;
}

if (mode == "find-pose") {
    // BFS pointer-crawl from the FFX instance looking for a Phyre PMeshInstance:
    //   +0x00 ptr m_mesh ; +0x08 u32 m_currentPose.count (mask 0x7FFFFFFF) ; +0x0C ptr PMatrix4[] (64B/bone)
    // Phyre skeleton count may be NJOINT (~79) not the .chr boneCount (136); accept a plausible range and VERIFY the matrix.
    int idv = args.Length > 1 ? (int)ParseHex(args[1]) : 1;
    int maxDepth = args.Length > 2 ? int.Parse(args[2]) : 3;
    uint inst = ResolveInst(idv); if (inst == 0) { Console.WriteLine("instancia nao encontrada"); return 3; }
    var info = InstInfo(inst); int chrBones = info?.bones ?? 0;
    Console.WriteLine($"find-pose id=0x{idv:X} inst=0x{inst:X8} chrBoneCount={chrBones} (Phyre count pode ser ~79). depth={maxDepth}");
    bool LooksAffine(byte[] m) { // column-major affine: floats[3],[7],[11]~0, [15]~1, all finite
        float f3=BitConverter.ToSingle(m,12), f7=BitConverter.ToSingle(m,28), f11=BitConverter.ToSingle(m,44), f15=BitConverter.ToSingle(m,60);
        for (int k=0;k<16;k++){ float v=BitConverter.ToSingle(m,k*4); if(float.IsNaN(v)||float.IsInfinity(v)||Math.Abs(v)>1e6f) return false; }
        bool col = Math.Abs(f3)<1e-3 && Math.Abs(f7)<1e-3 && Math.Abs(f11)<1e-3 && Math.Abs(f15-1f)<1e-3;
        float r12=BitConverter.ToSingle(m,48), r13=BitConverter.ToSingle(m,52), r14=BitConverter.ToSingle(m,56);
        bool row = Math.Abs(r12)<1e-3 && Math.Abs(r13)<1e-3 && Math.Abs(r14)<1e-3 && Math.Abs(f15-1f)<1e-3; // row-major bottom row [0,0,0,1]
        return col || row;
    }
    var visited = new HashSet<uint>();
    var q = new Queue<(uint addr,int depth,string path)>();
    q.Enqueue((inst,0,"inst"));
    int scanned=0, matches=0;
    while (q.Count>0 && scanned<40000 && matches<40) {
        var (addr,depth,path)=q.Dequeue();
        if(!visited.Add(addr)) continue;
        int blockLen = addr==inst ? 2176 : 256;
        var bytes=new byte[blockLen]; bool ok=true;
        for(int off=0;off<blockLen;off+=512){ var ch=ReadAbs(addr+(uint)off,Math.Min(512,blockLen-off)); if(ch==null){ok=false;break;} Array.Copy(ch,0,bytes,off,ch.Length);}
        if(!ok) continue; scanned++;
        if(addr!=inst){
            uint c8=BitConverter.ToUInt32(bytes,8)&0x7FFFFFFF; uint p0=BitConverter.ToUInt32(bytes,0); uint pC=BitConverter.ToUInt32(bytes,0xC);
            if(c8>=10 && c8<=220 && PtrOk(p0) && PtrOk(pC)){
                var m0=ReadAbs(pC,64);
                if(m0!=null && LooksAffine(m0)){
                    float tx=BitConverter.ToSingle(m0,48),ty=BitConverter.ToSingle(m0,52),tz=BitConverter.ToSingle(m0,56);
                    // self-consistency: m_mesh (p0) -> m_skeletonMatrices count @ p0+8 ; deve == c8 (pose count) no PMeshInstance certo
                    var skN=Ru32(p0+8); uint skc = skN!=null ? (skN.Value & 0x7FFFFFFF) : 0;
                    string flag = (skc==c8) ? " <== SELF-CONSISTENT (pose==skel)" : $" (mesh.skel={skc})";
                    Console.WriteLine($"  MATCH @0x{addr:X8} pose.count={c8} mesh@+0=0x{p0:X8} data@+0xC=0x{pC:X8} m0.trans=({tx:F2},{ty:F2},{tz:F2}){flag} path={path}");
                    matches++;
                }
            }
        }
        if(depth<maxDepth) for(int o=0;o+4<=blockLen;o+=4){ uint p=BitConverter.ToUInt32(bytes,o); if(PtrOk(p)&&!visited.Contains(p)) q.Enqueue((p,depth+1,$"{path}+0x{o:X}")); }
    }
    Console.WriteLine($"-> scanned={scanned} matches={matches}");
    return 0;
}

if (mode == "aurora-calib-v2") {
    // 🌅 AURORA CALIB V2 — structured paired residual probe (READ-ONLY). Mints CSV + JSON for offline analysis.
    //   Spec: docs/reverse/FFX_AURORA_CALIBRATION_PROBE_SPEC_2026-06-15.md (XZ identity proved 2026-06-05; Y residual
    //   per area/model height = actor+0x534 still RT2-pending). For each live monLive slot we pair the chunk3 spawn
    //   coord with the actor world cache (+0x3B0) and emit residuals under three hypotheses: identity, flip-Z, yaw180.
    //   identity_rms_xz < 0.5 + 5x smaller than the alternatives = identity confirmed (post-IDA truth).
    //
    // Args (all optional):
    //   --route <field> <group> <formation>   Force-Battle route used to enter the encounter (recorded only).
    //   --battle <id>                          battleId string (recorded only; e.g. "azit03_00").
    //   --out <path.json>                      JSON output path. Default: work/actor_overlay/aurora_calib_<stamp>.json
    //   --csv <path.csv>                       CSV output path. Default: work/actor_overlay/aurora_calib_<stamp>.csv
    //   --max-slots <N>                        Cap on slots emitted (default 16; engine cap is also 16).
    const uint VA_AreaChunk  = 0x112A9B0;
    const uint VA_ActorArray = 0x11334CC;
    const uint ACTOR_STRIDE  = 0xF90;
    const uint OFF_DICT_ID   = 0x0E;
    const uint OFF_WORLD_POS = 0x3B0;
    const uint OFF_WORLD_CACHE = 0x3C0;
    const uint OFF_HEIGHT    = 0x534;
    string? routeField = null, routeGroup = null, routeFormation = null, battleId = null, outJson = null, outCsv = null;
    int maxSlots = 16;
    for (int i = 1; i < args.Length; i++) {
        string k = args[i];
        if (k == "--route" && i + 3 < args.Length) { routeField = args[++i]; routeGroup = args[++i]; routeFormation = args[++i]; }
        else if (k == "--battle" && i + 1 < args.Length) { battleId = args[++i]; }
        else if (k == "--out" && i + 1 < args.Length) { outJson = args[++i]; }
        else if (k == "--csv" && i + 1 < args.Length) { outCsv = args[++i]; }
        else if (k == "--max-slots" && i + 1 < args.Length) { maxSlots = int.Parse(args[++i], CultureInfo.InvariantCulture); }
    }
    string stamp = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss", CultureInfo.InvariantCulture);
    string defStem = string.IsNullOrEmpty(battleId) ? stamp : $"{battleId}_{stamp}";
    outJson ??= $"work/actor_overlay/aurora_calib_{defStem}.json";
    outCsv  ??= $"work/actor_overlay/aurora_calib_{defStem}.csv";
    long qpc = System.Diagnostics.Stopwatch.GetTimestamp();

    var c3N = Ru32(AbsOf(VA_AreaChunk));
    if (c3N == null || !PtrOk(c3N.Value)) {
        Console.WriteLine($"chunk3 nao carregado (g_FFX_Battle_AreaChunk=0x{c3N?.ToString("X8") ?? "?"}) — entre NUMA BATALHA primeiro.");
        return 3;
    }
    uint c3 = c3N.Value;
    var hdr = ReadAbs(c3, 8);
    if (hdr == null) { Console.WriteLine("chunk3 header read fail"); return 3; }
    int monCount = hdr[6];
    var monRel = Ru32(c3 + 0x20);
    if (monRel == null) { Console.WriteLine("monLive ptr read fail"); return 3; }
    uint monArr = c3 + monRel.Value;
    uint actorArrayBase = Ru32(AbsOf(VA_ActorArray)) ?? 0;

    var rows = new List<Dictionary<string, object?>>();
    int identityWins = 0, flipWins = 0, yawWins = 0, inconclusive = 0, blocked = 0;
    int slotBudget = Math.Min(monCount, maxSlots);
    for (int s = 0; s < slotBudget; s++) {
        var m = ReadAbs(monArr + (uint)(16 * s), 16);
        if (m == null) continue;
        float cx = BitConverter.ToSingle(m, 0), cy = BitConverter.ToSingle(m, 4), cz = BitConverter.ToSingle(m, 8), cw = BitConverter.ToSingle(m, 12);
        var row = new Dictionary<string, object?> {
            ["qpc"] = qpc, ["battle_id"] = battleId, ["field"] = routeField, ["group"] = routeGroup, ["formation"] = routeFormation,
            ["area_chunk_va"] = $"0x{c3:X8}", ["actor_array_va"] = actorArrayBase != 0 ? $"0x{actorArrayBase:X8}" : null,
            ["slot"] = s, ["chunk_x"] = cx, ["chunk_y"] = cy, ["chunk_z"] = cz, ["chunk_w"] = cw,
        };
        if (actorArrayBase == 0 || !PtrOk(actorArrayBase)) {
            row["winner"] = "blocked-runtime-state";
            row["reason"] = "actor_array_va invalid (battle not stable yet)";
            rows.Add(row); blocked++; continue;
        }
        uint actor = actorArrayBase + ACTOR_STRIDE * (uint)s;
        var dictBytes = ReadAbs(actor + OFF_DICT_ID, 2);
        ushort? dictId = dictBytes != null ? BitConverter.ToUInt16(dictBytes, 0) : (ushort?)null;
        var worldBytes = ReadAbs(actor + OFF_WORLD_POS, 16);
        var cacheBytes = ReadAbs(actor + OFF_WORLD_CACHE, 16);
        var heightBytes = ReadAbs(actor + OFF_HEIGHT, 4);
        if (worldBytes == null) {
            row["winner"] = "blocked-runtime-state";
            row["reason"] = $"actor[{s}]+0x3B0 read fail";
            rows.Add(row); blocked++; continue;
        }
        float ax = BitConverter.ToSingle(worldBytes, 0), ay = BitConverter.ToSingle(worldBytes, 4),
              az = BitConverter.ToSingle(worldBytes, 8), aw = BitConverter.ToSingle(worldBytes, 12);
        if (!float.IsFinite(ax) || !float.IsFinite(ay) || !float.IsFinite(az)) {
            row["winner"] = "blocked-runtime-state";
            row["reason"] = $"actor[{s}] world pos non-finite (despawned/animating)";
            rows.Add(row); blocked++; continue;
        }
        float cax = cacheBytes != null ? BitConverter.ToSingle(cacheBytes, 0) : float.NaN;
        float cay = cacheBytes != null ? BitConverter.ToSingle(cacheBytes, 4) : float.NaN;
        float caz = cacheBytes != null ? BitConverter.ToSingle(cacheBytes, 8) : float.NaN;
        float h534 = heightBytes != null ? BitConverter.ToSingle(heightBytes, 0) : float.NaN;
        double idDx = ax - cx, idDy = ay - cy, idDz = az - cz;
        double fzDx = ax - cx, fzDy = ay - cy, fzDz = az - (-cz);
        double ywDx = ax - (-cx), ywDy = ay - cy, ywDz = az - (-cz);
        double idRms = Math.Sqrt((idDx * idDx + idDy * idDy + idDz * idDz) / 3.0);
        double fzRms = Math.Sqrt((fzDx * fzDx + fzDy * fzDy + fzDz * fzDz) / 3.0);
        double ywRms = Math.Sqrt((ywDx * ywDx + ywDy * ywDy + ywDz * ywDz) / 3.0);
        double idRmsXz = Math.Sqrt((idDx * idDx + idDz * idDz) / 2.0);
        string winner; string reason;
        if (idRmsXz < 0.5 && idRms * 5 < fzRms && idRms * 5 < ywRms) {
            winner = "identity"; reason = $"identity_rms_xz={idRmsXz:F3} (<0.5) and 5x dominates flip-z/yaw180";
            identityWins++;
        } else if (fzRms < idRms && fzRms < ywRms && fzRms < 1.0) {
            winner = "flip-z"; reason = $"flipz_rms={fzRms:F3} dominates (UNEXPECTED post-IDA — investigate)";
            flipWins++;
        } else if (ywRms < idRms && ywRms < fzRms && ywRms < 1.0) {
            winner = "yaw-180"; reason = $"yaw180_rms={ywRms:F3} dominates (UNEXPECTED post-IDA — investigate)";
            yawWins++;
        } else {
            winner = "inconclusive"; reason = $"id_rms_xz={idRmsXz:F3} fz_rms={fzRms:F3} yw_rms={ywRms:F3} (animating? wait for command-menu stable)";
            inconclusive++;
        }
        row["dict_id"] = dictId.HasValue ? $"0x{dictId.Value:X4}" : null;
        row["actor_x"] = ax; row["actor_y"] = ay; row["actor_z"] = az; row["actor_w"] = aw;
        row["actor_cache_x"] = cax; row["actor_cache_y"] = cay; row["actor_cache_z"] = caz;
        row["height_0x534"] = h534;
        row["identity_dx"] = idDx; row["identity_dy"] = idDy; row["identity_dz"] = idDz; row["identity_rms"] = idRms; row["identity_rms_xz"] = idRmsXz;
        row["flipz_dx"] = fzDx; row["flipz_dy"] = fzDy; row["flipz_dz"] = fzDz; row["flipz_rms"] = fzRms;
        row["yaw180_dx"] = ywDx; row["yaw180_dy"] = ywDy; row["yaw180_dz"] = ywDz; row["yaw180_rms"] = ywRms;
        row["winner"] = winner; row["reason"] = reason;
        rows.Add(row);
    }

    string jsonDir = System.IO.Path.GetDirectoryName(outJson);
    string csvDir  = System.IO.Path.GetDirectoryName(outCsv);
    if (!string.IsNullOrEmpty(jsonDir)) System.IO.Directory.CreateDirectory(jsonDir);
    if (!string.IsNullOrEmpty(csvDir))  System.IO.Directory.CreateDirectory(csvDir);

    var summary = new Dictionary<string, object?> {
        ["qpc"] = qpc, ["stamp_utc"] = DateTime.UtcNow.ToString("o"),
        ["battle_id"] = battleId, ["field"] = routeField, ["group"] = routeGroup, ["formation"] = routeFormation,
        ["area_chunk_va"] = $"0x{c3:X8}", ["actor_array_va"] = actorArrayBase != 0 ? $"0x{actorArrayBase:X8}" : null,
        ["mon_count"] = monCount, ["slots_emitted"] = rows.Count,
        ["identity_wins"] = identityWins, ["flipz_wins"] = flipWins, ["yaw180_wins"] = yawWins,
        ["inconclusive"] = inconclusive, ["blocked_runtime"] = blocked,
        ["verdict"] = identityWins > 0 && flipWins == 0 && yawWins == 0
            ? "identity-confirmed-live"
            : (identityWins == 0 && (flipWins > 0 || yawWins > 0))
              ? "ALERT-non-identity-winner"
              : (blocked > 0 && identityWins + flipWins + yawWins == 0)
                ? "blocked-runtime-state"
                : "partial",
        ["rows"] = rows,
    };
    string json = System.Text.Json.JsonSerializer.Serialize(summary,
        new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
    System.IO.File.WriteAllText(outJson, json);

    var csvLines = new List<string> {
        "qpc,battle_id,field,group,formation,area_chunk_va,actor_array_va,slot,dict_id," +
        "chunk_x,chunk_y,chunk_z,chunk_w,actor_x,actor_y,actor_z,actor_w," +
        "actor_cache_x,actor_cache_y,actor_cache_z,height_0x534," +
        "identity_dx,identity_dy,identity_dz,identity_rms,identity_rms_xz," +
        "flipz_dx,flipz_dy,flipz_dz,flipz_rms,yaw180_dx,yaw180_dy,yaw180_dz,yaw180_rms,winner,reason"
    };
    string CsvCell(object? v) {
        if (v == null) return "";
        if (v is double d) return d.ToString("G9", CultureInfo.InvariantCulture);
        if (v is float f)  return f.ToString("G9", CultureInfo.InvariantCulture);
        string s2 = v.ToString() ?? "";
        if (s2.Contains(',') || s2.Contains('"') || s2.Contains('\n')) return "\"" + s2.Replace("\"", "\"\"") + "\"";
        return s2;
    }
    foreach (var r in rows) {
        string[] cols = {
            CsvCell(r.GetValueOrDefault("qpc")), CsvCell(r.GetValueOrDefault("battle_id")),
            CsvCell(r.GetValueOrDefault("field")), CsvCell(r.GetValueOrDefault("group")), CsvCell(r.GetValueOrDefault("formation")),
            CsvCell(r.GetValueOrDefault("area_chunk_va")), CsvCell(r.GetValueOrDefault("actor_array_va")),
            CsvCell(r.GetValueOrDefault("slot")), CsvCell(r.GetValueOrDefault("dict_id")),
            CsvCell(r.GetValueOrDefault("chunk_x")), CsvCell(r.GetValueOrDefault("chunk_y")),
            CsvCell(r.GetValueOrDefault("chunk_z")), CsvCell(r.GetValueOrDefault("chunk_w")),
            CsvCell(r.GetValueOrDefault("actor_x")), CsvCell(r.GetValueOrDefault("actor_y")),
            CsvCell(r.GetValueOrDefault("actor_z")), CsvCell(r.GetValueOrDefault("actor_w")),
            CsvCell(r.GetValueOrDefault("actor_cache_x")), CsvCell(r.GetValueOrDefault("actor_cache_y")),
            CsvCell(r.GetValueOrDefault("actor_cache_z")), CsvCell(r.GetValueOrDefault("height_0x534")),
            CsvCell(r.GetValueOrDefault("identity_dx")), CsvCell(r.GetValueOrDefault("identity_dy")),
            CsvCell(r.GetValueOrDefault("identity_dz")), CsvCell(r.GetValueOrDefault("identity_rms")),
            CsvCell(r.GetValueOrDefault("identity_rms_xz")),
            CsvCell(r.GetValueOrDefault("flipz_dx")), CsvCell(r.GetValueOrDefault("flipz_dy")),
            CsvCell(r.GetValueOrDefault("flipz_dz")), CsvCell(r.GetValueOrDefault("flipz_rms")),
            CsvCell(r.GetValueOrDefault("yaw180_dx")), CsvCell(r.GetValueOrDefault("yaw180_dy")),
            CsvCell(r.GetValueOrDefault("yaw180_dz")), CsvCell(r.GetValueOrDefault("yaw180_rms")),
            CsvCell(r.GetValueOrDefault("winner")), CsvCell(r.GetValueOrDefault("reason")),
        };
        csvLines.Add(string.Join(",", cols));
    }
    System.IO.File.WriteAllLines(outCsv, csvLines);
    Console.WriteLine($"aurora-calib-v2: monCount={monCount} emitted={rows.Count} identity={identityWins} flipz={flipWins} yaw180={yawWins} inconclusive={inconclusive} blocked={blocked}");
    Console.WriteLine($"  verdict: {summary["verdict"]}");
    Console.WriteLine($"  json: {outJson}");
    Console.WriteLine($"  csv:  {outCsv}");
    return 0;
}

if (mode == "aurora-calib") {
    // 🌅 AURORA: confirm the battle->scene transform = IDENTITY, LIVE and fully in-memory (no battleId resolve).
    // READ-ONLY. Compares the live chunk3 monLive coords (g_FFX_Battle_AreaChunk + ptr@+0x20) against the live
    // battle-actor world positions (actor[idx] = *0x11334CC + 0xF90*idx, world pos @ +0x3B0). IDA proof:
    // docs/reverse/FFX_AURORA_BATTLE_TO_SCENE_TRANSFORM_IDA_PROVEN_2026-06-05.md
    const uint VA_AreaChunk  = 0x112A9B0; // g_FFX_Battle_AreaChunk: holds ptr to the loaded chunk3 area-chunk
    const uint VA_ActorArray = 0x11334CC; // battle actor array base ptr; actor[idx] = (*VA_ActorArray) + 0xF90*idx
    var c3N = Ru32(AbsOf(VA_AreaChunk));
    if (c3N == null || !PtrOk(c3N.Value)) { Console.WriteLine($"chunk3 nao carregado (g_FFX_Battle_AreaChunk=0x{c3N?.ToString("X8") ?? "?"}) — entre NUMA BATALHA primeiro, depois rode de novo."); return 3; }
    uint c3 = c3N.Value;
    var hdr = ReadAbs(c3, 8); if (hdr == null) { Console.WriteLine("chunk3 header read fail"); return 3; }
    int fmt = hdr[0], areaCount = hdr[1], monCount = hdr[6];
    var monRel = Ru32(c3 + 0x20);
    if (monRel == null) { Console.WriteLine("monLive ptr read fail"); return 3; }
    uint monArr = c3 + monRel.Value;
    Console.WriteLine($"chunk3 @0x{c3:X8} fmt={fmt} areaCount={areaCount} monCount={monCount} monLive@0x{monArr:X8} (rel +0x{monRel.Value:X})");
    var cc = new List<(float x, float y, float z)>();
    for (int s = 0; s < monCount && s < 16; s++) {
        var m = ReadAbs(monArr + (uint)(16 * s), 16); if (m == null) continue;
        float x = BitConverter.ToSingle(m, 0), y = BitConverter.ToSingle(m, 4), z = BitConverter.ToSingle(m, 8), w = BitConverter.ToSingle(m, 12);
        cc.Add((x, y, z));
        Console.WriteLine($"  chunk3 monLive[{s}] = ({x,8:F2},{y,7:F2},{z,8:F2})  w={w:F2}");
    }
    if (cc.Count == 0) { Console.WriteLine($"=> SEM DADOS (monCount={monCount}). Entre numa batalha com monstros na tela e rode de novo."); return 0; }
    // DECISIVE identity-vs-flipZ test. The live actor world pos == chunk3 spawn under IDENTITY (Z same sign), or
    // == (X, -Z) under flip-Z. Scan a wide window; for each spawn coord track the NEAREST finite live position to
    // BOTH the identity target (cx,+cz) and the flip target (cx,-cz). Small identity residual + huge flip residual
    // => IDENTITY proven live (and flip-Z disproven). Source region excluded.
    uint scanLo = c3 > 0x8000 ? c3 - 0x8000 : 0x10000;   // tight window (~64KB): actors sit right next to chunk3; fast enough for the live hook
    uint scanHi = c3 + 0x8000;
    uint monLiveEnd = monArr + (uint)(cc.Count * 16);
    Console.WriteLine($"scanning [0x{scanLo:X8}..0x{scanHi:X8}] (nearest-match, identity vs flip-Z)...");
    int idWins = 0, flipWins = 0, found = 0;
    for (int s = 0; s < cc.Count; s++) {
        var (cx, cy, cz) = cc[s];
        double bestId = 1e9, bestFlip = 1e9; uint atId = 0, atFlip = 0; float yId = 0;
        for (uint addr = scanLo; addr + 512 <= scanHi; addr += 504) {
            var buf = ReadAbs(addr, 512); if (buf == null) continue;
            for (int k = 0; k + 16 <= 512; k += 4) {
                float x = BitConverter.ToSingle(buf, k); if (!float.IsFinite(x)) continue;
                float z = BitConverter.ToSingle(buf, k + 8); if (!float.IsFinite(z)) continue;
                float y = BitConverter.ToSingle(buf, k + 4); if (!float.IsFinite(y) || Math.Abs(y) > 300f) continue;
                uint at = addr + (uint)k;
                if (at >= monArr && at < monLiveEnd) continue;                 // skip the chunk3 source array
                double dId = Math.Max(Math.Abs(x - cx), Math.Abs(z - cz));     // identity: same Z sign
                double dFl = Math.Max(Math.Abs(x - cx), Math.Abs(z + cz));     // flip-Z: negated Z
                if (dId < bestId) { bestId = dId; atId = at; yId = y; }
                if (dFl < bestFlip) { bestFlip = dFl; atFlip = at; }
            }
        }
        string verdict = (bestId < 6 && bestId < bestFlip) ? "IDENTITY" : (bestFlip < 6 && bestFlip < bestId) ? "FLIP-Z" : "inconclusive";
        if (verdict == "IDENTITY") { idWins++; found++; }
        else if (verdict == "FLIP-Z") { flipWins++; found++; }
        Console.WriteLine($"  monLive[{s}]=({cx:F2},{cz:F2}): nearest IDENTITY={bestId:F2}@0x{atId:X8}(y={yId:F2})  flipZ={bestFlip:F2}@0x{atFlip:X8}  => {verdict}");
    }
    Console.WriteLine(idWins > 0 && flipWins == 0
        ? $"=> IDENTIDADE CONFIRMADA LIVE ({idWins}/{cc.Count}): a posicao viva do ator bate com a coord de spawn no MESMO sinal de Z (residual pequeno = animacao); flip-Z descartado. battle-local == cena."
        : flipWins > idWins
        ? $"=> FLIP-Z? ({flipWins}/{cc.Count}) — revisar a prova de IDA."
        : "=> inconclusivo: nenhum ator vivo bateu de perto (mob em animacao de entrada / morto / fora da janela). Espere a batalha ESTABILIZAR (menu aberto, ninguem agindo) e rode de novo.");
    return 0;
}

if (mode == "list-read") {
    // LAB POC (Degrau 3, READ-ONLY): dump the live selection state of a NATIVE menu list/popup object.
    //   The native list INPUT (FFX_Menu_List_UpdateInput @0x8B4460) is generic field-math; the selection lives in
    //   the 152-byte menu object: +40 state, +48 count, +50 top, +52 target, +58 page, +62 group, +64 active,
    //   +66 slots, +69 result(i8: >=0 confirmed row index), +70 scroll(i16), +72 selected(i16, row under cursor).
    //   The operator opens a native list (Customize/items/config), navigates with the game's OWN pad/keys, and we
    //   READ the live choice -> proves "ler a selecao de uma lista nativa" (the DLL arena/photo-mode menu will read
    //   the SAME +69/+72 from our own hand-rolled object; this de-risks that read step). Writes NOTHING.
    //   Current-list handles live in globals: scene3 list -> dword_1866214, light list -> dword_186A5DC,
    //   current popup -> dword_23CC120 (getter 0x8E3390), and dword_23CC128. Pass a hex handle to read a specific obj.
    //   Evidence: docs/reverse/FFX_NATIVE_MENU_LIST_ROW_SOURCE_2026-06-09.md (+ TICK_AND_ABI / DISPATCHER_ATLAS).
    void DumpListObj(string tag, uint obj) {
        var state = Ru32(obj + 40); var count = Ru16(obj + 48); var top = Ru16(obj + 50);
        var tgt = Ru16(obj + 52); var page = Ru16(obj + 58); var group = Ru16(obj + 62);
        var active = Ru8(obj + 64); var slots = Ru8(obj + 66); var res = Ru8(obj + 69);
        var scroll = Ru16(obj + 70); var sel = Ru16(obj + 72);
        if (state == null || count == null) { Console.WriteLine($"  {tag} @0x{obj:X8}: leitura falhou (handle invalido?)"); return; }
        Console.WriteLine($"  {tag} @0x{obj:X8}: state={state} count={count} top={top} target={tgt} page={page} " +
            $"group=0x{group:X} active={active} slots={slots} result={(sbyte?)res} scroll={(short?)scroll} SELECTED={(short?)sel}");
    }
    if (args.Length > 1) {
        uint h = ParseHex(args[1]);
        if (!PtrOk(h)) { Console.WriteLine($"handle 0x{h:X8} implausivel (esperado ptr de heap)"); return 2; }
        DumpListObj("obj", h);
        return 0;
    }
    // --- PRIMARY: scan the menu-object POOL directly (finds whatever is ACTIVE regardless of which global holds it).
    //   g_FFX_MenuObjPool VA 0x18408C0 = in-place array of 32 objects, stride 152 (obj+16 = 0x18408D0 confirms obj@pool base).
    //   active flag = +64 (set by RegisterAndEnter). Identify the menu by its callbacks (+12 input / +16 draw / +8 enter / +28 validator).
    const uint POOL_VA = 0x18408C0; const int POOL_STRIDE = 152, POOL_MAX = 32;
    string CbName(uint? pn) {
        uint p = pn ?? 0; if (p == 0) return "0";
        (uint va, string n)[] known = {
            (0x8B4460, "List_Input[GENERIC]"), (0x8E2E20, "YesNoPopup_Input"),
            (0x8B4A00, "scene3List_Draw"), (0x8CF9C0, "lightList_Draw"), (0x8E2F80, "YesNoPopup_Draw"),
            (0x8B3CC0, "List_EnterReset"), (0x8B5580, "scene3List_Aux"), (0x8B36E0, "scene3List_Validator"),
            (0x8D57E0, "CustomizeList_Input[wraps 8B4460]"), (0x8D5800, "Customize_KaizouSM[wraps 8B4460]"),
            (0x8D5F30, "CustomizeList_Draw"),
        };
        foreach (var (va, n) in known) if (p == AbsOf(va)) return $"{n}";
        return $"0x{p:X8}";
    }
    Console.WriteLine($"list-read: varrendo o POOL de menu (0x{POOL_VA:X} stride {POOL_STRIDE} x{POOL_MAX}) + globais (base=0x{baseAddr:X8})...");
    int activeSlots = 0;
    for (int idx = 0; idx < POOL_MAX; idx++) {
        uint obj = AbsOf(POOL_VA) + (uint)(POOL_STRIDE * idx);
        var active = Ru8(obj + 64);
        if (active == null) { Console.WriteLine($"  pool slot[{idx}] leitura falhou (pool nao mapeado?) — parando varredura do pool"); break; }
        if (active.Value == 0) continue;
        activeSlots++;
        var state = Ru32(obj + 40); var count = Ru16(obj + 48); var top = Ru16(obj + 50);
        var page = Ru16(obj + 58); var group = Ru16(obj + 62); var res = Ru8(obj + 69); var sel = Ru16(obj + 72);
        Console.WriteLine($"  slot[{idx,2}] @0x{obj:X8} group=0x{group:X} state={state} count={count} top={top} page={page} result={(sbyte?)res} SELECTED={(short?)sel}");
        Console.WriteLine($"          cb: input={CbName(Ru32(obj + 12))} draw={CbName(Ru32(obj + 16))} enter={CbName(Ru32(obj + 8))} valid={CbName(Ru32(obj + 28))}");
    }
    Console.WriteLine(activeSlots > 0 ? $"  -> {activeSlots} objeto(s) de menu ATIVO(s) no pool." : "  -> nenhum objeto de menu ativo no pool.");
    Console.WriteLine($"globais de 'lista atual':");
    (string name, uint va)[] cands = {
        ("scene3List(0x1866214)", 0x1866214u), ("lightList(0x186A5DC)", 0x186A5DCu),
        ("popup(0x23CC120)", 0x23CC120u), ("obj(0x23CC128)", 0x23CC128u),
    };
    int hits = 0;
    foreach (var (name, va) in cands) {
        var h = Ru32(AbsOf(va));
        if (h == null || !PtrOk(h.Value)) { Console.WriteLine($"  {name} -> 0x{h?.ToString("X8") ?? "????????"} (sem lista ativa)"); continue; }
        var active = Ru8(h.Value + 64);
        if (active == null || active.Value == 0) { Console.WriteLine($"  {name} -> 0x{h.Value:X8} (objeto inativo, +64={active})"); continue; }
        DumpListObj(name, h.Value); hits++;
    }
    Console.WriteLine(hits > 0
        ? $"-> {hits} lista/popup nativo ATIVO. Navegue no jogo e rode de novo: 'SELECTED'=linha sob o cursor; 'result'>=0=confirmou."
        : "-> nenhuma lista/popup nativa ativa nos globais conhecidos. Abra um menu de lista (Customize/itens/config) e rode de novo.");
    return 0;
}

Console.WriteLine("modo desconhecido"); return 1;
