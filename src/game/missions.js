// Interactive mission system for the night city: replaces passive checkpoints with six story-driven
// sector field operations (using the Blender street models) and eight repeatable street contract
// kiosks across the expanded 720m city grid. Outputs live checklist steps, contextual control tips,
// a 3D waypoint target and minimap markers every frame.
import * as THREE from 'three';
import { loadStreetAsset, streetClone } from '../street-assets.js';
import { SECTOR_MISSIONS, KIOSK_SPOTS, COMMS } from './story.js';
import { GOAL, loadProgress } from './levels.js';
import { AVENUES, STREETS, GRID_MIN, GRID_MAX } from '../world/grid.js';

const COLOR = {
  mission: 0xff2a6d,
  ring: 0x00f0ff,
  terminal: 0xfcee0a,
  kiosk: 0x42f59b,
  rival: 0xff5a4d,
  goal: 0xffb000,
};

function makeBeam(height, color, radius = 0.22) {
  const m = new THREE.Mesh(
    new THREE.CylinderGeometry(radius * 0.65, radius, height, 8, 1, true),
    new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.22, side: THREE.DoubleSide, depthWrite: false }),
  );
  m.position.y = height * 0.5;
  return m;
}

function makeGroundRing(r, color) {
  const m = new THREE.Mesh(
    new THREE.RingGeometry(r - 0.28, r, 40),
    new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.65, side: THREE.DoubleSide, depthWrite: false }),
  );
  m.rotation.x = -Math.PI / 2;
  return m;
}

