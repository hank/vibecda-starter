# Asset credits

Home Row Horde draws its artwork and makes its music and sounds in code
(`src/sdl/gfx.h`, `src/sdl/audio.h`). It loads no remote fonts, images, or audio.

| Asset | Creator | Source | License |
| --- | --- | --- | --- |
| SDL3 (linked into the game) | Sam Lantinga and SDL contributors | <https://libsdl.org> | zlib license |
| In-game 5×7 font (glyph table in `src/sdl/gfx.h`) | The standard 5×7 character-LCD glyph set, as found in Adafruit-GFX `glcdfont.c` | <https://github.com/adafruit/Adafruit-GFX-Library> | BSD |
| Press Start 2P font on the web page (`src/assets/fonts/`) | The Press Start 2P Project Authors (cody@zone38.net) | <https://fonts.google.com/specimen/Press+Start+2P> | SIL Open Font License 1.1; full text in `public/licenses/press-start-2p-OFL.txt` |
| Sprites, backgrounds, music, sound effects | Made for this game, as code | this repository | same as the repository |

## AI usage

The game's code, pixel art, music, and sound effects were written with Claude Code
(Claude Fable 5.2), directed by the team. No AI runs inside the game.
