# UI.Windows audio banks -> audio.json

Unity games built on UI.Windows keep their sounds in its audio system, not in FMOD Studio banks: an
`FMODChannels` asset (groups -> items: key, clips, SFX / Music, volume, play chance, max count, music channel,
loop, fades, `triggerOnBegin` / `triggerOnEnd` chains) and `FMODEventReference` assets (key to play, volume
events, haptics).

```sh
python3 tools/importers/fmod/import_audio.py --unity <UnityProject>/Assets --out <game>
```

writes `audio/` (clips as they are; aiversebuild compresses), merges `audio.json` (SFX -> `sounds` with `bus`
ui / sfx, `maxInstances` + `steal: none`, `chance` from the play chance; Music -> `music` playlists: a
`triggerOnBegin` chain becomes one adaptive playlist with `layers`, layers raised by volume events get an
`intensity`; a `triggerOnEnd` chain becomes consecutive entries; `menu` / `match` and the `click` / `victory` /
`defeat` cues are filled), and `audio_events.json` - the event map (reference guid / asset -> key, volume
events, haptics; per key the values audio.json has no key for: channel, fades, stop / mute other channels).
A real FMOD Studio project needs File > Export Audio and a `--map` JSON.
Test: `tools/importers/fmod/test_import_audio.py` (CTest `uiwindows_audio_importer`).
