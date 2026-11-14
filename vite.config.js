import { defineConfig } from 'vite';

// Relative base: the build has to run inside itch.io's iframe and from subdirectories.
export default defineConfig({ base: './' });
