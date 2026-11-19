// Story bible, multi-character voiced radio comms, six act summaries, character dossiers,
// twelve collectible Memory Echo Shards, six sector field missions, and eight repeatable street
// contract kiosks for "Komorebi: Protocol Zero-Seven". Imported by missions.js and anime.js.
import { SCALE } from './chapters.js';
import { GRID_MIN, GRID_MAX } from '../world/grid.js';
import { SPEAKERS } from './audio.js';
import { LEVELS, GOAL } from './levels.js';
import { DUELS } from './duel.js';

export const COMMS = {
  intro: {
    speaker: 'sora',
    line: 'Courier Zero-Seven, ghost-link is live. Aethelgard sealed all six gates under the Eternity Grid—overload both dampener jammers in Sunleaf Row, clear the patrol, and face Ren’s first Specter.',
  },
  sector1: {
    speaker: 'sora',
    line: 'Petal Arcade ahead. An Aethelgard runner just lifted your transit cipher—intercept that marked courier on the strip, then upload at the Arcade Terminal.',
  },
  sector2: {
    speaker: 'sora',
    line: 'Jade Heights is drowning our channel in cortical static. Shatter all three Aethelgard Beacons along the avenue so the Seed can breathe.',
  },
  sector3: {
    speaker: 'sora',
    line: 'Moonveil Stacks is dropping its blast shutters! Grab a bike or coupe and carve through all three cyan Relay Rings before Aethelgard locks the block.',
  },
  sector4: {
    speaker: 'sora',
    line: 'Ashen Hollow—ground zero of the Silent Autumn. Ren is staging memory-purge canisters down this block; smash all three Supply Caches before they arm!',
  },
  sector5: {
    speaker: 'sora',
    line: 'Final stretch to Amber Sanctuary Spire. Overload both Shield Pylons guarding the plaza, then cross blades with Ren’s last mirror.',
  },
  field0: {
    speaker: 'sora',
    line: 'Both dampener jammers are slag! Sweep the remaining Sunleaf crew and confront Ren’s first Neural Specter at the gate.',
  },
  field1: {
    speaker: 'sora',
    line: 'Transit cipher synced to the ghost-line! Clear the Arcade enforcers—Ren’s second Specter is waiting in the neon rain.',
  },
  field2: {
    speaker: 'sora',
    line: 'All three Aethelgard Beacons are dark! Cut through the Heights garrison and challenge Ren’s twin-current projection.',
  },
  field3: {
    speaker: 'sora',
    line: 'Relay rings overcharged—the lockdown failed! Break the Stacks defenders and test your steel against Ren’s fourth Specter.',
  },
  field4: {
    speaker: 'sora',
    line: 'Purge caches wrecked! Finish the Hollow strike team—only Ren’s fifth Specter stands before the Sanctuary Spire.',
  },
  field5: {
    speaker: 'sora',
    line: 'Sanctuary Shield Pylons overloaded! Clear the sovereign guard and face Ren’s true mirror beneath the amber canopy.',
  },
  duel0: {
    speaker: 'ren',
    line: 'Still carrying contraband ghosts, Zero-Seven? Draw your blade. Show me if your wrist remembers winter in our father’s courtyard.',
  },
  duel1: {
    speaker: 'ren',
    line: 'You always rushed the line when Hana watched us spar. Speed without stillness is panic—sidestep the thrust, little brother.',
  },
  duel2: {
    speaker: 'ren',
    line: 'Neo-Kyoto has not wept once since the Eternity Grid went live. Tell me why you would drag six million sleepers back into grief.',
  },
  duel3: {
    speaker: 'ren',
    line: 'Pride snaps under weight; only iron resolve cracks a master’s guard. If you wake their sorrow, Kael, can your shoulders carry it?',
  },
  duel4: {
    speaker: 'ren',
    line: 'Here in Ashen Hollow I held Hana while the cascade took her voice. I built the Grid so no brother would ever kneel in that ash again.',
  },
  duel5: {
    speaker: 'ren',
    line: 'Every street, every scar, every autumn leaf led you to this mirror, Kael. Come—prove the human heart is strong enough to remember!',
  },
  clear0: {
    speaker: 'sora',
    line: 'Sector One gate open! Neural Spark unlocked—press Q to hurl a high-voltage arc bolt downrange.',
  },
  clear1: {
    speaker: 'sora',
    line: 'Petal Arcade liberated! Phase Dash online—press E to slip cleanly through incoming strikes.',
  },
  clear2: {
    speaker: 'sora',
    line: 'Jade Heights breached! Heavy Cleave unlocked—press F to shatter iron guards and launch foes skyward.',
  },
  clear3: {
    speaker: 'sora',
    line: 'Moonveil Stacks is free! Lantern Flare online—press R to detonate a pulse that staggers every hostile around you.',
  },
  clear4: {
    speaker: 'sora',
    line: 'Ashen Hollow stands clear! Whirl Slash unlocked—press C to carve a spinning tempest through the whole pack.',
  },
  clear5: {
    speaker: 'sora',
    line: 'Ren’s final mirror is broken! Step into the golden Amber Sanctuary Terminal ahead to bloom the Komorebi Seed across Neo-Kyoto!',
  },
  firstRide: {
    speaker: 'sora',
    line: 'Transponder spoofed—wheels are yours, Seven. W and S throttle, Space kicks the handbrake, and G steps out once you slow down.',
  },
  heat: {
    speaker: 'sora',
    line: 'Easy, Kael—sleepwalking citizens witnessed that! Break line of sight or step into a Mission Relay to scrub your Aethelgard heat.',
  },
  contractStart: {
    speaker: 'dispatch',
    line: 'Encrypted street contract engaged. Follow the green telemetry diamond to complete the run before Grid security locks your trace.',
  },
  contractDone: {
    speaker: 'sora',
    line: 'Clean run, Zero-Seven! Resistance credits wired and suit nanites restored.',
  },
  win: {
    speaker: 'sora',
    line: 'Komorebi Seed deployed! The Eternity Grid is dissolving across all six districts—Neo-Kyoto is awake and yours to roam, Kael!',
  },
};

