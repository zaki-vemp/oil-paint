// The block demo's build: index.html at the top of dist, every path relative, nothing from other hosts.
import { fileURLToPath } from 'node:url';
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';

const here = fileURLToPath(new URL('.', import.meta.url));
const rapier = `${here}src/figure/rapier/rapier.mjs`;

export default {
  root: here,
  base: './',
  publicDir: 'public',
  plugins: [{
    name: 'vendored-rapier-source-map',
    apply: 'serve',
    async load(id) {
      if (id.split('?')[0] !== rapier || existsSync(`${rapier}.map`)) return;
      // The vendored distribution omits its upstream map. Keep the runtime bytes untouched on disk.
      return (await readFile(rapier, 'utf8')).replace(/\r?\n\/\/# sourceMappingURL=rapier\.mjs\.map\s*$/, '');
    },
  }],
  // the online city's room server (npm run room) answers on the page's own origin at /room
  server: { host: '0.0.0.0', port: 5406, strictPort: true, allowedHosts: true, proxy: { '/room': { target: 'ws://127.0.0.1:5407', ws: true } } },
  resolve: { alias: [{ find: /^patch-block$/, replacement: `${here}lib/patch-block.js` }] },
  build: {
    target: 'es2022',
    outDir: `${here}dist`,
    emptyOutDir: true,
    assetsInlineLimit: 0,
    modulePreload: { polyfill: false },
    chunkSizeWarningLimit: 8000,
    // three's notice, and any other library's, into the release
    license: { fileName: 'THIRD-PARTY-NOTICES.md' },
  },
};
