// Known FFX.exe RVAs — relative to image base 0x400000.
// Convention: RVA = VirtualAddress - 0x400000
// All entries are PLACEHOLDERS until confirmed via IDA.
// When IDA confirms a value: uncomment the #define, fill the address,
// and update the IDA .i64 with a rename + comment per REGRA DE OURO.
#ifndef FFX_ADDRESSES_H
#define FFX_ADDRESSES_H

/* ── Fmod Music ─────────────────────────────────────────────────────────────
 * FFX_FmodMusic_PlayTrackByIndex @ 0x7097E0
 *   int __thiscall(FmodMusicSystem* this, unsigned int trackIndex)
 *   trackIndex range: 0..0xB5 (181 FMOD music events)
 *
 * Evidence:
 *   - docs/reverse/FFX_MUSIC_SYSTEM_AND_MOD_IDEA_2026-06-04.md
 *   - docs/reverse/FFX_MUSIC_ASSIGNMENT_TABLE_RE_FINDINGS_2026-06-05.md
 *   - Codex 2026-06-08 IDA copy annotation in work/_claude_ida/FFX.exe.i64
 *
 * Related route:
 *   FFX_FmodMusic_SwitchTrackCrossfade @ 0x7089F0 (RVA 0x3089F0)
 *   loads/reads a track via 0x709170 before calling PlayTrackByIndex.
 */
#define RVA_FMOD_PLAY_TRACK       0x003097E0u
#define RVA_FMOD_SWITCH_CROSSFADE 0x003089F0u

/* ── Ability / battle streaming SFX (magic DLL SeSep → FMOD) ────────────────
 * Evidence: docs/reverse/FFX_ABILITY_SFX_FMOD_STREAMING_INFERNO_2026-06-15.md
 */
#define RVA_FMOD_SFX_PLAY_BATTLE_STREAMING       0x0030D270u  /* FFX_FmodSfx_PlayBattleStreaming */
#define RVA_FMOD_SFX_START_SEQUENCE              0x0030CDB0u  /* FFX_FmodSfx_StartSequence */
#define RVA_MAGIC_BATTLE_STREAMING_HANDOFF       0x0041D000u  /* FFX_Magic_BattleStreamingSoundHandoff */
#define RVA_MAGIC_REGISTER_PENDING_SESSEP          0x003FFEC0u  /* FFX_Magic_RegisterPendingSeSepRecord */
#define RVA_MAGIC_PROCESS_PENDING_QUEUE            0x00400090u  /* FFX_Magic_ProcessPendingQueue_structural */
#define RVA_SOUND_CMD_BATTLE_STREAMING_HANDLER     0x00308490u  /* FFX_SoundCmd_HandlerBattleStreaming */
#define RVA_FFX_MAGIC_CURRENT_MAGIC_ID             0x00864CA0u  /* g_FFX_MagicCurrentMagicId (n146) */
#define RVA_FFX_BATTLE_STREAMING_SEQUENCE_ID       0x00849498u  /* g_FFX_BattleStreamingSequenceId */

/* Battle-entry music (FSM case 8) — async preload path before FMOD PlayTrack.
 * FFX_Music_PrepBattleTrack @ 0x886940 — 5-byte JMP thunk to opcode-26 wrapper
 * FFX_Music_PlayTrackWithPreload @ 0x886980 → opcodes 26 chain + 39
 * FFX_Battle_PlayBattleEntryMusic @ 0x7A0060 — orchestrator (cdecl, 1 int arg):
 *   GetCurrentTrack -> StopAndFadeOut(cur) -> FadeOutAndPlayTrack(cur) ->
 *   PrepBattleEntry(new) -> PlayTrackWithPreload(new).
 *   This is the FULL battle-entry recipe used by FFX_Battle_BattleEntryStateMachine
 *   and FFX_Battle_VictoryFlow. Calling PlayTrackWithPreload alone leaves the
 *   current track holding the channel, so the new track returns 0 but produces
 *   no audio. Using this orchestrator fixes CustomMix override track 145.
 * Evidence: docs/reverse/FFX_ARENA_PLUS_MUSIC_BATTLE_ENTRY_RE_FINDINGS_2026-06-15.md
 *           IDA MCP decompile 2026-08-05 (work/_ida_caller2.py, _ida_caller3.py) */
#define RVA_MUSIC_PREP_BATTLE_TRACK       0x00486940u
#define RVA_MUSIC_PLAY_TRACK_WITH_PRELOAD 0x00486980u
#define RVA_BATTLE_PLAY_ENTRY_MUSIC       0x003A0060u

/* ── Battle damage cap (single-hit 9999 / 99999) ───────────────────────────
 * FFX_Battle_ComputeHitDamage @ PE RVA 0x38E680 (IDA flat 0x78E680 = RVA+0x400000)
 * Evidence: docs/reverse/FFX_DAMAGE_CAP_CLAMP_IDA_2026-06-15.md
 * IMPORTANT: runtime hooks use PE RVAs from GetModuleHandle — NOT IDA+0x400000.
 */
#define RVA_FFX_BATTLE_COMPUTE_HIT_DAMAGE        0x0038E680u
#define RVA_FFX_BATTLE_DAMAGE_CAP_CLAMP_JLE      0x0038EDD5u  /* jle before mov eax,ebx */
#define RVA_FFX_BATTLE_DAMAGE_WRITEBACK          0x0038EDD9u  /* mov [esi], eax */
#define RVA_FFX_BATTLE_DAMAGE_POST_WRITEBACK     0x0038EDDBu  /* add [ecx+650h], eax */
#define RVA_FFX_BATTLE_DAMAGE_CAP_BDL_MOV        0x0038ED41u  /* mov ebx, 99999 */
#define FFX_CMD_NOVA_ENCODED                     0x3073u      /* command.bin #115 */

/* ── Kimahri Ronso Mana (partial OD pool) ───────────────────────────────────
 * PE RVAs for runtime hooks (IDA flat = PE_RVA + 0x400000 for these bands).
 * Evidence: docs/reverse/FFX_RONSO_MANA_HOOK_IDA_2026-06-15.md
 * UI tree chain (semantic drift S02, v2.116.0.2): docs/reverse/FFX_RE_SEMANTIC_DRIFT_AUDIT_2026-06-15.md
 */
#define RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX        0x00394030u  /* FFX_Field_GetActorRecord — battle/field actor record; stride 0xF90 */
#define RVA_FFX_FIELD_GET_ACTOR_RECORD           RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX
#define RVA_FFX_BATTLE_GET_OVR_CHARGE            0x00395560u
#define RVA_FFX_BATTLE_GET_OVR_CHARGE_MAX        0x003955A0u
#define RVA_FFX_BATTLE_CTB_EDGE_OVERDRIVE_EVENT 0x003B13D0u  /* FFX_Battle_CtbEdgeOverdriveEvent — called when a valid CTB turn edge passes */
#define RVA_FFX_BATTLE_OVERDRIVE_ADD_CLAMP       0x003B15A0u
#define RVA_FFX_BATTLE_RESOLVE_TARGET_MASK       0x00394340u  /* sub_794340 — resolve ATEL target sentinel/literal to runtime target mask */
#define RVA_FFX_BATTLE_QUEUE_SCRIPT_COMMAND      0x003AC9E0u  /* sub_7AC9E0 — queue battle script command for the current actor */
#define RVA_FFX_BATTLE_OVERDRIVE_READY_GATE      0x004953F0u  /* G1: charge==max */
#define RVA_FFX_BATTLE_SUBMENU_OVR_ROW_BUILD     0x00497F80u  /* G3 legacy — hooks HUD */
#define RVA_FFX_BATTLE_SUBMENU_REFRESH           0x00492040u  /* G3': post-call row patch */
#define RVA_FFX_BTL_CMD_USABILITY_GATE           0x0038ABE0u  /* sub_78ABE0: confirm/grey gate. charge(0x5BC) >= entry+0x26(CostOverdrive). caller=0x00392260 */
#define RVA_FFX_BTL_MENU_CONFIRM_HANDLER         0x00392260u  /* sub_792260: confirm handler; only caller of the usability gate. Gates it behind src[3]!=0 + v20==0 */
#define RVA_FFX_BTL_GET_MENU_CONTEXT             0x003B0A20u  /* sub_7B0A20: pure getter -> active command-menu context (unk_112AC70) or 0 */
#define RVA_FFX_BATTLE_ACTION_AFTERMATH          0x0038F0B0u
#define RVA_FFX_BATTLE_OVR_CHARGE_ZERO_AFTER_ACTION 0x0038F1E5u /* G2: mov [+5BCh],0 */
#define RVA_FFX_BATTLE_OVERDRIVE_READY_GATE_CALLER  0x004958B0u  /* calls gate */
#define RVA_FFX_KERNEL_GET_COMMAND_ENTRY_BY_ID   0x00390AE0u  /* FFX_Kernel_GetCommandEntryById(cmdIndex, 0) → entry ptr */
#define RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU  0x0039BB70u  /* FFX_Btl_BuildActorCommandMenu */
#define RVA_FFX_BATTLE_REFRESH_ACTOR_MENU        0x0039B500u  /* FFX_Btl_RefreshActorBattleMenu */
#define RVA_FFX_BATTLE_SET_ACTOR_COMMAND_BIT     0x0039C090u  /* FFX_Btl_SetActorCommandBit */
#define RVA_FFX_BATTLE_HAS_COMMAND_BIT           0x0039AD40u  /* FFX_Btl_HasCommandBit (79AD40) */
#define RVA_FFX_BATTLE_HAS_COMMAND_BIT_SAVE      0x003850E0u  /* FFX_HasCommandBit (7850E0) party bank */
/* 2026-06-16 FIX: the command ring buffer is allocated at RUNTIME; its absolute
 * pointer lives in a BSS cell. 7AEFC0/79BB70 do `mov edi,[cell]` (DEREF) then index
 * +20592 (sort scratch) / +1144*slot (per-actor ring). The old _BSS_BASE below was the
 * raw cell address used WITHOUT deref AND off by 0x1000 — so every ring write (hudSafe
 * 11..21) missed the live buffer. Use _BASE_PTR and dereference it. */
