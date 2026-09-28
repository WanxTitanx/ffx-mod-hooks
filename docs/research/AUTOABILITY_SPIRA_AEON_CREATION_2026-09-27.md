# Jarvis-HOOK — auto-habilidades Spira/Aeon criadas nos arquivos locais

**27/09/2026: aplicação concluída.** O usuário pediu expressamente a criação das novas habilidades dos documentos, Spira Reforge e Aeons, incluindo Double/Triple Drop.

Repo documental: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, base `3b010845ece1d5f927485eae0cf67f04a46c97b2`. Editor: `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`, HEAD consultado `39916423`. Runtime de referência: `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`. Nenhum source de Hook, DLL, configuração, save ou equipamento foi alterado.

## Resultado aplicado

- **27 novas linhas, IDs 148–174**; todas as tabelas de habilidade alvo agora têm **175 registros**.
- **13 a_ability.bin e 12 arms_rate.bin**: Steam nos dez idiomas existentes; Spira Reforge JP/US e preço JP; FFX Extracted US e preço JP.
- Registros **0–147 e pool de textos anterior preservados byte a byte**, incluindo os 13 atributos MOD-002 anteriores.
- Novos rates zero como preenchimento neutro da tabela. Isso **não é o preço do Workshop**: as receitas Aeon continuam propostas em 10M/15M Gil.
- Inglês provisório nos idiomas latinos; JP/CH/KR usam os rótulos numéricos seguros previamente aprovados. Nomes completos permanecem no registro/menu futuro do Hook.

## IDs criados

| ID | Palavra | Nome | Estado dos dados |
|---:|---|---|---|
| 148 | `0x8094` | Aeon Break HP/MP Limit | Neutra; Hook/efeito pendente |
| 149 | `0x8095` | Aeon Break Damage Limit | Neutra; Hook/efeito pendente |
| 150 | `0x8096` | Mana Spring | Neutra; Hook/efeito pendente |
| 151 | `0x8097` | Break Limits | Campos nativos configurados |
| 152 | `0x8098` | Devil's Bargain | Neutra; Hook/efeito pendente |
| 153 | `0x8099` | Warden's Oath | Neutra; Hook/efeito pendente |
| 154 | `0x809A` | Arcane Focus | Neutra; Hook/efeito pendente |
| 155 | `0x809B` | Double Drop | Marcador para Hook de Drop existente |
| 156 | `0x809C` | Triple Drop | Marcador para Hook de Drop existente |
| 157 | `0x809D` | Element Eater | Campos nativos configurados |
| 158 | `0x809E` | HPMP +10% | Campos nativos configurados |
| 159 | `0x809F` | HPMP +20% | Campos nativos configurados |
| 160 | `0x80A0` | HPMP +40% | Campos nativos configurados |
| 161 | `0x80A1` | HPMP +60% | Campos nativos configurados |
| 162 | `0x80A2` | AIO +3% | Campos nativos configurados |
| 163 | `0x80A3` | AIO +6% | Campos nativos configurados |
| 164 | `0x80A4` | AIO +9% | Campos nativos configurados |
| 165 | `0x80A5` | AIO +12% | Campos nativos configurados |
| 166 | `0x80A6` | STR MAG +10% | Campos nativos configurados |
| 167 | `0x80A7` | STR MAG +20% | Campos nativos configurados |
| 168 | `0x80A8` | DEF MDEF +10% | Campos nativos configurados |
| 169 | `0x80A9` | DEF MDEF +20% | Campos nativos configurados |
| 170 | `0x80AA` | Foolstrike | Neutra; Hook/efeito pendente |
| 171 | `0x80AB` | Fooltouch | Neutra; Hook/efeito pendente |
| 172 | `0x80AC` | Fourstrike | 4 elementos nativos; estados extras pendentes |
| 173 | `0x80AD` | Fourtouch | 4 elementos nativos; estados extras pendentes |
| 174 | `0x80AE` | Spell Spring | Neutra; Hook/efeito pendente |

Registro unificado dos **40 IDs 135–174**: [registry.json](../../research/autoability_expansion/registry.json). Definições exatas utilizadas: [definitions.json](../../research/autoability_expansion/definitions.json). O menu futuro do Hook deve permitir remapeamento por chave com validação de existência, colisões, payload/marker e owner; alterar um ID na configuração não move a linha do arquivo.

