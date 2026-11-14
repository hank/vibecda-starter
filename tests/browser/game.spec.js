import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { mkdtemp, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, extname } from 'node:path';
import { unzipSync } from 'fflate';
import { packageGame } from '../../scripts/package-game.mjs';

// The game reports what is happening in the status line above the canvas, which
// is also what screen readers hear. Rank 1 always opens with F, then J.
const TITLE = 'Home Row Horde. Press Enter to start.';

test('production build loads, and typing the letters slays zombies', async ({ page }) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  const failures = [];
  page.on('response', response => { if (response.status() >= 400) failures.push(response.url()); });
  await page.goto('/');
  const status = page.getByRole('status');
  await expect(page.locator('canvas')).toBeVisible();
  await expect(status).toHaveText(TITLE);
  await page.locator('canvas').click();
  await page.keyboard.press('Enter');
  await expect(status).toHaveText('Rank 1 SPROUT. Slain 0/12. Type F');
  // A wrong key must not count; the right one must, in the real production game.
  await page.keyboard.press('q');
  await expect(status).toHaveText('Rank 1 SPROUT. Slain 0/12. Type F');
  await page.keyboard.press('f');
  await expect(status).toContainText('Slain 1/12');
  await expect(status).toHaveText('Rank 1 SPROUT. Slain 1/12. Type J');
  await page.keyboard.press('j');
  await expect(status).toContainText('Slain 2/12');
  // Escape pauses; Enter resumes where play left off; Q from the pause screen quits.
  await page.keyboard.press('Escape');
  await expect(status).toHaveText('Paused. Press Enter to keep playing.');
  await page.keyboard.press('f');
  await page.keyboard.press('Enter');
  await expect(status).toContainText('Slain 2/12');
  await page.keyboard.press('Escape');
  await page.keyboard.press('q');
  await expect(status).toHaveText(TITLE);
  expect(errors).toEqual([]);
  expect(failures).toEqual([]);
});

test('a higher rank can be chosen from the title and uses whole words', async ({ page }) => {
  await page.goto('/');
  const status = page.getByRole('status');
  await expect(status).toHaveText(TITLE);
  await page.locator('canvas').click();
  await page.keyboard.press('4');
  await page.keyboard.press('Enter');
  await expect(status).toContainText('Rank 4 ARCHER. Slain 0/16. Type ');
  // Type whatever the game asks for until the first word is finished.
  for (let i = 0; i < 12 && !(await status.textContent()).includes('Slain 1/16'); i++) {
    const match = (await status.textContent()).match(/Type (\w)$/);
    if (match) await page.keyboard.press(match[1].toLowerCase());
    await page.waitForTimeout(120);
  }
  await expect(status).toContainText('Slain 1/16');
});

test('F2 opens beat calibration, and tapping along measures and saves a delay', async ({ page }) => {
  await page.goto('/');
  const status = page.getByRole('status');
  await expect(status).toHaveText(TITLE);
  await page.locator('canvas').click();
  await page.keyboard.press('F2');
  await expect(status).toHaveText('Beat calibration. Tap space on every beat. Saved delay 0 ms.');
  // Nudging changes the saved delay, and it survives a reload.
  await page.keyboard.press('Equal');
  await page.keyboard.press('Equal');
  await expect(status).toContainText('Saved delay 10 ms.');
  // Tap steadily for a while (half a second apart, like the click), then save what was measured.
  await page.waitForTimeout(1200);
  for (let i = 0; i < 10; i++) { await page.keyboard.press('Space'); await page.waitForTimeout(500); }
  await page.keyboard.press('Enter');
  await expect(status).not.toContainText('Saved delay 10 ms.');
  const saved = (await status.textContent()).match(/Saved delay (-?\d+) ms/)[1];
  await page.reload();
  await page.locator('canvas').click();
  await page.keyboard.press('F2');
  await expect(status).toContainText(`Saved delay ${saved} ms.`);
  await page.keyboard.press('Escape');
  await expect(status).toHaveText(TITLE);
});

test('the canvas scales down to phone width without losing its shape', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('/');
  await expect(page.getByRole('status')).toHaveText(TITLE);
  const box = await page.locator('canvas').boundingBox();
  expect(box.width).toBeLessThanOrEqual(390);
  expect(Math.abs(box.width / box.height - 4 / 3)).toBeLessThan(0.02);
});

test('the actual submission ZIP runs under a subdirectory inside an iframe', async ({ page }) => {
  const dir = await mkdtemp(join(tmpdir(), 'vibecda-iframe-'));
  const { output } = await packageGame('dist', join(dir, 'game.zip'));
  const files = unzipSync(await readFile(output));
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm' };
  const server = createServer((req, res) => {
    if (req.url === '/') {
      res.setHeader('Content-Type', 'text/html');
      res.end('<!doctype html><title>Upload rehearsal</title><iframe title="Submitted game" src="/uploads/team-game/" width="960" height="800"></iframe>');
      return;
    }
    const path = decodeURIComponent(new URL(req.url, 'http://localhost').pathname).replace(/^\/uploads\/team-game\//, '') || 'index.html';
    const file = files[path];
    if (!file) { res.statusCode = 404; res.end('Missing file'); return; }
    res.setHeader('Content-Type', types[extname(path)] || 'application/octet-stream');
    res.end(file);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  try {
    await page.goto(`http://127.0.0.1:${port}/`);
    const frame = page.frameLocator('iframe[title="Submitted game"]');
    const status = frame.getByRole('status');
    await expect(frame.locator('canvas')).toBeVisible();
    await expect(status).toHaveText(TITLE);
    await frame.locator('canvas').click();
    await page.keyboard.press('Enter');
    await expect(status).toHaveText('Rank 1 SPROUT. Slain 0/12. Type F');
    await page.keyboard.press('f');
    await expect(status).toContainText('Slain 1/12');
  } finally {
    await page.goto('about:blank');
    await new Promise(resolve => server.close(resolve));
  }
});
