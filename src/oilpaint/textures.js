// Procedural tileable PBR micro-detail textures and botanical foliage cards.
// Generated once at startup (<40 ms) with zero external downloads so every surface
// (wet asphalt, concrete, brushed metal, carbon/cloth weave, skin pores, alpine rock,
// snow crystals, broadleaf clusters and conifer fronds) carries real high-frequency relief.
import * as THREE from 'three';

function makeRng(seed) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

function tileFbm(size, octaves, seed) {
  const rnd = makeRng(seed);
  const out = new Float32Array(size * size);
  for (const [cells, amp] of octaves) {
    const lat = new Float32Array(cells * cells);
    for (let i = 0; i < lat.length; i++) lat[i] = rnd();
    for (let y = 0; y < size; y++) {
      const fy = (y / size) * cells;
      const y0 = Math.floor(fy), y1 = (y0 + 1) % cells;
      let ty = fy - y0; ty = ty * ty * (3 - 2 * ty);
      for (let x = 0; x < size; x++) {
        const fx = (x / size) * cells;
        const x0 = Math.floor(fx), x1 = (x0 + 1) % cells;
        let tx = fx - x0; tx = tx * tx * (3 - 2 * tx);
        const a = lat[x0 + y0 * cells], b = lat[x1 + y0 * cells];
        const c = lat[x0 + y1 * cells], d = lat[x1 + y1 * cells];
        out[x + y * size] += amp * (a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty);
      }
    }
  }
  let lo = Infinity, hi = -Infinity;
  for (let i = 0; i < out.length; i++) { if (out[i] < lo) lo = out[i]; if (out[i] > hi) hi = out[i]; }
  const span = Math.max(1e-5, hi - lo);
  for (let i = 0; i < out.length; i++) out[i] = (out[i] - lo) / span;
  return out;
}

function tileVoronoi(size, cells, seed) {
  const rnd = makeRng(seed);
  const px = new Float32Array(cells * cells);
  const py = new Float32Array(cells * cells);
  for (let i = 0; i < cells * cells; i++) { px[i] = rnd(); py[i] = rnd(); }
  const dist = new Float32Array(size * size);
  const edge = new Float32Array(size * size);
  for (let y = 0; y < size; y++) {
    const gy = (y / size) * cells, cy = Math.floor(gy);
    for (let x = 0; x < size; x++) {
      const gx = (x / size) * cells, cx = Math.floor(gx);
      let d1 = 9, d2 = 9;
      for (let oy = -1; oy <= 1; oy++) {
        const ny = (cy + oy + cells) % cells;
        for (let ox = -1; ox <= 1; ox++) {
          const nx = (cx + ox + cells) % cells;
          const idx = nx + ny * cells;
          const dx = (cx + ox + px[idx]) - gx;
          const dy = (cy + oy + py[idx]) - gy;
          const d = Math.hypot(dx, dy);
          if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
        }
      }
      dist[x + y * size] = Math.min(1, d1);
      edge[x + y * size] = Math.min(1, (d2 - d1) * 2.2);
    }
  }
  return { dist, edge };
}

function makeTexture(data, size, { repeat = true, mipmaps = true } = {}) {
  const t = new THREE.DataTexture(data, size, size);
  t.wrapS = t.wrapT = repeat ? THREE.RepeatWrapping : THREE.ClampToEdgeWrapping;
  t.minFilter = mipmaps ? THREE.LinearMipmapLinearFilter : THREE.LinearFilter;
  t.magFilter = THREE.LinearFilter;
  t.generateMipmaps = mipmaps;
  t.anisotropy = 4;
  t.needsUpdate = true;
  return t;
}

/**
 * Packed organic/mineral micro-detail map:
 *   R, G = tangent-space normal XY (0.5 = flat) from multi-octave crushed aggregate + pebbles
 *   B    = mineral/asphalt/rock roughness & grain height (0..1)
 *   A    = Voronoi fissure / pebble-edge / sparkle mask (0..1)
 */
