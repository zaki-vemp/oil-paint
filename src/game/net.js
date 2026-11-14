// The online city (?mode=online): up to eight wanderers in one street, through server/room.mjs. With no room in the
// address the server quick-matches the page into an open room with strangers; ?room=… is a private room by invite.
// Each page sends its own wanderer ten times a second and draws the others as mirror-rival bodies with a name over
// them. The street crews are shared: the room's host (the server picks it) runs their AI and sends their state; the
// rest show them as puppets and send their blows to the host, so a death is one death for everybody. Strikes and
// gunner shots are broadcast as events and every page tests them against its own wanderer, so a dodge is still yours.
// On top of that, the room's activities:
//   contract  break one district's crew together; the host counts, the whole room is paid
//   revive    a downed wanderer is lifted by a teammate standing beside him for REVIVE seconds
//   mech      every few minutes the host calls a siege mech into a district; everyone who hit it is paid
//   race      drive through the start ring, then the gates along the way; times go to the whole room
//   duel      inside the ring blades hit other wanderers; outside it nobody can hurt anybody
//   cars      a car someone else drives is moved by their page, so you see them on the road
//   titles    each of those earns titles and name colours, kept in this browser
import * as THREE from 'three';
import { createRivalAppearance } from '../figure/avatar.js';
import { createCombatPose, ATTACKS } from './combat.js';
import { inFront } from './hunt.js';
import { LEVELS } from './levels.js';

const SAVE = 'oilpaint.online.v1';
const SWINGS = ['cut', 'backhand', 'thrust', 'heavy', 'spin'];
const SEND = 0.1; // seconds between state packets
const REVIVE = 2; // seconds beside a downed teammate
const MECH = { first: 75, every: 180, pay: 200 };
// Indices into the way's samples (about 2.2 m apart). The ring sits by the spawn, 29 m from the nearest shrine (a
// shrine heals, and must not end a duel); the race starts past it so the course never crosses the ring.
const RACE = { start: 30, gates: [42, 54, 66, 78, 90, 102, 114, 126, 138, 150, 162, 174], r: 6.5, limit: 150 };
const RING = { at: 10, r: 9, dmg: { cut: 12, backhand: 12, thrust: 15, heavy: 30, spin: 18 } };
// [label, stat it needs, how many]; the first is free
export const TITLES = [['', null, 0], ['Street Breaker', 'contracts', 1], ['Mech Hunter', 'mechs', 1], ['Road Ghost', 'races', 1],
  ['Duelist', 'duels', 1], ['Medic', 'revives', 1], ['Night Runner', 'races', 5], ['Ring Champion', 'duels', 5]];
export const COLORS = [['#00f0ff', null, 0], ['#ff2a6d', 'contracts', 3], ['#fcee0a', 'mechs', 2], ['#9dff6a', 'races', 3],
  ['#c9a8ff', 'duels', 3], ['#ffffff', 'revives', 3], ['#ff9f43', 'contracts', 8], ['#5ef2e0', 'mechs', 5]];
const unlocked = (stats, [, stat, n]) => !stat || (stats[stat] ?? 0) >= n;
const r2 = (v) => Math.round(v * 100) / 100;