// Sequenced multi-speaker radio banter queued ~4.8s per beat after the lead COMMS line.
export const COMMS_CHAINS = {
  intro: [
    {
      speaker: 'kael',
      line: 'Copy, Sora. Five years under Aethelgard’s dampener and nobody on this street even looks up at the rain. Let’s wake Sunleaf Row.',
      delay: 4.8,
    },
    {
      speaker: 'mira',
      line: 'Kael, my archive imprint is synced to your collar. Guard the Komorebi Seed—every stolen memory in Neo-Kyoto is breathing inside it.',
      delay: 4.8,
    },
  ],
  sector1: [
    {
      speaker: 'dispatch',
      line: 'Aethelgard Civic Notice: Unsanctioned memory variance detected in Petal Arcade. Citizens must remain calm and accept cortical smoothing.',
      delay: 4.8,
    },
    {
      speaker: 'kael',
      line: '“Cortical smoothing.” Hana used to buy paper lanterns down this strip, Sora. They turned her favorite market into a hall of sleepwalkers.',
      delay: 4.8,
    },
  ],
  sector2: [
    {
      speaker: 'mira',
      line: 'Those beacons broadcast my old trauma-relief harmonics, inverted to erase grief by force. Ren turned our healing research into a cage.',
      delay: 4.8,
    },
    {
      speaker: 'kael',
      line: 'After the Silent Autumn, Ren swore nobody would ever ache like we did. I never imagined he’d hollow out a whole city to keep that promise.',
      delay: 4.8,
    },
  ],
  sector3: [
    {
      speaker: 'dispatch',
      line: 'Security Alert: Rogue Courier Zero-Seven sighted in Moonveil Stacks. Siege frames authorized—defend the Eternity Grid at all costs.',
      delay: 4.8,
    },
    {
      speaker: 'kael',
      line: 'Send all the heavy iron you want. Every ring we ignite puts real sky back over these tenements.',
      delay: 4.8,
    },
  ],
  sector4: [
    {
      speaker: 'kael',
      line: 'Our childhood ward... I can still hear the sirens dying in the ash. If Ren fires those purge caches, no one in Kyoto ever remembers home.',
      delay: 4.8,
    },
    {
      speaker: 'mira',
      line: 'Steady your pulse, Kael. Hana’s last cello recording is safe inside the Seed—do not let Aethelgard erase this district twice.',
      delay: 4.8,
    },
  ],
  sector5: [
    {
      speaker: 'dispatch',
      line: 'Code Zero-Seven at Amber Sanctuary! All elite blades converge—the Chief Architect’s spire must not fall!',
      delay: 4.8,
    },
    {
      speaker: 'kael',
      line: 'Keep the ghost-line open, Sora. One last gate, brother—then we let the sunlight back through the leaves.',
      delay: 4.8,
    },
  ],
  duel0: [
    {
      speaker: 'kael',
      line: 'Even as a neural echo, you still lecture my grip, Ren. Watch closely—I haven’t forgotten a single cut you taught me.',
      delay: 4.8,
    },
  ],
  duel1: [
    {
      speaker: 'kael',
      line: 'Don’t speak Hana’s name through an Aethelgard filter! She loved this city loud, messy, and alive—not sedated!',
      delay: 4.8,
    },
  ],
  duel2: [
    {
      speaker: 'kael',
      line: 'Silence isn’t mercy, Ren—it’s a grave with the neon left on! Pain is just the proof that we loved someone real!',
      delay: 4.8,
    },
  ],
  duel3: [
    {
      speaker: 'kael',
      line: 'We were supposed to carry the grief together! You locked yours in a tower and made six million strangers pay the price!',
      delay: 4.8,
    },
  ],
  duel4: [
    {
      speaker: 'kael',
      line: 'In erasing the ash, you erased her song too! I’m bringing Hana’s memory home—even if I have to cut through every shadow you left!',
      delay: 4.8,
    },
  ],
  duel5: [
    {
      speaker: 'kael',
      line: 'For Hana, for Mira, and for the brother who taught me to stand—Protocol Zero-Seven ends right here!',
      delay: 4.8,
    },
    {
      speaker: 'mira',
      line: 'His neural guard is tied to the Spire core! Read each combo, Kael—punish the cyan recovery opening to break his loop!',
      delay: 4.8,
    },
  ],
  clear0: [
    {
      speaker: 'mira',
      line: 'The Seed is resonating across Sunleaf Row... Citizens are pausing on the wet pavement, remembering the names of the autumn trees.',
      delay: 4.8,
    },
  ],
  clear1: [
    {
      speaker: 'mira',
      line: 'Music is returning to Petal Arcade... street vendors humming melodies they haven’t recalled in five winters.',
      delay: 4.8,
    },
    {
      speaker: 'dispatch',
      line: 'Warning: Emotional telemetry exceeding Aethelgard limits in Sectors One and Two. Escalating Jade Heights containment.',
      delay: 4.8,
    },
  ],
  clear2: [
    {
      speaker: 'mira',
      line: 'The Seed decrypted Ren’s private log here... He wept the night he compiled these Specters, Kael. Part of him prayed you would beat them.',
      delay: 4.8,
    },
  ],
  clear3: [
    {
      speaker: 'mira',
      line: 'Warm light is filling the Stacks... mothers recognizing their children’s voices through five years of fog.',
      delay: 4.8,
    },
    {
      speaker: 'dispatch',
      line: 'Critical alert: Eternity Grid integrity below forty percent. Lethal purge teams deployed to Ashen Hollow.',
      delay: 4.8,
    },
  ],
  clear4: [
    {
      speaker: 'mira',
      line: 'Hana’s cello phrase just echoed across the Hollow’s towers. Only the Amber Sanctuary Spire stands between Kyoto and dawn.',
      delay: 4.8,
    },
  ],
  clear5: [
    {
      speaker: 'ren',
      line: 'Your blade didn’t waver once, little brother... Step into the light. Remember her for both of us.',
      delay: 4.8,
    },
  ],
  win: [
    {
      speaker: 'mira',
      line: 'Six million unedited lives blooming like sunlight through autumn leaves... Thank you, Courier Zero-Seven. Hana’s song belongs to the city now.',
      delay: 4.8,
    },
    {
      speaker: 'kael',
      line: 'Hear the avenues waking up, Ren? Tears, laughter—all of it ours again. Keep the channel warm, Sora; the streets still need a courier.',
      delay: 4.8,
    },
  ],
};