export function createDetailMap(size = 256) {
  const h1 = tileFbm(size, [[8, 0.45], [16, 0.30], [32, 0.28], [64, 0.22], [128, 0.14]], 101);
  const { dist, edge } = tileVoronoi(size, 18, 207);
  const height = new Float32Array(size * size);
  for (let i = 0; i < height.length; i++) {
    height[i] = h1[i] * 0.68 + (1 - dist[i]) * 0.22 + edge[i] * 0.10;
  }
  const data = new Uint8Array(size * size * 4);
  const strength = 5.2;
  for (let y = 0; y < size; y++) {
    const ym = (y - 1 + size) % size, yp = (y + 1) % size;
    for (let x = 0; x < size; x++) {
      const xm = (x - 1 + size) % size, xp = (x + 1) % size;
      const dx = (height[xp + y * size] - height[xm + y * size]) * strength;
      const dy = (height[x + yp * size] - height[x + ym * size]) * strength;
      const inv = 1 / Math.hypot(dx, dy, 1);
      const nx = -dx * inv, ny = -dy * inv;
      const i = x + y * size;
      data[i * 4] = Math.max(0, Math.min(255, Math.round((nx * 0.5 + 0.5) * 255)));
      data[i * 4 + 1] = Math.max(0, Math.min(255, Math.round((ny * 0.5 + 0.5) * 255)));
      data[i * 4 + 2] = Math.max(0, Math.min(255, Math.round(height[i] * 255)));
      data[i * 4 + 3] = Math.max(0, Math.min(255, Math.round(edge[i] * 255)));
    }
  }
  return makeTexture(data, size);
}

/**
 * Packed manufactured-surface micro-map:
 *   R, G = fine diagonal techwear / carbon-twill cloth normal XY
 *   B    = anisotropic brushed-metal streaks (horizontal grain)
 *   A    = fine leather / skin-pore micro-pebble height
 */
export function createSurfaceMap(size = 256) {
  const pores = tileFbm(size, [[16, 0.25], [32, 0.45], [64, 0.35], [128, 0.22]], 313);
  const brush = tileFbm(size, [[4, 0.2], [8, 0.25], [32, 0.35], [64, 0.45]], 419);
  const weaveH = new Float32Array(size * size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      // Twill weave (3/1 diagonal) + subtle yarn roughness
      const u = (x / size) * 48 * Math.PI * 2;
      const v = (y / size) * 48 * Math.PI * 2;
      const warp = Math.sin(u) * (0.6 + 0.4 * Math.sin(v * 0.5 + u * 0.5));
      const weft = Math.sin(v) * (0.6 - 0.4 * Math.sin(v * 0.5 + u * 0.5));
      weaveH[x + y * size] = 0.5 + 0.22 * warp + 0.22 * weft + (pores[x + y * size] - 0.5) * 0.14;
    }
  }
  const data = new Uint8Array(size * size * 4);
  const strength = 3.8;
  for (let y = 0; y < size; y++) {
    const ym = (y - 1 + size) % size, yp = (y + 1) % size;
    for (let x = 0; x < size; x++) {
      const xm = (x - 1 + size) % size, xp = (x + 1) % size;
      const dx = (weaveH[xp + y * size] - weaveH[xm + y * size]) * strength;
      const dy = (weaveH[x + yp * size] - weaveH[x + ym * size]) * strength;
      const inv = 1 / Math.hypot(dx, dy, 1);
      // Stretched horizontal brushed-metal lines
      const bx = (x >> 2) % size;
      const streak = brush[bx + y * size] * 0.65 + brush[((bx + 17) % size) + y * size] * 0.35;
      const i = x + y * size;
      data[i * 4] = Math.max(0, Math.min(255, Math.round((-dx * inv * 0.5 + 0.5) * 255)));
      data[i * 4 + 1] = Math.max(0, Math.min(255, Math.round((-dy * inv * 0.5 + 0.5) * 255)));
      data[i * 4 + 2] = Math.max(0, Math.min(255, Math.round(streak * 255)));
      data[i * 4 + 3] = Math.max(0, Math.min(255, Math.round(pores[i] * 255)));
    }
  }
  return makeTexture(data, size);
}

