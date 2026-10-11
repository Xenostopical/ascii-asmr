# ASCII ASMR

A terminal-native generative ambience playground written in C. It combines 22 animated ASCII scenes, colour palettes, a lightweight procedural stereo synthesizer, and an interactive keyboard/mouse interface. It uses no downloaded art or model assets.

## Build

Requirements: a C compiler (GCC or Clang), `make`, POSIX threads, and `libm`.

```sh
make
./asmr
```

Audio playback is automatic when a supported command-line player is available (`paplay`, `aplay`, `pw-cat`, `play`, or `ffplay`). If no player is installed, the visual app can still run; use `-n` to silence audio or export sound to a file.

## Controls

| Key | Action |
| --- | --- |
| `←` / `→`, `↑` / `↓`, `Tab`, `,` / `.` | Previous or next scene |
| `1`–`9` | Jump to a scene |
| `G` | Open the scene browser; arrows preview and Enter selects |
| `Space` / `S` | Shuffle the scene and palette |
| `P` / `R` | Cycle palette / choose a random palette |
| `A` | Toggle automatic scene shuffle |
| `M` | Toggle sound |
| `+` / `-`, mouse wheel | Adjust volume |
| `[` / `]` | Slow down / speed up animation |
| `Y` | Toggle the binaural layer (headphones recommended) |
| `B` | Toggle breathing guide |
| `J` | Toggle the guided journey |
| `T` | Cycle the sleep timer |
| `X` | Save an ANSI and plain-text screenshot in the current directory |
| `U` | Hide / show the interface |
| `?` / `H` | Keyboard guide |
| `Q` / `Esc` | Quit |
| Click / drag | Interact with scenes; click “Visit my site” in the header to open the website |

## Command line

```text
./asmr --help                 Show options
./asmr --list                 List scenes and palettes
./asmr -s rain -p aurora      Pick a scene and palette
./asmr -n                     Disable sound
./asmr -f 60 -x 1.5           Set frame rate and animation speed
./asmr -S 1234                Use a reproducible random seed
./asmr -F 1800 -n             Render 1800 frames without a terminal or audio
./asmr -F 1800 -O out.wav     Render an exact-length 44.1 kHz stereo WAV
./asmr -F 1800 -d out.raw     Export exact-length raw signed 16-bit stereo PCM
./asmr -B 200                 Benchmark each scene for 200 frames
```

`-O` and `-d` require audio rendering and cannot be used together or with `-n`. `-F` runs headlessly, making deterministic exports and smoke tests straightforward. The WAV output is 44.1 kHz, stereo, signed 16-bit PCM. Raw output has the same PCM format but no file header.

## Checks

```sh
make test   # ASan + UBSan regression tests
make smoke  # CLI validation and exact output-size smoke tests
make check  # Both suites
```

Configuration is stored at `$XDG_CONFIG_HOME/asmr/config`, or `$HOME/.config/asmr/config` if `XDG_CONFIG_HOME` is not set. Unknown, malformed, or non-finite values are ignored and settings are clamped to safe ranges.

## Notes

The renderer is designed to degrade gracefully on small terminal sizes and supports true-colour or 256-colour terminals. Set `COLORTERM=truecolor` for true-colour detection if your terminal does not advertise it automatically; `-t` and `-2` force true-colour and 256-colour modes, respectively.

## Install as `asciiasmr`

The Makefile supports `make install` (default destination `/usr/local/bin/asciiasmr`) and `make uninstall`. For package-manager publishing starter files and platform-specific notes, see [`packaging/README.md`](packaging/README.md). The package recipes are templates until the source release URL, checksum, maintainer, and actual software license are set.
