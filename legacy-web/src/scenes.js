// Which picture is shown. There are two, each a whole world of its own, so going from one to the other loads the
// page again with the other named in its address: ?scene=aurora (the snowy side story) or
// ?scene=autumn (the default adventure). The pause menu does it (src/boot.js). It is not a control on Patch's page: a control
// there has to take effect at once, and this can't.
const q = new URLSearchParams(location.search);
export const SCENE = q.get('scene') === 'aurora' ? 'aurora' : 'autumn';

/** Goes to the other picture, from its start. Asked for the one already showing, does nothing. */
export function showScene(scene) {
  if (scene === SCENE || (scene !== 'autumn' && scene !== 'aurora')) return;
  const next = new URLSearchParams(location.search);
  next.set('scene', scene);
  location.search = next.toString();
}
