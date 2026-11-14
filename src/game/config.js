// Every number the street's consequences use. Tune here; hunt.js only applies them.
// A sedan is mass 1. Ram force is speed (m/s) times that. Crews are not banded.

export const GUILT = 15; // health a bruise costs; a death costs twice this
export const CASH = { thief: 25, enemy: 40 };

export const MOB = { radius: 34, time: 30 }; // who turns, and for how long they stay angry
export const WITNESS = { radius: MOB.radius, time: MOB.time, close: 12, pull: 2.4 };

// below stumble they flinch, below kill they are thrown and live, past that the bumper is fatal
export const RAM = { per: 5, max: 130, crew: 42, min: 4, stumble: 7, kill: 12 };

// Heat is 0..max. stars are the thresholds for 1, 2 and 3. Police arrive at 2, a roadblock at 3.
// One death is heat.kill, which is under the police star. Decay is per second, and only while unseen.
export const HEAT = {
  hurt: 8, kill: 22, carjack: 14, max: 100,
  stars: [12, 40, 70],
  decay: 3.5, safe: 10,
  cops: 2,
};

export function demoConfig() {
  if (!(GUILT > 0 && GUILT * 2 < 100)) throw new Error('guilt hurts, and one death is not the end');
  if (!(WITNESS.radius >= 25 && WITNESS.radius <= 40 && WITNESS.close < WITNESS.radius)) throw new Error('witnesses are a block, not the whole city');
  if (!(RAM.min < RAM.stumble && RAM.stumble < RAM.kill && RAM.crew === 42)) throw new Error('ram bands, and a punk still survives one hit');
  if (!(HEAT.hurt < HEAT.stars[0] && HEAT.kill >= HEAT.stars[0] && HEAT.kill < HEAT.stars[1] && HEAT.stars[1] < HEAT.stars[2] && HEAT.stars[2] < HEAT.max)) throw new Error('one death is wanted, not yet the police');
  if (!(HEAT.decay > 0 && HEAT.safe > HEAT.decay && CASH.thief > 0 && CASH.enemy > 0)) throw new Error('heat cools, and a thief pays');
}

if (globalThis.process?.argv?.[1]?.endsWith('config.js')) demoConfig();
