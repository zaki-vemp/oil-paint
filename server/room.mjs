// The online city's room server: `npm run room` (port 5407, or PORT). It owns who is in a room and which of them is
// the host. The host's page runs the crews' AI and sends their state; the server stamps every message with its
// sender, passes it on, and refuses foe state from anyone but the host. A page that hides tells the server it is
// idle, and the host moves to a player who is still looking, so a backgrounded tab never freezes the street.
// Quick match ({ match: true }, no room) puts a player in the fullest open room with space, or opens a new one; a
// named room is private, joined only by its link, and never matched into. Each scene (`pool`: autumn, aurora) has its
// own rooms. `pub` is any player's event (a revive, a race time, a duel blow): to one player with `to`, else to all.
//   ws://host:5407/room   { t: 'hello', room | match, pool, name, title, color } first,
//   then { t: 'me' | 'foes' | 'ev' | 'hit' | 'pub' | 'name' | 'idle' }
// ponytail: host-trusted, no rate limit. Move the crew AI here if strangers start cheating.
import { WebSocketServer } from 'ws';
import { randomUUID } from 'node:crypto';

export const MAX = 8;
const ROOM = /^[a-z0-9-]{1,24}$/;
const clean = (name, max = 16, empty = 'Wanderer') => String(name ?? '').replace(/[^\p{L}\p{N} _.-]/gu, '').trim().slice(0, max) || empty;
const tag = (c) => ({ id: c.id, name: c.name, title: c.title, color: c.color });

export function startRoomServer({ port = 5407, server } = {}) {
  const wss = new WebSocketServer(server ? { server, path: '/room', maxPayload: 16384 } : { port, maxPayload: 16384 });
  const rooms = new Map();
  let next = 1;
  const send = (c, m) => { if (c.ws.readyState === 1) c.ws.send(typeof m === 'string' ? m : JSON.stringify(m)); };
  const others = (room, from, m) => { const s = JSON.stringify(m); for (const c of room.members) if (c !== from) send(c, s); };
  /** The oldest player still looking; with nobody looking, the oldest at all. */
  function elect(room) {
    const host = room.members.find((c) => !c.idle) ?? room.members[0] ?? null;
    if (host === room.host) return;
    room.host = host;
    if (host) { const s = JSON.stringify({ t: 'host', id: host.id }); for (const c of room.members) send(c, s); }
  }

  wss.on('connection', (ws) => {
    const me = { ws, id: next++, name: 'Wanderer', title: '', color: 0, room: null, idle: false };
    const dress = (m) => { me.name = clean(m.name); me.title = clean(m.title, 24, ''); me.color = Math.max(0, Math.min(7, m.color | 0)); };
    ws.on('message', (data) => {
      let m;
      try { m = JSON.parse(data); } catch { return; }
      if (!m || typeof m !== 'object') return;
      const room = me.room;
      if (!room) {
        if (m.t !== 'hello') return ws.close(4000, 'bad hello');
        const pool = m.pool === 'aurora' ? 'aurora' : 'autumn';
        let name = m.room;
        if (m.match) {
          const open = [...rooms.values()].filter((r) => r.open && r.pool === pool && r.members.length < MAX).sort((a, b) => b.members.length - a.members.length)[0];
          name = open?.name ?? 'pub-' + randomUUID().slice(0, 6);
        }
        if (!ROOM.test(name)) return ws.close(4000, 'bad hello');
        // `open` also comes back with a matched player who reconnects, so a room emptied meanwhile stays matchable
        const key = pool + '/' + name;
        const r = rooms.get(key) ?? { key, name, pool, members: [], host: null, open: !!(m.match || m.open) };
        if (r.members.length >= MAX) return ws.close(4001, 'room full');
        rooms.set(key, r);
        dress(m); me.room = r; r.members.push(me);
        send(me, { t: 'welcome', id: me.id, room: r.name, peers: r.members.filter((c) => c !== me).map(tag) });
        others(r, me, { t: 'join', ...tag(me) });
        // a hidden host gives way to a newcomer who is looking
        const before = r.host;
        me.idle = !!m.idle;
        elect(r);
        if (r.host === before) send(me, { t: 'host', id: r.host.id });
        return;
      }
      if (m.t === 'me' && Array.isArray(m.s)) others(room, me, { t: 'me', id: me.id, s: m.s.slice(0, 10) });
      else if ((m.t === 'foes' || m.t === 'ev') && me === room.host) others(room, me, { ...m, id: me.id });
      else if (m.t === 'hit' && room.host && room.host !== me) send(room.host, { ...m, id: me.id });
      else if (m.t === 'pub') {
        const out = { ...m, id: me.id };
        if (m.to == null) others(room, me, out);
        else { const to = room.members.find((c) => c.id === m.to); if (to) send(to, out); }
      }
      else if (m.t === 'name') { dress(m); others(room, me, { t: 'name', ...tag(me) }); }
      else if (m.t === 'idle') { me.idle = !!m.on; elect(room); }
    });
    ws.on('close', () => {
      const room = me.room;
      if (!room) return;
      room.members = room.members.filter((c) => c !== me);
      if (!room.members.length) return void rooms.delete(room.key);
      others(room, me, { t: 'leave', id: me.id });
      if (room.host === me) { room.host = null; elect(room); }
    });
  });
  return wss;
}