#define RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR       0x01F10CD8u  /* &dword holding runtime ringBase; *(u32*)(g_base+THIS)=real base (imm32 of `mov edi,[..]` @7AEFC8) */
#define RVA_FFX_BATTLE_COMMAND_RING_BSS_BASE       0x01F0FCD8u  /* DEPRECATED (wrong: raw cell -0x1000, no deref) — kept for reference only */
#define RVA_FFX_BATTLE_ACTOR_MENU_LAYOUT           RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR
#define FFX_BATTLE_COMMAND_RING_TEMPLATE_OFF     20592u       /* ringBase+20592 = 7AEFC0 sort-key SCRATCH (not command source); per-slot ring @ ringBase+1144*slot */
#define RVA_FFX_BATTLE_SYNC_COMMAND_MENU_TEMPLATE 0x003AEFC0u  /* FFX_Btl_SyncCommandMenuTemplate (7AEFC0) */
#define FFX_BATTLE_COMMAND_RING_SLOT_STRIDE      1144u
#define FFX_BATTLE_COMMAND_RING_OD_OFFSET        296u
#define FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT    24u
#define FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT    20u
#define FFX_BATTLE_COMMAND_RING_CAT_STRIDE       572u
#define FFX_BATTLE_COMMAND_RING_CAT_BASE         20u
#define FFX_BATTLE_COMMAND_RING_SPECIAL_OFFSET   232u
#define FFX_BATTLE_COMMAND_RING_SPECIAL_SLOT_COUNT 32u
#define FFX_BATTLE_MENU_LAYOUT_STRIDE            0x478u
#define FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF        0x128u
#define FFX_BATTLE_MENU_OVERDRIVE_SLOT_COUNT     24u
#define RVA_FFX_BATTLE_MENU_ROUTE_SWITCH_VS_OD   0x00392170u  /* FFX_Btl_MenuRoute_SwitchVsOverdrive */
#define RVA_FFX_BATTLE_IS_OVERDRIVE_READY_MENU   0x0039AF70u  /* FFX_Btl_IsOverdriveReadyMenu (79AF70) */
#define RVA_FFX_BATTLE_UI_BUILD_COMMAND_RING     0x003ACEC0u  /* FFX_Btl_UI_BuildCommandRing (7ACEC0) */
#define RVA_FFX_BATTLE_MENU_INPUT_DISPATCH       0x00392AB0u  /* FFX_Btl_BattleMenuInputDispatch (792AB0) */
#define RVA_FFX_BATTLE_MENU_EARLY_RETURN_FLAG    0x00D2A8E4u  /* unk_112A8E4 — 792AB0 early exit w/o clearing +0xDF7 */
#define RVA_FFX_BATTLE_UI_PUSH_MENU_TREE_ENTRY   0x00397B80u  /* FFX_Btl_UI_PushMenuTreeEntry (797B80) */
#define RVA_FFX_BATTLE_UI_RESOLVE_MENU_TREE_NODE 0x00397D60u  /* FFX_Btl_UI_ResolveMenuTreeNode (797D60) */
#define RVA_FFX_BATTLE_UI_FINISH_MENU_TREE       0x003979E0u  /* FFX_Btl_UI_FinishMenuTree (7979E0) */
#define RVA_FFX_BATTLE_UI_MENU_STACK_RESET       0x00398000u  /* FFX_Btl_UI_MenuStackReset (798000) */
#define RVA_FFX_BATTLE_UI_MENU_STACK             0x00D34564u  /* dword_1134564 — menu tree stack depth @+0 */
#define RVA_FFX_BATTLE_UI_DISPLAY_BLOB_CASE4     0x00D35DF0u  /* dword_1134564[1571] — case-4 display blob ptr */
#define RVA_FFX_BATTLE_UI_MENU_BLOB_TYPE2_BASE   0x00D2A994u  /* unk_112A994 — array[8]: blob ptrs per a2 slot */
#define RVA_FFX_BATTLE_UI_MENU_BLOB_TYPE2_SLOT1  0x00D2A9B4u  /* unk_112A9B4 — case-2 a2=1 blob ptr (player tree) */
#define RVA_FFX_BATTLE_UI_MENU_DATA_BASE_PTR     0x00D2A9A8u  /* unk_112A9A8 — menu data base (783ED0 sets blobs from this) */
#define RVA_FFX_BATTLE_UI_LOOKUP_MENU_BLOB       0x003985A0u  /* FFX_Btl_UI_LookupMenuBlob (7985A0) — shared w/ camReq */
#define RVA_FFX_BATTLE_UI_WALK_MENU_BLOB_INDEX   0x00397420u  /* FFX_Btl_UI_WalkMenuBlobIndex (797420) — shared w/ camReq */
#define RVA_FFX_BATTLE_UI_OPEN_SUBMENU           0x0049BA80u  /* FFX_Btl_UI_OpenSubmenu */
#define RVA_FFX_BATTLE_AGGREGATE_ACTOR_PROPERTY  0x003B2DD0u  /* FFX_Battle_AggregateActorProperty (7B2DD0) */
#define RVA_FFX_BATTLE_RESOLVE_HIT_PRECHECK      0x0038C330u  /* FFX_Battle_ResolveHitDamagePrecheck_structural */
#define RVA_FFX_FIELD_RESOLVE_ENCOUNTER_TOKEN    0x003828B0u  /* FFX_Field_ResolveEncounterToken (7828B0) */

/* ── Field RT2 probe (encounter zones + map texture streaming) ─────────────
 * Evidence:
 *   docs/reverse/FFX_MAPOUT_VPA_ENCOUNTER_ZONES_IDA_2026-06-16.md
 *   docs/reverse/FFX_PHYRE_TEXTURE_LOAD_HOOK_SPEC_2026-06-07.md
 */
#define RVA_FFX_BATTLE_ENCOUNTER_EXE               0x00380DE0u  /* MsBattleEncountExe (780DE0) */
#define RVA_FFX_BATTLE_ACTIVE_FLAG                 0x00D2A8E0u  /* g_FFX_BattleActive — 1 during battle (MemoryMap ADDR_BATTLE_ACTIVE) */
#define RVA_FFX_FIELDMAP_DECODE_ENCOUNTER_GROUP    0x0043E980u  /* FFX_FieldMap_DecodeEncounterGroupFromPolyMeta (83E980) */
#define RVA_FFX_FIELD_RESOLVE_ENCOUNTER_ZONE_INDICES 0x00475AC0u /* FFX_Field_ResolveEncounterZoneIndices (875AC0) */
#define RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A        0x00F28230u  /* byte_1325B61[9935] — zone index A after resolver */
#define RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B        0x00F28231u  /* byte_1325B61[9936] — zone index B after resolver */
#define FFX_SCENE_STATE_ENCOUNTER_GROUP_OFFSET     0x10u        /* group byte @ scene+0x10 → MsBattleEncountExe arg2 */
#define RVA_FFX_SCENE_STATE_ENCOUNTER_GROUP_BYTE   (RVA_FFX_SCENE_STATE_OBJECT + FFX_SCENE_STATE_ENCOUNTER_GROUP_OFFSET)

/* Field Scout MAX MODE — ATEL natives (docs/reverse/FFX_MAPOUT_VPA_ENCOUNTER_ZONES_IDA_2026-06-16.md + IDA table @0xC51600) */
#define RVA_FFX_ATEL_COMMON_OBTAIN_TREASURE        0x0045A740u  /* FFX_Atel_Common_obtainTreasure func 0x015B @ 0x85A740 */
#define RVA_FFX_ATEL_COMMON_OBTAIN_TREASURE_SILENT 0x004579E0u  /* obtainTreasureSilently func 0x01A7 @ 0x8579E0 */
#define RVA_FFX_ATEL_CMD_SET_ACTOR_POSITION        0x0045F8E0u  /* warpToPoint ATEL wrapper → WarpActorToPosition @ 0x85F8E0 */
#define RVA_FFX_FIELD_WARP_ACTOR_TO_POSITION       0x00470AC0u  /* FFX_Field_WarpActorToPosition @ 0x870AC0 */
#define RVA_FFX_ATEL_LOAD_TAKARA_ROW               0x00398FE0u  /* sub_798FE0 — takara index → reward row (obtainTreasure path) */
#define RVA_FFX_FIELD_SAMPLE_ENCOUNTER_ZONE_SLOT   0x00475BA0u  /* FFX_Field_SampleEncounterZoneSlot @ 0x875BA0 */
#define RVA_FFX_PSDATA_BUILD_TEXTURE_SLOT_LOADTIME 0x002451F0u  /* FFX_Ps3Data_BuildTextureSlotRecord_LoadTime (6451F0) */

