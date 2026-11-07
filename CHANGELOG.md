# Changelog

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
