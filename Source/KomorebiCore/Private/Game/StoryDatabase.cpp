// Copyright Komorebi Project. Complete story database for "Komorebi: Protocol Zero-Seven".
#include "Game/StoryDatabase.h"

namespace KomorebiCore
{
	const std::vector<CommsEntry>& GetComms()
	{
		static const std::vector<CommsEntry> Comms = {
			{
				"intro",
				"sora",
				"Courier Zero-Seven, ghost-link is live. Aethelgard sealed all six gates under the Eternity Grid—overload both dampener jammers in Sunleaf Row, clear the patrol, and face Ren’s first Specter.",
			},
			{
				"sector1",
				"sora",
				"Petal Arcade ahead. An Aethelgard runner just lifted your transit cipher—intercept that marked courier on the strip, then upload at the Arcade Terminal.",
			},
			{
				"sector2",
				"sora",
				"Jade Heights is drowning our channel in cortical static. Shatter all three Aethelgard Beacons along the avenue so the Seed can breathe.",
			},
			{
				"sector3",
				"sora",
				"Moonveil Stacks is dropping its blast shutters! Grab a bike or coupe and carve through all three cyan Relay Rings before Aethelgard locks the block.",
			},
			{
				"sector4",
				"sora",
				"Ashen Hollow—ground zero of the Silent Autumn. Ren is staging memory-purge canisters down this block; smash all three Supply Caches before they arm!",
			},
			{
				"sector5",
				"sora",
				"Final stretch to Amber Sanctuary Spire. Overload both Shield Pylons guarding the plaza, then cross blades with Ren’s last mirror.",
			},
			{
				"field0",
				"sora",
				"Both dampener jammers are slag! Sweep the remaining Sunleaf crew and confront Ren’s first Neural Specter at the gate.",
			},
			{
				"field1",
				"sora",
				"Transit cipher synced to the ghost-line! Clear the Arcade enforcers—Ren’s second Specter is waiting in the neon rain.",
			},
			{
				"field2",
				"sora",
				"All three Aethelgard Beacons are dark! Cut through the Heights garrison and challenge Ren’s twin-current projection.",
			},
			{
				"field3",
				"sora",
				"Relay rings overcharged—the lockdown failed! Break the Stacks defenders and test your steel against Ren’s fourth Specter.",
			},
			{
				"field4",
				"sora",
				"Purge caches wrecked! Finish the Hollow strike team—only Ren’s fifth Specter stands before the Sanctuary Spire.",
			},
			{
				"field5",
				"sora",
				"Sanctuary Shield Pylons overloaded! Clear the sovereign guard and face Ren’s true mirror beneath the amber canopy.",
			},
			{
				"duel0",
				"ren",
				"Still carrying contraband ghosts, Zero-Seven? Draw your blade. Show me if your wrist remembers winter in our father’s courtyard.",
			},
			{
				"duel1",
				"ren",
				"You always rushed the line when Hana watched us spar. Speed without stillness is panic—sidestep the thrust, little brother.",
			},
			{
				"duel2",
				"ren",
				"Neo-Kyoto has not wept once since the Eternity Grid went live. Tell me why you would drag six million sleepers back into grief.",
			},
			{
				"duel3",
				"ren",
				"Pride snaps under weight; only iron resolve cracks a master’s guard. If you wake their sorrow, Kael, can your shoulders carry it?",
			},
			{
				"duel4",
				"ren",
				"Here in Ashen Hollow I held Hana while the cascade took her voice. I built the Grid so no brother would ever kneel in that ash again.",
			},
			{
				"duel5",
				"ren",
				"Every street, every scar, every autumn leaf led you to this mirror, Kael. Come—prove the human heart is strong enough to remember!",
			},
			{
				"clear0",
				"sora",
				"Sector One gate open! Neural Spark unlocked—press Q to hurl a high-voltage arc bolt downrange.",
			},
			{
				"clear1",
				"sora",
				"Petal Arcade liberated! Phase Dash online—press E to slip cleanly through incoming strikes.",
			},
			{
				"clear2",
				"sora",
				"Jade Heights breached! Heavy Cleave unlocked—press F to shatter iron guards and launch foes skyward.",
			},
			{
				"clear3",
				"sora",
				"Moonveil Stacks is free! Lantern Flare online—press R to detonate a pulse that staggers every hostile around you.",
			},
			{
				"clear4",
				"sora",
				"Ashen Hollow stands clear! Whirl Slash unlocked—press C to carve a spinning tempest through the whole pack.",
			},
			{
				"clear5",
				"sora",
				"Ren’s final mirror is broken! Step into the golden Amber Sanctuary Terminal ahead to bloom the Komorebi Seed across Neo-Kyoto!",
			},
			{
				"firstRide",
				"sora",
				"Transponder spoofed—wheels are yours, Seven. W and S throttle, Space kicks the handbrake, and G steps out once you slow down.",
			},
			{
				"heat",
				"sora",
				"Easy, Kael—sleepwalking citizens witnessed that! Break line of sight or step into a Mission Relay to scrub your Aethelgard heat.",
			},
			{
				"contractStart",
				"dispatch",
				"Encrypted street contract engaged. Follow the green telemetry diamond to complete the run before Grid security locks your trace.",
			},
			{
				"contractDone",
				"sora",
				"Clean run, Zero-Seven! Resistance credits wired and suit nanites restored.",
			},
			{
				"win",
				"sora",
				"Komorebi Seed deployed! The Eternity Grid is dissolving across all six districts—Neo-Kyoto is awake and yours to roam, Kael!",
			},
		};
		return Comms;
	}