/* ── Field Scout (walk manifest: player anchor + asset paths) ───────────────
 * Evidence: docs/reverse/FFX_FIELD_MAPLOAD_WARP_RE_2026-06-06.md
 */
#define RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR        0x00F00740u  /* g_FFX_ControlledChrInstance @ 0x1300740 */
#define RVA_FFX_SCENE_STATE_OBJECT                 0x00D2CA90u  /* g_FFX_SceneStateObject @ 0x112CA90 */
#define FFX_CHR_INSTANCE_WORLD_X_OFFSET            0x0Cu        /* float X/Y/Z @ +0x0C/+0x10/+0x14 */
#define FFX_SCENE_STATE_SCENE_ID_OFFSET            0x0u         /* dword sceneId @ +0 */
#define FFX_SCENE_STATE_MAP_TOKEN_OFFSET           0x4u         /* dword map token @ +4 */

/* Field Scout geometry — scene load chain (docs/reverse/FFX_PHYRE_SCENELOAD_MATERIAL_RE_2026-06-06.md) */
#define RVA_FFX_FIELDMAP_LOAD_ENTRY_GRAPHIC_FIELDMAP 0x002403C0u  /* FFX_FieldMap_LoadEntry_graphicFieldMapLoad (6403C0) */
#define RVA_FFX_FIELDMAP_LOAD_AND_ACTIVATE_DRIVER    0x0025CD70u  /* FFX_FieldMap_LoadAndActivateDriver (65CD70) */
#define RVA_FFX_PHYRE_GET_INSTANCE_NAME_BY_INDEX     0x002F42F0u  /* FFX_Phyre_GetInstanceNameByIndex (6F42F0) */
#define RVA_FFX_PHYRE_PSCENENODE_COMPOSE_WORLD_MATRIX 0x001067C0u  /* Phyre_PSceneNode_composeWorldMatrix_parentChain (5067C0) */
#define RVA_FFX_FIELDMAP_BIND_MATERIAL_TEXTURE_SAMPLER 0x002F6D40u  /* FFX_FieldMap_BindMaterialTextureSampler (6F6D40) — NOT PNode setup; binds TextureSampler into material slot */
#define RVA_FFX_FIELDMAP_SETUP_SCENE_NODE            RVA_FFX_FIELDMAP_BIND_MATERIAL_TEXTURE_SAMPLER  /* deprecated alias — FieldScout hook ABI was wrong */
#define RVA_FFX_FIELDMAP_WIRE_INSTANCE_TO_SCENE_NODES 0x0025B0F0u  /* sub_65B0F0 — WireInstanceToSceneNodes: PMeshInstance → PNode wiring */
#define RVA_FFX_FIELDMAP_COMMIT_INSTANCE_MAPPINGS   0x0025A850u  /* sub_65A850 — commit instance-wire mappings after all instances processed */
#define RVA_FFX_CHR_SET_WORLD_POSITION               0x0042B500u  /* FFX_Chr_SetWorldPosition (82B500) inst+0x0C XYZ */
#define RVA_FFX_CHR_SET_SCALE_AXIS                   0x0042B5E0u  /* FFX_Chr_SetScaleAxis (82B5E0) — per-axis scale at inst+0x5C */
#define RVA_FFX_ACTIVE_CHR_INSTANCE_COUNT            0x01FC44E0u  /* FFX_ActiveChrInstanceCount (23C44E0) */
#define RVA_FFX_ACTIVE_CHR_INSTANCE_TABLE            0x01FC44E4u  /* FFX_ActiveChrInstanceTable ptr (23C44E4) */
#define FFX_ACTIVE_CHR_INSTANCE_STRIDE               0x880u
#define FFX_CHR_INSTANCE_SCALE_X_OFFSET              0x5Cu       /* float scaleXYZ @ +0x5C (3 floats: X/Y/Z) */
#define FFX_PNODE_LOCAL_MATRIX_OFFSET                0x10u         /* PNode::m_localMatrix inline PMatrix4 */
#define FFX_PNODE_WORLD_MATRIX_PTR_OFFSET            0x0Cu         /* PNode::m_worldMatrix ptr cache */
#define FFX_PNODE_NAME_OFFSET                        0x50u         /* PNode::m_name PString */
#define FFX_CHR_INSTANCE_ID_OFFSET                   0x0u          /* u16 packedId @ +0 */
#define FFX_CHR_INSTANCE_ACTIVE_OFFSET               0x2u          /* u8 active @ +2 */
#define FFX_CHR_INSTANCE_NAME_PTR_OFFSET             0x4u          /* char* model name @ +4 e.g. c001 */

/* ── Battle End cleanup pipeline (Arena+ Lane 3, Jarvis-ARENA 2026-06-16) ────
 * RE: docs/reverse/FFX_ARENA_PLUS_BATTLE_END_HOOK_RE_2026-06-16.md
 * IDA renames + comments saved in work/reverse/ida/FFX_recon.i64.
 *
 * Pipeline (from FFX_Btl_MainBattleTick @ 0x790C60, branch (v1 & 0x20) != 0):
 *   sub_780C10();
 *   FFX_Battle_EndCleanupDispatcher();   // <- our scaffold hook target
 *   sub_781DD0(); sub_782830(); sub_7817D0(); sub_782800();
 *   return FFX_Battle_InitEncounterFromBtlbin(...);
 *
 * FFX_Battle_EndCleanupDispatcher (small: 0x31 bytes) reads the global
 *   dword_1134564[1743] (= [0x11360A0]) — non-zero means an active battle effect
 *   handle is being torn down. Calls FFX_Battle_EffectFreeAtEnd (which emits the
 *   literal log "(op)\top_et_battle_effect_free( battle end )"), releases via
 *   sub_6871B0, then zeroes the handle. Hooking the cleanup dispatcher is the
 *   safest single-function entry point for "a battle just finished".
 *
 * NOT YET MAPPED (TODO Lane 3 RT2 spike, Halyson):
 *   - Distinguishing victory vs defeat vs escape. The old sub_888CE0/sub_888AF0
 *     hypothesis was reconciled as input/pad, so the real battle-end outcome
 *     word still needs live capture from the correct battle-state path.
 *   - Mapping the battle handle [0x11360A0] back to a recognizable battle_id /
 *     formation_id. The current ResolverLogHook already captures the resolved
 *     tuple (field/group/entry) per ResolveEncounterToken call; correlating
 *     the LAST resolve with the dispatcher fire is the next step.
 */
#define RVA_FFX_BATTLE_END_CLEANUP_DISPATCHER    0x0039E650u  /* FFX_Battle_EndCleanupDispatcher (79E650) */
#define RVA_FFX_BATTLE_EFFECT_FREE_AT_END        0x003FB090u  /* FFX_Battle_EffectFreeAtEnd (7FB090) */
#define RVA_FFX_BATTLE_GET_NEXT_ENCOUNTER_TOKEN  0x003C5EE0u  /* FFX_Battle_GetNextEncounterToken (7C5EE0) */
#define RVA_FFX_BATTLE_MAIN_TICK                 0x00390C60u  /* FFX_Btl_MainBattleTick (790C60) */
#define RVA_FFX_BATTLE_END_EFFECT_HANDLE         0x00D360A0u  /* dword_1134564[1743] = battle-effect handle, non-zero during teardown */
#define RVA_FFX_BATTLE_GET_PLAYER_LIST_BASE      0x00395980u  /* FFX_Battle_GetPlayerListBase → g_BattlePlayerList */
#define RVA_FFX_BATTLE_PLAYER_LIST               0x00D334CCu  /* g_BattlePlayerList (IDA flat 0x11334CC) → MemoryChr[] */
#define RVA_FFX_BATTLE_LOAD_MONSTER_FILES        0x00383730u  /* FFX_Battle_LoadMonsterFilesIntoMemoryChr_structural */
#define RVA_FFX_BATTLE_INIT_ENCOUNTER            0x003810F0u  /* FFX_Battle_InitEncounterFromBtlbin */
#define RVA_FFX_BTL_ATEL_DISPATCH_OPCODE         0x003A50E0u  /* FFX_Btl_ATEL_DispatchOpcode(context, unused, scriptPtr) -> ApplyActorOpcode */
#define RVA_FFX_BTL_ATEL_APPLY_ACTOR_OPCODE      0x003B4B80u  /* FFX_Btl_ATEL_ApplyActorOpcode(instrByte, funcId, a3, n7_1) — handles ALL opcodes */
#define RVA_FFX_BATTLE_AiQueryMoveProperty       0x003B2DD0u  /* FFX_Battle_AiQueryMoveProperty(a1, propertyId, a3, *result) — 346-case switch */
#define RVA_SAVE_GIL                             0x00D307D8u  /* save RAM gil, uint32 */
#define FFX_BATTLE_CHR_STRIDE                    0xF90u

