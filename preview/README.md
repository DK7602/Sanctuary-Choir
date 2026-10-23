# Browser preview

`python3 tools/preview/build_preview.py <dir>` copies the plugin UI plus these demo renders into a folder that runs
in any browser. Without the plugin, `ui/js/bridge.js` switches to a simulated engine.

The demo MP3s were made with `sanctuary_render --synth-demo` ("Amazing Grace" sung by the built-in
synthetic test voice, then learned and rendered by the engine):

- `demo-input.mp3`: the solo test voice
- `demo-ah.mp3`: `--learn --voice ah --style sacred --size 70`
- `demo-gospel.mp3`: `--learn --voice words --style gospel --harmony gospel --size 45 --reverb 40`
- `demo-massive.mp3`: `--learn --voice ah --style sacred --size 70 --energy 90 --sub 80` (the recorded choir at Massive energy, with oktavists)