/** Two players join, see each other, the host relays foes, a guest cannot, and the host moves when it hides or leaves. */
export async function demo() {
  const { WebSocket } = await import('ws');
  const wss = startRoomServer({ port: 0 });
  await new Promise((r) => wss.on('listening', r));
  const url = `ws://127.0.0.1:${wss.address().port}`;
  const join = (name, room = 'test', idle = false, pool = 'autumn') => new Promise((resolve) => {
    const ws = new WebSocket(url), got = [];
    ws.on('message', (d) => got.push(JSON.parse(d)));
    ws.on('open', () => ws.send(JSON.stringify(room ? { t: 'hello', room, name, idle, pool, title: 'Medic', color: 3 } : { t: 'hello', match: true, name, idle, pool })));
    resolve({ ws, got, say: (m) => ws.send(JSON.stringify(m)) });
  });
  const wait = () => new Promise((r) => setTimeout(r, 60));
  const ok = (c, msg) => { if (!c) throw new Error(msg); };
  const a = await join('Ann'); await wait();
  const b = await join('<b>Bo</b>'); await wait();
  const welcome = b.got.find((m) => m.t === 'welcome');
  ok(welcome && welcome.peers[0]?.name === 'Ann', 'a newcomer sees who is there');
  ok(a.got.some((m) => m.t === 'join' && m.name === 'bBob'), 'names are cleaned');
  const hostId = a.got.find((m) => m.t === 'host').id;
  ok(b.got.some((m) => m.t === 'host' && m.id === hostId), 'both agree on the first host');
  a.say({ t: 'foes', f: [1, 2] }); b.say({ t: 'foes', f: [9] }); b.say({ t: 'hit', i: 3, n: 10 }); b.say({ t: 'me', s: [1, 2, 3] });
  await wait();
  ok(b.got.some((m) => m.t === 'foes' && m.f[0] === 1), 'host foes reach the guest');
  ok(!a.got.some((m) => m.t === 'foes'), 'a guest cannot send foes');
  ok(a.got.some((m) => m.t === 'hit' && m.id !== hostId && m.n === 10), 'hits go to the host, stamped');
  ok(a.got.some((m) => m.t === 'me' && m.s[2] === 3), 'state is relayed');
  a.say({ t: 'idle', on: true }); await wait();
  ok(b.got.some((m) => m.t === 'host' && m.id !== hostId), 'a hidden host hands over');
  a.ws.close(); await wait();
  ok(b.got.some((m) => m.t === 'leave'), 'a leaver is announced');
  const hidden = await join('h', 'away', true); await wait();
  const seen = await join('s', 'away'); await wait();
  const sid = seen.got.find((m) => m.t === 'welcome').id;
  ok(hidden.got.some((m) => m.t === 'host' && m.id === sid) && seen.got.some((m) => m.t === 'host' && m.id === sid), 'a hidden host gives way to a newcomer who is looking');
  hidden.ws.close(); seen.ws.close();
  const crowd = [];
  for (let i = 0; i < MAX; i++) crowd.push(await join('p' + i, 'full'));
  await wait();
  const late = await join('late', 'full'); await wait();
  ok(late.ws.readyState >= 2, 'a full room turns the next one away');
  const roomOf = (c) => c.got.find((m) => m.t === 'welcome')?.room;
  const m1 = await join('m1', null); await wait();
  const m2 = await join('m2', null); await wait();
  ok(/^pub-/.test(roomOf(m1)) && roomOf(m1) === roomOf(m2), 'quick match puts strangers together');
  ok(roomOf(m1) !== 'test', 'quick match never lands in a private room');
  const more = [];
  for (let i = 2; i < MAX; i++) more.push(await join('m' + i, null));
  await wait();
  const spill = await join('spill', null); await wait();
  ok(roomOf(spill) && roomOf(spill) !== roomOf(m1), 'a full match room opens a new one');
  const snow = await join('snow', null, false, 'aurora'); await wait();
  ok(roomOf(snow) !== roomOf(spill) && roomOf(snow) !== roomOf(m1), 'each scene matches into its own rooms');
  const p1 = await join('p1', 'pubs'); await wait();
  const p2 = await join('p2', 'pubs'); await wait();
  const p3 = await join('p3', 'pubs'); await wait();
  ok(p2.got.find((m) => m.t === 'welcome').peers[0].title === 'Medic', 'titles travel with names');
  const id2 = p2.got.find((m) => m.t === 'welcome').id;
  p1.say({ t: 'pub', k: 'race', time: 41.2 }); p1.say({ t: 'pub', to: id2, k: 'revive' }); await wait();
  ok(p3.got.some((m) => m.k === 'race') && !p3.got.some((m) => m.k === 'revive'), 'a pub goes to all, or only to its `to`');
  ok(p2.got.some((m) => m.k === 'revive' && m.id), 'a targeted pub arrives, stamped');
  for (const c of [snow, p1, p2, p3]) c.ws.close();
  for (const c of [b, ...crowd, m1, m2, ...more, spill]) c.ws.close();
  wss.close();
  console.log('room.mjs: ok');
}

if (process.argv[1]?.endsWith('room.mjs')) {
  if (process.argv.includes('--check')) await demo();
  else { const port = +(process.env.PORT || 5407); startRoomServer({ port }); console.log(`room server on :${port}`); }
}