// Six narrative acts for the Story Codex UI, pairing each district's uprising with Ren's sword memory.
export const STORY_ACTS = [
  {
    act: 1,
    seg: 0,
    district: 'Sunleaf Row',
    title: 'Act I · Sunlight Through Static',
    epigraph: '“Sunlight only has shape because the leaves stand in its way. Memory works the same.” — Dr. Mira, Archive Note 01',
    synopsis: 'Five years after the Silent Autumn neural cascade, former elite runner Kael—Courier 07—drops into Sunleaf Row carrying Dr. Mira’s contraband Komorebi Seed. Beneath Aethelgard’s Eternity Grid, citizens drift in painless amnesia until Kael shatters the sector’s dampener jammers and faces the first Neural Specter of his brother, Ren.',
    memoryTheme: 'The First Wooden Bokken — patience at the threshold, and the frosty dawn Ren taught Kael that a blade is drawn to shelter, never to rule.',
  },
  {
    act: 2,
    seg: 1,
    district: 'Petal Arcade',
    title: 'Act II · Paper Lanterns in Rain',
    epigraph: '“Take away the ache of missing someone, and you erase the proof they ever lived.” — Sora, Ghost-Line Broadcast',
    synopsis: 'Through the rain-slick markets of Petal Arcade, an Aethelgard Syndicate courier steals Kael’s transit cipher to quarantine the Seed. Hunting the thief past stalls where their sister Hana once hung origami cranes, Kael recovers the key and crosses steel with Ren’s second memory projection.',
    memoryTheme: 'Footwork on Wet Stone — stepping off the line of a committed thrust, practiced beside the Kamo canal while Hana kept time on the steps.',
  },
  {
    act: 3,
    seg: 2,
    district: 'Jade Heights',
    title: 'Act III · The Mercy Algorithm',
    epigraph: '“Grief is a hemorrhage of the mind. If a surgeon stops the bleeding, do you call him a tyrant?” — Ren, Directorate Address',
    synopsis: 'Ascending into Jade Heights, Kael topples three corporate Beacons flooding Neo-Kyoto with cortical white noise. Inside the Seed, Dr. Mira’s archive ghost reveals how Aethelgard twisted Ren’s trauma-relief breakthrough into permanent civic erasure—using Ren’s own sorrow over Hana as its template.',
    memoryTheme: 'Twin Currents — phasing cleanly through a two-beat storm before answering, forged the night the brothers swore to walk Hana safely home.',
  },
  {
    act: 4,
    seg: 3,
    district: 'Moonveil Stacks',
    title: 'Act IV · Sleepless Beneath Glass',
    epigraph: '“A city that forgets how to weep soon forgets why it sang.” — Old Kyoto Canal Mural',
    synopsis: 'Grid Dispatch declares Code Crimson across Moonveil Stacks, rolling out heavy siege mechs and dropping blast doors to trap Courier 07. Kael hotwires street machines to thread Sora’s high-voltage Relay Rings at full throttle, waking tenements full of families who still tend shrines they cannot explain.',
    memoryTheme: 'Iron Over Pride — breaking an unyielding guard through committed weight, from the tournament Ren secretly forfeited to save an injured Kael.',
  },
  {
    act: 5,
    seg: 4,
    district: 'Ashen Hollow',
    title: 'Act V · Embers of the Silent Autumn',
    epigraph: '“We called it the Silent Autumn because the sirens cut out before the leaves touched the street.” — Kael // Courier 07',
    synopsis: 'In the quarantined ruins of Ashen Hollow—ground zero of the cascade that claimed Hana—Aethelgard stages memory-purge munitions to wipe the city clean. Kael wrecks the Syndicate’s caches across his childhood ward and confronts Ren’s fifth Specter, where the Architect’s stoic mask cracks into raw survivor’s guilt.',
    memoryTheme: 'The Ember Spiral — turning encirclement into a single rising cut, drilled in the family courtyard the evening before the conservatory surge.',
  },
  {
    act: 6,
    seg: 5,
    district: 'Amber Sanctuary',
    title: 'Act VI · Protocol Zero-Seven',
    epigraph: '“Do not forgive me, Kael. Only remember her for both of us.” — Ren // Chief Architect',
    synopsis: 'At the golden spire of Amber Sanctuary, Kael overloads the final Shield Pylons and steps beneath the amber tree to face Ren’s complete Neural Mirror. Every sword lesson returns to its master in one decisive duel over Neo-Kyoto’s soul: painless oblivion, or the fierce, living light of Komorebi.',
    memoryTheme: 'The Unbroken Mirror — student and master standing equal as every lesson converges, letting real sunlight filter back through autumn leaves.',
  },
];

