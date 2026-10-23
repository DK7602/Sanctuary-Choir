# Browser preview

`python3 tools/preview/build_preview.py <dir>` copies the plugin UI plus these demo renders into a folder that runs
in any browser. Without the plugin, `ui/js/bridge.js` switches to a simulated engine.

The demo MP3s were made with `sanctuary_render --synth-demo` ("Amazing Grace" sung by the built-in
synthetic test voice, then learned and rendered by the engine). The recorded choir ones add
`--voices <dir>`: Sanctuary Voices decoded to WAV (the render tool reads WAV only), or the
voice pack builder's output. All of them add `--rooms <dir>`, the venues' measured rooms as WAV
(`tools/rooms/build_rooms.py <cache> <dir> --wav`): the Grand Cathedral unless a venue is given.

- `demo-input.mp3`: the solo test voice
- `demo-ah.mp3`: `--learn --voice ah --style sacred --size 70`
- `demo-gospel.mp3`: `--learn --voice words --style gospel --harmony gospel --size 45 --reverb 40 --venue gospel`
- `demo-passes.mp3`: the same with `--pass demo --pass demo --pass demo` (the choir singing from four passes)
- `demo-gospel-plain.mp3`: the same with `--real-voices 0` (every singer your own voice)
- `demo-massive.mp3`: `--learn --voice ah --style sacred --size 70 --energy 90 --sub 80` (the recorded choir at Massive energy, with oktavists)
- `demo-children.mp3`: `--learn --voice ah --style sacred --size 50 --reverb 65 --lead -60 --ensemble children` (the children's choir alone)
- `demo-kyrie.mp3`: `--words "Kyrie eleison, Christe eleison, Kyrie eleison" --size 80` (typed words on MIDI chords; the recorded singers sing the consonants they have)