/**
 * Botanical foliage branch-cluster card atlas (two modes packed in one 256x256 texture? No — crisp dedicated cards):
 *   R = leaf variation / shade (veins & per-leaf tone)
 *   G = translucency thickness (brighter at tips/edges)
 *   B = twig / stem mask (brown woody twigs)
 *   A = crisp botanical alpha mask (broadleaf cluster in left/full or conifer frond when kind='pine')
 */
export function createFoliageCard(kind = 'broadleaf', size = 256) {
  const data = new Uint8Array(size * size * 4);
  const rnd = makeRng(kind === 'pine' ? 911 : 733);
  const alpha = new Float32Array(size * size);
  const shade = new Float32Array(size * size);
  const trans = new Float32Array(size * size);
  const woody = new Float32Array(size * size);

  function stampLeaf(cx, cy, ang, len, wid, tone) {
    const ca = Math.cos(ang), sa = Math.sin(ang);
    const pad = Math.ceil(Math.max(len, wid) * 1.15);
    const x0 = Math.max(0, Math.floor(cx - pad)), x1 = Math.min(size - 1, Math.ceil(cx + pad));
    const y0 = Math.max(0, Math.floor(cy - pad)), y1 = Math.min(size - 1, Math.ceil(cy + pad));
    for (let y = y0; y <= y1; y++) {
      for (let x = x0; x <= x1; x++) {
        const rx = (x - cx) * ca + (y - cy) * sa;
        const ry = -(x - cx) * sa + (y - cy) * ca;
        const u = rx / len; // -0.2..1.0 along leaf
        if (u < -0.06 || u > 1.04) continue;
        // Ovate-lanceolate botanical leaf profile with subtle serrated edge
        const t = Math.max(0, Math.min(1, u));
        const profile = Math.sin(Math.pow(t, 0.72) * Math.PI) * (0.92 + 0.08 * Math.sin(t * 28));
        const v = Math.abs(ry) / Math.max(1e-3, wid * profile);
        if (v > 1.05) continue;
        const cov = Math.max(0, Math.min(1, (1.02 - v) * 8));
        const vein = Math.exp(-Math.abs(ry) * 1.4) * 0.22 + Math.exp(-Math.abs(Math.sin((u * 9 - Math.abs(ry) * 0.4) * Math.PI)) * 4) * 0.08;
        const idx = x + y * size;
        if (cov > alpha[idx]) {
          alpha[idx] = cov;
          shade[idx] = Math.max(0, Math.min(1, tone * (0.82 + 0.24 * t - 0.14 * v + vein)));
          trans[idx] = Math.max(0, Math.min(1, 0.45 + 0.55 * t - vein));
          woody[idx] = 0;
        }
      }
    }
  }

  function stampTwig(x0, y0, x1, y1, r0, r1) {
    const dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    if (len2 < 1e-3) return;
    const minX = Math.max(0, Math.floor(Math.min(x0, x1) - r0 - 2));
    const maxX = Math.min(size - 1, Math.ceil(Math.max(x0, x1) + r0 + 2));
    const minY = Math.max(0, Math.floor(Math.min(y0, y1) - r0 - 2));
    const maxY = Math.min(size - 1, Math.ceil(Math.max(y0, y1) + r0 + 2));
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const t = Math.max(0, Math.min(1, ((x - x0) * dx + (y - y0) * dy) / len2));
        const d = Math.hypot(x - (x0 + dx * t), y - (y0 + dy * t));
        const r = r0 + (r1 - r0) * t;
        if (d <= r + 1) {
          const cov = Math.max(0, Math.min(1, (r + 0.6 - d) * 1.8));
          const idx = x + y * size;
          if (cov > alpha[idx]) {
            alpha[idx] = cov;
            shade[idx] = 0.35 + 0.15 * t;
            trans[idx] = 0.05;
            woody[idx] = 1;
          }
        }
      }
    }
  }

  const cx = size * 0.5, cy = size * 0.5;
  if (kind === 'pine') {
    // Radiating conifer branchlets with dense crisp needle sprays
    const mainBranches = [
      [cx, size * 0.94, cx, size * 0.08, 3.8, 1.2],
      [cx, size * 0.72, size * 0.16, size * 0.44, 2.8, 0.9],
      [cx, size * 0.72, size * 0.84, size * 0.44, 2.8, 0.9],
      [cx, size * 0.48, size * 0.22, size * 0.22, 2.2, 0.8],
      [cx, size * 0.48, size * 0.78, size * 0.22, 2.2, 0.8],
      [cx, size * 0.28, size * 0.34, size * 0.10, 1.6, 0.6],
      [cx, size * 0.28, size * 0.66, size * 0.10, 1.6, 0.6],
    ];
    for (const [x0, y0, x1, y1, r0, r1] of mainBranches) {
      stampTwig(x0, y0, x1, y1, r0, r1);
      const ang = Math.atan2(y1 - y0, x1 - x0);
      const steps = 26;
      for (let i = 1; i <= steps; i++) {
        const t = i / steps;
        const bx = x0 + (x1 - x0) * t, by = y0 + (y1 - y0) * t;
        const nLen = (18 + 18 * (1 - t * 0.55)) * (size / 256);
        for (const s of [-1, 1]) {
          for (let k = 0; k < 2; k++) {
            const na = ang + s * (0.45 + 0.42 * rnd()) + (rnd() - 0.5) * 0.2;
            stampLeaf(bx, by, na, nLen * (0.75 + 0.45 * rnd()), (2.4 + 1.1 * rnd()) * (size / 256), 0.48 + 0.42 * rnd());
          }
        }
      }
    }
  } else {
    // Organic radiating twig cluster bearing ~85 crisp overlapping botanical leaves
    const twigs = [
      [cx, size * 0.92, cx + 4, size * 0.16, 3.6, 1.0],
      [cx, size * 0.70, size * 0.18, size * 0.46, 2.6, 0.9],
      [cx, size * 0.68, size * 0.82, size * 0.44, 2.6, 0.9],
      [cx, size * 0.46, size * 0.22, size * 0.20, 2.0, 0.8],
      [cx, size * 0.45, size * 0.78, size * 0.22, 2.0, 0.8],
    ];
    for (const b of twigs) stampTwig(...b);
    for (let i = 0; i < 92; i++) {
      const b = twigs[i % twigs.length];
      const t = 0.12 + 0.88 * rnd();
      const bx = b[0] + (b[2] - b[0]) * t + (rnd() - 0.5) * size * 0.16;
      const by = b[1] + (b[3] - b[1]) * t + (rnd() - 0.5) * size * 0.16;
      if (Math.hypot(bx - cx, by - cy) > size * 0.43) continue;
      const outAng = Math.atan2(by - cy, bx - cx) + (rnd() - 0.5) * 1.4;
      const len = (20 + 18 * rnd()) * (size / 256);
      const wid = len * (0.38 + 0.16 * rnd());
      const radial = Math.hypot(bx - cx, by - cy) / (size * 0.48);
      const tone = Math.min(1, 0.42 + 0.42 * radial + 0.24 * rnd());
      stampLeaf(bx, by, outAng, len, wid, tone);
    }
  }

  // Flip rows so canvas-style y=size (twig base) lands at GL UV v=0 (bottom of PlaneGeometry) and y=0 (tip) at v=1
  for (let y = 0; y < size; y++) {
    const fy = size - 1 - y;
    for (let x = 0; x < size; x++) {
      const src = x + y * size;
      const dst = (x + fy * size) * 4;
      data[dst] = Math.round(shade[src] * 255);
      data[dst + 1] = Math.round(trans[src] * 255);
      data[dst + 2] = Math.round(woody[src] * 255);
      data[dst + 3] = Math.round(alpha[src] * 255);
    }
  }
  return makeTexture(data, size, { repeat: false, mipmaps: true });
}