// Character dossiers for the Story Codex UI covering the five central figures of Protocol Zero-Seven.
export const CHARACTER_DOSSIERS = [
  {
    id: 'kael',
    name: 'Kael // Courier 07',
    role: 'Protagonist · Neural Runner & Bearer of the Komorebi Seed',
    affiliation: 'Free Couriers // Protocol Zero-Seven',
    color: '#fcee0a',
    quote: '“Pain isn’t a flaw in being human, Ren. It’s the receipt that we loved someone real.”',
    bio: 'Once Neo-Kyoto’s fastest neural courier and his brother Ren’s prized sword pupil, Kael abandoned the corporate towers after the Silent Autumn claimed their younger sister Hana. Refusing Aethelgard’s cortical dampeners, he carries Dr. Mira’s Komorebi Seed behind his collar—cutting south through six quarantined districts to give a sedated city back its heart.',
  },
  {
    id: 'ren',
    name: 'Ren // Chief Architect',
    role: 'Antagonist · Architect of the Eternity Grid & Master Bladesman',
    affiliation: 'Aethelgard Directorate',
    color: '#ff2a6d',
    quote: '“I watched grief hollow this city to ash. If I must be its jailer to end its agony, so be it.”',
    bio: 'Kael’s older brother and former kendo master. Broken after holding Hana during the final hours of the Silent Autumn cascade, Ren built the Eternity Grid for megacorp Aethelgard, convinced that excising sorrow is the highest mercy. Yet he keyed the six sector gates to Kael’s neural tag—leaving six Specters of his blade to test if Kael can bear what Ren could not.',
  },
  {
    id: 'sora',
    name: 'Sora // Resistance Netrunner',
    role: 'Tactical Handler · Ghost-Line Operator',
    affiliation: 'Kamo Underground // Free Kyoto',
    color: '#00f0ff',
    quote: '“Aethelgard owns the skyline, Seven. Tonight we remind them who owns the rain.”',
    bio: 'A sharp-witted signal breaker broadcasting from a shielded barge beneath the Kamo floodgates. After the Eternity Grid erased her mother’s memory of her late father—and of why she ever raised Sora to fight—Sora dedicated her life to cracking Aethelgard’s relays and guiding Courier 07 to the Sanctuary Spire.',
  },
  {
    id: 'mira',
    name: 'Dr. Mira // Archive Ghost',
    role: 'Creator of the Komorebi Seed · Preserved Neural Imprint',
    affiliation: 'Former Aethelgard Neuro-Ethics Lead',
    color: '#19ffc6',
    quote: '“Komorebi means sunlight filtering through autumn leaves—precious because every shadow moves.”',
    bio: 'Ren’s former partner on clinical trauma relief who rebelled the night Aethelgard weaponized their research into mass civic amnesia. Before Directorate squads raided her Jade Heights vault, Dr. Mira wove millions of unedited human memories, art, and a living echo of her own mind into the Komorebi Seed.',
  },
  {
    id: 'hana',
    name: 'Hana // Echo in the Amber',
    role: 'The Lost Sister · Conservatory Cellist of Old Kyoto',
    affiliation: 'Preserved within the Komorebi Seed',
    color: '#ffb000',
    quote: '“Stop glaring at your swords so hard, you two—come listen to the rain on the ginkgo leaves!”',
    bio: 'Kael and Ren’s seventeen-year-old sister, a gifted cellist whose conservatory rehearsal hall was ground zero of the Silent Autumn neural surge. Her death tore her brothers down opposite philosophies: Ren silenced an entire metropolis to stop the hurting, while Kael fights to let her final autumn melody ring across Neo-Kyoto.',
  },
];