	const CommsEntry* FindComms(std::string_view Key)
	{
		for (const CommsEntry& Entry : GetComms())
		{
			if (Entry.Key == Key)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const std::vector<CommsChain>& GetCommsChains()
	{
		static const std::vector<CommsChain> Chains = {
			{
				"intro",
				{
					{
						"kael",
						"Copy, Sora. Five years under Aethelgard’s dampener and nobody on this street even looks up at the rain. Let’s wake Sunleaf Row.",
						4.8,
					},
					{
						"mira",
						"Kael, my archive imprint is synced to your collar. Guard the Komorebi Seed—every stolen memory in Neo-Kyoto is breathing inside it.",
						4.8,
					},
				},
			},
			{
				"sector1",
				{
					{
						"dispatch",
						"Aethelgard Civic Notice: Unsanctioned memory variance detected in Petal Arcade. Citizens must remain calm and accept cortical smoothing.",
						4.8,
					},
					{
						"kael",
						"“Cortical smoothing.” Hana used to buy paper lanterns down this strip, Sora. They turned her favorite market into a hall of sleepwalkers.",
						4.8,
					},
				},
			},
			{
				"sector2",
				{
					{
						"mira",
						"Those beacons broadcast my old trauma-relief harmonics, inverted to erase grief by force. Ren turned our healing research into a cage.",
						4.8,
					},
					{
						"kael",
						"After the Silent Autumn, Ren swore nobody would ever ache like we did. I never imagined he’d hollow out a whole city to keep that promise.",
						4.8,
					},
				},
			},
			{
				"sector3",
				{
					{
						"dispatch",
						"Security Alert: Rogue Courier Zero-Seven sighted in Moonveil Stacks. Siege frames authorized—defend the Eternity Grid at all costs.",
						4.8,
					},
					{
						"kael",
						"Send all the heavy iron you want. Every ring we ignite puts real sky back over these tenements.",
						4.8,
					},
				},
			},
			{
				"sector4",
				{
					{
						"kael",
						"Our childhood ward... I can still hear the sirens dying in the ash. If Ren fires those purge caches, no one in Kyoto ever remembers home.",
						4.8,
					},
					{
						"mira",
						"Steady your pulse, Kael. Hana’s last cello recording is safe inside the Seed—do not let Aethelgard erase this district twice.",
						4.8,
					},
				},
			},
			{
				"sector5",
				{
					{
						"dispatch",
						"Code Zero-Seven at Amber Sanctuary! All elite blades converge—the Chief Architect’s spire must not fall!",
						4.8,
					},
					{
						"kael",
						"Keep the ghost-line open, Sora. One last gate, brother—then we let the sunlight back through the leaves.",
						4.8,
					},
				},
			},
			{
				"duel0",
				{
					{
						"kael",
						"Even as a neural echo, you still lecture my grip, Ren. Watch closely—I haven’t forgotten a single cut you taught me.",
						4.8,
					},
				},
			},
			{
				"duel1",
				{
					{
						"kael",
						"Don’t speak Hana’s name through an Aethelgard filter! She loved this city loud, messy, and alive—not sedated!",
						4.8,
					},
				},
			},
			{
				"duel2",
				{
					{
						"kael",
						"Silence isn’t mercy, Ren—it’s a grave with the neon left on! Pain is just the proof that we loved someone real!",
						4.8,
					},
				},
			},
			{
				"duel3",
				{
					{
						"kael",
						"We were supposed to carry the grief together! You locked yours in a tower and made six million strangers pay the price!",
						4.8,
					},
				},
			},
			{
				"duel4",
				{
					{
						"kael",
						"In erasing the ash, you erased her song too! I’m bringing Hana’s memory home—even if I have to cut through every shadow you left!",
						4.8,
					},
				},
			},
			{
				"duel5",
				{
					{
						"kael",
						"For Hana, for Mira, and for the brother who taught me to stand—Protocol Zero-Seven ends right here!",
						4.8,
					},
					{
						"mira",
						"His neural guard is tied to the Spire core! Read each combo, Kael—punish the cyan recovery opening to break his loop!",
						4.8,
					},
				},
			},
			{
				"clear0",
				{
					{
						"mira",
						"The Seed is resonating across Sunleaf Row... Citizens are pausing on the wet pavement, remembering the names of the autumn trees.",
						4.8,
					},
				},
			},
			{
				"clear1",
				{
					{
						"mira",
						"Music is returning to Petal Arcade... street vendors humming melodies they haven’t recalled in five winters.",
						4.8,
					},
					{
						"dispatch",
						"Warning: Emotional telemetry exceeding Aethelgard limits in Sectors One and Two. Escalating Jade Heights containment.",
						4.8,
					},
				},
			},
			{
				"clear2",
				{
					{
						"mira",
						"The Seed decrypted Ren’s private log here... He wept the night he compiled these Specters, Kael. Part of him prayed you would beat them.",
						4.8,
					},
				},
			},
			{
				"clear3",
				{
					{
						"mira",
						"Warm light is filling the Stacks... mothers recognizing their children’s voices through five years of fog.",
						4.8,
					},
					{
						"dispatch",
						"Critical alert: Eternity Grid integrity below forty percent. Lethal purge teams deployed to Ashen Hollow.",
						4.8,
					},
				},
			},
			{
				"clear4",
				{
					{
						"mira",
						"Hana’s cello phrase just echoed across the Hollow’s towers. Only the Amber Sanctuary Spire stands between Kyoto and dawn.",
						4.8,
					},
				},
			},
			{
				"clear5",
				{
					{
						"ren",
						"Your blade didn’t waver once, little brother... Step into the light. Remember her for both of us.",
						4.8,
					},
				},
			},
			{
				"win",
				{
					{
						"mira",
						"Six million unedited lives blooming like sunlight through autumn leaves... Thank you, Courier Zero-Seven. Hana’s song belongs to the city now.",
						4.8,
					},
					{
						"kael",
						"Hear the avenues waking up, Ren? Tears, laughter—all of it ours again. Keep the channel warm, Sora; the streets still need a courier.",
						4.8,
					},
				},
			},
		};
		return Chains;
	}

	const std::vector<CommsChainStep>* FindCommsChain(std::string_view Key)
	{
		for (const CommsChain& Chain : GetCommsChains())
		{
			if (Chain.Key == Key)
			{
				return &Chain.Steps;
			}
		}
		return nullptr;
	}

	const std::vector<StoryAct>& GetStoryActs()
	{
		static const std::vector<StoryAct> Acts = {
			{
				1,
				0,
				"Sunleaf Row",
				"Act I · Sunlight Through Static",
				"“Sunlight only has shape because the leaves stand in its way. Memory works the same.” — Dr. Mira, Archive Note 01",
				"Five years after the Silent Autumn neural cascade, former elite runner Kael—Courier 07—drops into Sunleaf Row carrying Dr. Mira’s contraband Komorebi Seed. Beneath Aethelgard’s Eternity Grid, citizens drift in painless amnesia until Kael shatters the sector’s dampener jammers and faces the first Neural Specter of his brother, Ren.",
				"The First Wooden Bokken — patience at the threshold, and the frosty dawn Ren taught Kael that a blade is drawn to shelter, never to rule.",
			},
			{
				2,
				1,
				"Petal Arcade",
				"Act II · Paper Lanterns in Rain",
				"“Take away the ache of missing someone, and you erase the proof they ever lived.” — Sora, Ghost-Line Broadcast",
				"Through the rain-slick markets of Petal Arcade, an Aethelgard Syndicate courier steals Kael’s transit cipher to quarantine the Seed. Hunting the thief past stalls where their sister Hana once hung origami cranes, Kael recovers the key and crosses steel with Ren’s second memory projection.",
				"Footwork on Wet Stone — stepping off the line of a committed thrust, practiced beside the Kamo canal while Hana kept time on the steps.",
			},
			{
				3,
				2,
				"Jade Heights",
				"Act III · The Mercy Algorithm",
				"“Grief is a hemorrhage of the mind. If a surgeon stops the bleeding, do you call him a tyrant?” — Ren, Directorate Address",
				"Ascending into Jade Heights, Kael topples three corporate Beacons flooding Neo-Kyoto with cortical white noise. Inside the Seed, Dr. Mira’s archive ghost reveals how Aethelgard twisted Ren’s trauma-relief breakthrough into permanent civic erasure—using Ren’s own sorrow over Hana as its template.",
				"Twin Currents — phasing cleanly through a two-beat storm before answering, forged the night the brothers swore to walk Hana safely home.",
			},
			{
				4,
				3,
				"Moonveil Stacks",
				"Act IV · Sleepless Beneath Glass",
				"“A city that forgets how to weep soon forgets why it sang.” — Old Kyoto Canal Mural",
				"Grid Dispatch declares Code Crimson across Moonveil Stacks, rolling out heavy siege mechs and dropping blast doors to trap Courier 07. Kael hotwires street machines to thread Sora’s high-voltage Relay Rings at full throttle, waking tenements full of families who still tend shrines they cannot explain.",
				"Iron Over Pride — breaking an unyielding guard through committed weight, from the tournament Ren secretly forfeited to save an injured Kael.",
			},
			{
				5,
				4,
				"Ashen Hollow",
				"Act V · Embers of the Silent Autumn",
				"“We called it the Silent Autumn because the sirens cut out before the leaves touched the street.” — Kael // Courier 07",
				"In the quarantined ruins of Ashen Hollow—ground zero of the cascade that claimed Hana—Aethelgard stages memory-purge munitions to wipe the city clean. Kael wrecks the Syndicate’s caches across his childhood ward and confronts Ren’s fifth Specter, where the Architect’s stoic mask cracks into raw survivor’s guilt.",
				"The Ember Spiral — turning encirclement into a single rising cut, drilled in the family courtyard the evening before the conservatory surge.",
			},
			{
				6,
				5,
				"Amber Sanctuary",
				"Act VI · Protocol Zero-Seven",
				"“Do not forgive me, Kael. Only remember her for both of us.” — Ren // Chief Architect",
				"At the golden spire of Amber Sanctuary, Kael overloads the final Shield Pylons and steps beneath the amber tree to face Ren’s complete Neural Mirror. Every sword lesson returns to its master in one decisive duel over Neo-Kyoto’s soul: painless oblivion, or the fierce, living light of Komorebi.",
				"The Unbroken Mirror — student and master standing equal as every lesson converges, letting real sunlight filter back through autumn leaves.",
			},
		};
		return Acts;
	}

	const std::vector<CharacterDossier>& GetCharacterDossiers()
	{
		static const std::vector<CharacterDossier> Dossiers = {
			{
				"kael",
				"Kael // Courier 07",
				"Protagonist · Neural Runner & Bearer of the Komorebi Seed",
				"Free Couriers // Protocol Zero-Seven",
				"#fcee0a",
				"“Pain isn’t a flaw in being human, Ren. It’s the receipt that we loved someone real.”",
				"Once Neo-Kyoto’s fastest neural courier and his brother Ren’s prized sword pupil, Kael abandoned the corporate towers after the Silent Autumn claimed their younger sister Hana. Refusing Aethelgard’s cortical dampeners, he carries Dr. Mira’s Komorebi Seed behind his collar—cutting south through six quarantined districts to give a sedated city back its heart.",
			},
			{
				"ren",
				"Ren // Chief Architect",
				"Antagonist · Architect of the Eternity Grid & Master Bladesman",
				"Aethelgard Directorate",
				"#ff2a6d",
				"“I watched grief hollow this city to ash. If I must be its jailer to end its agony, so be it.”",
				"Kael’s older brother and former kendo master. Broken after holding Hana during the final hours of the Silent Autumn cascade, Ren built the Eternity Grid for megacorp Aethelgard, convinced that excising sorrow is the highest mercy. Yet he keyed the six sector gates to Kael’s neural tag—leaving six Specters of his blade to test if Kael can bear what Ren could not.",
			},
			{
				"sora",
				"Sora // Resistance Netrunner",
				"Tactical Handler · Ghost-Line Operator",
				"Kamo Underground // Free Kyoto",
				"#00f0ff",
				"“Aethelgard owns the skyline, Seven. Tonight we remind them who owns the rain.”",
				"A sharp-witted signal breaker broadcasting from a shielded barge beneath the Kamo floodgates. After the Eternity Grid erased her mother’s memory of her late father—and of why she ever raised Sora to fight—Sora dedicated her life to cracking Aethelgard’s relays and guiding Courier 07 to the Sanctuary Spire.",
			},
			{
				"mira",
				"Dr. Mira // Archive Ghost",
				"Creator of the Komorebi Seed · Preserved Neural Imprint",
				"Former Aethelgard Neuro-Ethics Lead",
				"#19ffc6",
				"“Komorebi means sunlight filtering through autumn leaves—precious because every shadow moves.”",
				"Ren’s former partner on clinical trauma relief who rebelled the night Aethelgard weaponized their research into mass civic amnesia. Before Directorate squads raided her Jade Heights vault, Dr. Mira wove millions of unedited human memories, art, and a living echo of her own mind into the Komorebi Seed.",
			},
			{
				"hana",
				"Hana // Echo in the Amber",
				"The Lost Sister · Conservatory Cellist of Old Kyoto",
				"Preserved within the Komorebi Seed",
				"#ffb000",
				"“Stop glaring at your swords so hard, you two—come listen to the rain on the ginkgo leaves!”",
				"Kael and Ren’s seventeen-year-old sister, a gifted cellist whose conservatory rehearsal hall was ground zero of the Silent Autumn neural surge. Her death tore her brothers down opposite philosophies: Ren silenced an entire metropolis to stop the hurting, while Kael fights to let her final autumn melody ring across Neo-Kyoto.",
			},
		};
		return Dossiers;
	}

	const CharacterDossier* FindCharacterDossier(std::string_view Id)
	{
		for (const CharacterDossier& Doc : GetCharacterDossiers())
		{
			if (Doc.Id == Id)
			{
				return &Doc;
			}
		}
		return nullptr;
	}

	const std::vector<EchoShard>& GetEchoShards()
	{
		static const std::vector<EchoShard> Shards = {
			{
				"echo-01",
				0,
				"The Word “Komorebi”",
				"mira",
				-33.4,
				213.4,
				"Hana chose the archive’s name herself, Kael. Komorebi—sunlight through autumn leaves, alive only because the shadows move too.",
				"Recorded six months before the Silent Autumn in Dr. Mira’s Sunleaf conservatory. Seventeen-year-old Hana sketched golden ginkgo leaves on wet slate while her brothers sparred outside, arguing that a mind scrubbed of longing would be like a cello suite with every rest cut out.",
			},
			{
				"echo-02",
				0,
				"Two Wooden Blades",
				"kael",
				46.6,
				186.6,
				"Ren carved a notch into my first bokken on this avenue. Told me a blade isn’t drawn to win—it’s drawn to stand between the storm and home.",
				"Before Aethelgard raised glass towers over Sunleaf Row, a cedar dojo overlooked the canal. Every frost-white morning, Ren drilled young Kael on patience against sweeping strikes while Hana warmed barley tea on the steps and kept rhythm with a pocket metronome.",
			},
			{
				"echo-03",
				1,
				"The Paper Crane Market",
				"kael",
				-46.6,
				133.4,
				"Hana strung a hundred crimson paper cranes above Petal Arcade before the storm. Five years on, people walk under the hooks and never look up.",
				"During Old Kyoto’s autumn festival, Petal Arcade shimmered with hand-dyed silk and street musicians. Once the Eternity Grid locked in, Aethelgard replaced live performers with 60-BPM ambient pulses calibrated to prevent nostalgic spikes in passing shoppers.",
			},
			{
				"echo-04",
				1,
				"Stranger at the Tea Table",
				"sora",
				68.0,
				102.0,
				"The morning the Grid booted, my mom poured tea and politely asked whose coat I was wearing. It was my dad’s. Ten years of love—wiped clean.",
				"Sora’s encrypted personal log from Year One of the Eternity Grid. Because human love and human grief share the same neural pathways, Aethelgard’s dampener could not excise sorrow without severing the deepest bonds of recognition between parents, lovers, and friends.",
			},
			{
				"echo-05",
				2,
				"Aethelgard Charter 09",
				"dispatch",
				-68.0,
				54.0,
				"Directorate Memo 09: Unregulated civic bereavement lowers output by forty-two percent. Authorize Chief Architect Ren’s citywide dampener.",
				"Recovered from a shattered Jade Heights relay. While Ren earnestly believed he was saving Neo-Kyoto from post-cascade despair, Aethelgard’s board quietly appended sub-routines that suppressed labor unrest, defiant art, and historical memory in the same pass.",
			},
			{
				"echo-06",
				2,
				"Mira’s Midnight Mutiny",
				"mira",
				74.0,
				72.0,
				"When I found Aethelgard’s permanent-erasure clause, I pulled the raw citizen backups before dawn. If I couldn’t escape the tower, the Seed would.",
				"Hours before corporate Enforcers breached Vault 4 in Jade Heights, Dr. Mira compressed 4.8 million unedited citizen memories—along with a neural ghost of her own consciousness—into the Komorebi Seed and passed it through an air-shaft drop to Courier Zero-Seven.",
			},
			{
				"echo-07",
				3,
				"Unlit Household Shrines",
				"mira",
				-66.0,
				-12.0,
				"Look at the tenement windows in Moonveil Stacks, Kael. Families still polish little brass bells on their shelves without remembering whose name is on them.",
				"Field telemetry gathered by Mira’s archive node. Even beneath total cortical suppression, the body remembered what the Grid forbade: bakers setting aside a third roll, cyclists braking at empty corners, and sleepers waking with salt on their cheeks.",
			},
			{
				"echo-08",
				3,
				"The Forfeited Semifinal",
				"ren",
				46.6,
				-53.4,
				"Archive Fragment, Ren: Kael fought with cracked ribs and refused to yield. So I stepped out of the ring myself—someone had to save him from his pride.",
				"A private voice note recorded three autumns before the cascade. Even in youth, Ren’s tragedy was already written: a fierce, lonely compulsion to carry every wound for his family without ever asking whether they wanted to be shielded.",
			},
			{
				"echo-09",
				4,
				"October 14: The Silent Autumn",
				"kael",
				-53.4,
				-133.4,
				"Twenty-two hundred hours, Ward Four. An experimental Aethelgard power spine surged into the conservatory’s neural acoustics... and the music stopped.",
				"Black-box emergency log buried beneath Ashen Hollow. Aethelgard blamed the Silent Autumn cascade on underground hackers, concealing telemetry that proved their own prototype grid overloaded the school auditorium where Hana was recording.",
			},
			{
				"echo-10",
				4,
				"Hana’s Unfinished Cello",
				"mira",
				74.0,
				-146.0,
				"I salvaged Hana’s last rehearsal track from the conservatory server, Kael. Right before the static hit, she laughed and said: tell my brothers I’m coming home.",
				"Stored at the luminous core of the Komorebi Seed. Rain patters on glass above a warm D-dorian cello phrase, followed by Hana’s unhurried laughter—the single recording Ren locked behind six gates because hearing her voice again would undo his armor.",
			},
			{
				"echo-11",
				5,
				"Six Mirrors in the Kernel",
				"ren",
				-33.4,
				-213.4,
				"Archive Fragment, Ren: If my mercy is a lie, only one blade in Kyoto can cut through it. I leave six Specters on the trail to temper him.",
				"Hidden inside the Eternity Grid’s root kernel, invisible to Aethelgard’s directors. Before sealing himself inside the Amber Spire, Ren forged his six Neural Specters not as executioners, but as final dojo lessons to ensure Kael was strong enough to finish the run.",
			},
			{
				"echo-12",
				5,
				"Signature Zero-Seven",
				"mira",
				66.6,
				-253.4,
				"Protocol Zero-Seven wasn’t my backdoor alone, Kael—Ren left the Sanctuary socket unlocked from within. Deep down, your brother was waiting for you.",
				"The final cryptographic layer of the Komorebi Seed reveals two authors on Protocol Zero-Seven: Dr. Mira wrote the memory-bloom sequence, and Chief Architect Ren quietly whitelisted Courier 07’s neural signature at the heart of the Amber Sanctuary.",
			},
		};
		return Shards;
	}

	const EchoShard* FindEchoShard(std::string_view Id)
	{
		for (const EchoShard& Shard : GetEchoShards())
		{
			if (Shard.Id == Id)
			{
				return &Shard;
			}
		}
		return nullptr;
	}

	const std::vector<SectorMission>& GetSectorMissions()
	{
		static const std::vector<SectorMission> Missions = {
			{
				0,
				"jammer",
				"OP 01 // Silent Autumn",
				"Overload Dampener Jammers",
				"Move WASD · Click/J Slash · G Ride bike · Smash both glowing Dampener Jammers",
				{
					{-7.0, 214.0, "Jammer Alpha", ""},
					{7.0, 184.0, "Jammer Beta", ""},
				},
			},
			{
				1,
				"intercept",
				"OP 02 // Stolen Cipher",
				"Catch Marked Courier & Sync Uplink",
				"Defeat the hooded Syndicate Courier (magenta diamond), then stand by the Terminal",
				{
					{-6.0, 126.0, "Marked Courier", "courier"},
					{8.0, 112.0, "Arcade Uplink", "terminal"},
				},
			},
			{
				2,
				"beacon",
				"OP 03 // Grief Dampeners",
				"Shatter Aethelgard Beacons",
				"Use Q Spark or sword combos to shatter all 3 Aethelgard Beacons",
				{
					{-10.0, 68.0, "West Beacon", ""},
					{26.0, 46.0, "East Beacon", ""},
					{14.0, 24.0, "South Beacon", ""},
				},
			},
			{
				3,
				"rings",
				"OP 04 // Moonveil Express",
				"Pass Through Relay Rings",
				"Ride a car or bike (G) or dash (E) through the 3 cyan Relay Rings",
				{
					{16.0, -18.0, "Relay Ring 1", ""},
					{4.0, -46.0, "Relay Ring 2", ""},
					{-8.0, -72.0, "Relay Ring 3", ""},
				},
			},
			{
				4,
				"cache",
				"OP 05 // Purge Caches",
				"Destroy Purge Supply Caches",
				"F Heavy Strike or ram at speed to wreck the 3 Purge Supply Caches",
				{
					{-14.0, -116.0, "North Cache", ""},
					{-20.0, -142.0, "Mid Cache", ""},
					{12.0, -166.0, "Gate Cache", ""},
				},
			},
			{
				5,
				"shield",
				"OP 06 // Protocol Zero-Seven",
				"Overload Sanctuary Shield Pylons",
				"Destroy both Sanctuary Shield Pylons, then face Ren’s Mirror at the Spire",
				{
					{16.0, -222.0, "West Shield Pylon", ""},
					{30.0, -252.0, "East Shield Pylon", ""},
				},
			},
		};
		return Missions;
	}

	const std::vector<KioskSpot>& GetKioskSpots()
	{
		static const std::vector<KioskSpot> Kiosks = {
			{"k0", "Sunleaf West Contract", -120.0 + 6.8, 200.0 + 6.8, "rings", -1, 0},
			{"k1", "Downtown East Contract", 120.0 - 6.8, 200.0 + 6.8, "jammers", 1, 0},
			{"k2", "Arcade Plaza Contract", -120.0 + 6.8, 120.0 - 6.8, "uplink", -1, 0},
			{"k3", "Heights Overlook Contract", 120.0 - 6.8, 40.0 + 6.8, "rings", 1, 0},
			{"k4", "Market Cross Contract", -120.0 + 6.8, -40.0 - 6.8, "jammers", -1, 0},
			{"k5", "Docks Freight Contract", 120.0 - 6.8, -40.0 + 6.8, "uplink", 1, 0},
			{"k6", "Suburbs Relay Contract", -120.0 + 6.8, -120.0 - 6.8, "rings", -1, 0},
			{"k7", "Sanctuary Outer Contract", 120.0 - 6.8, -240.0 - 6.8, "jammers", 1, 0},
		};
		return Kiosks;
	}

	const std::vector<Vec2>& GetExtraKeep()
	{
		static const std::vector<Vec2> ExtraKeep = []() {
			std::vector<Vec2> Points;
			for (const SectorMission& Mission : GetSectorMissions())
			{
				for (const MissionTarget& Target : Mission.Targets)
				{
					Points.push_back({Target.X, Target.Z});
				}
			}
			for (const KioskSpot& Kiosk : GetKioskSpots())
			{
				Points.push_back({Kiosk.X, Kiosk.Z});
			}
			for (const EchoShard& Shard : GetEchoShards())
			{
				Points.push_back({Shard.X, Shard.Z});
			}
			return Points;
		}();
		return ExtraKeep;
	}
}
