# Glizzarine Harmonizer

Live-Harmonizer: Du singst ins Mikro und spielst auf dem Keyboard Akkorde. Jede gedrückte Taste wird zu einer eigenen Stimme aus deiner Stimme, genau auf der Tonhöhe der Taste (wie bei Jacob Collier).

## Bauen (GitHub Actions)

1. Neues GitHub-Repository anlegen, z. B. `GlizzarineHarmonizer`.
2. Alle Dateien hochladen. `build.yml` muss in `.github/workflows/build.yml` liegen.
3. Unter **Actions** läuft der Build automatisch. Am Ende bei **Artifacts** `GlizzarineHarmonizer-Windows` herunterladen.
4. Darin sind zwei Sachen:
   - `VST3/Glizzarine Harmonizer.vst3`: nach `C:\Program Files\Common Files\VST3\` kopieren
   - `Standalone/Glizzarine Harmonizer.exe`: läuft ohne DAW

## Schnellster Test: Standalone

1. `Glizzarine Harmonizer.exe` starten, oben auf **Options → Audio/MIDI Settings**.
2. Audio-Gerät: dein Interface (am besten **ASIO**), Eingang = Mikro-Kanal, Ausgang = Kopfhörer.
3. Unter **Active MIDI inputs** die KeyLab anhaken.
4. Singen und Akkorde spielen.

## In Cakewalk Sonar

1. **Treiber:** Edit → Preferences → Audio → Playback and Recording → Driver Mode **ASIO**. Notfalls WASAPI Exclusive, nicht Shared (zu viel Verzögerung). Puffer klein (64–128 Samples).
2. **Gesangsspur:** Audiospur anlegen, Eingang = Mikro, **Input Echo** (Monitoring) an.
3. In die FX-Box dieser Spur **Glizzarine Harmonizer** einfügen.
4. **Keyboard-Spur:** MIDI-Spur anlegen, Eingang = KeyLab.
5. Den **Ausgang** der MIDI-Spur auf **Glizzarine Harmonizer** stellen. MIDI-fähige Effekte erscheinen dort in der Ausgangs-Liste.
6. Auch bei der MIDI-Spur **Input Echo** an, dann singen und spielen.

## Neu in v2.0

- **Chord Glide** (MAIN, unten): Beim Akkordwechsel gleitet jede Stimme zum nächstliegenden Ton des neuen Akkords (kleinste Gesamtbewegung). Überzählige Stimmen blenden aus oder ein. Glide Time 0 = aus, bis 2 s.
  - **Legato:** gleitet nur, wenn du den neuen Akkord greifst, bevor du den alten loslässt (oder mit Pedal).
  - **Always:** gleitet auch aus ausklingenden Stimmen heraus.
  - Einen einzelnen Ton zu einem gehaltenen Akkord dazunehmen (z. B. die 7) klaut keine Stimme.
  - Gemeinsame Töne einfach liegen lassen, dann bleiben sie stehen.
  - Funktioniert mit Pedal, Pyramide und Vocoder. Beim Arp ist es bewusst aus.
- **Voicing** (MAIN): Close, Bass ↓ (tiefster Ton eine Oktave tiefer dazu), Top ↑ (höchster eine Oktave höher dazu), Bass + Top, Spread (jede 2. Stimme von oben eine Oktave tiefer). Die zusätzlichen Stimmen sind leiser und breiter.
- **Humanize** (VOICE): Jede Stimme driftet leicht in der Tonhöhe, kommt bis zu 30 ms versetzt, hat ihr eigenes Vibrato und einen etwas anderen Pegel. Klingt nach Chor statt nach Kopie.
- **Konsonanten & Atem** neu: S/T/K und Atem (Anteil über 3 kHz) liegen jetzt auf allen Stimmen, jede mit eigenem kleinen Versatz und Stereo-Platz, auch während du singst. Regler: Consonants.
- **6 Werks-Presets**, pegelmäßig abgeglichen: Harmonizer Clean, Collier Choir, Wide Choir, Pyramid, Vocoder Pad, Shimmer Cathedral.
- **Eigene Presets:** SAVE oben rechts → Name → Datei in `Dokumente\Glizzarine Presets\*.glzpreset`. In der Preset-Liste stehen Werks- und eigene Presets getrennt. Gleicher Name = überschreiben (mit Rückfrage). Löschen und „Open preset folder“ ganz unten in der Liste. Dein Setup (Input, Gate, Sensitivity, Lowest Note, Latency, MIDI Out, Solo Voice, Output) gehört nicht zum Preset.
- **MIDI Out** (VOICE): Off / Melody / + Harmonies. Melodie = dein gesungener Ton als MIDI-Note auf **Kanal 1** (ohne Flattern, Note-Off in Pausen). Harmonien = die Töne der Stimmen auf **Kanal 2**.
- **Latency** (VOICE): Normal / Low. Low: bei Lowest Note A2 ca. 11 ms, bei E2 ca. 14 ms (statt 17/23 ms).
- **Stabilität:** Mit AddressSanitizer, UBSan und ThreadSanitizer getestet (Fuzz, Export-Simulation bis 32768er Blöcke und 192 kHz, 2-Minuten-Soak mit offenem Editor). Behoben: Filter wurden bei niedrigen Samplerates instabil, Robot-Pad lief nach Stunden über, Latenz wird nicht mehr aus dem Audio-Thread gemeldet, Engine liest nie mehr Samples, die noch nicht da sind.

### MIDI Out in Sonar
1. Im Plugin-Fenster oben im **VST3**-Menü **„Enable MIDI Output“** anhaken (bei Effekten ist es standardmäßig aus; falls ausgegraut: Plugin neu scannen).
2. Neue MIDI-Spur → **Input = Glizzarine Harmonizer** (Kanal 1 = Melodie, Kanal 2 = Harmonien, oder Omni).
3. Ausgang der MIDI-Spur auf einen Synth → aufnehmen oder live mitspielen.

## Neu in v1.2

- Komplett neues Design „Dark Clean“ (alles auf Englisch): 7 Tabs MAIN, VOICE, VOCODER, ARP, EFFECTS, PADS, AT & VEL.
- Regler gleiten weich zum Wert, ziehen eine dezente Spur und leuchten kurz auf – auch wenn du an der KeyLab drehst. Rechtsklick = MIDI Learn.
- MAIN: 8 große Regler (E1–E8) zum Zuweisen an die 8 KeyLab-Encoder, Anzeige der klingenden Stimmen, Eingang und Aftertouch.
- Neue Klangregler: Filter Resonance + On/Off, Tremolo Shape, Lo-Fi Bandwidth, Reverb Damping + Pre-delay, Shimmer Interval (+12/+7/+19/+24/-12), Decay, Tone, Velocity Curve.
- KeyLab-Encoder verbinden: Rechtsklick auf E1 → MIDI Learn → Encoder 1 drehen. Für alle 8 einmal machen, wird im Projekt gespeichert.

## Neu in v1.1

- Stabiler: Die Tonerkennung braucht nur noch ein Zehntel der Rechenzeit (keine Lastspitzen mehr, die in Sonar Aussetzer auslösen). Dazu Selbstheilung: Ein kaputter Wert kann den Sound nicht mehr abwürgen; links steht dann orange „repaired: Nx“.
- Solo Voice steht standardmäßig auf 0 und wird von Presets nicht mehr verändert.
- Sensitivity neu (0–100 %): höher = leises, hauchiges Singen wird erkannt. Deutlich hörbar.
- Noise Gate neu: ganz links = aus, aufdrehen bis das Raumrauschen zwischen den Phrasen weg ist.
- Weniger Leveling, mehr Konsonanten als Standard: mehr Details in den Stimmen.
- Arp: Loop Length steht auf „All“, das Muster läuft durch alle Oktaven. Eine Zahl = Schleifenlänge.

## Neu in v1.0

- Versionsnummer oben links.
- Klicks beim Anschlagen und Ausklingen behoben (Hauptursache: alte Grain-Reste kamen 0,19 s später wieder hoch; dazu ein Sprung im Stereo-Delay bei Arp/Pyramide).
- Abstürze: alle Effekte bekommen das Audio jetzt in festen Stücken von max. 1024 Samples, egal welche Puffergröße Sonar schickt. Zwei Instanzen parallel laufen getrennt.
- Eingang empfindlicher (Sensitivity bis -85 dB, Soft Gate bis -90 dB, nachsichtigere Tonerkennung).
- Vocoder-Schicht mit 5 Klängen, stufenlos mit dem Harmonizer mischbar.
- Arp/Pyramide: 9 Reihenfolgen, eigener Klang pro Note (Pluck/Legato/Swell), eigenes Release, damit der Akkord-Release nichts zuschmiert. Betonung und Stereo-Platz pro Stimme, damit man jeden Einsatz hört.
- 12 Pad-Effekte für die KeyLab-Pads (mit Learn).
- MIDI-Learn: Rechtsklick auf jeden Regler.

## Seiten

**Links (immer):** Pegel, Gesang erkannt, Ton, Tasten, Latenz, Tempo, Aftertouch-Balken, TAP.

- **MAIN:** Klingende Stimmen, Input/Aftertouch, 8 Regler E1–E8 für die KeyLab-Encoder (Blend, Harmonies, Env Filter, Filter, Motion, Delay, Reverb, Shimmer). Unten Chord Glide (Zeit + Always/Legato) und Voicing.
- **VOICE:** Input Gain, Sensitivity, Noise Gate, tiefster Ton, Solo Voice Timing, MIDI Out, Latency. Rechts Formant, Consonants, Follow Pitch, Leveling, Warmth, Width, Octave Down, Humanize, Attack, Release, Pedal Sustain, Solo Voice, Output.
- **VOCODER:** Klang (Saw Pad, Strings, Glass, Hollow Pulse, Noise Choir), Formant, S/T-Laute, Glätte, Breite.
- **ARP:** Mode (Off/Pyramid/Arp), Order (Up, Down, Up-Down, Down-Up, Crawl, Outside-In, Inside-Out, Random, As played), Rate, Steps, Octaves, Note Length, Style, Arp Release, Stereo Spread, Tempo.
- **EFFECTS:** Envelope Filter, Lowpass, Tremolo, Lo-Fi, Chorus, Phaser, Delay, Reverb, Shimmer. Alles außer Delay/Reverb nur auf den Harmonien.
- **PADS:** Freeze, Stutter 1/8, Stutter 1/16, Reverse, Tape Stop, Octave Up, Octave Down, Granular Cloud, Robot, Riser, Glide, Chaos. Standard: Pads auf MIDI-Kanal 10 ab C1 (Note 36). Sonst LEARN klicken und Pad drücken. LATCH = einmal tippen an, nochmal aus.
- **AT & VEL:** Aftertouch (Druck nach dem Anschlag: lauter, Shimmer, Hall, Filter, Vibrato, Detune, Formant) und Velocity (Lautstärke, Helligkeit, weicher Einsatz, Echo-Send).

## Aufnehmen in Sonar

Am besten die trockene Stimme und die MIDI-Spur aufnehmen, den Harmonizer als Effekt drauflassen. Dann kannst du später Akkorde und Sound noch ändern. Wenn es fertig ist: Spur einfrieren (Freeze) oder bouncen.

## Zwei Harmonizer parallel

Gesangsspur → zwei Sends auf zwei Busse. Auf jeden Bus einen Glizzarine Harmonizer. Für jeden eine eigene MIDI-Spur, deren Ausgang auf die jeweilige Instanz zeigt. Bei der Gesangsspur selbst den Ausgang leise oder Solo Voice in einer Instanz auf 0, sonst ist die Stimme doppelt.

## Presets

Harmonizer Clean · Collier Choir · Wide Choir · Pyramid · Vocoder Pad · Shimmer Cathedral, dazu deine eigenen.

## Tests (für Entwickler)

```
cmake -B build-tests -DGLZH_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --target qa glide bounce fuzz soak screenshots
cmake -B build-asan -DGLZH_TESTS=ON -DGLZH_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo   # ASan + UBSan
```
`glide` (V2-Engine), `qa` (jeder Regler, Presets, MIDI Out, Samplerates/Blockgrößen), `bounce [s]` (Export-Simulation), `fuzz <s> <seed>`, `soak [s]` (Audio + Editor parallel), `screenshots`. Die GitHub-Action baut das Plugin und lässt glide, qa, bounce und fuzz auch unter Windows laufen.
