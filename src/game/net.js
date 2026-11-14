// The online city (?mode=online): up to eight wanderers in one street, through server/room.mjs. With no room in the
// address the server quick-matches the page into an open room with strangers; ?room=… is a private room by invite.
// Each page sends its own wanderer ten times a second and draws the others as mirror-rival bodies with a name over
// them. The street crews are shared: the room's host (the server picks it) runs their AI and sends their state; the
// rest show them as puppets and send their blows to the host, so a death is one death for everybody. Strikes and
// gunner shots are broadcast as events and every page tests them against its own wanderer, so a dodge is still yours.
// The contract — break one district's crew together — is counted by the host and paid to the whole room.
import * as THREE from 'three';
import { createRivalAppearance } from '../figure/avatar.js';
import { createCombatPose } from './combat.js';
import { LEVELS } from './levels.js';

const SAVE = 'oilpaint.online.v1';
const SWINGS = ['cut', 'backhand', 'thrust', 'heavy', 'spin'];
const SEND = 0.1; // seconds between state packets
const r2 = (v) => Math.round(v * 100) / 100;

export function roomName(q) {
  return (q.get('room') || '').toLowerCase().replace(/[^a-z0-9-]/g, '').slice(0, 24);
}
export function savedName() {
  try { return JSON.parse(localStorage.getItem(SAVE))?.name || ''; } catch { return ''; }
}
function saveName(name) {
  try { localStorage.setItem(SAVE, JSON.stringify({ name })); } catch { /* the name is only a convenience */ }
}
function serverUrl(q) {
  const asked = q.get('server') || import.meta.env?.VITE_ROOM_SERVER;
  if (asked && /^wss?:\/\//.test(asked)) return asked;
  return `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/room`;
}

export function createNet({ scene, shared, man, camera, q = new URLSearchParams(location.search) }) {
  const url = serverUrl(q);
  // a matched page keeps its room across a dropped connection, and asks for a new match if that room filled up
  let room = roomName(q);
  const matched = !room;
  let name = savedName() || 'Wanderer-' + Math.floor(1000 + Math.random() * 9000);
  let ws = null, counting = false, id = 0, hostId = 0, tries = 0, sendT = 0, foeT = 0, hunt = null, status = 'Connecting…';
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

  function connect() {
    ws = new WebSocket(url);
    ws.onopen = () => { tries = 0; say(room ? { t: 'hello', room, open: matched, name, idle: document.hidden } : { t: 'hello', match: true, name, idle: document.hidden }); };
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

  function addPeer(pid, pname) {
    if (peers.has(pid)) return;
    const label = document.createElement('span');
    label.textContent = pname;
    tags.appendChild(label);
    const p = { id: pid, name: pname, label, s: null, x: 0, y: 0, z: 0, yaw: 0, speed: 0, u: -1, type: 'cut', actor: null, combat: null };
    peers.set(pid, p);
    createRivalAppearance(man, shared).then((actor) => {
      if (!peers.has(pid)) return;
      p.actor = actor;
      p.combat = createCombatPose(actor.man, shared, scene, { rival: true });
      actor.man.object.visible = false;
      scene.add(actor.man.object);
    });
  }
  function drop(pid) {
    const p = peers.get(pid);
    if (!p) return;
    p.label.remove();
    if (p.actor) { scene.remove(p.actor.man.object); p.combat.trail.visible = false; }
    peers.delete(pid);
  }

  function receive(m) {
    if (m.t === 'welcome') {
      id = m.id; room = m.room;
      for (const p of m.peers) addPeer(p.id, p.name);
      tell(peers.size ? `Joined · ${count()}` : 'Online · waiting for others');
    } else if (m.t === 'host') { hostId = m.id; tell(hostId === id ? 'Online · you host the street' : `Online · ${count()}`); }
    else if (m.t === 'join') { addPeer(m.id, m.name); hunt?.hud.toast(`${m.name} joined the city`); tell(`Online · ${count()}`); }
    else if (m.t === 'leave') { const p = peers.get(m.id); if (p) hunt?.hud.toast(`${p.name} left`); drop(m.id); tell(`Online · ${count()}`); }
    else if (m.t === 'name') { const p = peers.get(m.id); if (p) p.label.textContent = p.name = m.name; }
    else if (m.t === 'me') {
      const p = peers.get(m.id);
      if (!p) return;
      const first = !p.s;
      p.s = m.s;
      const [x, y, z, yaw, , , u, type] = m.s;
      if (first || Math.hypot(x - p.x, z - p.z) > 8) { p.x = x; p.y = y; p.z = z; p.yaw = yaw; }
      p.u = u; p.type = SWINGS[type] ?? 'cut';
    } else if (m.t === 'foes') {
      const f = m.f, at = performance.now();
      for (let i = 0; i < f.length; i += 7) foes.set(f[i], { x: f[i + 1], z: f[i + 2], yaw: f[i + 3], hp: f[i + 4], going: f[i + 5], windup: f[i + 6], at });
      if (m.m) { mission.seg = m.m[0]; mission.kills = m.m[1]; }
    } else if (m.t === 'ev' && m.k === 'done') pay(m.seg);
    else if (m.t === 'hit' || m.t === 'ev') hunt?.remote(m);
  }

  function pay(seg) {
    hunt?.reward(150, `Contract complete · ${LEVELS[seg]?.name ?? 'the district'} crew broken`);
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

  const v = new THREE.Vector3();
  function update(dt, { player, driving }) {
    if (!hunt) return;
    if ((sendT -= dt) <= 0 && id) {
      sendT = SEND;
      say({ t: 'me', s: [r2(player.feet.x), r2(player.feet.y), r2(player.feet.z), r2(player.yaw), r2(player.speed), driving ? 1 : 0, r2(hunt.swing), SWINGS.indexOf(hunt.combo)] });
    }
    if (!net.host) counting = false;
    else {
      countMission();
      if ((foeT -= dt) <= 0) {
        foeT = SEND;
        const f = [];
        for (const e of hunt.enemies) if (e.netId != null) f.push(e.netId, r2(e.x), r2(e.z), r2(e.yaw), Math.round(e.hp), r2(e.going ?? 0), r2(e.windup ?? 0));
        say({ t: 'foes', f, m: [mission.seg, mission.kills] });
      }
    }
    const w = innerWidth, h = innerHeight;
    for (const p of peers.values()) {
      if (!p.s) { p.label.hidden = true; continue; }
      const [x, y, z, yaw, speed, drive] = p.s;
      const k = Math.min(1, dt * 12);
      p.x += (x - p.x) * k; p.y += (y - p.y) * k; p.z += (z - p.z) * k;
      p.yaw += Math.atan2(Math.sin(yaw - p.yaw), Math.cos(yaw - p.yaw)) * k;
      if (p.u >= 0) p.u = Math.min(1, p.u + dt / 0.6); // between packets the swing keeps going
      if (p.actor) {
        const o = p.actor.man.object;
        o.visible = !drive && Math.hypot(p.x - player.feet.x, p.z - player.feet.z) < 120;
        p.combat.trail.visible = o.visible;
        if (o.visible) {
          o.position.set(p.x, p.y, p.z); o.rotation.set(0, p.yaw, 0);
          p.actor.update(dt, speed);
          p.combat.apply(dt, { u: p.u >= 1 ? -1 : p.u, type: p.type, hurt: 0 });
          p.actor.avatar.update();
        }
      }
      v.set(p.x, p.y + 2.15, p.z).project(camera);
      const far = Math.hypot(p.x - player.feet.x, p.z - player.feet.z);
      p.label.hidden = v.z > 1 || far > 140;
      if (!p.label.hidden) p.label.style.transform = `translate(-50%, -100%) translate(${(v.x + 1) * w / 2}px, ${(1 - v.y) * h / 2}px)`;
      p.label.classList.toggle('driving', !!drive);
    }
  }

  const net = {
    peers, mission,
    get room() { return room; },
    get host() { return !!id && hostId === id; },
    get status() { return status; },
    get name() { return name; },
    attach(h) { hunt = h; },
    send: say,
    /** The host's last word on a shared body, or undefined. */
    foe: (e) => foes.get(e.netId),
    /** The closest other wanderer on foot, for the host's crews to chase. */
    near(x, z) {
      let best = null, bd = Infinity;
      for (const p of peers.values()) {
        if (!p.s || p.s[5]) continue;
        const d = Math.hypot(p.x - x, p.z - z);
        if (d < bd) { bd = d; best = p; }
      }
      return best && { x: best.x, y: best.y, z: best.z, d: bd };
    },
    task(player) {
      const L = LEVELS[mission.seg];
      if (!L || !hunt) return '';
      const total = hunt.enemies.filter((e) => e.netId != null && e.seg === mission.seg).length;
      const d = Math.round(Math.hypot(L.checkpoint.x - player.feet.x, L.checkpoint.z - player.feet.z));
      return `Contract · ${L.name} crew · ${Math.min(mission.kills, total)}/${total}${d > 30 ? ` · ${d} m` : ''}`;
    },
    rename(next) {
      name = String(next).trim().slice(0, 16) || name;
      saveName(name);
      say({ t: 'name', name });
    },
    update,
  };
  document.addEventListener('visibilitychange', () => say({ t: 'idle', on: document.hidden }));
  const field = document.getElementById('net-name');
  if (field) { field.value = name; field.addEventListener('change', () => { net.rename(field.value); field.value = name; }); }
  saveName(name);
  connect();
  tell(status);
  return net;
}