// Twelve collectible Memory Echo Shards (2 per sector 0..5), placed on open avenues and park glades
// strictly inside each sector's z gate bounds and well clear of all checkpoints, duels, crews and mission props.
export const ECHO_SHARDS = [
  {
    id: 'echo-01',
    seg: 0,
    title: 'The Word “Komorebi”',
    speaker: 'mira',
    x: -33.4,
    z: 213.4,
    quote: 'Hana chose the archive’s name herself, Kael. Komorebi—sunlight through autumn leaves, alive only because the shadows move too.',
    lore: 'Recorded six months before the Silent Autumn in Dr. Mira’s Sunleaf conservatory. Seventeen-year-old Hana sketched golden ginkgo leaves on wet slate while her brothers sparred outside, arguing that a mind scrubbed of longing would be like a cello suite with every rest cut out.',
  },
  {
    id: 'echo-02',
    seg: 0,
    title: 'Two Wooden Blades',
    speaker: 'kael',
    x: 46.6,
    z: 186.6,
    quote: 'Ren carved a notch into my first bokken on this avenue. Told me a blade isn’t drawn to win—it’s drawn to stand between the storm and home.',
    lore: 'Before Aethelgard raised glass towers over Sunleaf Row, a cedar dojo overlooked the canal. Every frost-white morning, Ren drilled young Kael on patience against sweeping strikes while Hana warmed barley tea on the steps and kept rhythm with a pocket metronome.',
  },
  {
    id: 'echo-03',
    seg: 1,
    title: 'The Paper Crane Market',
    speaker: 'kael',
    x: -46.6,
    z: 133.4,
    quote: 'Hana strung a hundred crimson paper cranes above Petal Arcade before the storm. Five years on, people walk under the hooks and never look up.',
    lore: 'During Old Kyoto’s autumn festival, Petal Arcade shimmered with hand-dyed silk and street musicians. Once the Eternity Grid locked in, Aethelgard replaced live performers with 60-BPM ambient pulses calibrated to prevent nostalgic spikes in passing shoppers.',
  },
  {
    id: 'echo-04',
    seg: 1,
    title: 'Stranger at the Tea Table',
    speaker: 'sora',
    x: 68,
    z: 102,
    quote: 'The morning the Grid booted, my mom poured tea and politely asked whose coat I was wearing. It was my dad’s. Ten years of love—wiped clean.',
    lore: 'Sora’s encrypted personal log from Year One of the Eternity Grid. Because human love and human grief share the same neural pathways, Aethelgard’s dampener could not excise sorrow without severing the deepest bonds of recognition between parents, lovers, and friends.',
  },
  {
    id: 'echo-05',
    seg: 2,
    title: 'Aethelgard Charter 09',
    speaker: 'dispatch',
    x: -68,
    z: 54,
    quote: 'Directorate Memo 09: Unregulated civic bereavement lowers output by forty-two percent. Authorize Chief Architect Ren’s citywide dampener.',
    lore: 'Recovered from a shattered Jade Heights relay. While Ren earnestly believed he was saving Neo-Kyoto from post-cascade despair, Aethelgard’s board quietly appended sub-routines that suppressed labor unrest, defiant art, and historical memory in the same pass.',
  },
  {
    id: 'echo-06',
    seg: 2,
    title: 'Mira’s Midnight Mutiny',
    speaker: 'mira',
    x: 74,
    z: 72,
    quote: 'When I found Aethelgard’s permanent-erasure clause, I pulled the raw citizen backups before dawn. If I couldn’t escape the tower, the Seed would.',
    lore: 'Hours before corporate Enforcers breached Vault 4 in Jade Heights, Dr. Mira compressed 4.8 million unedited citizen memories—along with a neural ghost of her own consciousness—into the Komorebi Seed and passed it through an air-shaft drop to Courier Zero-Seven.',
  },
  {
    id: 'echo-07',
    seg: 3,
    title: 'Unlit Household Shrines',
    speaker: 'mira',
    x: -66,
    z: -12,
    quote: 'Look at the tenement windows in Moonveil Stacks, Kael. Families still polish little brass bells on their shelves without remembering whose name is on them.',
    lore: 'Field telemetry gathered by Mira’s archive node. Even beneath total cortical suppression, the body remembered what the Grid forbade: bakers setting aside a third roll, cyclists braking at empty corners, and sleepers waking with salt on their cheeks.',
  },
  {
    id: 'echo-08',
    seg: 3,
    title: 'The Forfeited Semifinal',
    speaker: 'ren',
    x: 46.6,
    z: -53.4,
    quote: 'Archive Fragment, Ren: Kael fought with cracked ribs and refused to yield. So I stepped out of the ring myself—someone had to save him from his pride.',
    lore: 'A private voice note recorded three autumns before the cascade. Even in youth, Ren’s tragedy was already written: a fierce, lonely compulsion to carry every wound for his family without ever asking whether they wanted to be shielded.',
  },
  {
    id: 'echo-09',
    seg: 4,
    title: 'October 14: The Silent Autumn',
    speaker: 'kael',
    x: -53.4,
    z: -133.4,
    quote: 'Twenty-two hundred hours, Ward Four. An experimental Aethelgard power spine surged into the conservatory’s neural acoustics... and the music stopped.',
    lore: 'Black-box emergency log buried beneath Ashen Hollow. Aethelgard blamed the Silent Autumn cascade on underground hackers, concealing telemetry that proved their own prototype grid overloaded the school auditorium where Hana was recording.',
  },
  {
    id: 'echo-10',
    seg: 4,
    title: 'Hana’s Unfinished Cello',
    speaker: 'mira',
    x: 74,
    z: -146,
    quote: 'I salvaged Hana’s last rehearsal track from the conservatory server, Kael. Right before the static hit, she laughed and said: tell my brothers I’m coming home.',
    lore: 'Stored at the luminous core of the Komorebi Seed. Rain patters on glass above a warm D-dorian cello phrase, followed by Hana’s unhurried laughter—the single recording Ren locked behind six gates because hearing her voice again would undo his armor.',
  },
  {
    id: 'echo-11',
    seg: 5,
    title: 'Six Mirrors in the Kernel',
    speaker: 'ren',
    x: -33.4,
    z: -213.4,
    quote: 'Archive Fragment, Ren: If my mercy is a lie, only one blade in Kyoto can cut through it. I leave six Specters on the trail to temper him.',
    lore: 'Hidden inside the Eternity Grid’s root kernel, invisible to Aethelgard’s directors. Before sealing himself inside the Amber Spire, Ren forged his six Neural Specters not as executioners, but as final dojo lessons to ensure Kael was strong enough to finish the run.',
  },
  {
    id: 'echo-12',
    seg: 5,
    title: 'Signature Zero-Seven',
    speaker: 'mira',
    x: 66.6,
    z: -253.4,
    quote: 'Protocol Zero-Seven wasn’t my backdoor alone, Kael—Ren left the Sanctuary socket unlocked from within. Deep down, your brother was waiting for you.',
    lore: 'The final cryptographic layer of the Komorebi Seed reveals two authors on Protocol Zero-Seven: Dr. Mira wrote the memory-bloom sequence, and Chief Architect Ren quietly whitelisted Courier 07’s neural signature at the heart of the Amber Sanctuary.',
  },
];