/* W2S matrix owner — BLOCKED I21 (upload/bind wrappers only; not canonical W2S owner).
 * Evidence: docs/reverse/FFX_AURORA_W2S_MATRIX_OWNER_IDA_DEEP_2026-06-15.md
 * Do not rename/hook as W2S generator until oracle proves otherwise.
 */
// #define RVA_FFX_RENDER_CONSTANT_BUNDLE_A_CANDIDATE 0x00193440u  /* sub_593440 */
// #define RVA_FFX_RENDER_CONSTANT_BUNDLE_B_CANDIDATE 0x001936A0u  /* sub_5936A0 */

#define FFX_CHARACTER_YUNA                       0x01u
#define FFX_CHARACTER_KIMAHRI                    0x03u
#define FFX_CMD_BLUE_MAGIC_MENU_ID               322u
#define FFX_CMD_BLUE_MAGIC_MENU_ENCODED          0x3142u
#define FFX_CMD_BLUE_MAGIC_CHILD_MIN             323u
#define FFX_CMD_BLUE_MAGIC_CHILD_MAX             336u
#define FFX_CMD_YUNA_WM_PLUS_CHILD_MIN           343u
#define FFX_CMD_YUNA_WM_PLUS_CHILD_MAX           347u
#define FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID         366u
#define FFX_CMD_WHITE_MAGIC_PLUS_MENU_ENCODED    0x316Eu
#define FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST            323u
#define RVA_FFX_BATTLE_APPLY_ACTION_RESULTS_LO     0x0038F0B0u
#define RVA_FFX_BATTLE_APPLY_ACTION_RESULTS_HI     0x0038F300u
/* Battle submenu OD row builder scratch (IDA flat 0x133Fxxx − 0x400000) */
#define RVA_FFX_BATTLE_SUBMENU_CMD_LIST          0x00F3F76Cu
#define RVA_FFX_BATTLE_SUBMENU_CMD_LIST_END      0x00F3F772u
#define RVA_FFX_BATTLE_SUBMENU_ROW_BASE          0x00F3F7A8u
#define FFX_BATTLE_SUBMENU_ROW_STRIDE            0x90u
#define FFX_BATTLE_SUBMENU_ROW_RATIO_OFF         0x24u
#define FFX_BATTLE_SUBMENU_UI_RATIO_FULL         100u
/* 6th cdecl param n12320 in FFX_Battle_ComputeHitDamage (push @ ApplyHitDamage_Loop+0x1C) */
#define FFX_BATTLE_COMPUTE_HIT_DAMAGE_ARG_N12320 0x1Cu
#define FFX_BATTLE_COMPUTE_HIT_DAMAGE_ELEM_FLAGS_OFF 0x2Cu /* ebp+2Ch in ComputeHitDamage */

/* ── Radiant / Umbral Ward (Nul Holy + Nul Dark) ────────────────────────────
 * Evidence: docs/reverse/FFX_RADIANT_UMBRA_WARD_HANDOFF_2026-06-15.md
 * Action pool: sub_7B09C0 @ IDA 0x7B09C0 — actor+0xDE5 slot, unk_112AC70 stride 72.
 */
#define FFX_CMD_RADIANT_WARD_ENCODED             0x3140u
#define FFX_CMD_UMBRAL_WARD_ENCODED              0x3141u
#define FFX_CMD_RADIANT_WARD_ID                  320u
#define FFX_CMD_UMBRAL_WARD_ID                   321u
#define FFX_ELEM_HOLY                            0x10u
#define FFX_ELEM_DARK                            0x80u
#define RVA_FFX_BATTLE_APPLY_HIT_DAMAGE_LOOP     0x00389800u
#define RVA_FFX_BATTLE_DAMAGE_WRITEBACK_PATCH    0x0038EDD9u  /* mov [esi], eax */
#define RVA_FFX_BATTLE_DAMAGE_WRITEBACK_RESUME   0x0038EDE1u  /* sub [ecx], eax */
#define RVA_FFX_BATTLE_ACTION_POOL_BSS           0x00D2AC70u
#define FFX_BATTLE_ACTION_POOL_STRIDE            72u
#define FFX_BATTLE_ACTOR_ACTION_SLOT_OFF         0xDE5u
#define FFX_BATTLE_ACTOR_INLINE_ACTION_OFF       0x72Cu
#define FFX_BATTLE_ACTOR_NUL_TIDE_BLOCK_OFF      0x60Eu
#define FFX_BATTLE_ACTOR_NUL_SHOCK_BLOCK_OFF     0x610u
#define FFX_BATTLE_ACTOR_NUL_HOLY_BLOCK_OFF      0x613u  /* ATEL case 56 — Radiant Ward */
#define FFX_BATTLE_ACTOR_NUL_DARK_BLOCK_OFF      0x614u  /* ATEL case 57 — Umbral Ward */
#define RVA_FFX_GRANT_COMMAND_TO_CHARACTER       0x00385D10u
#define FFX_BATTLE_MENU_COMMAND_ID_LIMIT         320u
#define FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED 367u  /* grown command.bin row count (ids 0..366) */
#define FFX_GRID_TEACH_SIDECAR_WORDS               18u   /* party bank shadow: ids 96..383 */
#define FFX_GRID_TEACH_SIDECAR_BYTES               36u
/* Sphere Grid node-activate FSM — grant LearnedMove via case 21 inside this function.
 * Evidence: docs/reverse/FFX_FRONT2_GROW_SPELL_LEARN_WIRING_2026-06-11.md (0x8CC300). */
#define RVA_FFX_SPHERE_GRID_NODE_ACTIVATE        0x004CC300u
#define RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_LO     0x004CC300u
#define RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_HI     0x004CC900u
/* Sphere grid stat/teach applier — sub_A54860 calls this with panel LearnedMove @+0x12.
 * Evidence: docs/history/FFX_SPHEREGRID_IDA_RUNTIME_PROBE_2026-06-02.md */
#define RVA_FFX_SPHERE_GRID_APPLY_LEARNED_MOVE   0x00398850u
/* Sphere Grid runtime state table (IDA 0x112EC7C):
 * 0x000 + 2*nodeIndex = low content/panel id, high status/reach mask.
 * 0xA00 + linkIndex = runtime link state. True New Node hook/compiler target.
 */
