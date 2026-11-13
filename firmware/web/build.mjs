import { build } from 'esbuild';
import { readFile, writeFile, mkdir } from 'node:fs/promises';

const worker = await build({ entryPoints: ['photo-worker.js'], bundle: true, write: false, minify: true, format: "iife", define: { "Buffer.from": "Uint8Array.from" } });
const script = await build({
  entryPoints: ['panel.js'], bundle: true, write: false, minify: true, format: 'iife',
  define: { PHOTO_WORKER: JSON.stringify(worker.outputFiles[0].text) },
});
const html = await readFile('index.html', 'utf8');
const css = await readFile('style.css', 'utf8');
const avatar = await readFile('profile-avatar.jpg').catch(error => {
  if (error.code !== 'ENOENT') throw error;
  return null;
});
await mkdir('dist', { recursive: true });
await writeFile('dist/index.html', html
  .replace('<!-- profile-avatar -->', avatar
    ? `<img class="profile-avatar" src="data:image/jpeg;base64,${avatar.toString('base64')}" alt="">`
    : '')
  .replace('<!-- styles -->', () => '<style>' + css + '</style>')
  .replace('<!-- scripts -->', () => '<script>' + script.outputFiles[0].text.replace(/<\/script/gi, '<\\/script') + '</script>'));