export const LORE_KEY = 'oilpaint.lore.v1';
const ECHO_IDS = new Set(ECHO_SHARDS.map(shard => shard.id));

/** Reads the city's archive independently of the campaign checkpoint. */
export function loadEchoIds() {
  try {
    const saved = JSON.parse(globalThis.localStorage?.getItem(LORE_KEY) ?? 'null');
    const ids = Array.isArray(saved) ? saved : saved?.unlockedIds;
    return new Set(Array.isArray(ids) ? ids.filter(id => ECHO_IDS.has(id)) : []);
  } catch {
    return new Set();
  }
}

// Six field missions, strictly inside their sector's gates at SCALE = 3.0.
export const SECTOR_MISSIONS = [
  {
    seg: 0,
    type: 'jammer',
    opTitle: 'OP 01 // Silent Autumn',
    stepLabel: 'Overload Dampener Jammers',
    tip: 'Move WASD · Click/J Slash · G Ride bike · Smash both glowing Dampener Jammers',
    targets: [
      { x: -7, z: 214, label: 'Jammer Alpha' },
      { x: 7, z: 184, label: 'Jammer Beta' },
    ],
  },
  {
    seg: 1,
    type: 'intercept',
    opTitle: 'OP 02 // Stolen Cipher',
    stepLabel: 'Catch Marked Courier & Sync Uplink',
    tip: 'Defeat the hooded Syndicate Courier (magenta diamond), then stand by the Terminal',
    targets: [
      { x: -6, z: 126, label: 'Marked Courier', role: 'courier' },
      { x: 8, z: 112, label: 'Arcade Uplink', role: 'terminal' },
    ],
  },
  {
    seg: 2,
    type: 'beacon',
    opTitle: 'OP 03 // Grief Dampeners',
    stepLabel: 'Shatter Aethelgard Beacons',
    tip: 'Use Q Spark or sword combos to shatter all 3 Aethelgard Beacons',
    targets: [
      { x: -10, z: 68, label: 'West Beacon' },
      { x: 26, z: 46, label: 'East Beacon' },
      { x: 14, z: 24, label: 'South Beacon' },
    ],
  },
  {
    seg: 3,
    type: 'rings',
    opTitle: 'OP 04 // Moonveil Express',
    stepLabel: 'Pass Through Relay Rings',
    tip: 'Ride a car or bike (G) or dash (E) through the 3 cyan Relay Rings',
    targets: [
      { x: 16, z: -18, label: 'Relay Ring 1' },
      { x: 4, z: -46, label: 'Relay Ring 2' },
      { x: -8, z: -72, label: 'Relay Ring 3' },
    ],
  },
  {
    seg: 4,
    type: 'cache',
    opTitle: 'OP 05 // Purge Caches',
    stepLabel: 'Destroy Purge Supply Caches',
    tip: 'F Heavy Strike or ram at speed to wreck the 3 Purge Supply Caches',
    targets: [
      { x: -14, z: -116, label: 'North Cache' },
      { x: -20, z: -142, label: 'Mid Cache' },
      { x: 12, z: -166, label: 'Gate Cache' },
    ],
  },
  {
    seg: 5,
    type: 'shield',
    opTitle: 'OP 06 // Protocol Zero-Seven',
    stepLabel: 'Overload Sanctuary Shield Pylons',
    tip: 'Destroy both Sanctuary Shield Pylons, then face Ren’s Mirror at the Spire',
    targets: [
      { x: 16, z: -222, label: 'West Shield Pylon' },
      { x: 30, z: -252, label: 'East Shield Pylon' },
    ],
  },
];

