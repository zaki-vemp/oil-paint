// The block demo's build: index.html at the top of dist, every path relative, nothing from other hosts.
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));

export default {
  root: here,
  base: './',
  publicDir: 'public',
  server: { host: '0.0.0.0', port: 5406, strictPort: true, allowedHosts: true },
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