export async function createMissions({ scene, shared, player, heightAt, levels = null, roam = false, audio = null, saved = null }) {
  const activeMode = Boolean(levels);
  const progress = !roam && activeMode ? (saved ?? loadProgress()) : null;
  const clearedSaved = progress?.cleared ?? 0;
  const doneSectors = new Set(progress?.missions ?? []);
  for (let i = 0; i < clearedSaved; i++) doneSectors.add(i);

  const assetNames = ['terminal', 'beacon', 'power-core', 'supply-crate', 'barricade'];
  const assets = activeMode
    ? Object.fromEntries(await Promise.all(assetNames.map(async (n) => [n, await loadStreetAsset(n)])))
    : {};

  const R = player.R;
  const world = player.world;
  const group = new THREE.Group();
  scene.add(group);

  const particles = [];
  const pGeo = new THREE.BoxGeometry(0.18, 0.18, 0.18);
  const pMat = new THREE.MeshBasicMaterial({ color: 0xfcee0a });
  function burst(x, y, z, colorHex = 0xfcee0a, count = 10) {
    for (let i = 0; i < count && particles.length < 48; i++) {
      const m = new THREE.Mesh(pGeo, pMat);
      if (colorHex !== 0xfcee0a) m.material = new THREE.MeshBasicMaterial({ color: colorHex });
      m.position.set(x, y + 0.8, z);
      group.add(m);
      particles.push({
        m,
        x, y: y + 0.8, z,
        vx: (Math.random() - 0.5) * 11,
        vy: 3 + Math.random() * 5,
        vz: (Math.random() - 0.5) * 11,
        life: 0.45,
      });
    }
  }

  function addCollider(x, y, z, hx = 0.65, hy = 0.9, hz = 0.65) {
    const body = world.createRigidBody(R.RigidBodyDesc.fixed().setTranslation(x, y + hy, z));
    const col = world.createCollider(R.ColliderDesc.cuboid(hx, hy, hz).setFriction(0), body);
    return { body, col };
  }

  function removeCollider(t) {
    if (t.body) {
      try { world.removeRigidBody(t.body); } catch { /* ignore */ }
      t.body = t.col = null;
    }
  }

  function buildProp(asset, x, z, color, ringR = 2.0, beamH = 14, withCollider = true) {
    const y = heightAt(x, z);
    const root = new THREE.Group();
    root.position.set(x, y, z);
    const model = streetClone(assets[asset], shared);
    model.traverse((o) => { if (o.isMesh) o.layers.disable(1); });
    root.add(model);
    const ring = makeGroundRing(ringR, color);
    ring.position.y = 0.06;
    root.add(ring);
    const beam = makeBeam(beamH, color, 0.18);
    root.add(beam);
    group.add(root);
    const phys = withCollider ? addCollider(x, y, z) : { body: null, col: null };
    return { root, model, ring, beam, x, y, z, ...phys };
  }

  function buildRingGate(x, z, yaw, color) {
    const y = heightAt(x, z);
    const root = new THREE.Group();
    root.position.set(x, y, z);
    root.rotation.y = yaw;
    const torus = new THREE.Mesh(
      new THREE.TorusGeometry(3.4, 0.22, 8, 32),
      new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.9, depthWrite: false }),
    );
    torus.position.y = 2.6;
    root.add(torus);
    const pad = makeGroundRing(4.2, color);
    pad.position.y = 0.06;
    root.add(pad);
    const beam = makeBeam(14, color, 0.16);
    root.add(beam);
    group.add(root);
    return { root, torus, ring: pad, beam, x, y, z };
  }

  // --- Build 6 Story Sector Field Missions -------------------------------------------------------
  const sectorOps = [];
  let courierEnemy = null;

  if (activeMode) {
    for (const spec of SECTOR_MISSIONS) {
      const done = roam || doneSectors.has(spec.seg);
      const targets = [];
      for (let i = 0; i < spec.targets.length; i++) {
        const pt = spec.targets[i];
        const nextPt = spec.targets[Math.min(spec.targets.length - 1, i + 1)];
        const prevPt = spec.targets[Math.max(0, i - 1)];
        const yaw = Math.atan2(nextPt.x - prevPt.x, nextPt.z - prevPt.z) || Math.PI;
        if (spec.type === 'rings') {
          const vis = buildRingGate(pt.x, pt.z, yaw, COLOR.ring);
          vis.root.visible = !done && i === 0;
          targets.push({ ...vis, kind: 'ring', label: pt.label, index: i, done, hp: 1, maxHp: 1, radius: 4.6 });
        } else if (spec.type === 'intercept' && pt.role === 'courier') {
          const halo = makeGroundRing(1.1, COLOR.mission);
          const beam = makeBeam(12, COLOR.mission, 0.14);
          const wrap = new THREE.Group();
          wrap.add(halo, beam);
          wrap.visible = false;
          group.add(wrap);
          targets.push({ root: wrap, ring: halo, beam, x: pt.x, y: heightAt(pt.x, pt.z), z: pt.z, kind: 'courier', label: pt.label, done, hp: 50, maxHp: 50 });
        } else if (spec.type === 'intercept' && pt.role === 'terminal') {
          const vis = buildProp('terminal', pt.x, pt.z, COLOR.terminal, 3.8, 15, true);
          vis.root.visible = !done;
          targets.push({ ...vis, kind: 'terminal', label: pt.label, done, progress: 0, hp: 1, maxHp: 1, radius: 4.2 });
        } else {
          const asset = spec.type === 'jammer' ? 'power-core' : spec.type === 'beacon' ? 'beacon' : spec.type === 'cache' ? 'supply-crate' : 'power-core';
          const vis = buildProp(asset, pt.x, pt.z, COLOR.mission, 2.2, 16, true);
          if (spec.type === 'cache') {
            const bar = streetClone(assets.barricade, shared);
            bar.position.set(1.2, 0, 0.8);
            bar.rotation.y = 0.5;
            vis.root.add(bar);
          } else if (spec.type === 'shield') {
            const top = streetClone(assets.beacon, shared);
            top.position.set(0, 1.35, 0);
            top.scale.setScalar(0.65);
            vis.root.add(top);
          }
          const maxHp = spec.type === 'shield' ? 80 : spec.type === 'cache' ? 65 : 55;
          vis.root.visible = !done;
          if (done) removeCollider(vis);
          targets.push({ ...vis, kind: 'destructible', label: pt.label, done, hp: done ? 0 : maxHp, maxHp, flash: 0, rammed: 0, radius: 1.5 });
        }
      }
      sectorOps.push({ ...spec, done, targets, announced: false });
    }
  }

  // --- Build 8 Repeatable Street Contract Kiosks ------------------------------------------------
  const kiosks = [];
  if (activeMode) {
    for (const k of KIOSK_SPOTS) {
      const vis = buildProp('terminal', k.x, k.z, COLOR.kiosk, 3.6, 12, true);
      kiosks.push({ ...k, ...vis, cool: 0 });
    }
  }

  // Active side-contract state (one repeatable contract at a time)
  let contract = null;
  let hunt = null;
  let lastTip = '';
  let lastOp = '';
  let syncSoundT = 0;
  let toldFirstField = new Set();

  function speak(commKey) {
    const entry = COMMS[commKey];
    if (!entry) return;
    audio?.speak(entry.line, entry.speaker);
    hunt?.hud?.comms?.(entry.speaker, entry.line);
  }

  function bind(h) {
    hunt = h;
    if (!activeMode || roam || doneSectors.has(1)) return;
    // Find or attach to the Marked Syndicate Courier in Sector 1
    const t = sectorOps[1]?.targets.find((x) => x.kind === 'courier');
    if (!t) return;
    const existing = h.enemies.find((e) => e.kind === 'thief' && e.thief && !e.dead && !e.gone && !e.mark);
    if (existing) {
      courierEnemy = existing;
      Object.assign(courierEnemy, {
        mark: true,
        pool: false,
        x: t.x,
        z: t.z,
        homeX: t.x,
        homeZ: t.z,
        tx: -12,
        tz: 136,
        walk: [[-12, 136], [12, 108]],
        leg: 0,
        roam: 3.4,
        wait: 0.4,
        hp: existing.maxHp,
      });
    }
  }

  function completeSector(seg) {
    doneSectors.add(seg);
    const op = sectorOps[seg];
    if (!op) return;
    op.done = true;
    for (const t of op.targets) {
      t.done = true;
      t.hp = 0;
      if (t.root) t.root.visible = false;
      removeCollider(t);
    }
    if (seg === 1 && courierEnemy && !courierEnemy.dead) {
      courierEnemy.dead = 2.5;
      courierEnemy.gone = true;
      courierEnemy.mesh.visible = false;
      if (courierEnemy.body) {
        try { world.removeRigidBody(courierEnemy.body); } catch { /* ignore */ }
        courierEnemy.body = courierEnemy.collider = null;
      }
    }
  }

  function startContract(kiosk) {
    if (contract || kiosk.cool > 0) return;
    const targets = [];
    const clampGrid = (v) => Math.max(GRID_MIN + 14, Math.min(GRID_MAX - 14, v));
    const snapRoad = (v, list) => list.reduce((b, c) => (Math.abs(c - v) < Math.abs(b - v) ? c : b));
    const ax = snapRoad(kiosk.x, AVENUES);
    const sz = snapRoad(kiosk.z, STREETS);
    const dx = kiosk.dir[0];
    const dz = kiosk.dir[1];

    if (kiosk.kind === 'rings') {
      for (let i = 1; i <= 4; i++) {
        const rx = clampGrid(ax + dx * i * 34);
        const rz = clampGrid(sz + dz * i * 34);
        const vis = buildRingGate(rx, rz, dx ? Math.PI / 2 : 0, COLOR.kiosk);
        vis.root.visible = i === 1;
        targets.push({ ...vis, kind: 'ring', label: `Sprint Ring ${i}/4`, index: i - 1, done: false, radius: 4.8 });
      }
    } else if (kiosk.kind === 'jammers') {
      for (let i = 1; i <= 3; i++) {
        const jx = clampGrid(ax + dx * i * 32 + (dz ? (i % 2 ? 5.8 : -5.8) : 0));
        const jz = clampGrid(sz + dz * i * 32 + (dx ? (i % 2 ? 5.8 : -5.8) : 0));
        const vis = buildProp('power-core', jx, jz, COLOR.kiosk, 2.2, 14, true);
        targets.push({ ...vis, kind: 'destructible', label: `Rogue Node ${i}/3`, done: false, hp: 50, maxHp: 50, flash: 0, rammed: 0, radius: 1.5 });
      }
    } else {
      for (let i = 1; i <= 2; i++) {
        const ux = clampGrid(ax + dx * i * 38 + (dz ? 6.4 : 0));
        const uz = clampGrid(sz + dz * i * 38 + (dx ? (i % 2 ? 6.4 : -6.4) : 0));
        const vis = buildProp('terminal', ux, uz, COLOR.kiosk, 3.6, 14, true);
        targets.push({ ...vis, kind: 'terminal', label: `Relay Sync ${i}/2`, done: false, progress: 0, radius: 4.2 });
      }
    }

    contract = {
      kiosk,
      kind: kiosk.kind,
      name: kiosk.name,
      timeLeft: 75,
      targets,
    };
    kiosk.cool = 25;
    audio?.play('missionStep');
    speak('contractStart');
    hunt?.hud?.toast(`Contract started · ${kiosk.name}`);
  }

  function finishContract(success) {
    if (!contract) return;
    for (const t of contract.targets) {
      group.remove(t.root);
      removeCollider(t);
    }
    const name = contract.name;
    contract = null;
    if (success) {
      audio?.play('missionDone');
      speak('contractDone');
      hunt?.reward?.(150, `Contract complete · ${name} (+$150)`);
      hunt?.heal?.(35);
    } else {
      hunt?.hud?.toast(`Contract expired · ${name}`);
    }
  }

  function activeDestructibles(seg) {
    const out = [];
    if (!roam && seg < sectorOps.length && !sectorOps[seg].done) {
      for (const t of sectorOps[seg].targets) if (!t.done && t.kind === 'destructible') out.push(t);
    }
    if (contract) {
      for (const t of contract.targets) if (!t.done && t.kind === 'destructible') out.push(t);
    }
    return out;
  }

  function damageTarget(t, dmg, source, seg) {
    if (t.done) return false;
    t.hp -= dmg;
    t.flash = 0.22;
    burst(t.x, t.y, t.z, source === 'ram' ? 0x00f0ff : 0xfcee0a, 6);
    if (t.hp <= 0) {
      t.done = true;
      t.hp = 0;
      t.root.visible = false;
      removeCollider(t);
      burst(t.x, t.y, t.z, COLOR.mission, 14);
      audio?.play('objBreak');
      hunt?.hud?.flashText('Target Down');
      if (seg != null) checkSectorCompletion(seg);
      else if (contract && contract.targets.every((x) => x.done)) finishContract(true);
      else audio?.play('missionStep');
    } else {
      audio?.play('objHit');
    }
    return true;
  }

  function checkSectorCompletion(seg) {
    const op = sectorOps[seg];
    if (!op || op.done) return;
    if (op.targets.every((t) => t.done)) {
      op.done = true;
      doneSectors.add(seg);
      audio?.play('missionDone');
      if (!toldFirstField.has(seg)) {
        toldFirstField.add(seg);
        speak('field' + seg);
      }
      hunt?.hud?.toast(`${op.opTitle} complete — clear crew & defeat Ren`);
    } else {
      audio?.play('missionStep');
    }
  }

  function hitAt(x, z, radius, damage, source = 'sword', struck = null, currentSeg = 0) {
    let hitAny = false;
    const targets = activeDestructibles(currentSeg);
    for (const t of targets) {
      if (struck && struck.has(t)) continue;
      if (source === 'ram' && t.rammed > 0) continue;
      if (Math.hypot(t.x - x, t.z - z) > radius + (t.radius ?? 1.4)) continue;
      if (struck) struck.add(t);
      if (source === 'ram') t.rammed = 0.4;
      const isContract = contract?.targets.includes(t);
      if (damageTarget(t, damage, source, isContract ? null : currentSeg)) hitAny = true;
    }
    return hitAny;
  }

  function hitBolt(bx, by, bz, radius, damage, currentSeg = 0) {
    const targets = activeDestructibles(currentSeg);
    for (const t of targets) {
      if (Math.hypot(t.x - bx, t.z - bz) < radius + (t.radius ?? 1.4) && Math.abs(by - (t.y + 1.0)) < 2.2) {
        const isContract = contract?.targets.includes(t);
        return damageTarget(t, damage, 'spark', isContract ? null : currentSeg);
      }
    }
    return false;
  }

  function update(dt, { seg = 0, cleared = 0, won = false, enemies = [], driven = null, heat = 0, boss = null } = {}) {
    const px = player.feet.x;
    const pz = player.feet.z;
    const timeNow = performance.now() * 0.001;

    // Particle debris updates
    for (let i = particles.length - 1; i >= 0; i--) {
      const p = particles[i];
      p.life -= dt;
      p.vy -= 18 * dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.z += p.vz * dt;
      p.m.position.set(p.x, p.y, p.z);
      p.m.scale.setScalar(Math.max(0.01, p.life * 2.2));
      if (p.life <= 0) {
        group.remove(p.m);
        particles.splice(i, 1);
      }
    }

    // Active story sector mission updates
    const currentSeg = Math.min(cleared, Math.max(0, sectorOps.length - 1));
    const op = !roam && activeMode && !won && cleared < sectorOps.length ? sectorOps[currentSeg] : null;
    for (const sop of sectorOps) {
      const showSec = sop === op && !sop.done;
      for (const t of sop.targets) {
        if (t.done) continue;
        const dist = Math.hypot(t.x - px, t.z - pz);
        if (t.kind === 'destructible' || t.kind === 'terminal') {
          t.root.visible = showSec || dist < 95;
          if (t.model) t.model.visible = dist < 72;
          if (t.ring) t.ring.visible = dist < 85;
          if (t.beam) t.beam.visible = showSec && dist < 170;
        }
      }
    }
    if (op && !op.done) {
      // Announce sector briefing when entering sector for first time
      if (!op.announced && (currentSeg === 0 || Math.abs(pz - levels[currentSeg].checkpoint.z) < 42)) {
        op.announced = true;
        if (currentSeg === 0) speak('intro');
        else speak('sector' + currentSeg);
      }

      if (op.type === 'intercept') {
        const cTarget = op.targets.find((t) => t.kind === 'courier');
        if (cTarget && !cTarget.done) {
          if (courierEnemy && !courierEnemy.dead && !courierEnemy.gone) {
            cTarget.x = courierEnemy.x;
            cTarget.z = courierEnemy.z;
            cTarget.y = heightAt(cTarget.x, cTarget.z);
            cTarget.root.visible = true;
            cTarget.root.position.set(cTarget.x, cTarget.y + 0.05, cTarget.z);
          } else {
            cTarget.done = true;
            cTarget.root.visible = false;
            audio?.play('missionStep');
            hunt?.hud?.toast('Cipher key recovered — sync at the Arcade Terminal');
          }
        }
        const term = op.targets.find((t) => t.kind === 'terminal');
        if (term && !term.done && (!cTarget || cTarget.done)) {
          const d = Math.hypot(px - term.x, pz - term.z);
          if (d < 4.5) {
            term.progress = Math.min(1, term.progress + dt / 2.0);
            if ((syncSoundT -= dt) <= 0) { syncSoundT = 0.28; audio?.play('syncTick', term.progress); }
            if (term.progress >= 1) {
              term.done = true;
              term.beam.material.color.setHex(COLOR.kiosk);
              term.ring.material.color.setHex(COLOR.kiosk);
              checkSectorCompletion(currentSeg);
            }
          } else {
            term.progress = Math.max(0, term.progress - dt * 0.4);
          }
        }
      } else if (op.type === 'rings') {
        const nextRing = op.targets.find((t) => !t.done);
        for (const t of op.targets) {
          t.root.visible = t === nextRing;
          if (t === nextRing) {
            t.torus.rotation.z += dt * 1.4;
            if (Math.hypot(px - t.x, pz - t.z) < t.radius) {
              t.done = true;
              t.root.visible = false;
              burst(t.x, t.y + 1.5, t.z, COLOR.ring, 12);
              hunt?.hud?.flashText(`Ring ${t.index + 1}/${op.targets.length}`);
              checkSectorCompletion(currentSeg);
            }
          }
        }
      }

      // Pulse destructibles & hit flashes
      for (const t of op.targets) {
        if (t.done) continue;
        t.rammed = Math.max(0, (t.rammed ?? 0) - dt);
        if (t.flash > 0) {
          t.flash = Math.max(0, t.flash - dt);
          const s = 1 + t.flash * 0.5;
          t.model?.scale.set(s, 1 - t.flash * 0.4, s);
        } else {
          t.model?.scale.setScalar(1);
        }
        if (t.ring) t.ring.scale.setScalar(1 + Math.sin(timeNow * 5) * 0.08);
      }
    }

    // Repeatable Street Contract Kiosks (distance-culled for frame budget)
    for (const k of kiosks) {
      k.cool = Math.max(0, k.cool - dt);
      const dist = Math.hypot(px - k.x, pz - k.z);
      k.root.visible = dist < 110;
      if (k.root.visible) {
        k.model.visible = dist < 72;
        k.ring.visible = dist < 85;
        k.ring.material.opacity = k.cool > 0 || contract ? 0.2 : 0.45 + 0.3 * Math.sin(timeNow * 4);
        k.beam.visible = k.cool <= 0 && !contract;
      }
      if (!contract && k.cool <= 0 && dist < 3.6) {
        startContract(k);
      }
    }

    if (contract) {
      contract.timeLeft -= dt;
      if (contract.timeLeft <= 0) {
        finishContract(false);
      } else if (contract.kind === 'rings') {
        const nextRing = contract.targets.find((t) => !t.done);
        for (const t of contract.targets) {
          t.root.visible = t === nextRing;
          if (t === nextRing) {
            t.torus.rotation.z += dt * 1.6;
            if (Math.hypot(px - t.x, pz - t.z) < t.radius) {
              t.done = true;
              t.root.visible = false;
              burst(t.x, t.y + 1.5, t.z, COLOR.kiosk, 10);
              if (contract.targets.every((x) => x.done)) finishContract(true);
              else audio?.play('missionStep');
            }
          }
        }
      } else if (contract.kind === 'uplink') {
        for (const t of contract.targets) {
          if (t.done) continue;
          if (Math.hypot(px - t.x, pz - t.z) < t.radius) {
            t.progress += dt / 1.8;
            if ((syncSoundT -= dt) <= 0) { syncSoundT = 0.26; audio?.play('syncTick', t.progress); }
            if (t.progress >= 1) {
              t.done = true;
              t.root.visible = false;
              removeCollider(t);
              if (contract.targets.every((x) => x.done)) finishContract(true);
              else audio?.play('missionStep');
            }
          }
        }
      } else {
        for (const t of contract.targets) {
          if (t.done) continue;
          t.rammed = Math.max(0, (t.rammed ?? 0) - dt);
          if (t.flash > 0) {
            t.flash = Math.max(0, t.flash - dt);
            t.model?.scale.setScalar(1 + t.flash * 0.4);
          } else t.model?.scale.setScalar(1);
        }
      }
    }

    // Build live guidance state for HUD (checklist, contextual tip, 3D waypoint, minimap markers)
    return buildState({ seg: currentSeg, cleared, won, enemies, driven, heat, boss, px, pz });
  }

  function buildState({ seg, cleared, won, enemies, driven, heat, boss, px, pz }) {
    const markers = [];
    for (const k of kiosks) {
      if (k.cool <= 0 && !contract) markers.push({ x: k.x, z: k.z, kind: 'kiosk', color: '#42f59b' });
    }

    if (contract) {
      const doneN = contract.targets.filter((t) => t.done).length;
      const next = contract.targets.find((t) => !t.done);
      for (const t of contract.targets) if (!t.done) markers.push({ x: t.x, z: t.z, kind: 'contract', color: '#42f59b' });
      const syncPct = next?.kind === 'terminal' && next.progress > 0 ? ` (${Math.round(next.progress * 100)}%)` : '';
      return {
        opTitle: `Contract // ${contract.name}`,
        steps: [
          { text: `${next?.label ?? 'Complete targets'} (${doneN}/${contract.targets.length})${syncPct}`, done: false, active: true },
          { text: `Timer: ${Math.max(0, Math.ceil(contract.timeLeft))}s`, done: false, active: false },
        ],
        tip: 'Follow the green diamond · Complete before timer expires for +$150',
        waypoint: next ? { x: next.x, y: next.y ?? heightAt(next.x, next.z), z: next.z, label: next.label + syncPct, color: '#42f59b' } : null,
        markers,
      };
    }

    if (!activeMode || roam || won) {
      const nearKiosk = kiosks
        .filter((k) => k.cool <= 0)
        .sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0];
      return {
        opTitle: won ? 'City Grid Unlocked' : 'Free Roam // All Systems',
        steps: [
          { text: 'Explore 196 blocks & ride any vehicle (G)', done: true, active: false },
          { text: nearKiosk ? `Visit ${nearKiosk.name} for +$150` : 'Kiosks recharging...', done: false, active: true },
        ],
        tip: driven ? 'W/S drive · A/D steer · Space drift · G step out' : 'Press G near any bike or car · Green diamonds mark $150 contracts',
        waypoint: nearKiosk ? { x: nearKiosk.x, y: nearKiosk.y, z: nearKiosk.z, label: 'Contract Kiosk', color: '#42f59b' } : null,
        markers,
      };
    }

    if (cleared >= sectorOps.length) {
      return {
        opTitle: 'Final Protocol // Sanctuary',
        steps: [
          { text: 'All 6 sectors cleared', done: true, active: false },
          { text: 'Enter Amber Sanctuary Terminal', done: false, active: true },
        ],
        tip: 'Step into the golden Amber Sanctuary ring ahead to finish the story',
        waypoint: { x: GOAL.x, y: heightAt(GOAL.x, GOAL.z), z: GOAL.z, label: 'Amber Sanctuary', color: '#ffb000' },
        markers: [...markers, { x: GOAL.x, z: GOAL.z, kind: 'goal', color: '#ffb000' }],
      };
    }

    const op = sectorOps[seg];
    const crew = enemies.filter((e) => !e.duel && !e.civ && e.seg === seg);
    const crewAlive = crew.filter((e) => !e.dead);
    const rival = enemies.find((e) => e.duel && e.seg === seg && !e.dead);
    const fieldDone = op.done;
    const crewDone = crewAlive.length === 0;
    const fieldDoneCount = op.targets.filter((t) => t.done).length;
    const activeFieldTargets = op.targets.filter((t) => !t.done);
    const nextField = op.type === 'intercept'
      ? (activeFieldTargets.find((t) => t.kind === 'courier') ?? activeFieldTargets[0])
      : activeFieldTargets.sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0];

    for (const t of activeFieldTargets) {
      markers.push({ x: t.x, z: t.z, kind: 'mission', color: t.kind === 'ring' ? '#00f0ff' : t.kind === 'terminal' ? '#fcee0a' : '#ff2a6d' });
    }
    if (fieldDone && crewDone && rival) {
      markers.push({ x: rival.x, z: rival.z, kind: 'rival', color: '#ff5a4d' });
    }

    let waypoint = null;
    if (nextField) {
      const syncPct = nextField.kind === 'terminal' && nextField.progress > 0 ? ` (${Math.round(nextField.progress * 100)}%)` : '';
      waypoint = {
        x: nextField.x,
        y: nextField.y ?? heightAt(nextField.x, nextField.z),
        z: nextField.z,
        label: nextField.label + syncPct,
        color: nextField.kind === 'ring' ? '#00f0ff' : nextField.kind === 'terminal' ? '#fcee0a' : '#ff2a6d',
      };
    } else if (!crewDone) {
      const nearestCrew = [...crewAlive].sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0];
      if (nearestCrew) {
        waypoint = { x: nearestCrew.x, y: heightAt(nearestCrew.x, nearestCrew.z), z: nearestCrew.z, label: 'Sector Crew', color: '#ff2a6d' };
      }
    } else if (rival) {
      waypoint = { x: rival.x, y: heightAt(rival.x, rival.z), z: rival.z, label: rival.name.split(',')[0], color: '#fcee0a' };
    }

    let tip = op.tip;
    if (boss) tip = 'Wait for the cyan Recovery glow after Ren swings, then strike!';
    else if (heat > 12) tip = 'Wanted! Break line of sight or step into a cyan Mission Relay to clear heat';
    else if (driven) tip = 'W/S throttle · A/D steer · Space handbrake · G step out';
    else if (fieldDone && crewDone && rival) tip = 'Face Ren’s Specter at the crimson beacon to open the Sector Gate!';
    else if (fieldDone && !crewDone) tip = 'Click/J Slash · Q Spark · Clear the remaining street crew';

    const termSync = nextField?.kind === 'terminal' && nextField.progress > 0 ? ` · ${Math.round(nextField.progress * 100)}%` : '';
    const steps = [
      { text: `${op.stepLabel} (${fieldDoneCount}/${op.targets.length})${termSync}`, done: fieldDone, active: !fieldDone },
      { text: `Defeat Sector Crew (${crew.length - crewAlive.length}/${crew.length})`, done: crewDone, active: fieldDone && !crewDone },
      { text: rival ? `Defeat ${rival.name.split(',')[0]}` : 'Specter defeated', done: !rival, active: fieldDone && crewDone && !!rival },
    ];

    return { opTitle: op.opTitle, steps, tip, waypoint, markers };
  }

  return {
    bind,
    update,
    hitAt,
    hitBolt,
    completeSector,
    speak,
    isSectorDone: (seg) => roam || doneSectors.has(seg),
    get doneSectors() { return [...doneSectors]; },
    get activeContract() { return contract; },
    get kiosks() { return kiosks; },
    get sectorOps() { return sectorOps; },
  };
}
