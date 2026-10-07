#include "F8FlagCatalog.h"
#include "VanguardCatalog.h"
#include "ModFeatureCatalog.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace FfxHooks {

namespace {

const char* const kTabNames[] = {"System", "Boosters", "Cheats", "Extras", "Input", "Dev", "Reforge"};

const F8ScalarSpec kApMultiplier = {"cheats.ap_multiplier", 100, 1, 100};
const F8ScalarSpec kGilMultiplier = {"cheats.gil_multiplier", 100, 1, 100};
const F8ScalarSpec kItemStackCapValue = {"labs.item_stack_cap_value", 255, 1, 255};

#define VANGUARD_GATE(index,key,help) {"Extras",Vanguard::Features[index].label, {"vanguard." key,"f8_authority.vanguard_" key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},Vanguard::HasNativeConsumer(index)?"RESTART REQUIRED - " help:"NOT WIRED - " help,Vanguard::HasNativeConsumer(index)?F8Activation::RestartRequired:F8Activation::NotWired,F8ApplyMode::None}

const F8FlagSpec kFlags[] = {
    {"System", "Borderless window",
     {"window.borderless", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Fill the monitor; OFF restores your window.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"System", "Keep cursor in game",
     {"window.clip_cursor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Keep cursor in game; Alt-Tab releases it.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"System", "Hide idle cursor",
     {"window.hide_cursor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Hide the cursor after two idle seconds.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"System", "Performance display",
     {"diagnostics.performance", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Show frame rate and frame time.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"System", "Free battle camera",
     {"camera.free_look", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Native battle camera; pauses during menus.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"System", "Freeze field scene",
     {"camera.freeze_scene", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Freeze field scenes; menus and focus loss resume.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},

    {"System", "Native Hooks",
     {"plugins.dinput8", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, true},
     "READ ONLY - Native functions are controlled in F8.", F8Activation::ReadOnly, F8ApplyMode::None},
    {"System", "External render module",
     {"plugins.dxgi", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "READ ONLY - External rendering module status.", F8Activation::ReadOnly, F8ApplyMode::None},
    {"System", "External UnX module",
     {"plugins.unx", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "READ ONLY - Hooks works independently of external UnX.", F8Activation::ReadOnly, F8ApplyMode::None},
    {"System", "Native diagnostics",
     {"plugins.ffx_probe", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "READ ONLY - Hooks provides native diagnostics.", F8Activation::ReadOnly, F8ApplyMode::None},

    {"Boosters", "Permanent Sensor",
     {"boosters.permanent_sensor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Keep enemy details visible without Sensor gear.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Boosters", "Playable Seymour",
     {"boosters.playable_seymour", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Experimental battle roster; Sphere Grid unsupported.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Boosters", "Speed Hack",
     {"boosters.speed_hack", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Native 2/4; field scenes 8; optional FMV acceleration.",
     F8Activation::Live, F8ApplyMode::ConfigPolled},
    {"Boosters", "SpeedHack FMV acceleration",
     {"boosters.speed_hack_fmv", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Accelerate movie picture and audio together.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Boosters", "Entire Party Earns AP",
     {"boosters.entire_party_earns_ap", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Share battle AP with eligible reserve members.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},

    {"Cheats", "Invincible Party",
     {"cheats.invincible_party", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Keep party HP from dropping.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "Invincible Enemies",
     {"cheats.invincible_enemies", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Keep enemy HP from dropping.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "Always Overdrive",
     {"cheats.always_overdrive", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Keep Overdrive gauges full in battle.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "Always Critical",
     {"cheats.always_critical", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Make eligible attacks critical hits.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "Damage 99999",
     {"cheats.damage_value", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Set supported damage to 99999.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "Always Rare Drop",
     {"cheats.always_rare_drop", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Force rare item drops when supported.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Cheats", "AP Multiplier",
     {"cheats.ap_100x", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Multiply supported AP rewards by the configured rate.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged, &kApMultiplier},
    {"Cheats", "Gil Multiplier",
     {"cheats.gil_100x", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Multiply supported Gil rewards by the configured rate.",
     F8Activation::Live, F8ApplyMode::RuntimeAcknowledged, &kGilMultiplier},

    {"Dev", "FieldScout Master",
     {"field_scout.master", "f8_authority.field_scout_master", "labs.field_scout",
      "FFXHOOKS_ENABLE_FIELD_SCOUT", "field_scout.flag", nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Capture basic field details next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Dev", "FieldScout Heavy",
     {"field_scout.heavy", "f8_authority.field_scout_heavy", "labs.field_scout_heavy",
      "FFXHOOKS_FIELD_SCOUT_HEAVY", "field_scout_heavy.flag", nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Capture extra field details next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Dev", "FieldScout Max",
     {"field_scout.max", "f8_authority.field_scout_max", "labs.field_scout_max",
      "FFXHOOKS_FIELD_SCOUT_MAX", "field_scout_max.flag", nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Max needs Heavy and Ultra next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Dev", "FieldScout Ultra",
     {"field_scout.ultra", "f8_authority.field_scout_ultra", "labs.field_scout_ultra",
      "FFXHOOKS_FIELD_SCOUT_ULTRA", "field_scout_ultra.flag", nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Ultra needs Heavy on next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},

    {"Reforge", "Arena+ Master",
     {"arena_plus.master", "f8_authority.arena_plus_master", nullptr,
      "FFXHOOKS_ENABLE_ARENA_PLUS", "arena_plus.flag", nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Enable Arena+ on next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Arena+ Compose F7",
     {"arena_plus.compose_f7", "f8_authority.arena_plus_compose_f7", "labs.arena_plus_compose_f7",
      "FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7", "arena_plus_compose_f7.flag",
      "FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7", nullptr, nullptr, false},
     "LIVE - F7 Custom Mix editor; requires Arena+ Master.",
     F8Activation::Live, F8ApplyMode::ConfigPolled},
    {"Reforge", "Bypass Progression",
     {"arena_plus.unlock_all", "f8_authority.arena_plus_unlock_all", nullptr,
      "FFXHOOKS_ARENAPLUS_UNLOCK_ALL", "arena_plus_unlock_all.flag",
      nullptr, nullptr, nullptr, false},
     "LIVE - Use all bosses without defeating them first.",
     F8Activation::Live, F8ApplyMode::ConfigPolled},
    {"Reforge", "Arena+ Victory Hook",
     {"arena_plus.victory_hook", "f8_authority.arena_plus_victory_hook", nullptr,
      "FFXHOOKS_ENABLE_ARENA_PLUS_VICTORY_HOOK", "arena_plus_victory_hook.flag",
      nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Log victories; rewards stay unchanged.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Arena+ Resolver Log",
     {"arena_plus.resolver_log", "f8_authority.arena_plus_resolver_log", nullptr,
      "FFXHOOKS_ENABLE_ARENA_PLUS_RESOLVER_LOG", "arena_plus_resolver_log.flag",
      nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Log Arena+ match choices next launch.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Arena+ Music",
     {"arena_plus.music", "f8_authority.arena_plus_music", "music.arena_plus",
      "FFXHOOKS_ARENAPLUS_MUSIC", "arena_plus_music.flag", "FFXHOOKS_DISABLE_ARENA_PLUS_MUSIC",
      "arena_plus_music.flag.off", "music.flag.off", false},
     "RESTART REQUIRED - ON archives Arena OFF; global OFF wins.",
     F8Activation::RestartRequired, F8ApplyMode::None},

    {"Input", "Block Windows Key",
     {"input.block_windows_key", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Windows-key filter; Linux desktop may intercept first.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Input", "Fix Background Input",
     {"input.fix_background_input", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Ignore background keyboard, mouse and raw input.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Input", "Filter IME",
     {"input.filter_ime", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Filter game IME; keep Hooks text editors usable.", F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
    {"Input", "Dialog Skip",
     {"input.dialog_skip", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Skip voiced lines when dialog skip is enabled.",
     F8Activation::Live, F8ApplyMode::ConfigPolled},
    {"Dev", "Fastload Autosave",
     {"development.fastload_autosave", "f8_authority.fastload_autosave", nullptr,
      "FFXHOOKS_ENABLE_FASTLOAD_AUTOSAVE", "fastload_autosave.flag",
      "FFXHOOKS_DISABLE_FASTLOAD_AUTOSAVE", "fastload_autosave.flag.off", nullptr, false},
     "RESTART REQUIRED - Skip opening; load autosave 000 next boot.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Dev", "Arcana: full deck",
     {"development.arcana_full_deck", "f8_authority.arcana_full_deck", nullptr,
      "FFXHOOKS_ARCANA_FULL_DECK", nullptr, nullptr, nullptr, nullptr, false},
     "LIVE - Grant all 78 cards. OFF keeps your collection.",
     F8Activation::Live, F8ApplyMode::ConfigPolled},
    {"Reforge", "Arcana of the Fayth",
     {"arcana.enabled", "f8_authority.arcana", nullptr,
      "FFXHOOKS_ARCANA", "arcana.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Equip Tarot in the native Equip menu.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Nova Super Damage",
     {"labs.nova_super_damage", "f8_authority.lab_nova_super_damage", nullptr,
      "FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE", "nova_super_damage.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Only Kimahri Nova HP damage exceeds 99999.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Ronso Mana",
     {"labs.kimahri_ronso_mana", "f8_authority.lab_kimahri_ronso_mana", nullptr,
      "FFXHOOKS_ENABLE_RONSO_MANA", "kimahri_ronso_mana.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Kimahri OD 200; partial costs; saves charge.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Equipment Workshop",
     {"labs.equipment_workshop", "f8_authority.equipment_workshop", nullptr,
      "FFXHOOKS_EQUIPMENT_WORKSHOP", "equipment_workshop.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Workshop; set its shortcut in Input.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Native equipment details",
     {"labs.equipment_workshop_native_ui", "f8_authority.equipment_workshop_native_ui", nullptr,
      "FFXHOOKS_WORKSHOP_NATIVE_UI", "equipment_workshop_native_ui.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Fifth slot/ranks; requires Workshop.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Scan Expanded",
     {"labs.scan_expanded", "f8_authority.scan_expanded", nullptr,
      "FFXHOOKS_SCAN_EXPANDED", "scan_expanded.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Eight stats + MP; separate from elements.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Scan Extra Elements",
     {"labs.element_scan_dark", "f8_authority.element_scan_dark", nullptr,
      "FFXHOOKS_ELEMENT_SCAN_DARK", "element_scan_dark.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - Holy/Dark/Custom; separate from stats.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Grid Teach",
     {"labs.grid_teach", "f8_authority.lab_grid_teach", nullptr,
      "FFXHOOKS_GRID_TEACH", "grid_teach.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: Sphere Grid nodes teach commands.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Lancet Dual Grant",
     {"labs.kimahri_lancet_dual_grant", "f8_authority.lab_kimahri_lancet_dual_grant", nullptr,
      "FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT", "kimahri_lancet_dual_grant.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: Blue skills; REQUIRES Grid Teach.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Reforge", "Item Stack Cap",
     {"labs.item_stack_cap", "f8_authority.lab_item_stack_cap", nullptr,
      "FFXHOOKS_ENABLE_ITEM_STACK_CAP", "item_stack_cap_255.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: set per-slot item limit (1-255).",
     F8Activation::RestartRequired, F8ApplyMode::None, &kItemStackCapValue},
    {"Reforge", "Double/Triple Drop",
     {"labs.double_triple_drop", "f8_authority.lab_double_triple_drop", nullptr,
      "FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP", "double_triple_drop.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: battle drops x2/x3 with drop abilities.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    VANGUARD_GATE(0,"stat_pct_universal","Bonuses follow the formula's actual stats."),
    VANGUARD_GATE(1,"defense_ehp_scaling","Defensive bonuses grant effective HP."),
    VANGUARD_GATE(2,"healing_ignore_shell","Protections do not reduce restoration."),
    VANGUARD_GATE(3,"breaks_additive_damage","Break adds 25% of pre-defense damage."),
    VANGUARD_GATE(4,"auto_crit_mp0_turn_end","Consume temporary benefits after all hits."),
    VANGUARD_GATE(5,"element_opposite_weakness","Opposite elemental weakness grants x1.25."),
    VANGUARD_GATE(6,"status_refresh_duration","Successful reapplication renews duration."),
    VANGUARD_GATE(7,"enemy_duration_resistance","Separate duration from application chance."),
    VANGUARD_GATE(8,"threaten_single_use","One successful Threaten per enemy."),
    VANGUARD_GATE(9,"guaranteed_hits_no_miss","Preserve explicitly guaranteed attacks."),
    VANGUARD_GATE(10,"quickcast_replace_doublecast","One spell, rank 2, double MP cost."),
    VANGUARD_GATE(11,"dualcast_white_magic","Independent White Magic cast submenu."),
    VANGUARD_GATE(12,"magic_mp_scaling","Current MP increases magic power."),
    VANGUARD_GATE(13,"party_switch_costs_turn","Voluntary switching costs a turn."),
    VANGUARD_GATE(14,"eject_shatter_auto_replace","Use the first eligible reserve ally."),
    VANGUARD_GATE(15,"single_multi_hit_normalization","Explicit single/multi-hit balance policy."),
    VANGUARD_GATE(16,"hero_bravery","Increase critical chance dealt/received."),
    VANGUARD_GATE(17,"energy_boost","Elemental potency above half Overdrive."),
    VANGUARD_GATE(18,"energy_burst","Increased potency above 75% Overdrive."),
    VANGUARD_GATE(19,"efficiency","Reduce MP and Overdrive costs."),
    VANGUARD_GATE(20,"vampirism","Recover 2% of hostile HP lost per action."),
    VANGUARD_GATE(21,"follow_up","Assist one ally's single-target action."),
    VANGUARD_GATE(22,"p_trade","Physical damage x0.8; magical x1.2."),
    VANGUARD_GATE(23,"m_trade","Magical damage x0.8; physical x1.2."),
    VANGUARD_GATE(24,"hero_caution","Critical policy; Hero Drink has priority."),
    VANGUARD_GATE(25,"mp_regen","Restore 2% maximum MP at turn start."),
    VANGUARD_GATE(26,"elude","Gain 50 evasion while defending."),
    VANGUARD_GATE(27,"energy_wall","Reduce damage above half Overdrive."),
    VANGUARD_GATE(28,"energy_barrier","Reduce damage above 75% Overdrive."),
    VANGUARD_GATE(29,"equipment_active_commands","Commands require verified equipped gear."),
    VANGUARD_GATE(30,"equipment_partial_overdrive","Display and debit the same partial fee."),
#define MOD_FEATURE(index) {"Extras",ModFeatures::Entries[index].label,ModFeatures::Entries[index].gate,ModFeatures::Entries[index].help,F8Activation::RestartRequired,F8ApplyMode::None}
    MOD_FEATURE(0),MOD_FEATURE(1),MOD_FEATURE(2),MOD_FEATURE(3),MOD_FEATURE(4),MOD_FEATURE(5),
    {"Cheats", "Per-monster AP/Gil",
     {"cheats.monster_rewards", "f8_authority.monster_rewards", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false},
     "RESTART REQUIRED - Per-monster AP/Gil before native bonuses.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    // Append the new identity; existing catalog row numbers remain stable.
    MOD_FEATURE(6),
    MOD_FEATURE(7),
    MOD_FEATURE(8),
#undef MOD_FEATURE
};
#undef VANGUARD_GATE

constexpr size_t kFlagCount = sizeof(kFlags) / sizeof(kFlags[0]);
constexpr size_t kTabCount = sizeof(kTabNames) / sizeof(kTabNames[0]);
constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

static_assert(kFlagCount == 51 + Vanguard::FeatureCount + ModFeatures::Entries.size(), "F8 preserves old rows and appends independent mod identities");
static_assert(kFlagCount <= kF8BulkRowResultMax,
              "bulk row results must hold every catalog row");
static_assert(kTabCount == 7, "F8 catalog must contain exactly seven tabs");

INIT_ONCE g_statusInitOnce = INIT_ONCE_STATIC_INIT;
SRWLOCK g_statusLocks[kFlagCount] = {};
F8RuntimeStatus g_runtimeStatuses[kFlagCount] = {};

size_t FindFlagIndex(const char* canonicalKey) {
    if (!canonicalKey || !canonicalKey[0]) return kInvalidIndex;
    for (size_t i = 0; i < kFlagCount; ++i) {
        if (strcmp(kFlags[i].gate.canonicalKey, canonicalKey) == 0) return i;
    }
    return kInvalidIndex;
}

BOOL CALLBACK InitializeRuntimeStatuses(PINIT_ONCE, PVOID, PVOID*) {
    for (size_t i = 0; i < kFlagCount; ++i) {
        g_runtimeStatuses[i] = {
            kFlags[i].activation == F8Activation::Live
                ? F8RuntimeAvailability::Pending
                : F8RuntimeAvailability::NotApplicable,
            false,
            false,
        };
    }
    return TRUE;
}

void EnsureRuntimeStatuses() {
    InitOnceExecuteOnce(&g_statusInitOnce, &InitializeRuntimeStatuses, nullptr, nullptr);
}

class ExclusiveRuntimeRowLock {
public:
    explicit ExclusiveRuntimeRowLock(size_t index) : lock_(&g_statusLocks[index]) {
        AcquireSRWLockExclusive(lock_);
    }
    ~ExclusiveRuntimeRowLock() { ReleaseSRWLockExclusive(lock_); }
    ExclusiveRuntimeRowLock(const ExclusiveRuntimeRowLock&) = delete;
    ExclusiveRuntimeRowLock& operator=(const ExclusiveRuntimeRowLock&) = delete;

private:
    SRWLOCK* lock_;
};

class ExclusiveRuntimeRowsLock {
public:
    ExclusiveRuntimeRowsLock(const size_t* indexes, size_t count)
        : indexes_(indexes), count_(count) {
        for (size_t i = 0; i < count_; ++i) AcquireSRWLockExclusive(&g_statusLocks[indexes_[i]]);
    }
    ~ExclusiveRuntimeRowsLock() {
        while (count_ != 0) ReleaseSRWLockExclusive(&g_statusLocks[indexes_[--count_]]);
    }
    ExclusiveRuntimeRowsLock(const ExclusiveRuntimeRowsLock&) = delete;
    ExclusiveRuntimeRowsLock& operator=(const ExclusiveRuntimeRowsLock&) = delete;

private:
    const size_t* indexes_;
    size_t count_;
};

F8RuntimeStatus RuntimeStatusAtLocked(size_t index) {
    if(!Coexistence::FeatureAllowed(kFlags[index].gate.canonicalKey))
        return {F8RuntimeAvailability::PeerOwned,true,false};
    return g_runtimeStatuses[index];
}

void SetRuntimeStatusAtLocked(size_t index, const F8RuntimeStatus& status) {
    g_runtimeStatuses[index] = status;
}

F8RuntimeStatus RuntimeStatusAt(size_t index) {
    if (index == kInvalidIndex) {
        return {F8RuntimeAvailability::NotApplicable, false, false};
    }
    EnsureRuntimeStatuses();
    AcquireSRWLockShared(&g_statusLocks[index]);
    const F8RuntimeStatus status = RuntimeStatusAtLocked(index);
    ReleaseSRWLockShared(&g_statusLocks[index]);
    return status;
}

void SetRuntimeStatusAt(size_t index, const F8RuntimeStatus& status) {
    EnsureRuntimeStatuses();
    ExclusiveRuntimeRowLock rowLock(index);
    SetRuntimeStatusAtLocked(index, status);
}

} // namespace

size_t F8FlagCount() {
    return kFlagCount;
}

const F8FlagSpec& F8FlagAt(size_t index) {
    return kFlags[index < kFlagCount ? index : 0];
}

size_t F8TabCount() {
    return kTabCount;
}

const char* F8TabName(size_t index) {
    return index < kTabCount ? kTabNames[index] : "";
}

const F8FlagSpec* FindF8Flag(const char* canonicalKey) {
    const size_t index = FindFlagIndex(canonicalKey);
    return index == kInvalidIndex ? nullptr : &kFlags[index];
}

Config::BoolGateResult ResolveF8Flag(const F8FlagSpec& flag) {
    return Config::ResolveBoolGate(flag.gate);
}

F8ScalarResult ResolveF8Scalar(const F8FlagSpec& flag) {
    if (!flag.scalar) return {F8ScalarState::NotApplicable, 0};
    const Config::IntReadResult result = Config::ReadIntExact(
        flag.scalar->canonicalKey, flag.scalar->minimum, flag.scalar->maximum);
    if (result.state == Config::IntReadState::Missing) {
        // Missing is compatibility: each row retains its documented legacy default.
        return {F8ScalarState::Defaulted, flag.scalar->defaultValue};
    }
    if (result.state == Config::IntReadState::Valid) {
        return {F8ScalarState::Valid, result.value};
    }
    return {F8ScalarState::Invalid, 0};
}

F8RuntimeStatus GetF8RuntimeStatus(const F8FlagSpec& flag) {
    return RuntimeStatusAt(FindFlagIndex(flag.gate.canonicalKey));
}

F8EditResult SetF8FlagValue(const F8FlagSpec& flag, bool requestedValue) {
    const size_t index = FindFlagIndex(flag.gate.canonicalKey);
    if (index == kInvalidIndex) {
        return {F8EditCode::RejectedNotWired, requestedValue, ResolveF8Flag(flag),
                {F8RuntimeAvailability::NotApplicable, false, false}};
    }

    const F8FlagSpec& catalogFlag = kFlags[index];
    EnsureRuntimeStatuses();
    // The row lock arbitrates edits against producer publication through persistence and readback.
    // Locks are per catalog row, so unrelated producers remain independent.
    ExclusiveRuntimeRowLock rowLock(index);
    Config::BoolGateResult effective = ResolveF8Flag(catalogFlag);
    F8RuntimeStatus runtime = RuntimeStatusAtLocked(index);
    if (catalogFlag.activation == F8Activation::NotWired || catalogFlag.activation == F8Activation::ReadOnly) {
        return {F8EditCode::RejectedNotWired, requestedValue, effective, runtime};
    }
    if (requestedValue && catalogFlag.scalar &&
        ResolveF8Scalar(catalogFlag).state == F8ScalarState::Invalid) {
        // The legacy boolean remains the stable gate, but it may never arm an invalid scalar.
        return {F8EditCode::RejectedInvalidParameter, requestedValue, effective, runtime};
    }
    const bool nonAvailableLive = catalogFlag.activation == F8Activation::Live &&
        runtime.availability != F8RuntimeAvailability::Available;
    if ((nonAvailableLive || runtime.availability == F8RuntimeAvailability::PeerOwned) && requestedValue) {
        return {F8EditCode::RejectedUnavailable, requestedValue, effective, runtime};
    }
    const bool persisted = requestedValue && std::strcmp(catalogFlag.gate.canonicalKey,"arena_plus.music")==0
        ? Config::EnableArenaMusicFromMenu(catalogFlag.gate)
        : Config::SetAuthoritativeBool(catalogFlag.gate, requestedValue);
    if (!persisted) {
        return {F8EditCode::PersistFailed, requestedValue, effective, runtime};
    }

    effective = ResolveF8Flag(catalogFlag);
    // OFF remains a fail-safe config disarm while a LIVE producer is unavailable. Preserve its
    // exact failure/readback status until that producer publishes recovery.
    if (!nonAvailableLive) {
        // Producer-acknowledged edits discard stale readback; polled rows can report the newly
        // resolved effective value immediately after durable persistence.
        if (catalogFlag.applyMode == F8ApplyMode::RuntimeAcknowledged) {
            SetRuntimeStatusAtLocked(index, {F8RuntimeAvailability::Pending, false, false});
        } else if (catalogFlag.applyMode == F8ApplyMode::ConfigPolled) {
            SetRuntimeStatusAtLocked(
                index, {F8RuntimeAvailability::Available, true, effective.value});
        }
    }
    runtime = RuntimeStatusAtLocked(index);
    return {F8EditCode::Saved, requestedValue, effective, runtime};
}

F8BulkEditResult SetF8TabValues(const char* tab, bool requestedValue) {
    F8BulkEditResult result = {requestedValue, 0, 0, 0, 0, 0, 0, 0, false, {}, 0};
    if (!tab || !tab[0]) return result;

    size_t indexes[kFlagCount] = {};
    size_t indexCount = 0;
    for (size_t i = 0; i < kFlagCount; ++i) {
        // The former Arena+ tab remains an exact logical bulk group for existing
        // callers. Reforge includes it; Extras must never inherit its controls.
        const bool matchesGroup = strcmp(kFlags[i].tab, tab) == 0 ||
            (strcmp(tab,"Arena+")==0 && strncmp(kFlags[i].gate.canonicalKey,"arena_plus.",11)==0);
        if (matchesGroup &&
            kFlags[i].activation != F8Activation::NotWired && kFlags[i].activation != F8Activation::ReadOnly) {
            indexes[indexCount++] = i;
        }
    }
    result.eligible = indexCount;
    if (indexCount == 0) return result;

    EnsureRuntimeStatuses();
    // WHY: lock rows in catalog order and persist one complete INI replacement.
    // Sequential SetF8FlagValue calls would freeze the pump for N disk writes and
    // expose a partially edited tab if a later replacement failed.
    ExclusiveRuntimeRowsLock rowsLock(indexes, indexCount);
    Config::AuthoritativeBoolUpdate updates[kFlagCount] = {};
    size_t updateIndexes[kFlagCount] = {};
    size_t updateRows[kFlagCount] = {};
    size_t updateCount = 0;
    for (size_t i = 0; i < indexCount; ++i) {
        const size_t index = indexes[i];
        const F8FlagSpec& flag = kFlags[index];
        const Config::BoolGateResult effective = ResolveF8Flag(flag);
        F8BulkRowResult& row = result.rows[result.rowCount++];
        row.flag = &flag;
        row.source = effective.source;
        if (effective.value == requestedValue) {
            row.code = F8BulkRowCode::Already;
            ++result.already;
            continue;
        }
        const F8RuntimeStatus runtime = RuntimeStatusAtLocked(index);
        const bool unavailableLive = flag.activation == F8Activation::Live &&
            runtime.availability != F8RuntimeAvailability::Available;
        if (requestedValue && (unavailableLive || runtime.availability == F8RuntimeAvailability::PeerOwned)) {
            // Quarantine/signature failures are expected skips, not generic blocks.
            row.code = F8BulkRowCode::Unavailable;
            ++result.unavailable;
            continue;
        }
        const bool invalidScalar = requestedValue && flag.scalar &&
            ResolveF8Scalar(flag).state == F8ScalarState::Invalid;
        if (invalidScalar) {
            row.code = F8BulkRowCode::InvalidParameter;
            ++result.invalidParameter;
            continue;
        }
        row.code = F8BulkRowCode::PersistFailed;  // queued; rewritten after the write
        updates[updateCount] = {&flag.gate, requestedValue};
        updateIndexes[updateCount] = index;
        updateRows[updateCount++] = result.rowCount - 1;
    }
    if (updateCount == 0) return result;
    if (!Config::SetAuthoritativeBools(updates, updateCount)) {
        result.persistFailed = true;
        return result;
    }

    for (size_t i = 0; i < updateCount; ++i) {
        const size_t index = updateIndexes[i];
        const F8FlagSpec& flag = kFlags[index];
        const Config::BoolGateResult effective = ResolveF8Flag(flag);
        F8RuntimeStatus runtime = RuntimeStatusAtLocked(index);
        const bool unavailableLive = flag.activation == F8Activation::Live &&
            runtime.availability != F8RuntimeAvailability::Available;
        if (!unavailableLive) {
            if (flag.applyMode == F8ApplyMode::RuntimeAcknowledged) {
                runtime = {F8RuntimeAvailability::Pending, false, false};
                SetRuntimeStatusAtLocked(index, runtime);
            } else if (flag.applyMode == F8ApplyMode::ConfigPolled) {
                runtime = {F8RuntimeAvailability::Available, true, effective.value};
                SetRuntimeStatusAtLocked(index, runtime);
            }
        }
        F8BulkRowResult& row = result.rows[updateRows[i]];
        row.source = effective.source;
        if (effective.value == requestedValue) {
            row.code = F8BulkRowCode::Changed;
            ++result.changed;
            continue;
        }
        // A persisted intent can still lose to sources that outrank the canonical INI:
        // disable-env, a .off marker, or an environment override name the real artifact
        // holding the row. Anything below canonical precedence reaching the readback
        // means the write did not take effect — an unexpected mismatch, not an override.
        const bool dominatedByExternal =
            effective.source == Config::BoolSource::DisableEnvironment ||
            effective.source == Config::BoolSource::LegacyOffFlag ||
            effective.source == Config::BoolSource::Environment;
        if (dominatedByExternal) {
            row.code = F8BulkRowCode::ExternalOverride;
            ++result.externalOverride;
        } else {
            row.code = F8BulkRowCode::EffectiveMismatch;
            ++result.effectiveMismatch;
        }
    }
    return result;
}

const char* F8BulkRowCodeName(F8BulkRowCode code) {
    switch (code) {
        case F8BulkRowCode::Changed: return "changed";
        case F8BulkRowCode::Already: return "already";
        case F8BulkRowCode::Unavailable: return "unavailable";
        case F8BulkRowCode::ExternalOverride: return "external";
        case F8BulkRowCode::InvalidParameter: return "invalid";
        case F8BulkRowCode::EffectiveMismatch: return "mismatch";
        case F8BulkRowCode::PersistFailed: return "save failed";
        default: return "unknown";
    }
}

const char* F8GateSourceDetail(const F8FlagSpec& flag, Config::BoolSource source) {
    switch (source) {
        case Config::BoolSource::DisableEnvironment:
            return flag.gate.disableEnvName;
        case Config::BoolSource::Environment:
            return flag.gate.envName;
        case Config::BoolSource::LegacyOffFlag:
            // ResolveBoolGate checks the specific marker before the global one.
            if (flag.gate.offFlagName && Config::LegacyFlagEnabled(flag.gate.offFlagName)) {
                return flag.gate.offFlagName;
            }
            return flag.gate.globalOffFlagName;
        case Config::BoolSource::LegacyFlagModules:
        case Config::BoolSource::LegacyFlagConfig:
        case Config::BoolSource::LegacyFlagModulesConfig:
        case Config::BoolSource::LegacyFlagRoot:
            return flag.gate.flagName;
        case Config::BoolSource::LegacyIni:
            return flag.gate.legacyKey;
        default:
            return nullptr;
    }
}

F8ScalarEditResult SetF8ScalarValue(const F8FlagSpec& flag, int requestedValue) {
    const size_t index = FindFlagIndex(flag.gate.canonicalKey);
    if (index == kInvalidIndex || !kFlags[index].scalar) {
        return {F8ScalarEditCode::RejectedNotApplicable, requestedValue,
                {F8ScalarState::NotApplicable, 0},
                {F8RuntimeAvailability::NotApplicable, false, false}};
    }

    const F8FlagSpec& catalogFlag = kFlags[index];
    const F8ScalarSpec& scalar = *catalogFlag.scalar;
    EnsureRuntimeStatuses();
    ExclusiveRuntimeRowLock rowLock(index);
    F8ScalarResult configured = ResolveF8Scalar(catalogFlag);
    F8RuntimeStatus runtime = RuntimeStatusAtLocked(index);
    if (requestedValue < scalar.minimum || requestedValue > scalar.maximum) {
        return {F8ScalarEditCode::RejectedInvalid, requestedValue, configured, runtime};
    }

    const Config::BoolGateResult gate = ResolveF8Flag(catalogFlag);
    const bool unavailableWhileEnabled = catalogFlag.activation == F8Activation::Live && gate.value &&
        runtime.availability != F8RuntimeAvailability::Available;
    const bool preserveExactFailure =
        runtime.availability != F8RuntimeAvailability::Available &&
        runtime.availability != F8RuntimeAvailability::Pending;
    // A valid active producer cannot be reconfigured while unavailable. Invalid text is allowed
    // through this guard so the UI never traps the user in an unrecoverable fail-closed state.
    if (unavailableWhileEnabled && configured.state != F8ScalarState::Invalid) {
        return {F8ScalarEditCode::RejectedUnavailable, requestedValue, configured, runtime};
    }
    if (!Config::SetInt(scalar.canonicalKey, requestedValue)) {
        return {F8ScalarEditCode::PersistFailed, requestedValue, configured, runtime};
    }

    configured = ResolveF8Scalar(catalogFlag);
    // Persistence does not prove the aligned runtime scalar changed. Pending/healthy rows discard
    // stale readback, while a precise signature/conflict failure survives until its producer recovers.
    if (catalogFlag.activation == F8Activation::Live && !preserveExactFailure) {
        SetRuntimeStatusAtLocked(index, {
            F8RuntimeAvailability::Pending, false, false, false, 0});
    }
    runtime = RuntimeStatusAtLocked(index);
    return {F8ScalarEditCode::Saved, requestedValue, configured, runtime};
}

// The same per-row lock covers edit persistence and publication, so a producer completing
// after an edit becomes the authoritative runtime status.
bool PublishF8RuntimeStatus(const char* canonicalKey, F8RuntimeAvailability availability,
                            bool hasAppliedValue, bool appliedValue) {
    const size_t index = FindFlagIndex(canonicalKey);
    if (index == kInvalidIndex) return false;
    SetRuntimeStatusAt(index, {availability, hasAppliedValue, appliedValue});
    return true;
}

bool PublishF8RuntimeScalarStatus(const char* canonicalKey,
                                  F8RuntimeAvailability availability,
                                  bool hasAppliedValue, bool appliedValue,
                                  bool hasAppliedScalar, int appliedScalar) {
    const size_t index = FindFlagIndex(canonicalKey);
    if (index == kInvalidIndex || !kFlags[index].scalar) return false;
    SetRuntimeStatusAt(index, {availability, hasAppliedValue, appliedValue,
                               hasAppliedScalar, appliedScalar});
    return true;
}

const char* F8ActivationName(F8Activation activation) {
    switch (activation) {
        case F8Activation::Live: return "LIVE";
        case F8Activation::RestartRequired: return "RESTART REQUIRED";
        case F8Activation::NotWired: return "NOT WIRED";
        case F8Activation::ReadOnly: return "READ ONLY";
        default: return "UNKNOWN";
    }
}

const char* F8AvailabilityName(F8RuntimeAvailability availability) {
    switch (availability) {
        case F8RuntimeAvailability::NotApplicable: return "NOT APPLICABLE";
        case F8RuntimeAvailability::Pending: return "PENDING";
        case F8RuntimeAvailability::Available: return "AVAILABLE";
        case F8RuntimeAvailability::UnsupportedBuild: return "UNSUPPORTED BUILD";
        case F8RuntimeAvailability::ProducerUnavailable: return "PRODUCER UNAVAILABLE";
        case F8RuntimeAvailability::SignatureMismatch: return "SIGNATURE MISMATCH";
        case F8RuntimeAvailability::RestorePending: return "RESTORE PENDING";
        case F8RuntimeAvailability::Conflict: return "CONFLICT";
        case F8RuntimeAvailability::PlatformLimited: return "LIMITED BY PLATFORM";
        case F8RuntimeAvailability::PeerOwned: return "FAHRENHEIT: ADAPTER REQUIRED";
        default: return "UNKNOWN";
    }
}

} // namespace FfxHooks
