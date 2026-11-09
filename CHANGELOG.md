# Changelog

## 0.2.3: lines that stay home

- Fixed: the COUNTER synth line could climb round after round (in TUMBLEWEED by more than an octave), and after part B
  the chords and the counter-line stayed where B left them when A came back (PILLOW TALK, OVERNIGHT, CIRCUIT LOVE,
  SUNDAY BEST). The counter-line now stays within a band round the synth's home, and each change of part starts the
  chords, the counter-line and the bass from their home, so A sounds like A again.
- An engine test plays the jams that drifted through many rounds of B and back to A.

## 0.2.2: USB LEVEL

- **USB LEVEL** (GLO → SYSTEM, K2; also in the companion's Jam tab): how loud the FM-1 comes into a computer over
  USB. MASTER (the default) follows the MASTER knob as before; 1–100 gives USB its own level, and MASTER then sets
  only the speaker and headphones. The headphone side limiter keeps its ceiling for the speaker either way.
- KEYS SOUND left the SYSTEM page for it (it's on GLO → KEYS and the ALGORITHM knob).

## 0.2.1: a smooth ring

- **The ring around the chord on PLAY fills smoothly**, following the band between beats instead of jumping a
  beat at a time. Each frame draws only the bit of ring it gained.
- **The companion's Guide is a full manual:** every page and knob with the FM-1's own screens, a drawn panel,
  MY LINE, performing, saving and how much fits, the loop pedal, MIDI and every list.
- The companion's Jam tab: the play and generate buttons line up again.

## 0.2: more band, your own synth line

- **More to play with:** 28 grooves (shaker pop, half-time shuffle, breakbeat, trap, new wave, afrobeat, Motown,
  rockers), 24 bass lines (syncopated, rockers, 808 slide, arpeggio, bossa, offbeat, gallop, locked), 24 ways of
  comping (skank, gallop, 16th strum, Travis picking, arpeggio down, afro, ripple, blocks), 15 synth lines (octaves,
  sequence, chimes, gated chords, arpeggio up) and 37 sounds (acid, 808 bass, piano, marimba, harp, clav, glass pad,
  saw lead, the last also on the keys). The generator uses them too.
- **160 factory jams:** ten in each of the 16 genres. The 38 from 0.1 are unchanged.
- **MY LINE, your own synth line:** LFO twice. A 16-step piano roll: SELECT moves the step, the keys write a note,
  OCT- a rest, OCT+ holds the note before; K1 note, K2 how hard, K3 length (1-16 steps), K4 follow the chords or stay
  on the key. ARP gives a line to start from. The line moves with each chord through the key's scale. The companion
  has the same piano roll (click, drag to hold, right-click for how hard) with a playhead.
- **Changing the scale changes the chords:** a chord that belongs to the old scale becomes the new scale's chord on
  the same number, keeping its colour (7th, 9th, 6th...); borrowed chords stay as written. Major and back is exact.
- Fixed: the generator's minor, dorian and mixolydian progressions wrote flats (b3, b6, b7) on top of the scale's
  own, so some generated jams played the wrong chords.
- The protocol is version 2: factory slots are 0x40 and up on the wire (160 no longer fit from 0x80). The companion
  speaks both.

## 0.1.1: the emulator

- **The emulator:** the whole firmware compiled to WebAssembly, on a drawn FM-1, in the browser
  (companion/emulator/, linked from the companion).

## 0.1.1

- **PRESETS is one list of every jam, filed by genre:** a heading for each genre, your jams tagged with it, the
  factory's, then **+ NEW JAM**; keep turning to reach the next genre. Nothing loads while you turn: 2.5 s after the
  last detent the jam under the cursor starts at the next bar, a bar under the list counting down. The first detent
  opens the list where you are. OCT+ loads now, OCT- never mind.
- **ALGORITHM picks the instrument the keys play** (the leads first), the way PRESETS picks a jam: nothing changes
  while it turns, the sound under the cursor plays 2.5 s after it stops (OCT+ now, OCT- never mind). It no longer
  chooses a genre.
- Fixed: the PRESETS and ALGORITHM lists could close the moment they opened and load at once. A turn stamped a
  millisecond after the panel's pass began read as 49 days ago (an unsigned difference); every "long enough?" on
  the panel now counts such a time as no time at all. While a list is up it is drawn over any message.
- The USER KIT page: readable drum labels, and its knobs are the kit, its tuning, glue and width.

## 0.1

The first release.


- The band: drums, bass, chords and a synth line over two parts (A and B) of up to 12 chords each, written as
  Nashville numbers in any key and scale (major, minor, dorian, mixolydian, lydian, phrygian, harmonic minor) and any of
  seven time signatures (4/4, 3/4, 6/8, 12/8, 2/4, 5/4, 7/8).
- 20 grooves, 16 bass lines, 16 ways of comping, 9 synth lines; energy, an alternative beat, fills, swing, human
  feel, changes on the push, chord colour; A/B by hand or chained; count-in and an ending.
- 26 sounds (electric pianos, guitars, organ, pads, leads, basses) and 10 drum kits (VINTAGE, STUDIO, ROOM, BRUSHES,
  LO-FI, ROCK, CAJON, LM-80, RHYTHM BOX, 808), all synthesized, plus a USER kit from your own samples.
- 38 factory jams, mostly indie, dream pop and bedroom; GENERATE draws new ones from 16 genres.
- MIDI clock in (a loop pedal's START begins on its first tick), clock out over USB, footswitch CCs, program changes.
- The companion: the jam editor, the library (on the FM-1, in the browser, as files), the user kit, the screen, the
  guide, and the flasher.

### Second round

- **Genres to browse by.** ALGORITHM picks a genre (or ALL); PRESETS browses that genre's jams (yours, then the
  factory's, then *+ NEW*, which generates one). A genre picked starts its first jam at the next bar.
- **Time to choose.** A jam or genre you browse to loads 2 s after the knob stops; OCT+ loads it now, OCT- never mind.
- **The ring.** HOME shows the chord playing by name inside a circle that fills as the chord plays, with the key, the
  next chord, the beat, the parts and the progression by name beside and below it.
- **Chord names first.** Names lead everywhere, numbers are small. EDIT's CHORD knob steps through the key's own
  chords; the keys still give any root (borrowed chords). The companion's picker does the same, others behind
  *Other chords*.
- **The keys, for soloing.** Their own sound, level and reverb (the band's mutes and levels leave them alone), mono
  with glide for leads, and three leads: SOLO LEAD (the default), WHISTLE, BRIGHT LEAD. SEL held + SELECT changes the
  sound; GLO → KEYS has it all; so does the companion.
- **Cymbals, hats and snares** rebuilt from noise shaped like theirs that darkens as it rings, with the plates'
  beating, the stick's ping and a ride's bell.
- A CPU guard: a block that runs hot stops the oldest voice already letting go.

### Fixed while testing on hardware

- Cymbals pitched like gongs (partials too low, 24 kHz); rebuilt bright and clear.
- Hiss on hats, cymbals and shakers: 4-bit ADPCM cannot follow them (12-16 dB SNR). They are now 8-bit mu-law (37 dB),
  in the firmware and in the user kit.
- A strong noise with TAPE (and quieter with chorus): their delay lines read one sample past their buffers. The
  engine's tests now run under AddressSanitizer and UBSan, which catch exactly this.
- Drum hits with high HUMAN could be scheduled hours late (a negative delay cast to unsigned).
