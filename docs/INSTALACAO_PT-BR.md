# Instalação do FFX Hooks v0.6.0-beta

O pacote atende ao **FFX.exe Steam, Windows x86**, no perfil suportado. Não implica
suporte equivalente ao FFX-2. Você precisa do jogo e do carregador de módulos FFX
via DINPUT8 já funcionando; nenhum dos dois está incluído. No Proton, use a mesma
DLL de Windows e os mesmos caminhos relativos à pasta do jogo.

## Download e conteúdo

Baixe `ffx-hooks-release-v0.6.0-beta.zip` e `ffx-hooks-v0.6.0-beta.sha256` na
[release](https://github.com/WanxTitanx/ffx-mod-hooks/releases/tag/v0.6.0-beta).
O arquivo `ffx-hooks-source-v0.6.0-beta.tar.gz` contém o código-fonte da mesma tag.
Compare os hashes dos downloads com o arquivo SHA-256. O ZIP inclui também
`SOURCE.md`, `release-manifest.json` e `CHECKSUMS.sha256` para conferir seu conteúdo.

A DLL tem 3.398.144 bytes e SHA-256
`734a0bf94b56157648e5391ca06dfb1c60aad2c6bb27748762a67315c98b8ffc`.
A versão interna do PE ainda é 0.2.0.0; a versão do pacote/tag é v0.6.0-beta.
Uma recompilação pode produzir outro hash e não identifica esse binário testado.

## Instale com o jogo fechado

1. Feche o FFX e localize a pasta com `FFX.exe` e `dinput8.dll`.
2. Faça backup da DLL anterior, das configurações e dos saves com seus sidecars.
3. Copie `ffx-hooks.dll` do ZIP para `<jogo>/modules/`.
4. Copie `mods/` do ZIP para `<jogo>/modules/`. As 78 cartas ficam em
   `modules/mods/arcana/cards/`; ícone e verso ficam em `shared/`.
5. Preserve o INI atual. Mescle apenas as opções desejadas de
   `examples/ffx-hooks.ini.example` no INI ativo, normalmente
   `<jogo>/_isolated/ffx-hooks.ini`. Os exemplos deixam as opções desligadas.
   Não instale diretórios de flags de laboratório do código-fonte.
6. Reinicie o jogo após habilitar uma opção que exige reinício. F8 abre o painel.
   Extras tem Additional mods e Vanguard; Dev tem FieldScout; Cheats tem
   AP/Gil Multipliers. Configurado, instalado e efetivo são estados diferentes.

Os multiplicadores individuais usam `monster-rewards-v1.tsv` **ao lado do INI
ativo**. O F8 salva a tabela; edição externa exige reinício. O exemplo tem apenas
o cabeçalho: monstros sem uma linha mantêm multiplicador neutro.

## Ativação e dependências

Todos os controles booleanos editáveis do F8 começam OFF; o painel começa ON.
Flags OFF e variáveis externas podem prevalecer sobre o INI. O menu informa esses
bloqueios. Para o gate legado do F7, crie `modules/config/f7_inlive.flag` com o
jogo fechado ou use `FFXHOOKS_ENABLE_F7=1`; cada família continua com seus gates.
O nome legado `f7_aiswap` habilita observação, não troca genérica de AI.

Para Arcana, mescle `examples/Arcana-settings.ini.example`, habilite
`[arcana] enabled=1` e reinicie. Abra **Main Menu > Equip > Tarot**. A opção Dev
para liberar o baralho inteiro é separada da aquisição normal, documentada em
`Arcana-reference/`.

As artes próprias do Arcana estão incluídas. Tradução PT-BR completa, tabelas de
habilidades derivadas do jogo, pacotes Elemental e AI privada do S.I.N. não estão
neste ZIP: o suporte de runtime não substitui esses produtos de authoring.
Siga os contratos de dependências, identidade e hashes no código-fonte. Fixtures
nativas de teste podem exigir seu próprio executável/save/kernel; não são
redistribuídas. O fonte da DLL é completo para compilação sem esses fixtures.

## Verificação, atualização e rollback

Consulte `%TEMP%/ffx-hooks.log` no ambiente Windows/Proton do jogo. Veja o motivo
se uma opção ON continuar indisponível: perfil, assinatura, pacote, caminho ou
flag externa. Outros donos de DINPUT8, Special K ou UnX podem conflitar.

Use save descartável para testar. O [roadmap](ROADMAP.md) separa implementação,
deploy e validação em jogo; testes offline não equivalem a RT2. O protocolo está
em [RT2_PROTOCOL.md](RT2_PROTOCOL.md). Descarregar a DLL com o jogo aberto não é
suportado.

Para atualizar, voltar à versão anterior ou remover a DLL, feche o jogo primeiro.
Preserve o INI, saves nativos e os sidecars correspondentes de Workshop/Aeon/
Arcana/Ronso. Eles podem guardar upgrades pagos ou coleção de cartas. Removê-los
não implica reembolso. Nem todas as funções são apenas RAM. Não remova o loader
compartilhado ou arquivos de outros mods ao desinstalar Hooks.

## Compilar

Use a tag da release e as [instruções de build](../README.md#build): Windows,
Visual Studio C++ x86 e dependências estáticas. O SIN offline usa .NET 8.
`tools/package_release.py` gera o ZIP a partir da DLL validada e de um commit limpo,
verificando hashes; o arquivo de fonte corresponde à árvore pública da tag.
As instruções detalhadas de instalação e ferramentas estão também no
[guia em inglês](INSTALL.md).