#define RVA_FFX_SPHERE_GRID_RUNTIME_STATE_TABLE  0x00D2EC7Cu
#define RVA_FFX_SPHERE_GRID_INIT_RUNTIME_STATE    0x00653DE0u  /* IDA 0xA53DE0: int __cdecl(__int16* state) */
/* A47210: use RVA_FFX_ABMAP_APPLY_ACTIVATION_STATS (was misnamed LOAD_DEFAULT_STATE) */
#define RVA_FFX_ABMAP_MENU_STATE_PTR              0x001F05834u /* IDA 0x2305834 -> live menu state pointer */
#define RVA_FFX_ABMAP_LOAD_LAYOUT                 0x00645570u  /* IDA 0xA45570: int __cdecl() dat01/02/03 load */
#define RVA_FFX_ABMAP_APPLY_STATE_TO_MENU         0x00649590u  /* IDA 0xA49590: FFX_Abmap_ApplyMenuSnapshot */
#define RVA_FFX_ABMAP_BUILD_ADJACENCY             0x0065B140u  /* IDA 0xA5B140: int __cdecl() link graph */
#define RVA_FFX_ABMAP_RECOMPUTE_STATS             0x00654860u  /* IDA 0xA54860: int __cdecl() exit/learn sweep */
#define RVA_FFX_ABMAP_NODE_PLACEMENT_ANIM         0x00647D50u  /* IDA 0xA47D50: int __cdecl() placement anim; A5BB70->A54860 when menu+71248>=20 */
#define RVA_FFX_ABMAP_ACTIVATE_NODE               0x00648910u  /* IDA 0xA48910: int __cdecl(char actor, int nodeIdx) node activation commit */
/* A5BB70: use RVA_FFX_ABMAP_PACK_MENU_SNAPSHOT (alias SAVE_MENU_TO_STATE below) */
#define RVA_FFX_ABMAP_DEACTIVATE_RETURN_TO_FIELD  0x004E27E0u  /* IDA 0x8E27E0: exit ABMAP -> field UI */
#define RVA_FFX_HEAP_ALLOC                        0x00287190u  /* IDA 0x687190: void* __cdecl(size_t, void* align) -> sub_6FB850 general allocator */
#define RVA_FFX_HEAP_ALLOC_CORE                   0x00542B60u  /* IDA 0x942B60: int __thiscall(state* ecx, int size) -> SOLE low-level allocator; ALL wrappers (sub_6FB850/sub_6FB9F0/Alloc16) funnel here. Free/coalesce is sub_9435A0. */
#define RVA_FFX_GAME_ALLOC_WRAPPER                0x00230670u  /* IDA 0x630670: int __cdecl(size) -> sub_6871C0; sub_681DB0 batch buffers */
#define RVA_FFX_MENU2D_UPLOAD_BATCHES_GPU         0x002859E0u  /* DEPRECATED alias → UPLOAD_BATCHES_TO_GPU (was wrong 0x6859E0 absolute) */
#define RVA_FFX_SG_DRAW_NODE_BATCH                0x003F4900u  /* IDA 0x7F4900: int __cdecl(a1,a2,n861) — vertex batch; n861>=861 remaps to slot n861-861 (slot 860 OOB on 860-slot buf) */
#define RVA_FFX_MENU2D_DRAW_BATCH_NEG_WRITER_PREP 0x003F4C0Fu  /* IDA 0x7F4C0F: after batch+6C writer resolve — F1 inline hook anchor */
#define RVA_FFX_MENU2D_STORE_COL_NEG_N861         0x003F50E6u  /* IDA 0x7F50E6: fst [eax+ecx*4] neg col */
#define RVA_FFX_MENU2D_COL_STRIDE_PREP_NEG        0x003F50DFu  /* IDA 0x7F50DF: col stride prep neg n861 */
#define RVA_FFX_MENU2D_STORE_POS_NEG_N861         0x003F5208u  /* IDA 0x7F5208: fstp [eax+ecx*4] neg pos */
#define RVA_FFX_MENU2D_STORE_UV_NEG_N861         0x003F54AAu  /* IDA 0x7F54AA: fst [eax+edx*4] neg uv */
#define RVA_FFX_MENU2D_STORE_COL_POS_N861EXT     0x003F56A4u  /* IDA 0x7F56A4: fst [eax+ecx*4] pos col ext */
#define RVA_FFX_MENU2D_STORE_POS_POS_N861         0x003F57E6u  /* IDA 0x7F57E6: fstp [eax+ecx*4] pos pos ext */
#define RVA_FFX_MENU2D_STORE_UV_POS_N861EXT      0x003F5B7Eu  /* IDA 0x7F5B7E: fstp [eax+ecx*4] pos uv ext */
#define RVA_FFX_ABMAP_DRAW_PANEL_NODES_PREP       0x0064FE40u  /* IDA 0xA4FE40: alternate draw loop; sub_7F4900(..., v3-NodeCount) */
#define RVA_FFX_ABMAP_DRAW_RUNTIME_PANEL          0x00651340u  /* IDA 0xA51340: primary draw; menu+63528 writer table; n861=NodeCount-iter+860 / -862 */
#define RVA_FFX_ABMAP_BUILD_NODE_DRAW_COORDS      0x0065AD30u  /* IDA 0xA5AD30: float* out, int16* nodeRec, float scale */
#define RVA_FFX_ABMAP_POPULATE_LINK_BATCHES       0x0065A800u  /* IDA 0xA5A800: post-activation link batch builder -> A57710/A581F0 */
#define RVA_FFX_ABMAP_RUN_PLACEMENT_FX            0x00645930u  /* IDA 0xA45930: FFX_Abmap_RunPlacementFx — A51700 callback; menu+70560/+70688 */
#define RVA_FFX_ABMAP_UPDATE_PLACEMENT_SLOT       0x00658080u  /* IDA 0xA58080: FFX_Abmap_UpdatePlacementSlotTransform — 8×80B @ menu+69768 */
#define RVA_FFX_ABMAP_BUILD_LINK_BATCH_SEGMENT    0x00657710u  /* IDA 0xA57710: link geometry segment writer */
#define RVA_FFX_MENU2D_PROJECT_NODE_COORDS        0x002ED700u  /* IDA 0x6ED700: project node coords for draw payload */
#define RVA_FFX_MENU2D_DRAW_QUAD_INDEXED          0x003F4900u  /* alias RVA_FFX_SG_DRAW_NODE_BATCH */
#define RVA_FFX_HEAP_ALLOC_DISPATCH               0x00545A00u  /* IDA 0x945A00: size-tier router -> 942B60 boot arena for mid sizes */
#define RVA_FFX_ABMAP_UIMODE19_DEACTIVATE_CB      0x004E27B0u  /* IDA 0x8E27B0: slot-19 deactivate callback */
#define RVA_FFX_ABMAP_RELEASE_GPU_ON_EXIT         0x00654720u  /* IDA 0xA54720: GPU resource teardown on exit */
#define RVA_FFX_ABMAP_EXIT_FULL_UI_FLUSH          0x00654660u  /* IDA 0xA54660: render teardown incl. A51340 */
#define RVA_FFX_ABMAP_EXIT_CONFIRM_HANDLER        0x00656060u  /* IDA 0xA56060: A5BB70->A54860->8E27E0 exit */
#define RVA_FFX_ABMAP_UPDATE_CAMERA_SCROLL        0x00653570u  /* IDA 0xA53570: int __cdecl() pad/camera scroll + UI dispatch tick */
#define RVA_FFX_ABMAP_DIALOG_DISPATCH             0x006583E0u  /* IDA 0xA583E0: __int16 __cdecl() — invokes A56060 via menu callback */
#define RVA_FFX_ABMAP_ANIM_INDEX_TICK             0x00647440u  /* IDA 0xA47440: int __cdecl() — post-dialog fade in A53570 tail */
#define RVA_FFX_ABMAP_RENDER_FRAME_HOOK           0x004E2720u  /* IDA 0x8E2720: int __cdecl(int ctx) — parallel render teardown path */
#define RVA_FFX_ABMAP_EXIT_RENDER_TEARDOWN        0x00654560u  /* IDA 0xA54560: int __cdecl() — 8E2720 → A54660 gate */

/* ── SGM 861 / ABMAP extended (Jarvis-MAGIC-SGM 2026-06-23) ─────────────────
 * Evidence: docs/reverse/FFX_SPHEREGRID_861_IDA_FIELD_MAP_2026-06-23.md §10-11
 *           docs/reverse/FFX_EXE_FUNCTION_NAMING_GOAL_2026-06-17.md (ABMAP/Menu2D)
 * Note: goal replay has ~1032 IDA symbols; this header keeps hook-operational RVAs only.
 */

/* Activate / placement chain (beyond existing A48910/A45930/A58080 detours) */
#define RVA_FFX_ABMAP_QUEUE_PLACEMENT_ANIM        0x0065BAD0u  /* IDA 0xA5BAD0: FFX_Abmap_QueuePlacementAnim */
#define RVA_FFX_ABMAP_DISPATCH_ACTIVATION_ANIM    0x0065AA30u  /* IDA 0xA5AA30: FFX_Abmap_DispatchActivationAnim; menu+71252..71270 */
#define RVA_FFX_ABMAP_PLACEMENT_FX_CALLBACK       0x00651700u  /* IDA 0xA51700: FFX_Abmap_PlacementFxCallback → A59350 → A45930 */
#define RVA_FFX_ABMAP_SWAP_ANIM_CALLBACK_CHAIN    0x00648280u  /* IDA 0xA48280: FFX_Abmap_SwapAnimCallbackChain; menu+71080..71092 */
#define RVA_FFX_ABMAP_DISPATCH_PLACEMENT_SFX      0x00659350u  /* IDA 0xA59350: FFX_Abmap_DispatchPlacementSfx; menu+71256..71270 */
#define RVA_FFX_ABMAP_APPLY_ACTIVATION_STATS      0x00647210u  /* IDA 0xA47210: FFX_Abmap_ApplyActivationStats (stats/SFX; no GPU batch). Goal name drift: LoadDefaultStateResourceAndValidate */
#define RVA_FFX_ABMAP_INIT_AND_ENTER_MENU         0x00654B40u  /* IDA 0xA54B40: FFX_Abmap_InitAndEnterMenu — load layout, populate batches */
#define RVA_FFX_ABMAP_DRAW_NODE_BY_INDEX          0x00651560u  /* IDA 0xA51560: FFX_Abmap_DrawNodeByIndex — layout index draw, not batch slot 860 */
#define RVA_FFX_ABMAP_EXIT_PANEL_TRANSITION       0x00658660u  /* IDA 0xA58660: FFX_Abmap_ExitPanelTransitionDispatch — 12 panel slots */
#define RVA_FFX_ABMAP_LOOKUP_NODE_LINK_COORDS     0x0065B4C0u  /* IDA 0xA5B4C0: node/link coords for PopulateLinkBatches */
#define RVA_FFX_ABMAP_BUILD_LINK_BATCH_END        0x006581F0u  /* IDA 0xA581F0: FFX_Abmap_BuildLinkBatchEnd — 12-float link quad */
#define RVA_FFX_ABMAP_RECOMPUTE_LINK_BUCKETS      0x0065A760u  /* IDA 0xA5A760: FFX_Abmap_RecomputeLinkEndpointBuckets (goal) */
#define RVA_FFX_ABMAP_PACK_MENU_SNAPSHOT          0x0065BB70u  /* IDA 0xA5BB70: alias semantic — PackMenuSnapshot (was SaveMenuToState) */
#define RVA_FFX_SPHERE_GRID_RUNTIME_SCRATCH       0x00385000u  /* IDA 0x785000: FFX_SphereGrid_GetRuntimeStateTable scratch buffer */

