# PARTE 3 MOD 002 — Patches reutilizáveis e questões de escopo

[Voltar à parte principal](<MOD 002.md>).

- [ ] Criar no Fahrenheit uma coleção de patches gerais de engine para outros mods reutilizarem, evitando que cada um reimplemente correções de runtime complexas. peppy comparou a ideia ao USSEP de Skyrim; alterações específicas de Master's Challenge, como o overhaul do limite de dano, ficariam fora.
- [ ] Usar o termo **Patches** em vez de **Fixes** para não dar a entender que toda alteração subjetiva corrige um bug real do jogo.
- [ ] Decidir se as fórmulas vanilla não usadas devem ser reaproveitadas ou se novas constantes/fórmulas devem ser adicionadas, preservando as existentes quando possível.
- [ ] Delimitar o conjunto entre correções de problemas objetivos e mudanças subjetivas de balanceamento/expansão. A conversa questiona explicitamente se a coleção deveria incluir apenas bugs, o patch de 4 GB e/ou extensões de gameplay.
- [ ] Reavaliar amplificadores multiplicativos de dano: Dawn Veilwinter depois considerou Energy Boost/Burst má ideia, pois FFX é conservador com amplificação de dano e multiplicadores podem incentivar min-maxing.
- Kari AP chegou a considerar nomes personalizados para equipamentos, mas concluiu que essa ideia pertencia a EFP.
- [ ] Confirmar o fragmento de 04/11/2025 sobre summon e CTB antes de registrar uma mudança: o trecho explica que personagens fora de campo mantêm seu CTB e que quase todos os Aeons recebem um turno imediato ao serem invocados, mas não conserva a proposta que iniciou a conversa.

## Caminhos dos repositórios e dados

- **Hooks:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, branch `codex/mod-002-parts-20260927` (documentos e futura implementação local).
- **Fahrenheit:** `/home/wanderson/Documents/external-compare/fahrenheit`, branch `main`, commit `c149c847b3a24a66114956f87f1b008599736f75` no levantamento; esta parte discute patches gerais para esse framework.
- **FFX Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor` no levantamento de 27/09/2026.
- **FFX Steam:** `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/`.
- **FFX Extracted:** `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`.
- **Spira Reforge:** `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`; os IDs concretos das habilidades novas estão na [Parte 2](<PARTE 2 MOD 002.md>).
