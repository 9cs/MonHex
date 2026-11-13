# MonHex

<img src="icon.jpg" width="96" align="right" alt="MonHex icon">

A Pokémon save editor that runs on the Nintendo Switch, in the spirit of [PKSM](https://github.com/FlagBrew/PKSM) on the 3DS. Open your game's save right on the console, edit and organise your Pokémon, keep them in a bank shared by every game, and write the save back, with a backup made first every time.

Built on [PKSM-Core](https://github.com/FlagBrew/PKSM-Core), libnx and SDL2.

> **Back up your saves.** MonHex makes a backup before opening and before writing a save, and can restore them, but it is still young software. Pokémon edited or moved between games with MonHex have no Pokémon HOME tracker and may be flagged online.

*Uma versão em português está no final deste arquivo.*

## Features

- **Boxes, party and bank** with box sprites, a summary panel and a detail view (IV/EV chart, stats, moves).
- **Full Pokémon editor**: species, nickname, level, gender, shiny, nature (with a nature chart), ability, held item, Poké Ball, friendship, moves, IVs, EVs, original trainer, IDs, origin game, met location/level/date, egg data, fateful encounter.
- **Create Pokémon** in any empty slot.
- **Organise**: move, swap, clone and release, one at a time or many at once.
- **Bank**: 50 boxes on the SD card shared by every game; Pokémon are converted when moved into a different game (when the game supports them).
- **Export and import** `.pk9`, `.pa9`, `.pk8`, `.pa8`, `.pb7` and older formats (compatible with PKHeX and PKSM).
- **Search** by name/species, shiny, type and level across the party, boxes and bank.
- **Bag**: change quantities, add, swap and remove items; TMs show the move they teach.
- **Backups**: automatic before opening and before writing (configurable limit), restorable from the app.
- **Languages**: English, Português (Brasil), Español, Français.
- Optional **cries**: touch a Pokémon in its detail view (downloaded once from Pokémon Showdown; needs internet the first time).

Everything else (sprites, icons, names, descriptions) is bundled, so MonHex works offline.

## Games

| Game | Status |
|---|---|
| Sword / Shield | Supported |
| Legends: Z-A | Supported |
| Scarlet / Violet | Supported |
| Legends: Arceus | Supported |
| Let's Go, Pikachu! / Eevee! | Supported |
| FireRed / LeafGreen (Switch, 2026) | Experimental |
| Brilliant Diamond / Shining Pearl | Not supported |

## Installing

1. Copy `monhex.nro` to `/switch/` on your SD card.
2. Open the Homebrew Menu, preferably by **holding R while starting any game** (title override). From the Album also works, with less memory.
3. Pick a user and a game.

MonHex keeps its files in `sdmc:/switch/monhex/`:

| Folder / file | Contents |
|---|---|
| `backups/<title id>/<user>/<date-time>/` | Save backups |
| `bank/box_NNN/` | The bank, one file per Pokémon |
| `pkm/` | Exported Pokémon (also read for importing, together with `sdmc:/pkm`) |
| `cache/cries/` | Downloaded cries |
| `config.ini`, `log.txt` | Settings and a log of what was read and written |

## Controls (boxes)

| Button | Action |
|---|---|
| A | Slot options (details, edit, move, clone, export, release, create, import) |
| Y / X / + / − | Boxes / Party / Bag / Bank (also the tabs at the top) |
| L / R | Previous / next box |
| ZR | Mark a Pokémon (for actions on many) |
| ZL | Save changes to the game |
| Right stick press (or the top button) | Search |
| B | Back one step |

Changes stay pending until you save; leaving without saving offers to discard them.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `switch-dev` and `switch-portlibs`.

```bash
git clone --recursive <this repository>
cd monhex
sh tools/fetch_sprites.sh   # sprites, item and type icons (PKHeX), HOME renders (PokeAPI)
make
```

`tools/fetch_sprites.sh` shrinks the HOME renders to WebP with `tools/resize_home.py`, which needs Python 3 and Pillow (`pip install pillow`). The generated data in the repository can be rebuilt with:

- `tools/gen_movedata.py`: move types, power, accuracy and PP (PokeAPI `moves.csv`)
- `tools/gen_itemdata.py`: TM moves and item/ability descriptions (PokeAPI CSVs)
- `tools/gen_strings.py`: the app's text in every language (`romfs/lang/strings.tsv`)
- `tools/make_logo.ps1`: the icon and header logo

Send a build to the console with the Homebrew Menu's netloader (press Y there): `nxlink -s monhex.nro`.

## Credits

- [PKSM-Core](https://github.com/FlagBrew/PKSM-Core) by FlagBrew: all save and Pokémon reading and writing.
- [PKHeX](https://github.com/kwsch/PKHeX) by kwsch: the format research PKSM-Core builds on, and the box sprites, item, ball and type icons.
- [PokeAPI](https://github.com/PokeAPI) (data and sprites): HOME-style renders, move data, TM tables, item and ability descriptions.
- [Pokémon Showdown](https://play.pokemonshowdown.com): cries, downloaded on demand.
- [libnx](https://github.com/switchbrew/libnx) by Switchbrew and [SDL2](https://www.libsdl.org) for the Switch by devkitPro.

Pokémon and all related names and images are trademarks of Nintendo, Game Freak, Creatures and The Pokémon Company. MonHex is an unofficial fan project, not affiliated with or endorsed by them.

## License

GPLv3 (see `LICENSE`). PKSM-Core is GPLv3 with additional terms under section 7 (preserving author attributions and marking modified versions); MonHex keeps those attributions.

---

## Em português

MonHex é um editor de saves de Pokémon que roda no próprio Nintendo Switch, no estilo do PKSM do 3DS. Ele abre o save do jogo no console, permite editar e organizar os Pokémon, guardar Pokémon num banco compartilhado entre os jogos e gravar o save de volta, sempre com um backup antes.

- **Instalação:** copie `monhex.nro` pra `/switch/` no cartão SD e abra pelo Homebrew Menu, de preferência segurando R ao iniciar um jogo.
- **Idioma:** o padrão é inglês. Mude em Configurações (botão − na lista de jogos) → Idioma.
- **Arquivos:** tudo fica em `sdmc:/switch/monhex/` (backups, banco, Pokémon exportados, configurações).
- **Atenção:** faça backup dos seus saves. Pokémon editados ou transferidos entre jogos não têm o rastreador do Pokémon HOME e podem ser detectados online.
