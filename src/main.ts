import './style.css';

// The game is C on SDL3, compiled to WebAssembly by `npm run wasm`, which writes
// game.js and game.wasm into public/. Emscripten reads its settings from a
// global Module object, which has to exist before game.js runs.
const canvas = document.querySelector<HTMLCanvasElement>('#canvas')!;
const status = document.querySelector<HTMLElement>('#score')!;

(window as unknown as { Module: object }).Module = {
  canvas,
  print: (line: string) => console.log(line),
  printErr: (line: string) => console.warn(line),
};

const script = document.createElement('script');
script.src = './game.js';   // relative, so the build works in itch.io's iframe and in subdirectories
script.onerror = () => {
  status.textContent = 'The game could not load. Run "npm run wasm" to build it first.';
};
document.body.append(script);

// Keys go to the game once it has focus; clicking anywhere on it is enough.
canvas.addEventListener('pointerdown', () => canvas.focus());
