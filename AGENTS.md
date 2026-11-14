# vibeCDA starter instructions

This branch holds Home Row Horde, a typing defence game written in C on SDL3
(src/sdl/) and compiled to WebAssembly with Emscripten. Vite and TypeScript only
provide the page around the canvas (index.html, src/main.ts, src/style.css).
The Phaser and Three.js starter demos were removed from src/; their packages and
the participant guides in docs/ are left as they were in the starter.

- npm run native builds build/home-row-horde for quick local play (needs SDL3).
  npm run wasm writes public/game.js and public/game.wasm, using emcc if it is
  on PATH and the emscripten/emsdk Docker image otherwise. Run npm run wasm
  before npm run build or npm run package after changing any C code.
- Headless checks of the native build: SDL_VIDEODRIVER=offscreen with
  HRH_FRAMES=n, HRH_RANK=1..8, HRH_BOT=keys-per-second, HRH_BOSS=1, and
  HRH_SHOT=file.bmp to save the last frame.
- The game has three modes chosen on the title screen: typing, math (answers
  typed as digits), and an Idaho driver's test (multiple choice). Driver's test
  questions live in src/sdl/drivers.h and were written from the Idaho Driver's
  Handbook; check any new or changed fact against the current handbook.
  HRH_MODE=1..3 picks the mode in headless runs. Headless runs use a fixed
  random seed unless HRH_SEED=n is set; normal play seeds from the clock.
- The browser tests read the status line above the canvas, which the game
  updates from C (set_status in src/sdl/game.c). Keep the two in step.

- Read package.json before installing anything. The devcontainer already has
  Node, Claude Code, Codex, Gemini CLI, Chromium/Playwright, ffmpeg, ImageMagick,
  zip, and ripgrep. Participants authenticate their own AI accounts.
- Run npm run dev for port 5173, npm run build for type/build checks, and
  npm run package for artifacts/game.zip. Run npm run test:browser when changing
  controls or the submission path. Update the browser test if changing mechanics.
- Use relative asset paths; the production build must work inside itch.io's
  iframe and from subdirectories. Keep index.html at the root of the ZIP.
- The starter has no game backend or runtime AI API. Do not add either without
  discussing the requirement with the team. Do not put secrets in public/,
  VITE_ variables, Git, built assets, or container images.
- Keep gameplay small and playable. Preserve keyboard and touch controls where
  possible. Record third-party asset licenses and AI usage in credits.
- Do not change event/ rules or event settings during gameplay work. Internal
  organizer operations and scoring tools live in a separate private repository.
- Commit and push useful milestones to the team's repository. Before publishing
  or uploading, check the target account and destination with the participant.