// Eight repeatable street contract terminals placed on avenue corners across the expanded city grid.
// Horizontal dirs keep side contracts inside their sector's z band so closed gates never block them.
export const KIOSK_SPOTS = [
  { id: 'k0', name: 'Sunleaf West Contract', x: -120 + 6.8, z: 200 + 6.8, kind: 'rings', dir: [-1, 0] },
  { id: 'k1', name: 'Downtown East Contract', x: 120 - 6.8, z: 200 + 6.8, kind: 'jammers', dir: [1, 0] },
  { id: 'k2', name: 'Arcade Plaza Contract', x: -120 + 6.8, z: 120 - 6.8, kind: 'uplink', dir: [-1, 0] },
  { id: 'k3', name: 'Heights Overlook Contract', x: 120 - 6.8, z: 40 + 6.8, kind: 'rings', dir: [1, 0] },
  { id: 'k4', name: 'Market Cross Contract', x: -120 + 6.8, z: -40 - 6.8, kind: 'jammers', dir: [-1, 0] },
  { id: 'k5', name: 'Docks Freight Contract', x: 120 - 6.8, z: -40 + 6.8, kind: 'uplink', dir: [1, 0] },
  { id: 'k6', name: 'Suburbs Relay Contract', x: -120 + 6.8, z: -120 - 6.8, kind: 'rings', dir: [-1, 0] },
  { id: 'k7', name: 'Sanctuary Outer Contract', x: 120 - 6.8, z: -240 - 6.8, kind: 'jammers', dir: [1, 0] },
];

