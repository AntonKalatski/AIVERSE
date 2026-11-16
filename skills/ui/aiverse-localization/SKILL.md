---
name: aiverse-localization
description: Localize an AIVERSE game or engine UI - lang key files, no concatenated sentences, missing-key and consistency checks, long-string and font checks on phone screenshots.
---

# Localization

Engine strings: `engine/assets/lang/en.json` + `ru.json` through `i18n::Localization::tr()` (`engine/i18n/README.md`). Game strings: the game's `lang/*.json`.

1. Every user-facing text is a key: RML uses `@lang.key`, data uses lang keys (briefings, objectives, dialogs, unit names). Never concatenate sentences - one key per full sentence with placeholders.
2. Add a language: `aiverse_i18n extract --project <game> --lang <code> --write` writes `lang/<code>.json` with every
   translatable key (existing translations kept, missing ones get the source text and a TODO). Translate values only;
   keys, placeholders and markup stay unchanged.
3. Check: `aiverse_i18n check --project <game>` (CMake target `aiverse_i18n_tool`) (tool source `tools/i18n/main.cpp`; engine tables with
   `--engine engine/assets`) - keys missing from a table, unknown placeholders against the base language, hard-coded
   RML text, missing `@language`. Exit 1 on errors; fix all errors. Asset-level consistency of lang files is also in
   `skills/workflow/aiverse-content-pipeline`.
4. Fonts: scripts like CJK, Arabic or Cyrillic need glyphs in the game's fonts - check on screenshots.
5. Look: `ui_preview` per language on a small phone profile (`device_set`): longest strings fit, nothing clipped, RTL if used.
6. Documentation translations follow AGENTS.md section 11 (`skills/workflow/aiverse-docs-upkeep`).

Runtime library: none. Tooling: the `aiverse_i18n` executable (`tools/i18n`).
