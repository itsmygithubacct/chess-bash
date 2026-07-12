# Asset provenance and licensing

This file records the origin and redistribution terms of every non-code asset
shipped with Chess Bash. The runtime files in `assets/` are the canonical
release assets; private workstations, unpublished archives, and external source
trees are not required to build or run the game.

Except for the embedded font described below, the project-specific visual and
audio assets are distributed under the repository's MIT License to the extent
copyright or related rights exist. They were created specifically for Chess
Bash and do not incorporate source files from another chess game.

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

The WAV files under `assets/sfx/` and `assets/music/` were synthesized for this
project with deterministic local DSP—oscillators, filtered noise, envelopes,
modal resonators, and simple sequencing. No recorded samples, commercial sound
libraries, or third-party compositions are included.

The game also contains a C-based procedural fallback for sound effects. Runtime
music uses the shipped mono 44.1 kHz, 16-bit PCM WAV files.

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

Historical experiments, intermediate generations, unused atlases, and
alternate media encodings are intentionally excluded from the public runtime
repository.
