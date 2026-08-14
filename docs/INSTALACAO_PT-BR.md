# Guia de instalação (PT-BR)

> **Aviso BETA:** Este projeto saiu do alfa recentemente. Use um **save descartável**.
> Todos os hooks vêm DESLIGADOS por padrão. Espere bugs e incompatibilidades com
> outros mods. Veja [KNOWN_BUGS.md](KNOWN_BUGS.md) e [ROADMAP.md](ROADMAP.md).

**Este projeto é mantenido por um desenvolvedor brasileiro** (WanxTitanx / FFX Mod
Studio). O código e a documentação técnica estão em inglês (padrão da comunidade),
mas este guia de instalação está em português para facilitar.

---

## Pré-requisitos

- **FINAL FANTASY X/X-2 HD Remaster** (Steam, PC)
- O **FFX module loader** — um proxy `dinput8.dll` na raiz do jogo que carrega
  DLLs da pasta `modules\`. Se você já usa mods que carregam via `modules\`, você
  já tem isso. Se não, precisa instalar (veja o
  [ff10-file-loader do ffgriever](https://github.com/ffgriever)).
- **.NET 8 desktop runtime** — só se quiser usar o injetor SIN (`SinScaleInject.exe`).
  Baixe em [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/8.0).

---

## Opção A: Instalar pelo zip de release (recomendado)

### Passo 1 — Baixar

Vá na [página de Releases](https://github.com/WanxTitanx/ffx-mod-hooks/releases)
e baixe o zip da versão mais recente (ex: `ffx-hooks-release-v0.1.0-beta.1.zip`).

### Passo 2 — Achar o diretório do jogo

O jogo costuma estar em:

```
D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\
```

Ou onde sua biblioteca Steam estiver. A pasta tem que ter `FFX.exe` e uma
subpasta `modules\`.

### Passo 3 — Fazer backup

Antes de mexer em nada, faça backup da pasta `modules\`:

```powershell
Copy-Item "D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\modules" `
          "D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\modules.backup_$(Get-Date -Format yyyyMMdd)" `
          -Recurse -Force
```

### Passo 4 — Extrair e copiar

Abra o zip. Você vai ver:

```
ffx-hooks.dll          -> copiar para <jogo>\modules\
ffx-probe.dll          -> copiar para <jogo>\modules\
ffx-hooks.ini          -> copiar para <jogo>\ (AO LADO do FFX.exe, não em modules\)
SinScaleInject\        -> copiar para <jogo>\modules\tools\SinScaleInject\
INSTALL.md             -> referência (não precisa copiar)
```

Depois de copiar, a pasta do jogo deve ficar assim:

```
<jogo>\
  FFX.exe
  dinput8.dll              (o proxy loader — já deve existir)
  ffx-hooks.ini            (nossa config)
  modules\
    ffx-hooks.dll          (nossa camada de hooks)
    ffx-probe.dll          (nosso probe — desligado por padrão)
    tools\
      SinScaleInject\
        SinScaleInject.exe
        SinCoreLib.dll
        Xe.BinaryMapper.dll
        ...
```

### Passo 5 — Ligar uma feature (jogo FECHADO)

Todos os hooks vêm DESLIGADOS. Você liga criando um arquivo de flag:

```powershell
# Menu F7 In-Live (o núcleo funcional)
New-Item -ItemType File "<jogo>\modules\config\f7_inlive.flag"
```

Outras flags úteis:

```powershell
# F7 Monster AI Swap
New-Item -ItemType File "<jogo>\modules\config\f7_aiswap.flag"

# Hook de música
New-Item -ItemType File "<jogo>\config\music.flag"