export function roomName(q) {
  return (q.get('room') || '').toLowerCase().replace(/[^a-z0-9-]/g, '').slice(0, 24);
}
function load() {
  try { return JSON.parse(localStorage.getItem(SAVE)) || {}; } catch { return {}; }
}
function serverUrl(q) {
  const asked = q.get('server') || import.meta.env?.VITE_ROOM_SERVER;
  if (asked && /^wss?:\/\//.test(asked)) return asked;
  return `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/room`;
}

/** `route` (the way's samples) turns on the street activities; `contract` the district contract. */
export function createNet({ scene, shared, man, camera, heightAt, route = null, pool = 'autumn', contract = false, q = new URLSearchParams(location.search) }) {
  const url = serverUrl(q);
  // a matched page keeps its room across a dropped connection, and asks for a new match if that room filled up
  let room = roomName(q);
  const matched = !room;
  const me = load();
  me.name ||= 'Wanderer-' + Math.floor(1000 + Math.random() * 9000);
  me.stats ||= {}; me.title ||= ''; me.color |= 0;
  const save = () => { try { localStorage.setItem(SAVE, JSON.stringify(me)); } catch { /* titles are only a convenience */ } };
  let ws = null, counting = false, id = 0, hostId = 0, tries = 0, sendT = 0, foeT = 0, hunt = null, rides = null, status = 'Connecting…';
  const peers = new Map(), foes = new Map();
  const mission = { seg: 0, kills: 0 };
  const deadSeen = new Set();
  const tags = document.createElement('div');
  tags.className = 'net-tags';
  document.body.appendChild(tags);

  const statusEl = () => document.getElementById('net-status');
  function tell(text) {
    status = text;
    const el = statusEl();
    if (el) el.textContent = room ? `${text} · ${matched ? 'quick match' : 'private'} room ${room}` : text;
    const pill = document.getElementById('net-pill');
    if (pill) pill.textContent = id ? `Online · ${peers.size + 1}` : text;
  }
  const count = () => peers.size ? `${peers.size + 1} players` : 'just you';
  const say = (m) => { if (ws?.readyState === 1) ws.send(JSON.stringify(m)); };
  const toast = (text) => hunt?.hud.toast(text);
  const dress = () => ({ name: me.name, title: me.title, color: me.color });

  function connect() {
    ws = new WebSocket(url);
    ws.onopen = () => { tries = 0; say({ t: 'hello', ...(room ? { room, open: matched } : { match: true }), pool, idle: document.hidden, ...dress() }); };
    ws.onmessage = (ev) => { try { receive(JSON.parse(ev.data)); } catch (error) { console.warn('net', error); } };
    ws.onclose = (ev) => {
      for (const p of [...peers.keys()]) drop(p);
      id = hostId = 0;
      if (ev.code === 4001 && !matched) return tell('Room is full — try Quick match');
      if (ev.code === 4001) room = '';
      // back off, but keep trying: a dropped phone or a restarted server should rejoin on its own
      tell('Reconnecting…');
      setTimeout(connect, Math.min(8000, 1000 * 2 ** tries++));
    };
  }

  // --- the others ------------------------------------------------------------------------
  function badge(p) {
    p.label.textContent = p.title ? `${p.name} · ${p.title}` : p.name;
    p.label.style.background = COLORS[p.color]?.[0] ?? COLORS[0][0];
  }
  function addPeer(t) {
    if (peers.has(t.id)) return;
    const label = document.createElement('span');
    tags.appendChild(label);
    const p = { id: t.id, name: t.name, title: t.title ?? '', color: t.color | 0, label, s: null, x: 0, y: 0, z: 0, yaw: 0, u: -1, type: 'cut', actor: null, combat: null, ride: null, rev: 0 };
    badge(p);
    peers.set(t.id, p);
    createRivalAppearance(man, shared).then((actor) => {
      if (!peers.has(t.id)) return;
      p.actor = actor;
      p.combat = createCombatPose(actor.man, shared, scene, { rival: true });
      actor.man.object.visible = false;
      scene.add(actor.man.object);
    });
  }
  function letGo(p) { if (p.ride) { p.ride.net = null; p.ride.driver = false; p.ride = null; } }
  function drop(pid) {
    const p = peers.get(pid);
    if (!p) return;
    p.label.remove();
    letGo(p);
    if (p.actor) { scene.remove(p.actor.man.object); p.combat.trail.visible = false; }
    peers.delete(pid);
  }
  /** The car a peer says they drive: their page moves it, ours only shows it (ride.js reads v.net). */
  function steer(p, r) {
    const v = Array.isArray(r) ? rides?.rides[r[0]] : null;
    if (p.ride && p.ride !== v) letGo(p);
    if (!v || v === rides.driven) return;
    v.net = { x: +r[1], z: +r[2], yaw: +r[3], vx: +r[4], vz: +r[5], at: performance.now() };
    v.driver = true; v.traffic = null; p.ride = v;
  }

  function receive(m) {
    if (m.t === 'welcome') {
      id = m.id; room = m.room;
      for (const p of m.peers) addPeer(p);
      tell(peers.size ? `Joined · ${count()}` : 'Online · waiting for others');
    } else if (m.t === 'host') { hostId = m.id; tell(hostId === id ? 'Online · you host the street' : `Online · ${count()}`); }
    else if (m.t === 'join') { addPeer(m); toast(`${m.name} joined`); tell(`Online · ${count()}`); }
    else if (m.t === 'leave') { const p = peers.get(m.id); if (p) toast(`${p.name} left`); drop(m.id); tell(`Online · ${count()}`); }
    else if (m.t === 'name') { const p = peers.get(m.id); if (p) { Object.assign(p, { name: m.name, title: m.title ?? '', color: m.color | 0 }); badge(p); } }
    else if (m.t === 'me') {
      const p = peers.get(m.id);
      if (!p || !Array.isArray(m.s)) return;
      const first = !p.s;
      p.s = m.s;
      const [x, y, z, yaw, , , u, type] = m.s;
      if (first || Math.hypot(x - p.x, z - p.z) > 8) { p.x = x; p.y = y; p.z = z; p.yaw = yaw; }
      p.u = u; p.type = SWINGS[type] ?? 'cut';
      steer(p, m.s[9]);
    } else if (m.t === 'foes') {
      const f = m.f, at = performance.now();
      for (let i = 0; i < f.length; i += 7) foes.set(f[i], { x: f[i + 1], z: f[i + 2], yaw: f[i + 3], hp: f[i + 4], going: f[i + 5], windup: f[i + 6], at });
      if (m.m) { mission.seg = m.m[0]; mission.kills = m.m[1]; }
    } else if (m.t === 'ev' && m.k === 'done') pay(m.seg);
    else if (m.t === 'ev' && m.k === 'mech') announce(+m.x, +m.z);
    else if (m.t === 'hit' || m.t === 'ev') hunt?.remote(m);
    else if (m.t === 'pub') heard(m);
  }

  /** Another player's own event. */
  function heard(m) {
    const p = peers.get(m.id), who = p?.name ?? 'Someone';
    if (m.k === 'revive' && hunt?.revive()) toast(`${who} revived you`);
    else if (m.k === 'race') {
      const t = +m.time;
      if (t > 0) { best = Math.min(best, t); toast(`${who} ran the street race · ${t.toFixed(1)}s`); }
    } else if (m.k === 'pvp' && player && inRing(player.feet.x, player.feet.z) && !rides?.driven) {
      if (hunt.hurtMe(Math.max(0, Math.min(40, +m.n || 0)), { x: +m.x || p?.x, z: +m.z || p?.z }) && hunt.downed) {
        say({ t: 'pub', k: 'duel', by: m.id });
        toast(`${who} won the duel`);
      }
    } else if (m.k === 'duel') {
      if (m.by === id) { earn('duels'); toast(`You beat ${who} in the ring`); }
      else toast(`${peers.get(m.by)?.name ?? 'Someone'} beat ${who} in the ring`);
    }
  }

  // --- titles ------------------------------------------------------------------------------
  function earn(stat) {
    const before = [...TITLES, ...COLORS].filter((t) => unlocked(me.stats, t));
    me.stats[stat] = (me.stats[stat] ?? 0) + 1;
    save();
    for (const t of TITLES) if (!before.includes(t) && unlocked(me.stats, t)) toast(`Title unlocked · ${t[0]}`);
    for (const c of COLORS) if (!before.includes(c) && unlocked(me.stats, c)) toast('New name colour unlocked');
    menu();
  }
  /** The title and colour pickers in the pause menu; locked ones show what opens them. */
  function menu() {
    const fill = (el, list, label, chosen) => {
      if (!el) return;
      el.replaceChildren(...list.map(([text, stat, n], i) => {
        const o = document.createElement('option');
        const open = unlocked(me.stats, list[i]);
        o.value = i; o.disabled = !open; o.selected = i === chosen;
        o.textContent = label(text, i) + (open ? '' : ` · ${n} ${stat}`);
        return o;
      }));
    };
    fill(document.getElementById('net-title'), TITLES, (t) => t || 'No title', Math.max(0, TITLES.findIndex(([t]) => t === me.title)));
    fill(document.getElementById('net-color'), COLORS, (_, i) => ['Cyan', 'Pink', 'Yellow', 'Lime', 'Violet', 'White', 'Orange', 'Teal'][i], me.color);
  }
  function wear() { save(); say({ t: 'name', ...dress() }); }

  // --- the contract ------------------------------------------------------------------------
  function pay(seg) {
    hunt?.reward(150, `Contract complete · ${LEVELS[seg]?.name ?? 'the district'} crew broken`);
    earn('contracts');
  }
  /** The host counts the contract's district: every crew death there is a step, the whole crew pays the room. */
  function countMission() {
    // a new host has not seen the old deaths: they are not new kills
    if (!counting) { counting = true; for (const e of hunt.enemies) if (e.netId != null && e.dead) deadSeen.add(e); }
    const crew = hunt.enemies.filter((e) => e.netId != null && e.seg === mission.seg);
    for (const e of hunt.enemies) {
      if (e.netId == null) continue;
      if (e.dead && !deadSeen.has(e)) { deadSeen.add(e); if (e.seg === mission.seg) mission.kills++; }
      else if (!e.dead) deadSeen.delete(e);
    }
    if (crew.length && mission.kills >= crew.length) {
      say({ t: 'ev', k: 'done', seg: mission.seg });
      pay(mission.seg);
      mission.seg = (mission.seg + 1) % LEVELS.length; mission.kills = 0;
    }
  }

  // --- the street: mech, race gates, duel ring -----------------------------------------------
  const district = (x, z) => LEVELS.reduce((a, L) => Math.hypot(L.checkpoint.x - x, L.checkpoint.z - z) < Math.hypot(a.checkpoint.x - x, a.checkpoint.z - z) ? L : a).name;
  let mechT = MECH.first, mechUp = false, best = Infinity, race = null, ringTold = false, lastSwing = -1, player = null;
  const struck = new Set();
  function announce(x, z) {
    toast(`Siege mech in ${district(x, z)} · everyone who hits it is paid`);
  }
  /** The host picks a wanderer at random and calls the mech onto the way a little ahead of them. */
  function callMech() {
    const who = [player.feet, ...[...peers.values()].filter((p) => p.s)];
    const at = who[Math.floor(Math.random() * who.length)];
    let i = 0;
    for (let k = 0; k < route.length; k++) if (Math.hypot(route[k][0] - at.x, route[k][1] - at.z) < Math.hypot(route[i][0] - at.x, route[i][1] - at.z)) i = k;
    const [x, z] = route[Math.min(route.length - 1, i + 10)];
    hunt.summon(x, z);
    say({ t: 'ev', k: 'mech', x: r2(x), z: r2(z) });
    announce(x, z);
  }
  const marks = [];
  const ringAt = route ? route[RING.at] : null;
  const inRing = (x, z) => !!ringAt && Math.hypot(x - ringAt[0], z - ringAt[1]) < RING.r;
  if (route) {
    const gateGeo = new THREE.TorusGeometry(RACE.r - 1.5, 0.16, 6, 40);
    const make = (i, color) => {
      const [x, z] = route[i], [ax, az] = route[i - 1], [bx, bz] = route[i + 1];
      const m = new THREE.Mesh(gateGeo, new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.8, depthWrite: false }));
      m.position.set(x, heightAt(x, z) + RACE.r - 1.3, z); m.rotation.y = Math.atan2(bx - ax, bz - az);
      scene.add(m); return m;
    };
    marks.start = make(RACE.start, 0x9dff6a);
    marks.gates = RACE.gates.map((i) => { const m = make(i, 0xfcee0a); m.visible = false; return m; });
    const ring = new THREE.Mesh(new THREE.RingGeometry(RING.r - 0.35, RING.r, 64), new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.7, side: THREE.DoubleSide, depthWrite: false }));
    ring.rotation.x = -Math.PI / 2; ring.position.set(ringAt[0], heightAt(ringAt[0], ringAt[1]) + 0.06, ringAt[1]);
    scene.add(ring);
  }

  function street(dt, driving) {
    const fx = player.feet.x, fz = player.feet.z;
    // the mech: called by the host, paid on every page to whoever hit it in its last seconds
    const mech = hunt.mech;
    if (mech) {
      const up = !mech.dead && !mech.gone;
      if (mechUp && !up && performance.now() - (mech.mineT ?? -1e9) < 10000) { hunt.reward(MECH.pay, 'Siege mech down'); earn('mechs'); }
      mechUp = up;
      if (net.host && !up && mech.gone && (mechT -= dt) <= 0) { mechT = MECH.every; callMech(); }
    }
    // the race: a vehicle through the start ring, then each gate in turn
    const gate = (i) => route[i];
    if (!race && driving && Math.hypot(fx - gate(RACE.start)[0], fz - gate(RACE.start)[1]) < RACE.r) {
      race = { g: 0, t: 0, out: 0 };
      toast(`Street race · ${RACE.gates.length} gates${best < Infinity ? ` · room best ${best.toFixed(1)}s` : ''}`);
    }
    if (race) {
      race.t += dt;
      race.out = driving ? 0 : race.out + dt;
      const [gx, gz] = gate(RACE.gates[race.g]);
      if (Math.hypot(fx - gx, fz - gz) < RACE.r && driving) race.g++;
      if (race.g >= RACE.gates.length) {
        const t = r2(race.t);
        say({ t: 'pub', k: 'race', time: t });
        toast(`Finish · ${t.toFixed(1)}s${t < best ? ' · room best' : ''}`);
        best = Math.min(best, t); race = null; earn('races');
      } else if (race.out > 4 || race.t > RACE.limit) { toast('Race abandoned'); race = null; }
    }
    marks.start.visible = !race;
    marks.gates.forEach((m, i) => { m.visible = !!race && i === race.g; });
    // the ring: a blade that lands on another wanderer inside it is sent to their page
    const here = inRing(fx, fz);
    if (here && !ringTold) toast('Duel ring · blades hit other players in here');
    ringTold = here;
    if (hunt.swing < 0 || hunt.swing < lastSwing) struck.clear();
    lastSwing = hunt.swing;
    const type = hunt.combo, a = ATTACKS[type];
    if (!here || driving || hunt.downed || !a || hunt.swing < a.hit[0] || hunt.swing > a.hit[1]) return;
    for (const p of peers.values()) {
      if (!p.s || p.s[8] || p.s[5] || struck.has(p) || !inRing(p.x, p.z)) continue;
      if (!inFront(fx, fz, player.yaw, p.x, p.z, a.reach, type === 'spin' ? Math.PI : 1.3)) continue;
      struck.add(p);
      say({ t: 'pub', to: p.id, k: 'pvp', n: RING.dmg[type] ?? 12, x: r2(fx), z: r2(fz) });
    }
  }

  const v = new THREE.Vector3();
  function update(dt, { player: who, driving }) {
    if (!hunt) return;
    player = who;
    if ((sendT -= dt) <= 0 && id) {
      sendT = SEND;
      const car = rides?.driven, ride = car ? [rides.rides.indexOf(car), r2(car.x), r2(car.z), r2(car.yaw), r2(car.vx), r2(car.vz)] : 0;
      say({ t: 'me', s: [r2(player.feet.x), r2(player.feet.y), r2(player.feet.z), r2(player.yaw), r2(player.speed), driving ? 1 : 0, r2(hunt.swing), SWINGS.indexOf(hunt.combo), hunt.downed ? 1 : 0, ride] });
    }
    if (!net.host) counting = false;
    else {
      if (contract) countMission();
      if ((foeT -= dt) <= 0) {
        foeT = SEND;
        const f = [];
        // a body that is gone reads as dead, so no page brings it back before the host does
        for (const e of hunt.enemies) if (e.netId != null) f.push(e.netId, r2(e.x), r2(e.z), r2(e.yaw), e.gone ? 0 : Math.round(e.hp), r2(e.going ?? 0), r2(e.windup ?? 0));
        say({ t: 'foes', f, m: [mission.seg, mission.kills] });
      }
    }
    if (route) street(dt, driving);
    const w = innerWidth, h = innerHeight;
    for (const p of peers.values()) {
      if (!p.s) { p.label.hidden = true; continue; }
      const [x, y, z, yaw, speed, drive, , , down] = p.s;
      // a car that stopped being sent is let go: its page closed or dropped
      if (p.ride && performance.now() - p.ride.net.at > 1500) letGo(p);
      const k = Math.min(1, dt * 12);
      p.x += (x - p.x) * k; p.y += (y - p.y) * k; p.z += (z - p.z) * k;
      p.yaw += Math.atan2(Math.sin(yaw - p.yaw), Math.cos(yaw - p.yaw)) * k;
      if (p.u >= 0) p.u = Math.min(1, p.u + dt / 0.6); // between packets the swing keeps going
      const far = Math.hypot(p.x - player.feet.x, p.z - player.feet.z);
      if (p.actor) {
        const o = p.actor.man.object;
        o.visible = !drive && far < 120;
        p.combat.trail.visible = o.visible && !down;
        if (o.visible) {
          o.position.set(p.x, p.y, p.z); o.rotation.set(0, p.yaw, 0);
          p.actor.update(dt, down ? 0 : speed, !!down);
          p.combat.apply(dt, { u: down || p.u >= 1 ? -1 : p.u, type: p.type, hurt: 0 });
          p.actor.avatar.update();
        }
      }
      // standing by a downed teammate lifts them
      if (down && far < 2.4 && !driving && !hunt.downed) {
        if (p.rev === 0) hunt.hud.flashText(`Reviving ${p.name}`);
        if ((p.rev += dt) >= REVIVE) { say({ t: 'pub', to: p.id, k: 'revive' }); earn('revives'); toast(`You revived ${p.name}`); p.rev = -3; }
      } else if (p.rev > 0) p.rev = 0;
      else if (p.rev < 0) p.rev = Math.min(0, p.rev + dt);
      v.set(p.x, p.y + 2.15, p.z).project(camera);
      p.label.hidden = v.z > 1 || far > 140;
      if (!p.label.hidden) p.label.style.transform = `translate(-50%, -100%) translate(${(v.x + 1) * w / 2}px, ${(1 - v.y) * h / 2}px)`;
      p.label.classList.toggle('driving', !!drive);
      p.label.classList.toggle('down', !!down);
    }
  }

  const net = {
    peers, mission,
    get room() { return room; },
    get host() { return !!id && hostId === id; },
    get status() { return status; },
    get name() { return me.name; },
    get stats() { return { ...me.stats }; },
    attach(h, r = null) { hunt = h; rides = r; },
    send: say,
    /** The host's last word on a shared body, or undefined. */
    foe: (e) => foes.get(e.netId),
    /** The closest other wanderer on foot and standing, for the host's crews to chase. */
    near(x, z) {
      let best = null, bd = Infinity;
      for (const p of peers.values()) {
        if (!p.s || p.s[5] || p.s[8]) continue;
        const d = Math.hypot(p.x - x, p.z - z);
        if (d < bd) { bd = d; best = p; }
      }
      return best && { x: best.x, y: best.y, z: best.z, d: bd };
    },
    task(at) {
      if (!hunt) return '';
      const fx = at.feet.x, fz = at.feet.z, m = (x, z) => Math.round(Math.hypot(x - fx, z - fz));
      if (hunt.downed) return `Down · revive in reach · ${Math.ceil(hunt.downLeft)}s`;
      if (race) return `Race · gate ${race.g + 1}/${RACE.gates.length} · ${race.t.toFixed(1)}s${best < Infinity ? ` · best ${best.toFixed(1)}s` : ''}`;
      const mech = hunt.mech;
      if (mech && !mech.dead && !mech.gone) return `Siege mech · ${Math.round(100 * mech.hp / mech.maxHp)}% · ${m(mech.x, mech.z)} m`;
      const L = contract && LEVELS[mission.seg];
      if (!L) return route ? 'Roam with the room · race ring at the start' : 'Explore with the room';
      const total = hunt.enemies.filter((e) => e.netId != null && e.seg === mission.seg).length;
      const d = m(L.checkpoint.x, L.checkpoint.z);
      return `Contract · ${L.name} crew · ${Math.min(mission.kills, total)}/${total}${d > 30 ? ` · ${d} m` : ''}`;
    },
    rename(next) { me.name = String(next).trim().slice(0, 16) || me.name; wear(); },
    update,
  };
  document.addEventListener('visibilitychange', () => say({ t: 'idle', on: document.hidden }));
  const field = document.getElementById('net-name');
  if (field) { field.value = me.name; field.addEventListener('change', () => { net.rename(field.value); field.value = me.name; }); }
  document.getElementById('net-title')?.addEventListener('change', (e) => { me.title = TITLES[+e.target.value]?.[0] ?? ''; wear(); });
  document.getElementById('net-color')?.addEventListener('change', (e) => { me.color = +e.target.value | 0; wear(); });
  menu();
  save();
  connect();
  tell(status);
  return net;
}

/** Titles open in order of their stat, and the first of each list is always worn. */
export function demoNet() {
  const ok = (c, msg) => { if (!c) throw new Error(msg); };
  ok(unlocked({}, TITLES[0]) && unlocked({}, COLORS[0]), 'the plain title and cyan are free');
  ok(!unlocked({}, TITLES[2]) && unlocked({ mechs: 1 }, TITLES[2]), 'a mech kill opens Mech Hunter');
  ok(COLORS.length === 8, 'the server keeps colour as 0..7');
  ok(RACE.gates.every((g, i) => g > RACE.start && (i === 0 || g > RACE.gates[i - 1])), 'race gates run forward along the way');
  console.log('net.js: ok');
}
if (globalThis.process?.argv?.[1]?.endsWith('net.js')) demoNet();
