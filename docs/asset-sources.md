# Asset provenance and licensing

This file records the origin and redistribution terms of every non-code asset
shipped with Chess Bash. The runtime files in `assets/` are the canonical
release assets; private workstations, unpublished archives, and external source
trees are not required to build or run the game.

Except for the embedded font and ElevenLabs-generated gameplay sound effects
described below, the project-specific visual assets and deterministic music are
distributed under the repository's MIT License to the extent copyright or
related rights exist. They were created specifically for Chess Bash and do not
incorporate source files from another chess game.

## Visual assets

| Runtime files | Origin | Release processing |
|---|---|---|
| `assets/title.ppm` | Original pixel-art concept generated with Google Gemini | Cropped and normalized to binary PPM |
| `assets/pieces.ppm` | Original chess-army sprite concept generated with Google Gemini | Chroma-keyed runtime atlas |
| `assets/pieces_direction.ppm` | Gemini-generated directional study derived from the project sprite concept | Normalized directional atlas |
| `assets/pieces_fight.ppm` | Gemini-generated battle poses using the project sprites as character references | Identity-checked and normalized fight atlas |
| `assets/effects.ppm` | Original effects study generated with Google Gemini | Chroma-keyed runtime atlas |
| `assets/boards/*.ppm` | Original Gemini environment concepts, edited with OpenAI image generation | Boards and checker grids removed; center-cleared and normalized to 640x360 PPM |

The image-generation briefs required original retro pixel art and prohibited
copied game sprites, screenshots, logos, recognizable copyrighted characters,
and third-party artwork. The background edit brief additionally required a
boardless environment with an open, low-contrast center because the game draws
its own board there.

All generated images were visually reviewed and materially prepared for the
game through selection, layout decisions, cropping, atlas construction,
chroma-key cleanup, and runtime conversion. Google and OpenAI are tool
providers, not authors, sponsors, or endorsers of Chess Bash.

## Audio assets

The 25 WAV files under `assets/sfx/` form seven cue-specific variation banks:
wooden selection taps, armored boot footsteps, steel sword contacts, armored
body falls, start and victory trumpets, and a forged-steel check-warning bell.
They were generated specifically for Chess Bash with ElevenLabs Text to Sound
Effects v2 (`eleven_text_to_sound_v2`) during the account owner's paid Starter
subscription.

The production field contained 58 candidates: six selections, twelve
footsteps, twelve sword contacts, ten falls, and six candidates for each
trumpet and bell cue. Six move/capture/fall pilot files were heard before the
full run was approved. A pinned LAION CLAP model provided semantic triage;
candidate score arrays were identical across two independent runs. The final
selection also used cue-specific event-shape checks and a waveform-correlation
diversity penalty. CLAP was only an evaluator and none of its files are
distributed.

Starter API responses were 128 kbps, 44.1 kHz MP3. Selected sources were
decoded, onset-aligned, downmixed with equal power, trimmed or padded to exact
runtime length, high-pass/DC cleaned, edge-faded, and gain-staged against a 4x
reconstructed peak. No procedural sound, synthetic timing layer, saturation,
recorded library sample, or third-party sound file was mixed into the final
masters. Runtime files are mono 44.1 kHz, signed 16-bit PCM WAV, and the game
avoids immediate repeats within a bank.

ElevenLabs states that qualifying output generated during a paid subscription
may be used commercially and indefinitely. Its Terms of Service say that, as
between the customer and ElevenLabs, the customer retains rights in output.
The Sound Effects Terms and Prohibited Use Policy still apply, output may not
be unique, and standalone sale, distribution, licensing, sublicensing, or other
commercial exploitation of Sound Effects output is prohibited. Accordingly,
`assets/sfx/*.wav` is excluded from this repository's MIT grant and is included
only as bundled Chess Bash game content, not as an isolated sample pack or
sound library.

Terms were checked on 2026-07-14: [paid-plan commercial
use](https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform),
[Terms of Service](https://elevenlabs.io/terms-of-use), [Sound Effects
Terms](https://elevenlabs.io/sound-effects-terms), and [Prohibited Use
Policy](https://elevenlabs.io/use-policy). Exact prompts, source and final
hashes, selection metrics, mastering settings, account-plan attestation, and
terms snapshot are recorded in
[`audio-provenance.json`](audio-provenance.json).

The WAV files under `assets/music/` remain original deterministic local DSP
compositions made from oscillators, filtered noise, envelopes, resonators, and
simple sequencing. The game also contains a C-based procedural fallback for
sound effects when runtime assets cannot be loaded.

## Embedded font

`src/font8x16.h` contains glyphs extracted from Debian console-setup's
`Lat15-Fixed16` console font. Debian describes its console fonts as public
domain. The font's X11-lineage permissive notice, including the Sony Corp.,
BIZNET Poland, and Dmitry Yu. Bolkhovityanov notices, is preserved verbatim in
the header and summarized in the root `LICENSE` file.

## Runtime inventory

The release payload contains:

```text
assets/title.ppm
assets/pieces.ppm
assets/pieces_direction.ppm
assets/pieces_fight.ppm
assets/effects.ppm
assets/boards/*.ppm
assets/sfx/*.wav
assets/music/*.wav
```

Historical experiments, intermediate generations, raw API responses, unused
atlases, and alternate media encodings are intentionally excluded from the
public runtime repository.
