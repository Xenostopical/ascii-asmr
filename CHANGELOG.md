# Changelog

## 1.1.0 — UI refresh and reliability hardening

- Rebuilt the terminal interface into a responsive ambient-studio dashboard with scene title art, atmosphere details, audio meter, clearer status/footer, clickable controls, palette cycling, and a scene browser.
- Improved compact-window behavior and hitbox cleanup when the interface is hidden or resized.
- Made `--help` and `--version` successful exits and strengthened validation of numeric and conflicting CLI options.
- Reworked configuration parsing to tolerate whitespace, comments, malformed values, non-finite numbers, and out-of-range settings safely.
- Hardened Unicode encoding, easing/remapping edge cases, canvas glow bounds, write loops, terminal escape-sequence parsing, and terminal-state restoration.
- Removed shared global-RNG use from the audio thread, synchronized audio controls, sanitized audio events, made backend state atomic, added exact-length raw PCM export, and restored the prior `SIGPIPE` policy on shutdown.
- Added sanitizer-backed regression checks for invalid inputs/configs, audio reinitialization/export, UI hitboxes, and edge cases, plus CLI/export smoke tests.

Validation on the development environment: `make check` passes; the regression suite reports 2,168 checks with zero failures. A separate Clang build, focused static analysis, and interactive pseudo-terminal smoke test also passed.

## Website link update

- Updated the clickable header link to display **Visit my site** and open `https://gg.elitehub.page.gd/`.
