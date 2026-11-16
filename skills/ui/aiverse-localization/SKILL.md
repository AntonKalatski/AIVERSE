---
name: aiverse-localization
description: Localize an AIVERSE game or engine UI - lang key files, no concatenated sentences, missing-key and consistency checks, long-string and font checks on phone screenshots.
---

# Localization

Engine strings: `engine/assets/lang/en.json` + `ru.json` through `i18n::Localization::tr()` (`engine/i18n/README.md`). Game strings: the game's `lang/*.json`.

1. Every user-facing text is a key: RML uses `@lang.key`, data uses lang keys (briefings, objectives, dialogs, unit names). Never concatenate sentences - one key per full sentence with placeholders.
2. Add a language: copy the base file to `lang/<code>.json` with `game_file_read` / `game_file_write`, translate values only; keys, placeholders and markup stay unchanged.
3. Consistency: every key present in every language, no orphan keys, placeholders identical - run the content checks of `skills/workflow/aiverse-content-pipeline`.
4. Fonts: scripts like CJK, Arabic or Cyrillic need glyphs in the game's fonts - check on screenshots.
5. Look: `ui_preview` per language on a small phone profile (`device_set`): longest strings fit, nothing clipped, RTL if used.
6. Documentation translations follow AGENTS.md section 11 (`skills/workflow/aiverse-docs-upkeep`).

Runtime library: none. Workflow over game lang files.
