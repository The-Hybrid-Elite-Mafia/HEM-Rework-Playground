# HEM Rework Playground

Steven Reliford's Windows mixing playground (Hybrid Elite Mafia): a 13-track stem mixer with the
nine Elite plug-ins (Vocalis, EQ, Comp, Bass, Channel, Verb, Delay, Swirl Killer, Master),
Band Forge (a band written and played on this computer), Song Forge (local ACE-Step), a private
Library, and WAV export. Built with JUCE 8.0.15 (C++20) for Windows x64.

Moved here from the `playground` branch of hybrid-studio-android (0.6.0, commit 7db6c1f) so it
builds on its own.

## Build (Windows, Visual Studio 2022, CMake 3.22+)
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target ReworkPlayground PlaygroundTests
```
The cloud build (`.github/workflows/windows.yml`) also builds the Band Forge sound pack from CC0
sample libraries at fixed commits, runs every unit test and makes the Inno Setup installer.

No passwords, API keys or signing keys are in this repository; the app and installer are unsigned.
