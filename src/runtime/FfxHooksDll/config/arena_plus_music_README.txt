Arena+ music config (copy into game modules\ or modules\config\)

Requires: arena_plus_music.flag

Per-boss defaults (FMOD runtime track id, see docs/reverse/FFX_MUSIC_RUNTIME_THEATER_CROSSWALK_2026-06-08.md):
  0 Dark Valefor       145  Challenge (Penance battle theme)
  1 Dark Ifrit         145  Challenge
  2 Dark Ixion         145  Challenge
  3 Dark Shiva         145  Challenge
  4 Dark Bahamut       145  Challenge
  5 Dark Yojimbo       145  Challenge
  6 Dark Anima         145  Challenge
  7 Dark Magus Sisters 145  Challenge
  8 Penance            145  Challenge

Override files (single integer per file):
  arena_plus_music_default.txt     — fallback for all rows
  arena_plus_music_0.txt .. _8.txt — per boss row (-1 disables music for that row)

Timing / softness:
  arena_plus_music_delay_ms.txt     — fallback crossfade delay (default 500)
  arena_plus_music_fade_frames.txt — minimum crossfade length on fallback path (default 90; 0=instant)

Env vars (optional): FFXHOOKS_ARENAPLUS_MUSIC_TRACK, FFXHOOKS_ARENAPLUS_MUSIC_TRACK_0..8,
  FFXHOOKS_ARENAPLUS_MUSIC_DELAY_MS, FFXHOOKS_ARENAPLUS_MUSIC_FADE_FRAMES

Log: %TEMP%\ffx-hooks.log — search "music override" / "PlayTrack" / "SwitchCrossfade"