# Nova Super Damage (lab — dano 99999)
New-Item -ItemType File "<jogo>\config\nova_super_damage.flag"
```

Também dá pra ligar por variável de ambiente em vez de arquivo:

```powershell
$env:FFXHOOKS_ENABLE_F7 = "1"   # depois abre o FFX por esse terminal
```

### Passo 6 — Abrir e verificar

1. Feche o jogo se estiver aberto.
2. Abra o FFX (pelo Steam, ou pelo terminal com a variável de ambiente).
3. Verifique o log: abra `%TEMP%\ffx-hooks.log` — deve aparecer:

```
[ffx-hooks] DLL_PROCESS_ATTACH enter
[ffx-hooks] FFX.exe base = 0x...
[ffx-hooks] InstallHooks enter
[ffx-hooks] F7: instalado ok=1 (difficulty=ON, ...)
[ffx-hooks] InstallHooks leave
```

4. Aperte **F7** no jogo — o menu In-Live deve abrir.

### Passo 7 — Usar o menu F7

| Submenu | O que faz |
|---|---|
| **DIFF** | Presets de dificuldade + multiplicadores de stats + auto-status + Apply Now / Save |
| **FORCE** | Force Last Battle (re-dispara o último encontro natural) |
| **MUSIC** | Lock de faixa / trocar música de batalha / randomizer / fade |

Navega com as setas (ou D-pad). Liga/desliga com Enter. Volta com Esc.

## Opção B: Compilar do código-fonte

### Pré-requisitos

- Windows 10/11
- [Visual Studio 2022](https://visualstudio.microsoft.com/) com a workload
  "Desktop development with C++" (ferramentas MSVC x86/x64)
- [vcpkg](https://github.com/microsoft/vcpkg) — gerenciador de pacotes C++
- PowerShell
- .NET 8 SDK (para o injetor SIN)

### Passo 1 — Clonar

```powershell
git clone https://github.com/WanxTitanx/ffx-mod-hooks.git
cd ffx-mod-hooks
```

### Passo 2 — Instalar dependências do vcpkg (uma vez)

```powershell
# Se ainda não tem o vcpkg:
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Instalar as libs estáticas x86:
C:\vcpkg\vcpkg install --triplet x86-windows-static polyhook2 zydis
```

> **Nota:** MinHook já vem dentro do repo (`src/runtime/FfxHooksDll/third_party/minhook/`)
> — não precisa instalar pelo vcpkg.

### Passo 3 — Copiar vcpkg_installed para o projeto

O script de build espera `vcpkg_installed\` dentro da pasta FfxHooksDll:

```powershell
Copy-Item C:\vcpkg\installed\x86-windows-static .\src\runtime\FfxHooksDll\vcpkg_installed -Recurse -Force
```

### Passo 4 — Compilar a DLL de hooks

```powershell
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release
```

Saída: `src\runtime\FfxHooksDll\bin\Release\ffx-hooks.dll`

> **Dica:** Também dá pra abrir `FfxHooksDll.vcxproj` no Visual Studio e compilar
> lá (o build da IDE é a referência — 0 erros). Um hook novo precisa ser
> registrado nos **dois** (vcxproj E build_hooks.ps1).

### Passo 5 — Compilar o probe

```powershell
.\src\runtime\FfxDinput8Probe\build.ps1
```

Saída: `src\runtime\FfxDinput8Probe\ffx-probe.dll`

### Passo 6 — Compilar o injetor SIN

```powershell
dotnet build src\sin\SinScaleInject\SinScaleInject.csproj -c Release
```

Saída: `src\sin\SinScaleInject\bin\Release\net8.0\` (exe + dependências)

### Passo 7 — Deploy

Copie os arquivos compilados pro jogo como na Opção A, Passo 4. Ou use a flag
de deploy (lab deploy pra uma cópia descartável):

```powershell
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release -Deploy -LabDeploy -GameRoot "D:\copia\do\jogo"
```

---

## Desinstalar

### Desinstalação rápida (remove os hooks, mantém as DLLs)

Delete os arquivos de flag — os hooks desligam no próximo restart:

```powershell
Remove-Item "<jogo>\modules\config\f7_inlive.flag" -Force
Remove-Item "<jogo>\modules\config\f7_aiswap.flag" -Force
Remove-Item "<jogo>\config\music.flag" -Force
```

Reinicie o FFX. As DLLs continuam carregadas mas não fazem nada (tudo OFF).

### Desinstalação completa (remove tudo)

1. Feche o FFX.
2. Delete as DLLs e config:

```powershell
Remove-Item "<jogo>\modules\ffx-hooks.dll" -Force
Remove-Item "<jogo>\modules\ffx-probe.dll" -Force
Remove-Item "<jogo>\ffx-hooks.ini" -Force
Remove-Item "<jogo>\modules\tools\SinScaleInject" -Recurse -Force
```

3. Delete qualquer arquivo de flag que você criou.
4. Restaure o backup da pasta `modules\` se fez um.
5. Reinicie o FFX — o jogo roda vanilla.

Tudo é RAM-only e reversível. Os hooks **nunca** editam arquivos `.bin` (o
injetor SIN edita `.bin`, mas só quando explicitamente chamado — e tem
`--restore` e `--restore-area` pra desfazer).

---

## Problemas comuns

### O F7 não abre

- Verifique `%TEMP%\ffx-hooks.log` — a DLL carregou? InstallHooks rodou?
- Confirme que a flag existe: `Test-Path "<jogo>\modules\config\f7_inlive.flag"`
- Confirme que a DLL está em `modules\`, não na raiz do jogo.
- Confirme que `dinput8.dll` (o proxy loader) existe na raiz do jogo.

### O jogo crasha ao abrir

- Verifique se `ffx-hooks-polyhook-lab.dll` está na pasta do jogo — **delete**
  (é artefato de lab que crasha o menu — K-20).
- Verifique `%TEMP%\ffx-hooks.log` por linhas FAULT (endereço do crash).
- Remova todas as flags e tente de novo (boot limpo com hooks dormindo).

### O hook some depois de um update do jogo

- Um update da Steam pode mudar os bytes do `FFX.exe`, quebrando a validação de
  assinatura (K-23). O hook loga "unexpected bytes" e fica OFF. Os RVAs precisam
  ser re-validados contra o novo exe. Abra uma issue no repo.

### Conflito com outros mods

- Se usa Special K (`dxgi.dll`), UnX (`unx.dll`), ou qualquer mod que hooka
  `GetDeviceState` (vtable slot 9), espere conflito (K-24). UnX crashou 3/3
  com nosso probe. Tente rodar sem esses mods primeiro.