/* Menu2D capture / anim batch chain — OOB producer bypass (animObj+48, not menu+0xF858) */
#define RVA_FFX_MENU2D_FLUSH_CAPTURE_BATCH48      0x00312330u  /* IDA 0x712330: FFX_Menu2D_FlushCaptureBatch48(a1, stride=48) */
#define RVA_FFX_MENU2D_INIT_CAPTURE_ANIM_OBJECT   0x003124A0u  /* IDA 0x7124A0: FFX_Menu2D_InitCaptureAnimObject */
#define RVA_FFX_MENU2D_INSERT_CAPTURE_BATCH_ENTRY 0x00316850u  /* IDA 0x716850: FFX_Menu2D_InsertCaptureBatchEntry — list @ animObj+48 */
#define RVA_FFX_MENU2D_FLUSH_CAPTURE_SLOTS        0x00312C60u  /* IDA 0x712C60: FFX_Menu2D_FlushCaptureSlots */
#define RVA_FFX_MENU2D_CLEAR_CAPTURE_BATCH_LIST   0x00312000u  /* IDA 0x712000: FFX_Menu2D_ClearCaptureBatchList — pre-free animObj+60 */
#define RVA_FFX_MENU2D_UPLOAD_CAPTURE_BATCH_CHAIN 0x00316D20u  /* IDA 0x716D20: FFX_Menu2D_UploadCaptureBatchChain */
#define RVA_FFX_MENU2D_PPP_MEM_ALLOC              0x00316B10u  /* IDA 0x716B10: FFX_Menu2D_PppMemAlloc → 72C4C0 anim heap */
#define RVA_FFX_MENU2D_RESOLVE_CAPTURE_CTX_CORE   0x00284E70u  /* IDA 0x684E70: capture ctx resolver (639180 → this, layer 3) */
#define RVA_FFX_MENU2D_BEGIN_FRAME_RESET_BATCHES  0x002424D0u  /* IDA 0x6424D0: FFX_Menu2D_BeginFrameResetBatches (goal) */
#define RVA_FFX_MENU2D_RESET_CAPTURE_BATCH_COUNTS 0x00239270u  /* IDA 0x639270: FFX_Menu2D_ResetCaptureBatchCounts (goal) */
#define RVA_FFX_MENU2D_ENQUEUE_QUAD_CAPTURED      0x0023EAE0u  /* IDA 0x63EAE0: FFX_Menu2D_EnqueueQuadCaptured (goal) */
#define RVA_FFX_MENU2D_IS_CAPTURE_RECORDING       0x00241210u  /* IDA 0x641210: FFX_Menu2D_IsCaptureRecording (goal) */

/* Heap helpers (SGM dual-buffer / boot arena) */
#define RVA_FFX_HEAP_FREE_COALESCE                0x005435A0u  /* IDA 0x9435A0: boot-arena free/coalesce (41252 slab cells) */
#define RVA_FFX_HEAP_ALLOC_BOOT_ALIGNED           0x002FB850u  /* IDA 0x6FB850: FFX_Heap_AllocBootAligned — memset 0xCD, align wrapper */
#define RVA_FFX_HEAP_ALLOC_GAME                   0x002871C0u  /* IDA 0x6871C0: game heap wrapper → 6FB9F0 */

/* ABMAP live menu struct offsets (base = *g_FFX_AbmapMenuStatePtr @ menu+0) */
#define FFX_ABMAP_MENU_NODE_COUNT_OFF               0x02u
#define FFX_ABMAP_MENU_LINK_COUNT_OFF               0x04u
#define FFX_ABMAP_MENU_NODE_RECORD_OFF              2056u
#define FFX_ABMAP_MENU_NODE_RECORD_STRIDE           40u
#define FFX_ABMAP_MENU_LINK_RECORD_OFF              43016u
#define FFX_ABMAP_MENU_WRITER_TABLE_OFF             63528u   /* 0xF858 — patch writer +0/+4 only, NOT +8 payload */
#define FFX_ABMAP_MENU_WRITER_ENTRY_STRIDE          48u
#define FFX_ABMAP_MENU_PLACEMENT_SLOT_OFF           69768u   /* 80×n placement slots (A58080) */
#define FFX_ABMAP_MENU_PLACEMENT_ANIM_OFF            69828u   /* 8× anim timer slots only (A47D50) */
#define FFX_ABMAP_MENU_PROJECTION_A_OFF              70560u
#define FFX_ABMAP_MENU_PROJECTION_DRAW_OFF          70624u
#define FFX_ABMAP_MENU_PROJECTION_B_OFF              70688u
#define FFX_ABMAP_MENU_GRID_SCALE_OFF                70480u
#define FFX_ABMAP_MENU_CALLBACK_CHAIN_OFF            71080u
#define FFX_ABMAP_MENU_ANIM_DISPATCH_OFF             71252u
#define FFX_ABMAP_MENU_ACTIVATION_NODE_OFF           71336u
#define FFX_ABMAP_MENU_ACTIVATION_FLAGS_OFF          71340u

/* Anim capture object offsets (712330 / 716850 chain) */
#define FFX_MENU2D_ANIMOBJ_CAPTURE_LIST_OFF         48u      /* linked batch nodes — bypasses menu scan */
#define FFX_MENU2D_ANIMOBJ_SLOT_ARRAY_OFF           60u
#define FFX_MENU2D_ANIMOBJ_UPLOAD_COUNT_OFF           24u

/* SGM 861 batch geometry (41252 vanilla vs 41328 pinned) */
#define FFX_SG_POS_BATCH_VANILLA_BYTES              41252u   /* 860×48−28 — slot860 @ +41280 = OOB */
#define FFX_SG_POS_BATCH_861_BYTES                  41328u   /* 861×48 — pinned target */
#define FFX_SG_BATCH_SLOT_STRIDE_BYTES              48u
#define FFX_SG_BATCH_SLOT860_OFFSET                 41280u   /* 860×48 */
#define FFX_SG_BATCH_SLOT860_OOB_TAIL               28u      /* bytes past vanilla cap */

/* Backward-compat aliases (older hook names / goal drift) */
#define RVA_FFX_ABMAP_SAVE_MENU_TO_STATE           RVA_FFX_ABMAP_PACK_MENU_SNAPSHOT
#define RVA_FFX_SPHERE_GRID_LOAD_DEFAULT_STATE     RVA_FFX_ABMAP_APPLY_ACTIVATION_STATS  /* DEPRECATED name — addr is A47210 ApplyActivationStats per IDA 2026-06-23 */

#define RVA_FFX_UIMODE_TICK_LOOP                  0x004AA1B0u  /* IDA 0x8AA1B0: int __cdecl() — UI mode slot tick pump */
#define RVA_FFX_UIMODE_ACTIVATE_SLOT              0x004AA0B0u  /* IDA 0x8AA0B0: void __cdecl(int slot, int a2) */
#define RVA_FFX_UIMODE_SLOT20_HANDOFF             0x004E2870u  /* IDA 0x8E2870: void __cdecl(int ctx) — 2nd field handoff */
#define RVA_FFX_PARTY_SAVE_SLICE_TICK             0x004AE0A0u  /* IDA 0x8AE0A0: int __cdecl(int a1) — post-mode-loop save/party */
#define RVA_FFX_MENU_DRAW_ALL_LAYERS              0x004AA240u  /* IDA 0x8AA240: void __cdecl() — 9× layer draw after pump update */
#define RVA_FFX_MENU_POOL_UPDATE_LAYER            0x004A91E0u  /* IDA 0x8A91E0: int __cdecl(int layer) */
#define RVA_FFX_FIELD_UI_DISPATCH_SLOT1           0x004E0340u  /* IDA 0x8E0340: void __cdecl(int ctx) — field UI mode slot 1 */
/* Sphere Grid exit R10 — post-DrawAllLayers pump tail (Jarvis-MAGIC 2026-06-18) */
#define RVA_FFX_MENU_CURSOR_WIDGET_FLUSH          0x004ABDF0u  /* IDA 0x8ABDF0: int __cdecl() — post-draw cursor/widget flush */
#define RVA_FFX_MENU2D_END_CAPTURE_AND_UPLOAD     0x002392A0u  /* IDA 0x6392A0: int __cdecl() — capture ctx GPU upload */
#define RVA_FFX_MENU2D_RESOLVE_CAPTURE_CTX        0x00239180u  /* IDA 0x639180: int __cdecl(char* str,int layer) -> sub_684E70(this,*,3) ctx */
#define RVA_FFX_MENU2D_UPLOAD_BATCHES_TO_GPU      0x002859E0u  /* IDA 0x6859E0: int __cdecl() — upload captured menu-2D batches (crash stack frame[1] @ exit) */
#define RVA_FFX_PHYRE_RENDER_FLUSH_TEXTURE_BINDS  0x00242560u  /* IDA 0x642560: int __cdecl() — field/menu batch composite */
/* Sphere Grid exit R11 — post-flush pump epilogue + field handoff (Jarvis-MAGIC 2026-06-18) */
#define RVA_FFX_MENU_PUMP_ALIVE_CHECK             0x004AA5C0u  /* IDA 0x8AA5C0: int __cdecl() — pump teardown gate after flush */
#define RVA_FFX_MENU_PER_FRAME_PUMP              0x004A9C50u  /* IDA 0x8A9C50: int __cdecl(uint screenWord) */
#define RVA_FFX_MENU_PUMP_ENTRY                   0x004AAFE0u  /* IDA 0x8AAFE0: void __cdecl(int ctx,int a2,int screen) */
#define RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG        0x00F407E4u  /* IDA 0x13407E4: g_FFX_MenuSubsystemActive — field 3D skip gate */
#define RVA_FFX_MENU_SUBSYSTEM_ALT_FLAG           0x00F407E8u  /* IDA 0x13407E8: alt menu gate (820860 path) */

