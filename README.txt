Genesis Recomp v0.1.2 - Windows x64 code-generation tool
======================================================

GenesisRecomp.exe translates qualified Sega Genesis 68000 ROM code into C.
It is a command-line development tool. Game runners and their launcher assets
are downloaded separately from the corresponding game projects.

  GenesisRecomp.exe --help
  GenesisRecomp.exe --version
  GenesisRecomp.exe your-rom.bin --game path/to/game.toml --output-dir generated

Supply your own ROM and a matching game.toml plus its referenced annotation and
discovery files. No ROM, BIOS, donor assets, saves or generated game code is
distributed in this package. An arbitrary ROM/configuration is not a qualified
game integration. Generated C must be compiled with the matching shared runtime
and game-owned adapter; this executable alone does not run games.

Source: https://github.com/mstan/segagenesisrecomp/tree/v0.1.2
Game setup: https://github.com/mstan/segagenesisrecomp/blob/v0.1.2/docs/GETTING_STARTED.md
To build the SDK from source, clone the tag and initialize its pinned submodules.
GitHub's automatic source archives do not include submodule contents.

The tool uses the clean-room decoder/timing model and links no emulator core.
It ships under the included PolyForm Noncommercial 1.0.0 LICENSE, with the
third-party notices and license texts provided alongside it. This Windows x64
package is distinct from the game's portable C runtime and optional host paths.
