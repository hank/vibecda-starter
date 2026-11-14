// Builds the SDL game in src/sdl/.
//   node scripts/build-sdl.mjs native   -> build/home-row-horde (needs SDL3 and a C compiler)
//   node scripts/build-sdl.mjs wasm     -> public/game.js + public/game.wasm (needs Emscripten)
// For the wasm build, emcc on PATH is used when present; otherwise the official
// emscripten/emsdk Docker image is used, so nothing has to be installed.
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { userInfo } from 'node:os';

const EMSDK_IMAGE = 'emscripten/emsdk:4.0.15';
const source = 'src/sdl/game.c';
const target = process.argv[2] ?? 'wasm';
const run = (cmd, args) => {
  console.log([cmd, ...args].join(' '));
  const result = spawnSync(cmd, args, { stdio: 'inherit' });
  if (result.error) throw new Error(`${cmd} could not run: ${result.error.message}`);
  if (result.status !== 0) process.exit(result.status ?? 1);
};
const has = cmd => spawnSync(cmd, ['--version'], { stdio: 'ignore' }).status === 0;

if (target === 'native') {
  mkdirSync('build', { recursive: true });
  const flags = execFileSync('pkg-config', ['--cflags', '--libs', 'sdl3'], { encoding: 'utf8' }).trim().split(/\s+/);
  run('cc', ['-O2', '-Wall', '-Wextra', source, ...flags, '-lm', '-o', 'build/home-row-horde']);
  console.log('Built build/home-row-horde');
} else if (target === 'wasm') {
  const emcc = ['-O2', '-Wall', source, '--use-port=sdl3', '-sALLOW_MEMORY_GROWTH=1', '-sENVIRONMENT=web', '-o', 'public/game.js'];
  if (has('emcc')) run('emcc', emcc);
  else if (has('docker')) {
    const { uid, gid } = userInfo();
    run('docker', ['run', '--rm', '-u', `${uid}:${gid}`,
      '-v', `${process.cwd()}:/src`, '-w', '/src', EMSDK_IMAGE, 'emcc', ...emcc]);
  } else {
    console.error('The wasm build needs Emscripten (emcc) or Docker. See https://emscripten.org/docs/getting_started/downloads.html');
    process.exit(1);
  }
  console.log('Built public/game.js and public/game.wasm');
} else {
  console.error('Usage: node scripts/build-sdl.mjs native|wasm');
  process.exit(1);
}