/* F8 dashboard / UnXBoosterHook (Operacao Demonio 2026-08-02): debug flags UnX-style.
 * Absoluto 0xD2A8F8 (mesma area do F7_LeverApply / F7InLive) -> RVA. Byte layout:
 * +0x00 Invincible Enemies +0x01 Invincible Party +0x04 Always Overdrive +0x05 Always
 * Critical +0x06 Damage 1 +0x07 Damage 10000 +0x08 Damage 99999 +0x09 Rare Drop
 * +0x0A AP 100x +0x0B Gil 100x +0x15 Permanent Sensor. */
#define RVA_FFX_DEBUG_FLAGS                       0x0092A8F8u  /* 0xD2A8F8 - 0x400000 */
#define RVA_FFX_PERMANENT_SENSOR                  0x0092A90Du  /* debug flags +0x15 */
/* UnX legado: FFX_BattleParticipation 0x1F10EA0 / FFX_AP_Earn 0x1F10EC4 (absolutos). */
#define RVA_FFX_BATTLE_PARTICIPATION              0x01B10EA0u
#define RVA_FFX_AP_EARN                           0x01B10EC4u
/* DialogSkipHook (Onda 3, Operacao Demonio 2026-08-02): FFX_FmodVoice_ReadEventData
 * (absoluto 0x70B040; alvo do patch ret 8 do UnX legado — porte seguro por hook). */
#define RVA_FFX_FMODVOICE_READ_EVENT_DATA         0x0030B040u
#define RVA_FFX_MENU_LAYER_SUPPRESS_FLAG          0x00F407E0u  /* IDA 0x13407E0: suppress DrawAllLayers when set */
#define RVA_FFX_RENDER_SKIP_SUBMIT_790            0x00EFB790u  /* IDA 0x12FB790: g_Render_SkipSubmit_790 — 2D enqueue kill */
#define RVA_FFX_RENDER_DISABLED_798               0x00EFB798u  /* IDA 0x12FB798: GPU batch upload skip */
#define RVA_FFX_RENDER_ENGINE_MODE_NOTIFY         0x00486DE0u  /* IDA 0x886DE0: void __cdecl(int) — 8E27E0 calls 0x80000001 */
#define RVA_FFX_MENU2D_CAPTURE_CTX_PTR            0x00CCC838u  /* IDA 0xCCC838: g_Menu2D_CaptureCtx; +4 capture phase */
#define RVA_FFX_MENU2D_BATCH_MASTER_PTR           0x00CCC81Cu  /* IDA 0xCCC81C: g_Menu2D_BatchMaster BSS struct (NOT a pointer); sub_684E70 this; batch objs @+136..+184 */
#define RVA_FFX_SCENE_FIELD_SERVICE_TICK          0x00420C00u  /* IDA 0x820C00: int __cdecl(float dt) */

/* Boot fast-skip lab (docs/reverse/FFX_BOOT_FAST_SKIP_RE_2026-06-22.md) */
#define RVA_FFX_MENU_SUBSYSTEM_ACTIVE             0x00F407E4u  /* g_FFX_MenuSubsystemActive @ 0x13407E4 */
#define RVA_FFX_CURRENT_MENU_SCREEN_ID              0x00EFBBF0u  /* g_FFX_CurrentMenuScreenId @ 0x12FBBF0 */
#define RVA_FFX_SCENE_TRANSITION_PENDING            0x00F3080Cu  /* dword_133080C — RequestTransition pending */
#define RVA_FFX_SCENE_REQUEST_TRANSITION            0x0048E9D0u  /* FFX_Scene_RequestTransition @ 0x88E9D0 */
#define RVA_FFX_SCENE_INIT_SCENE                    0x0048DFE0u  /* FFX_Scene_InitScene @ 0x88DFE0 */
#define RVA_FFX_SCENE_BOOTSTRAP                     0x00420970u  /* sub_820970 — boot RequestTransition(23) */
#define RVA_FFX_SAVE_LOAD_ORCHESTRATOR              0x004B1580u  /* Save_LoadOrchestrator_WAKKA @ 0x8B1580 */
#define RVA_FFX_SAVE_READ_FILE                      0x00246CE0u  /* FFX_Save_ReadFile_CHAPPU @ 0x646CE0 */
#define RVA_FFX_SAVE_LOAD_DATA_FROM_BUF             0x004B5450u  /* Save_LoadSaveDataFromBuf_WAKKA @ 0x8B5450 */
#define RVA_FFX_MS_GET_SAVE_EVENT_ADDRESS           0x00385300u  /* MsGetSaveEventAddress_WAKKA @ 0x785300 */
/* Sphere Grid exit R26 — Phyre present/RT probes (Jarvis-MAGIC 2026-06-18) */
#define RVA_FFX_PHYRE_BIND_RENDER_TARGET_STACK    0x00240120u  /* IDA 0x640120: int __cdecl(int mode) — RT ring pop/push */
#define RVA_FFX_FIELD_SCENE_DRAW_DISPATCH         0x0042BCD0u  /* IDA 0x82BCD0: int __cdecl() — main field 3D draw @ 820C00 */
#define RVA_FFX_RENDER_ENGINE_NOTIFY_FRAME_END    0x00487E70u  /* IDA 0x887E70: int __cdecl(int phase) — frame epilogue notify */
#define RVA_FFX_MENU_SUBSYSTEM_SUPPRESS_FLAG      0x00F407F0u  /* IDA 0x13407F0: third 820de2 gate */
#define RVA_FFX_PHYRE_RT_BIND_ACTIVE              0x00EFB79Cu  /* IDA 0x12FB79C: RT-stack bind active flag */
/* Kimahri Ronso Rage unlock — 12 bits @ RAM (Jump=bit0 .. Nova=bit11). DebugMenu mirror. */
#define RVA_FFX_KIMAHRI_RONSO_UNLOCK             0x009307FDu
#define FFX_CMD_RONSO_RAGE_ID_MIN                104
#define FFX_CMD_RONSO_RAGE_ID_MAX                115
#define FFX_PARTY_WIDE_COMMAND_BANK_WORDS        16
/* Party-wide command bank persistence (Jarvis-MAGIC 2026-06-16, RE verdict
 * docs/reverse/FFX_NUL_WARD_TEACH_SURFACE_RE_VERDICT_2026-06-16.md):
 * FFX_Btl_PrepareSaveCommandState (0x786BC0) runs at EVERY battle init and
 * `rep movsd` 0x84 bytes from the `party_data` kernel (table id 4) into
 * dst__0=0x11307D8, which fully covers g_PartyWideCommandBank (0x11307FC, 16
 * words, ids 96..351). This OVERWRITES any prior grant of ids>=96 — so a
 * one-shot startup grant (or sphere-grid teach) of Radiant/Umbral (320/321 ->
 * bank word 14 bits 0/1) is wiped before FFX_Btl_BuildActorCommandMenu seeds
 * the actor → IsCommandAvailable(320/321)=0 → wards never reach the White-magic
 * submenu. Fix: re-assert bank word 14 |= 3 AFTER this function (lab teach_grant). */
#define RVA_FFX_BTL_PREPARE_SAVE_COMMAND_STATE   0x00386BC0u  /* FFX_Btl_PrepareSaveCommandState / InitPlySaveMenuPanel (same RVA) */
#define RVA_FFX_BTL_KERNEL_INIT_LO               0x003817D0u  /* sub_7817D0 — battle-only caller of PrepareSave (~0x381C20) */
#define RVA_FFX_BTL_KERNEL_INIT_HI               0x00382800u  /* return-addr gate hi (generous .text window) */
#define RVA_FFX_PARTY_WIDE_COMMAND_BANK          0x00D307FCu  /* g_PartyWideCommandBank (IDA 0x11307FC); word=((id-96)&0xFFF)/16, bit=(id-96)&0xF */
/* IDA: sub_7B0C30 reads encoded tokens @ entry+8 (u16 array); +4 overwritten by sub_7B0A40 */
#define FFX_BATTLE_ACTION_ENCODED_CMD_OFF        0x08u
#define FFX_BATTLE_ACTION_ENCODED_CMD_LEGACY_OFF 0x04u /* fallback only */

