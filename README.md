# MTRX-1K

A VST3 instrument and standalone app for Windows that plays and edits the sounds of a classic six-voice analogue synthesizer module of 1987, the Oberheim Matrix-1000, by running its original firmware on an emulated processor board that drives six models of its voice chip.

MTRX-1K is made by **GPR Music Project**. It is a modified version of [Retromulator](https://github.com/reales/retromulator) by discoDSP, reduced to its Matrix-1000 core and extended with a patch editor. It is free software under the GNU General Public License v3.

MTRX-1K is not affiliated with, endorsed by or connected to Oberheim. Oberheim and Matrix-1000 are trademarks of their owners and are named here only to say what the software emulates.

## What it does

- Runs the original firmware (version 1.20) on an emulated 6809 board: timers, UART, sample and hold DAC, latches. The firmware calibrates the voices itself on the first start, and the factory banks are read out of the patch EPROM; a panel shows the progress of both. The results are kept, so later starts are immediate.
- Six voice models of the CEM3396 chip with band-limited oscillators, the three sync modes of the voice board, and 2x oversampling.
- A rack panel with display, keypad, bank lock and patch selection as on the instrument, in a black or a white finish.
- A patch editor the instrument never had: every parameter of a patch on one page, with the ten buses of the modulation matrix, envelope and tracking graphs, and the real depth of a modulation amount shown under its knob.
- Init, Revert and Save: a patch can be written to a position of the RAM bank or to a bank of your own. The RAM bank is copied to a backup folder before its first change.
- The 800 factory patches of banks 2 to 9 are read from the patch EPROM into bank files with their factory names.
- Every patch parameter is a host parameter that can be automated.
- MIDI learn: any patch parameter, the buses of the modulation matrix included, is assigned to a control of your keyboard by clicking it and moving the control. Whether the control sends control changes or NRPNs, and with 7 or 14 bit values, is taken from what arrives. Sets of assignments are kept as named profiles that can be saved, changed and deleted, and exported to a JSON file and imported from one, to keep them or pass them on. The map the plugin always had, one control change per parameter, is the profile "Standard"; "None" assigns nothing.
- 6, 12 or 24 voices, picked with the Voices selector of the editor. The firmware is not changed: for 12 or 24 voices MTRX-1K runs two or four complete instruments, firmware and voice board each, the way Matrix-1000 modules chained over MIDI play, and gives every note to one of them; everything else reaches all of them. In unison the first one plays alone, as a single Matrix-1000 does. The setting belongs to the instance and is saved with it. The added instruments are rendered on threads of their own beside the host's audio thread, so the audio thread carries no more than with six voices and the latency stays the same; each instrument costs about 9% of one core of an i9-13900H.
- Both LFOs can follow the host tempo. A cycle of the firmware's LFO lasts a whole number of steps of 26.6 ms, so a division is met within a few percent, not exactly.
- A MIDI program change selects that position of the loaded bank.
- A window that can be resized, from 60% to 200% of its normal size, by its bottom right corner or the edges of the host's window. The layout keeps its proportions and is drawn again at the new size, so legends, knobs and the brushed panel stay sharp; the size is remembered, and a window that would not fit on the screen when the editor opens is made smaller.
- A Manual key on the panel opens the user manual, a PDF that the setup program installs next to the standalone app.

Where the original moves a pitch or a control voltage in audible steps, about 75 times a second, MTRX-1K joins the steps. Everything else is meant to behave as the instrument does, firmware quirks included: the LFOs are one per voice, portamento glides from the note a voice played last, and the keyboard mode "Reassign Rob" does not rob a voice in firmware 1.20.

## Status

Version 0.9.4, October 2026. Built and tested on Windows 11 as a VST3 in Cubase 15 and as a standalone app. The build files for other systems come from Retromulator and have not been tried with this version.

## Firmware

The firmware is not included and is not part of this repository. You need your own copy of:

- the 32 KB system EPROM of the instrument, firmware version 1.20;
- the 64 KB patch EPROM, for the factory patches of banks 2 to 9 (optional).

Put the two `.bin` files in `Documents\GPR Music Project\MTRX-1K\ROM`. They are recognised by their size and content, not by their name. The banks are kept next to that folder, in `Documents\GPR Music Project\MTRX-1K\Patch`, and the MIDI learn profiles in `Documents\GPR Music Project\MTRX-1K\MIDI`.

## Building on Windows

You need Visual Studio with the C++ desktop workload and its CMake tools (CMake 3.22 or later and Ninja).

```
git clone --recurse-submodules https://github.com/gprmusicproject/mtrx-1k
cd mtrx-1k
build_win.bat
```

JUCE 8.0.15 comes as a submodule in `Modules/JUCE`. The plugin and the app are written to `build\win-release\Retromulator_artefacts\Release`, as `VST3\MTRX-1K.vst3` and `Standalone\MTRX-1K.exe`. To install the plugin, copy the `MTRX-1K.vst3` folder to `C:\Program Files\Common Files\VST3` while the host is closed.

## The setup program

`installer\mtrx-1k.iss` is the script of a setup program for Windows, made with [Inno Setup](https://jrsoftware.org/isinfo.php) 6:

```
ISCC.exe installer\mtrx-1k.iss
```

It takes the built app and plugin from `build\win-release` and the user manual, `MTRX-1K_User_Manual_v<version>.pdf`, from the root of the checkout, and writes `build\installer\out\MTRX-1K_<version>_Setup.exe`. The setup puts the standalone app and the manual in `Program Files\GPR Music Project\MTRX-1K`, the plugin in `Program Files\Common Files\VST3\GPR Music Project`, and makes the empty folders `ROM`, `Patch` and `MIDI` in `Documents\GPR Music Project\MTRX-1K`. It includes no firmware and no sounds. MTRX-1K is listed among the installed apps of Windows and removed from there; the uninstaller asks whether MIDI profiles and sounds are to be removed too. `/DTestRoot=<folder>` builds a setup that needs no administrator and installs under that folder, for trying it out. The pictures of the wizard are drawn by `installer\make_images.ps1`.

## What was changed

This is a modified version of Retromulator 1.8.0 (commit `c20db50` of github.com/reales/retromulator), changed by GPR Music Project in October 2026. The history of this repository shows every change. In short:

- **Build**: a CMake project for Windows (`CMakeLists.txt`, `cmake/`, `build_win.bat`) next to the Projucer project of Retromulator.
- **Product**: name, vendor and plugin identifiers of MTRX-1K; only the Matrix-1000 core is started. The other cores of Retromulator are still in the source tree and are compiled in.
- **Interface** (`source/plugin`): the rack panel (`RackPanel`), the patch editor (`EditPanel`), saving and bank handling, the session state of the editor, automatable parameters, program change, and the MIDI learn with its profiles (`MidiLearn`), and the scaling of the whole window.
- **Emulation** (`source/matrixLib`): the three sync modes after the schematic of the voice board; the timer writes with which the firmware moves a pitch; joined pitch and control voltage steps; the height of the oscillator ramp through a pitch change; the DCO click; pacing of remote parameter edits so that held notes follow a knob; portamento rate 0 in unison; the LFO rates used for the tempo sync; two or four instruments side by side for 12 or 24 voices.

## The name and the symbol

"GPR Music Project", "MTRX-1K" and the GPR symbol drawn on the panel identify this product and its author. The licence of the code lets you change and redistribute the code; it does not give you the right to present a changed version as coming from GPR Music Project. If you publish a modified version, give it your own name and symbol.

## Where the code comes from

The two parts below are taken unchanged from the README of Retromulator 1.8.0, whose code this repository contains.

### How Retromulator differs from Gearmulator

Retromulator is built on top of the open-source emulation engines from [Gearmulator](https://github.com/dsp56300/gearmulator) by dsp56300. Gearmulator ships as standalone applications and open-source plugins built with CMake. Retromulator packages the same engines into a polished single-plugin experience using JUCE, with a unified rack-style UI, DAW state persistence, bank/patch browsing, focused on preset playing.

The emulation cores (dsp56300, mc68k, h8s, synthLib and all synth-specific libraries) are from Gearmulator. The DX7 emulation is ported from VDX7, a separate project (see Credits below). The OPL3 emulation uses Nuked OPL3 by Nuke.YKT. The SID emulation uses reSID by Dag Lem. The AY-3-8910 / YM2149 emulation uses Ayumi by Peter Sovietov. The JE-8086 and SC-88 family emulations are by Giulio Zausa. The Tracker core uses the Fasttracker 2 clone replayer by Olav Sorensen and the Schism Tracker player. The Akai S1000 sampler uses the SFZero module, an MIT-licensed JUCE sample engine maintained by discoDSP. The Wurlitzer 200A (OpenWurli) is a physical model fully ported by discoDSP. The Matrix-1000 emulation (6809 board and CEM3396 voice model) is by discoDSP. The synthLib device adapters that wrap each engine (SID, AY-3-8910, OpenWurli, OPL3 and Tracker), the bank and patch handling, the offline WAV / MP3 renderer and the host parameter pool are by discoDSP.

### Credits of Retromulator

- **[dsp56300](https://github.com/dsp56300)** — DSP56300 emulator, Virus TI / microQ / XT / Nord N2X engines, GPL v3
- All contributors to [github.com/dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
- **Giulio Zausa** — Roland JP-8000 (JE-8086) and SC-88 family emulation
- **chiaccona** — [VDX7](https://github.com/chiaccona/VDX7), cycle-accurate Yamaha DX7 emulation (HD6303R CPU, EGS, OPS), GPL v3
- **Nuke.YKT** — [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3), cycle-accurate YMF262 emulation, LGPL v2.1
- **Dag Lem** — [reSID](https://en.wikipedia.org/wiki/ReSID), cycle-accurate MOS 6581 / 8580 SID emulation, GPL v2
- **Lasse Öörni / Cadaver** — [GoatTracker](https://sourceforge.net/projects/goattracker2/) `.sng` / `.ins` format reference, GPL v2
- **Olav Sorensen** — [Fasttracker 2 clone](https://github.com/8bitbubsy/ft2-clone), XM / MOD replayer, BSD 3-Clause
- **Schism Tracker contributors** — [Schism Tracker](https://github.com/schismtracker/schismtracker), S3M / IT player, GPL v2
- **Peter Sovietov** — [Ayumi](https://github.com/true-grue/ayumi), AY-3-8910 / YM2149 emulation, MIT license
- **Timothy Lamb** — [Ym2149Synth](https://github.com/trash80/Ym2149Synth), voice engine and soft envelope firmware, GPL v3
- **Dominic Mazzoni** — [libresample](https://github.com/minorninth/libresample), sinc resampling, BSD 3-Clause / LGPL dual
- **Jean-Marc Valin** — [Speex](https://www.speex.org/) arbitrary-rate resampler, BSD 3-Clause
- **Steve Folta** — original [SFZero](https://github.com/stevefolta/SFZero) SFZ/SF2 sample player, MIT license
- **Leo Olivers** — SFZero JUCE module port
- **discoDSP** — [SFZero v3.0.0](https://github.com/reales/retromulator/tree/main/Modules/SFZero), 8-point sinc interpolation, Bliss format, extended opcode support, MIT license; synthLib device adapters, bank and patch handling, offline WAV / MP3 render and host parameter pool, Oberheim Matrix-1000 emulation, GPL v3
- **Joshua Price** — [OpenWurli](https://github.com/hal0zer0/openwurli) Wurlitzer 200A physical model

JUCE integration, UI, AAX/AU/VST3 plumbing is developed separately.

## License

MTRX-1K as a whole, with the changes made by GPR Music Project, is licensed under the **GNU General Public License v3.0**, see [LICENSE.txt](LICENSE.txt). It comes with no warranty. JUCE, in `Modules/JUCE`, is used under the **GNU Affero General Public License v3.0**, see its own LICENSE.md. The standalone app on Windows talks to ASIO drivers through the ASIO headers that come with JUCE, used under the **GPL v3**, one of the two licences Steinberg gives them under. ASIO is a trademark and software of Steinberg Media Technologies GmbH.

From the README of Retromulator, for the parts it contains ("this repository" there means Retromulator):

The emulation engine source code in this repository is licensed under the **GNU General Public License v3.0** — see [LICENSE.txt](LICENSE.txt). The SFZero module is licensed under the **MIT License** — see [Modules/SFZero/LICENSE](Modules/SFZero/LICENSE). The Nuked OPL3 engine is licensed under the **GNU Lesser General Public License v2.1**. The reSID engine is licensed under the **GNU General Public License v2.0**. The Ayumi engine is licensed under the **MIT License** — see [source/ayumiLib/AYUMI_LICENSE.txt](source/ayumiLib/AYUMI_LICENSE.txt). The Fasttracker 2 replayer is licensed under the **BSD 3-Clause License** — see [source/trackerLib/ft2/LICENSE](source/trackerLib/ft2/LICENSE). The Schism Tracker player is licensed under the **GNU General Public License v2.0** — see [source/trackerLib/schism/COPYING](source/trackerLib/schism/COPYING). The libresample engine is licensed under the **BSD 3-Clause License** — see [source/libresample/LICENSE-BSD.txt](source/libresample/LICENSE-BSD.txt). The Speex resampler is licensed under the **BSD 3-Clause License**. The LAME MP3 encoder is licensed under the **GNU Lesser General Public License v2** — see [source/lameLib/COPYING](source/lameLib/COPYING).