export const EXTRA_KEEP = [
  ...SECTOR_MISSIONS.flatMap((m) => m.targets.map((t) => ({ x: t.x, z: t.z }))),
  ...KIOSK_SPOTS.map((k) => ({ x: k.x, z: k.z })),
  ...ECHO_SHARDS.map((s) => ({ x: s.x, z: s.z })),
];

export function demoStory() {
  if (SECTOR_MISSIONS.length !== 6) throw new Error('expected 6 sector missions');
  for (const [i, m] of SECTOR_MISSIONS.entries()) {
    if (m.seg !== i || !m.opTitle || !m.targets.length) throw new Error('bad sector mission ' + i);
  }
  for (const [key, entry] of Object.entries(COMMS)) {
    if (!SPEAKERS[entry.speaker] || !entry.line) throw new Error('bad comms entry ' + key);
  }
  const requiredChains = [
    'intro',
    'sector1', 'sector2', 'sector3', 'sector4', 'sector5',
    'duel0', 'duel1', 'duel2', 'duel3', 'duel4', 'duel5',
    'clear0', 'clear1', 'clear2', 'clear3', 'clear4', 'clear5',
    'win',
  ];
  for (const key of requiredChains) {
    const chain = COMMS_CHAINS[key];
    if (!Array.isArray(chain) || !chain.length) throw new Error('missing comms chain ' + key);
    for (const step of chain) {
      if (!SPEAKERS[step.speaker] || !step.line || !Number.isFinite(step.delay) || step.delay <= 0) {
        throw new Error('bad comms chain step in ' + key);
      }
    }
  }
  if (STORY_ACTS.length !== 6) throw new Error('expected 6 story acts');
  for (const [i, act] of STORY_ACTS.entries()) {
    if (act.seg !== i || !act.title || !act.epigraph || !act.synopsis || !act.memoryTheme) {
      throw new Error('bad story act ' + i);
    }
  }
  const expectedCast = ['kael', 'ren', 'sora', 'mira', 'hana'];
  if (CHARACTER_DOSSIERS.length !== expectedCast.length) throw new Error('expected 5 character dossiers');
  for (const id of expectedCast) {
    const doc = CHARACTER_DOSSIERS.find((c) => c.id === id);
    if (!doc || !doc.name || !doc.role || !doc.quote || !doc.bio) throw new Error('bad character dossier ' + id);
  }
  if (ECHO_SHARDS.length !== 12) throw new Error('expected 12 echo shards');
  const seenIds = new Set();
  const avoid = [
    GOAL,
    ...DUELS,
    ...LEVELS.flatMap((L) => [L.checkpoint, ...L.spots, ...(L.gate ? [L.gate] : [])]),
    ...SECTOR_MISSIONS.flatMap((m) => m.targets),
    ...KIOSK_SPOTS,
  ];
  for (let seg = 0; seg < 6; seg++) {
    const inSeg = ECHO_SHARDS.filter((s) => s.seg === seg);
    if (inSeg.length !== 2) throw new Error('expected 2 echo shards in sector ' + seg);
  }
  for (const s of ECHO_SHARDS) {
    if (!s.id || seenIds.has(s.id)) throw new Error('duplicate or missing shard id ' + s.id);
    seenIds.add(s.id);
    if (!SPEAKERS[s.speaker] || !s.title || !s.quote || !s.lore) throw new Error('bad echo shard fields ' + s.id);
    if (s.x <= GRID_MIN || s.x >= GRID_MAX || s.z <= GRID_MIN || s.z >= GRID_MAX) {
      throw new Error('echo shard outside grid ' + s.id);
    }
    const zMax = s.seg === 0 ? LEVELS[0].checkpoint.z : LEVELS[s.seg - 1].gate.z;
    const zMin = LEVELS[s.seg].gate ? LEVELS[s.seg].gate.z : GOAL.z;
    if (!(s.z < zMax - 4 && s.z > zMin + 4)) {
      throw new Error(`echo shard ${s.id} outside sector ${s.seg} z bounds (${zMin}..${zMax}): ${s.z}`);
    }
    for (const p of avoid) {
      if (Math.hypot(p.x - s.x, p.z - s.z) < 10) {
        throw new Error(`echo shard ${s.id} too close (<10m) to (${p.x}, ${p.z})`);
      }
    }
    if (!EXTRA_KEEP.some((p) => p.x === s.x && p.z === s.z)) {
      throw new Error('echo shard missing from EXTRA_KEEP: ' + s.id);
    }
  }
  for (const k of KIOSK_SPOTS) {
    if (k.x <= GRID_MIN || k.x >= GRID_MAX || k.z <= GRID_MIN || k.z >= GRID_MAX) throw new Error('kiosk outside grid ' + k.id);
  }
  if (SCALE < 2.8) throw new Error('expected expanded SCALE');
  console.log('story.js: ok');
}

if (globalThis.process?.argv?.[1]?.endsWith('story.js')) demoStory();