/* ── Item stack cap (vanilla = 99 / 0x63, byte ceiling = 255 / 0xFF) ────────
 * FFX_Inventory_AddItem @ PE RVA 0x003905A0 (IDA flat 0x7905A0).
 * Two clamp sites both push imm8 0x63 before calling FFX_Math_ClampInt(v, 0, 99).
 * Byte-narrow patch caps at 0x7F (push imm8 is sign-extended) = 127.
 * Full 255 cap requires 5-byte jmp trampoline + heap stub w/ push imm32 0FFh.
 *
 * Evidence: docs/reverse/FFX_ITEM_STACK_CAP_99_RESEARCH_2026-06-16.md
 * Lane: Jarvis-MAGIC (Halyson req 2026-06-16, raise per-slot count cap to 255).
 */
#define RVA_FFX_INVENTORY_ADD_ITEM                      0x003905A0u
#define RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_NEW            0x0039061Du  /* push 63h site #1 (new slot) */
#define RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_NEW_RESUME     0x00390622u  /* after 5-byte window @ site #1 */
#define RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_EXIST          0x0039064Du  /* push 63h site #2 (existing slot) */
#define RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_EXIST_RESUME   0x00390652u  /* after 5-byte window @ site #2 */
#define RVA_FFX_MATH_CLAMP_INT                          0x0039A0D0u  /* generic CLAMP(v, lo, hi) — DO NOT patch */
#define RVA_FFX_INVENTORY_AGGREGATE                     0x00D3081Cu  /* g_FFX_InventoryAggregate base; IDs @+0x140, Counts @+0x340 */
#define FFX_INVENTORY_ITEM_COUNT_ARRAY_OFF              0x340u
#define FFX_INVENTORY_ITEM_ID_ARRAY_OFF                 0x140u
#define FFX_INVENTORY_SLOT_COUNT                        112u
#define FFX_ITEM_STACK_CAP_VANILLA                      99u
#define FFX_ITEM_STACK_CAP_EXTENDED                     255u

/* ── Double / Triple Drop (battle item qty mult, Jarvis-MAGIC 2026-06-23) ───
 * Detour FFX_Inventory_AddItem; whitelist battle callers only.
 * Doc: docs/reverse/FFX_DOUBLE_TRIPLE_DROP_HOOK_2026-06-23.md
 */
#define FFX_ITEM_ID_NAMESPACE_MASK                      0x2000u
#define FFX_MEMORY_CHR_AUTO_ABILITIES_2_OFF             0x6BEu
#define FFX_AUTOABILITY2_DOUBLE_DROP                    0x1000u  /* entry +0x64; spare id 129 */
#define FFX_AUTOABILITY2_TRIPLE_DROP                    0x2000u  /* entry +0x64; spare id 130 */
#define FFX_BATTLE_PARTY_SCAN_SLOTS                     7u       /* max active party size */
#define RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_DROP        0x0038B820u  /* battle drop/steal pickup */
#define RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_MENU        0x003A2CC0u  /* battle target/menu */
#define RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_ACTION      0x003B0530u  /* battle action result */
#define RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_COMMAND     0x003B0C30u  /* battle command execute */

/* ── Elemental Damage ───────────────────────────────────────────────────────
 * Routine that reads ElementFlags bits and applies weakness/resistance.
 * IDA task: search for reads of the ElementFlags byte in the battle damage
 *   path; the current known bits are 0x01..0x10 (Fire/Ice/Thunder/Water/Holy).
 *   Extension target: add 0x20 (Earth), 0x40 (Wind), 0x80 (Dark).
 */
// #define RVA_ELEMENTAL_DAMAGE    0x00000000u

/* ── Scan UI: Holy/Dark element-affinity icons (Jarvis-RE-SCANUI 2026-06-16) ──
 * The Scan affinity rows hardcode only Fire/Ice/Thunder/Water (bits
 * 0x01/0x02/0x04/0x08) via inverse-masking. ElementHook detours the row drawer
 * and appends two positive-logic ball draws for Holy (0x10) and Dark (0x80),
 * sampling 2 balls authored into atlas 16128 (battle.dds.phyre) by
 * FFXProjectEditor/Tools/ScanWardBallInjectRt0.cs.
 * Full RE + ABI + authored UVs:
 *   docs/reverse/FFX_SCAN_WEAKNESS_UI_RENDER_LOOP_2026-06-16.md (§14)
 * IDA flat = PE RVA + 0x400000 for these.
 *
 *  FFX_BtlUI_DrawScanElementResistRow @ flat 0x894AB0
 *     int __cdecl(u8 actor, int cat, int x, int y)
 *     switch(cat) draws the label, then a 4-elem strip + 4 inverse masks at
 *     x+125/+188/+251/+316 (Δ63 design px), y+4, tile 32.700001; coords scaled.
 *     New slots continue the cadence: Holy=x+379, Dark=x+442.
 *  FFX_Menu2D_DrawTexQuadSolid @ flat 0x903BB0
 *     int __cdecl(uint atlasId, float x, float y, float w, float h,
 *                 float u0, float v0, float u1, float v1)
 *     id 0x1AF (431) → atlas 16128 (battle.dds.phyre); for ids 400..598 the UVs
 *     are used pre-normalized (no per-atlas divide). x2=x+w, y2=y+h.
 *  FFX_GetActorElementCategoryMask_Switch4 @ flat 0x8975C0
 *     char __cdecl(u8 actor, int cat) — cat0 Weak(rec+1501), cat1 Absorb,
 *     cat2 Null, cat3 Resist; returns the element bitmask byte for that category.
 *  Pure coord scalers: ScaleX(v)=v*512/1920 (flat 0x644990),
 *                      ScaleY(v)=v*416/1080 (flat 0x6449D0).
 */
#define RVA_FFX_BTLUI_DRAW_SCAN_ELEMENT_RESIST_ROW   0x00494AB0u
#define RVA_FFX_MENU2D_DRAW_TEX_QUAD_SOLID           0x00503BB0u
#define RVA_FFX_GETACTOR_ELEMENT_CATEGORY_MASK       0x004975C0u
#define FFX_SCAN_WIDGET_ATLAS_ID                     0x1AFu

/* ── Scan/Sensor info-panel BOX WIDTH (Jarvis-RE-SCANUI 2026-06-16, follow-up) ──
 * The affinity panel (sub_8939A0 @ flat 0x8939A0 — both the Sensor "Info" lite
 * panel and the Scan-ability "Scan data" full panel) draws its rounded box via
 *   FFX_Menu2D_DrawRoundedPanel9Slice(x, y, w=ScaleX(385), h, style) @ flat 0x893B88.
 * The width is a 385.0f constant (flt_ScanInfoPanelWidth385 @ flat 0xB5EF60) whose
 * ONLY xref is that frame draw — so it is safe to data-patch in isolation. The 4
 * vanilla element slots end at design x+350; our appended Holy(x+379)/Dark(x+442)
 * balls land past the 385-wide box and spill onto the scene (visible against the
 * sky in Sensor, lost over the model in full-Scan). Widening the constant to 490f
 * stretches the 9-slice box so both new balls sit inside, in BOTH panel modes.
 * The frame drawer stretches cleanly, and nothing else is right-anchored to the
 * box, so no other layout shifts. IDA flat = PE RVA + 0x400000.
 *   FFX_Menu2D_DrawRoundedPanel9Slice @ flat 0x8F41B0 (shared by 8 panels).
 */
#define RVA_FFX_SCAN_INFO_PANEL_WIDTH_CONST          0x0075EF60u
#define FFX_SCAN_INFO_PANEL_WIDTH_VANILLA            385.0f
#define FFX_SCAN_INFO_PANEL_WIDTH_WIDENED            490.0f

/* ── Sphere Grid Node Activation ────────────────────────────────────────────
 * Routine called when the player activates a Sphere Grid node.
 * IDA task: trace the "use sphere" code path to find the activation handler.
 */
// #define RVA_SPHEREGRID_ACTIV    0x00000000u

/* ── Battle Status Tick ─────────────────────────────────────────────────────
 * Per-frame per-actor status tick (poison counter, haste timer, etc.).
 * IDA task: search for the actor loop that decrements status counters.
 */
// #define RVA_BATTLE_STATUS_TICK  0x00000000u

/* ── Camera Update ──────────────────────────────────────────────────────────
 * vtable hook target for custom battle camera control.
 * IDA task: trace from camSetPolar (0x6004 confirmed) to the update routine.
 */
// #define RVA_CAMERA_UPDATE       0x00000000u

#endif /* FFX_ADDRESSES_H */
