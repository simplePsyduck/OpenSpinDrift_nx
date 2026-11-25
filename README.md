# OpenSpinDrift_nx

Nintendo Switch homebrew android wrapper for **[Mr. Matt's OpenSpinDrift (Sonic 3D Blast)](https://themrmatt.net/projects/openspindrift)**.
OpenSpinDrift is a native port of the Sega Saturn version of Sonic 3D Blast with widescreen, the Genesis version’s graphics and sound a button press away, and a Director’s Cut mode. 
It’s built from a matching decompilation of the Saturn game. The game’s own C code runs natively, the Saturn’s libraries and hardware are replaced with native code, and the parts that were hand-written assembly (the special stage, the movie decoder and the sound driver) are rewritten in C & built for Android. 

This "Nintendo Switch Port" is a small native Android shim that loads the original Android APK's own `libmain.so`, binds
its SDL2/GLES2/libc imports to the Switch build of SDL2 and mesa GLES2, and
calls its `SDL_main` entry point. The wrapper contains no game code and no game
data — you have to supply the APK game files & Saturn/MegaDrive ROMs from your own copies.

# Features
The whole game in one program: 
- the intro and ending movies, the special stages, the CD music and the Saturn sound effects
- Widescreen (16:9 and 16:10) and frame rates above 60 fps
- Screen filters, including a CRT TV look
- The Genesis version’s graphics, music, sound effects and special stages, if you have its ROM you can switch between the two at any time
- A Director’s Cut mode with the gameplay changes from Jon Burton’s Sonic 3D Blast: Director’s Cut
- Save slots, remappable controls and an in-game settings menu



## Install

Copy the `.nro` , your APK's `libmain.so` & `libSDL2.so` into the _OpenSpinDrift_nx_ folder on the SD card.

Your Sonic 3D Blast (USA) Sega Saturn ROM & optional Sonic 3D Blast (UE) Sega Genesis ROM go in the `/assets` subfolder
ROMs must be named `sonic3d-saturn-us.chd` and `sonic3d-gen.md` respectively

```
sdmc:/switch/OpenSpinDrift_nx/
├── OpenSpinDrift_nx.nro
├── libmain.so                    <- from your game files
├── libSDL2.so                    <- optional, see notes
├── assets/
│   ├── sonic3d-saturn-us.chd     <- your disc image (or -eu / -jp)
│   ├── sonic3d-gen.md            <- your disc image (or -eu / -jp)
│   └── ...                       <- the rest of the game's assets folder
├── save/                         <- created on first launch
└── controls.ini                  <- created on first launch (button remap)
```

Launch it from the homebrew menu, or install it with a forwarder/title
override. A title override or forwarder is recommended (it gives the game the
full memory budget; applet/album mode is much more limited).

The wrapper looks for `libmain.so` and `assets/` **in the same folder as the
`.nro`**. It tells the game that this folder is its storage folder, so
`assets/`, `save/`, `log.txt` and the game's config all live there.

## Notes

* The game was built against its own Android `libSDL2.so`. That library is a
  thin client of SDLActivity's Java glue, which does not exist here, so the
  wrapper does not load it: the standard SDL2 API is provided by the Switch
  build of SDL2 (same ABI, native window/HID/audio backend). Leaving
  `libSDL2.so` in the folder is harmless; it is simply unused.
* Face buttons use the Xbox layout the game expects by default (A/B and X/Y
  swapped from their Switch labels). Any button can be remapped in
  `controls.ini` next to the `.nro`; see Controls below.
* Save data is written to `save/` next to the `.nro`.
* `OpenSpinDrift_nx.log` next to the `.nro` records the wrapper's boot steps.
* If the wrapper itself hits a CPU fault, `crash.log` next to the `.nro` gets
  the faulting PC/LR with their offsets into `libmain.so`.
* If the CHD or ROM is missing, the game reports it on screen and in
  `log.txt`.

## Controls

`controls.ini` next to the `.nro` maps the Switch buttons to the Xbox buttons
the game uses. It is created on first launch with every button listed:

```ini
[controls]
a = b
b = a
x = y
y = x
back = minus
start = plus
guide = home
leftstick = l3
rightstick = r3
leftshoulder = l
rightshoulder = r
dpup = up
dpdown = down
dpleft = left
dpright = right
misc1 = capture
```

The left side is the **Xbox** button the game wants; the right side is the
**Switch** button (or buttons) that press it. The file's header repeats this
as a legend. Everything is one-to-one except A/B and X/Y, which are swapped so
the Switch face buttons sit in the same *position* as the Xbox labels on
screen (Switch B south presses A, Switch A east presses B).

Separate several Switch buttons with spaces or commas to let any of them press
the same Xbox button:

```ini
a = b, y
```

Use `none` to disable a game button (`a = none`); a Switch button that no
longer appears on any line is ignored. ZL/ZR are analog triggers the game
reads directly and are not remapped here. The file is read at launch;
malformed lines are reported in `OpenSpinDrift_nx.log` and keep that button's
built-in default. Keyboard bindings are not affected.

## Build

Requires devkitA64 and devkitPro packages:

```
pacman -S switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau
```

Then:

```
make
```

produces `OpenSpinDrift_nx.nro` (title `OpenSpinDrift`, author
`Mr. Matt & simplePsyduck`).

`make clean` removes the build output.

## Credits

The `.so` loader (`so_util`, `libc_shim`, `util`, `error`) derives from the
open-source Switch loader lineage by Andy Nguyen, fgsfds and contributors,
which builds on TheOfficialFloW's Vita/Switch work — MIT licensed (see
`LICENSE`). The SDL2 path binding, import table and wrapper entry point are new.

## Legal

This is a fan project and ships no game code and no game assets. Sonic 3D
Blast are trademarks of SEGA and we hold no affiliation. Provide your own legally
obtained game files. OpenSpinDrift is another fan project by [Mr. Matt](https://themrmatt.net/)