## Decisões de compatibilidade

**Double/Triple Drop:** nenhuma das 13 tabelas de entrada continha marcadores `+0x64 & 0x3000`. Os registros129/130 legados tinham diferenças entre idiomas e foram preservados. Os novos **155/156** receberam **0x1000/0x2000**, como exige `DoubleTripleDropHook.cpp`. O consumidor lê os bits agregados, não exige IDs129/130. É necessário equipar o novo ID e habilitar o Hook existente; nenhum toggle foi alterado e nenhum RT2 foi executado.

**Reaproveitamentos históricos:** One MP Cost13, BHP23, BMP24 e famílias antigas permanecem intactos. Novas identidades aditivas: Mana Spring150, Break Limits151 e Devil's Bargain152. Os **14 registros native** têm apenas os campos definidos: HP/MP, absorção Fire ou bônus de stats, sem copiar valores divergentes do JP antigo.

**Custom134:** preservado com seu nome/payload anterior. Element Eater157 é uma variante limpa de absorção Fire; não copiou os buffs extras do134 chamado Ribbon no US.

**Exclusivas/efeitos não implementados:** Aeon148/149, Warden153/Auron, Arcane154/Lulu e os demais `hook_only` têm payload nativo neutro. Suas linhas não concedem bônus irrestritos a humanos; owner, origem Workshop e efeitos dependem do handler futuro. Arcane Focus/Spell Spring e riders de Fool/Four conservam pendências de design. Direções sem uma auto-habilidade nomeada/definida para outros personagens não receberam efeitos inventados.

**Reworks já existentes:** Sensor, Piercing e demais famílias nativas já têm registros e continuam catalogadas; não foi duplicado todo o catálogo nem foram reescritos bytes legados divergentes por suposição. Fourstrike172/Fourtouch173 receberam somente a base elemental0x0F; estados adicionais continuam pendentes.

## Backup e verificação

Backup completo fora do Git:
`/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json`.

- **13/13 testes offline do gravador:** preservação, headers/bounds, strings/CJK, payloads, colisões, limites, escrita atômica, recusa de drift e rollback sobre fixtures temporários.
- **13/13 tabelas preparadas** aprovadas pelo leitor real do Editor (`--autoability-rt0`).
- **25/25 arquivos aplicados** comparados com a transformação exata, incluindo preservação das linhas/textos anteriores.
- **13/13 tabelas aplicadas** novamente aprovadas pelo leitor do Editor, 175 registros e round-trip byte-idêntico. Esse teste é estrutural, não gameplay.

[Relatório aplicado e hash do leitor](../../research/autoability_expansion/application_report.json) · [validação de staging](../../research/autoability_expansion/staging_validation.json).

```bash
python3 research/autoability_expansion/author.py --verify '/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json'
python3 -m unittest discover -s research/autoability_expansion -p 'test_*.py' -v
# Executar rollback somente quando essa for a ação desejada:
python3 research/autoability_expansion/author.py --rollback '/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json'
```

`--apply` recusa o estado já modificado pelo inventário fixado, evitando duplicação. Rollback recusa edições posteriores e restaura o estado IDs0–147, mantendo o MOD-002 anterior. O verificador do backup antigo não é o recibo do estado atual; usar o manifesto acima.

## Handoff à implementação

1. Consumir o registro de 40 IDs, preservando remapeamento e tags MOD-002/004/005/Spira.
2. Ampliar catálogo/validação do Workshop com handlers e tabelas registrados; o source consultado ainda limita suportados a0x8082. Não aumentar apenas o range. Registros novos não criam receitas nem ações de menu automaticamente.
3. Aeon148/149: ação exclusiva Workshop, recibo, owner8–17, aquisição/Crest e receita já documentada. Nada foi distribuído em equipamentos existentes.
4. Warden153 e Arcane154: gates separados de Auron/Lulu. Código de teto compartilhado não compartilha autorização.
5. Implementar efeitos pendentes e validar RT2 separadamente. Não declarar as 40 linhas funcionais só porque o reader passou.

Os25 binários locais e os backups ficam **fora do commit/push**. Esta branch registra código de authoring, definições, inventários, verificações e documentos; nenhuma mudança da lane runtime foi incorporada implicitamente.
