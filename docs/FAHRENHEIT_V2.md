# Optional Fahrenheit V2 add-on — v0.6.0-beta.3

> Historical beta.3 add-on: no new provider/bridge is included in beta.4. Compatibility with the updated Steam executable is not certified.
> Complemento histórico da beta.3: a beta.4 não inclui novo provider/bridge. A compatibilidade com o executável Steam atualizado não está certificada.

Jarvis-HOOK, 2026-09-29. This add-on targets Fahrenheit alpha12 source
`cdb145d93295c1c6e2bf4766fda5a12877369f54` from
[the Fahrenheit contributors](https://github.com/fahrenheit-crew/fahrenheit).
It is a local cooperative adaptation, not an upstream release or endorsement.

## English

The main Hooks ZIP works through the existing FFX module loader. The **optional**
`ffx-hooks-fahrenheit-v2-v0.6.0-beta.3.zip` supplies the paired managed bridge and
modified provider for an existing compatible Fahrenheit installation. It is not
a complete Fahrenheit distribution: retain its existing bootstrap, runtime and
dependencies. Do not install a second DINPUT8 proxy or MinHook provider.

Expanded coexistence includes shared hook ownership, frame/input coordination,
managed save transactions and paired read-only text/font resources. Ordinary
feature, profile, signature, package and readiness checks still apply. Stock
Fahrenheit with the V1 bridge keeps its save/text feature restrictions. Fastload
selection remains Fahrenheit-owned. Arbitrary mods/load orders are not certified.

1. Close FFX. Back up the existing Hooks DLL, Fahrenheit provider files,
   `mods/loadorder`, configuration, saves and sidecars.
2. Install the main beta.3 package following [INSTALL](INSTALL.md). Its native
   DLL must be **4,683,264 bytes**, SHA-256
   `4e7dfe943e9496729b69c9c3af24dfef465ec4efddfb93dba5b6fdcd77af8a6e`.
3. In the existing Fahrenheit root, replace only the matching members of `bin/`
   with the add-on's `fahrenheit/bin/`. Preserve all other dependencies/files.
4. Copy `fahrenheit/mods/ffxhooks_fahrenheit/` into Fahrenheit's `mods/` directory.
   Keep its four files together. Add `ffxhooks_fahrenheit` to `mods/loadorder`
   preserving existing entries. Fahrenheit loads `fhr` automatically first; do not add a duplicate `fhr` entry.
5. Keep the main native DLL at `<game>/modules/ffx-hooks.dll`. Do not place
   `fh.dll`, `fhr.dll`, a MinHook DLL or another native Hooks DLL in the bridge
   directory. Do not mix the V2 bridge with the stock V1 provider.
6. Restart after provider, bridge, native DLL or text-package changes. Hot unload
   is unsupported. Test selected features with a disposable save; existing
   configuration is preserved and gameplay options remain opt-in.

The release includes complete corresponding modified provider source in
`fahrenheit-provider-source-v0.6.0-beta.3.tar.gz`; the same archive is embedded
inside the add-on. COPYING, COPYING.LESSER and the adaptation NOTICE accompany
it. Hooks/bridge/overlay source is in `ffx-hooks-source-v0.6.0-beta.3.tar.gz`.
Build instructions are in [provider/README](../integrations/fahrenheit/provider/README.md).
The provider source verifier covers all 501 original tracked files and reverses
only the declared overlay. Checksums attest file identity, not arbitrary compiler
or third-party dependency authenticity.

Windows tests, source verification and isolated CLR/native transport passed for
this composition. **Live auditory/visual/gameplay and save-lifecycle RT2 remains
pending.** Publishing these files neither installs them nor promotes them to
Production. To roll back with FFX closed, restore the complete backed-up
provider/bridge/native set and load order together; retain the user's data.

## Português (Brasil)

O ZIP principal contém a DLL do Hooks. O ZIP **opcional** Fahrenheit V2 é um
complemento para uma instalação compatível do Fahrenheit alpha12: ele traz o
provider modificado e a bridge correspondentes, sem redistribuir todo o
Fahrenheit nem instalar outro DINPUT8/MinHook.

Com o FFX fechado, faça backup da DLL anterior, dos arquivos do provider, de
`mods/loadorder`, configurações, saves e sidecars. Instale o pacote principal;
substitua somente os arquivos correspondentes de `fahrenheit/bin/` no `bin/`
do Fahrenheit existente. Copie `fahrenheit/mods/ffxhooks_fahrenheit/` para
`mods/` e acrescente `ffxhooks_fahrenheit` em `mods/loadorder`, preservando as
outras entradas. O Fahrenheit já carrega `fhr` primeiro; não duplique essa entrada.
Mantenha os quatro arquivos da bridge juntos.
A DLL nativa continua em `<game>/modules/ffx-hooks.dll`.

Não misture bridge V2 com provider V1. Reinicie ao trocar esses componentes;
remoção a quente não é suportada. Os controles de gameplay continuam opcionais,
e as configurações existentes devem ser preservadas. O Fahrenheit original/V1
mantém suas restrições. A seleção de Fastload continua sob controle do Fahrenheit.

O fonte completo do provider modificado e suas licenças acompanham o complemento
e também estão disponíveis separadamente. Os testes comprovam a composição em
RT0/RT1 isolado. **RT2 em jogo continua pendente**, e não se garante qualquer
combinação de mods. Para reverter, restaure o conjunto completo e a ordem de
carregamento do backup com o jogo fechado, preservando os dados do usuário.
