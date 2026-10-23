# The venues' measured rooms

`build_rooms.py` makes the reverb of each venue from a measured room: two impulse responses of a real hall (a source on the stage, two listening positions a few metres apart), from datasets on Zenodo under Creative Commons Attribution licences. It reads only the files it needs out of the datasets' zip archives (HTTP ranges) and caches them.

```bash
pip install numpy scipy soundfile     # and ffmpeg with libvorbis on the PATH
python3 tools/rooms/build_rooms.py /path/to/cache            # writes rooms/
python3 tools/rooms/build_rooms.py /path/to/cache /tmp/rooms --wav   # also float WAVs for sanctuary_render --rooms
```

| Venue | Measured room | Dataset | Licence | Decay |
|---|---|---|---|---|
| Grand Cathedral | Santi Marcellino e Pietro, Cremona: 21 m down the nave | ChurchIR ([14901751](https://zenodo.org/records/14901751)) | CC BY 4.0 | 3.8 s, lengthened to 5.0 s |
| Stone Abbey | St. Nicholas Chapel, Aachen Cathedral: source in the apse | [23312556](https://zenodo.org/records/23312556) | CC BY 4.0 | 4.3 s, as measured |
| Wooden Chapel | Brahmssaal, Detmold University of Music | [4116247](https://zenodo.org/records/4116247) | CC BY 4.0 | 1.25 s, lengthened to 1.5 s |
| Concert Hall | Giovanni Arvedi Auditorium, Cremona | [20098848](https://zenodo.org/records/20098848) | CC BY 4.0 | 1.5 s, lengthened to 2.1 s |
| Gospel Church | Santi Marcellino e Pietro, Cremona: 10 m down the nave | ChurchIR | CC BY 4.0 | 3.8 s, shortened to 1.7 s |
| Studio Room | BBC Maida Vale Studio 5 | [10020866](https://zenodo.org/records/10020866) | CC BY 3.0 | 0.37 s, as measured |

For each venue the script removes the direct sound (the dry choir is mixed separately), replaces the measurement's noise floor with each 1/6-octave band's own decay, stretches or shortens the decay band by band where the table says, extends the response far enough for the Decay knob at 250%, high-passes at 40 Hz and scales it to unit energy. It stores the result as Ogg Vorbis with its decay flattened (rising `emphasisDbPerSecond` dB a second, in `SOURCES.json`), so the encoder's noise falls with the decay once the plug-in takes the emphasis back out: the quiet end of the tail, which the Decay knob turns up, stays clean. The six rooms take about 600 KB.

`rooms/SOURCES.json` records, per venue, the dataset, record, licence, the archive members read and their SHA-256, the positions used and the processing. `rooms/CREDITS.txt` is the attribution shipped with the plug-in.

Admission rule, as for the voices: CC0, CC BY or public domain only, and when a dataset's own files state a stricter licence than its Zenodo page, the stricter one wins. That rules out, for now, the MusicSphere church measurements (St. Jakobus Ilmenau, Sint-Martinus Heers: their SOFA files say CC BY-NC-SA although Zenodo lists CC BY) and the WDR broadcast studios (their SOFA files say CC BY-SA), and OpenAIR could not be checked (its site's certificate had expired).

The plug-in builds each room for the session's sample rate on a background thread (`ConvolutionRoom` in the core: partitioned convolution, about 5 ms late, which the pre-delay absorbs). `getMeasuredRoomLevel()` in `core/src/Effects.cpp` sets each room's level to the algorithmic hall's for the same venue, so presets keep their balance; re-measure it if a room changes.
