
# Building ScummVM for Webassembly
The [Emscripten](https://emscripten.org/) target provides a script to build ScummVM as a single page browser app.

## Goals
This port of ScummVM has two primary use cases as its goals:

- **Demo App**: The goal of this use case is to provide an easy way for people to discover ScummVM and old adventure games. Game preservation is not just about archival but also accessibility. The primary goal is to make it as easy as possible to play any game which can legally be made available, and there's probably nothing easier than opening a webpage to do so.

- **ScummVM as a PWA** (progressive web app): There are platforms where native ScummVM is not readily available (primarily iOS/iPadOS). A PWA can work around these limitations. To really make this work, a few more features beyond what's in a Demo App would be required: 
  * Offline Support: PWAs can run offline. This means we have to find a way to cache some data which is downloaded on demand (engine plugins, game data etc.) 
  * Cloud Storage Integration: Users will have to have a way to bring their own games and export savegame data. This is best possible through cloud storage integration. This already exists in ScummVM, but a few adjustments will be necessary to make this work in a PWA.
  
See [chkuendig/scummvm-demo](http://github.com/chkuendig/scummvm-demo/) on how a ScummVM demo app can be built (incl. playable demo).
  
## About Webassembly and Emscripten
Emscripten is an LLVM/Clang-based compiler that compiles C and C++ source code to WebAssembly for execution in web browsers. 

**Note:** In general most code can be cross-compiled to Webassembly just fine. There's a few minor things which are different, but the mayor difference comes down to how instructions are processed: Javascript and Webassembly do support asynchronous/non-blocking code, but in general everything is running in the same [event loop](https://developer.mozilla.org/en-US/docs/Web/JavaScript/EventLoop). This means also that Wwebassembly code has to pause for the browser to do it's operations - render the page, process inputs, run I/O and so on. One consequence of this is that the page is not re-drawn until the Webassembly code "yields" to the browser. Emscripten provides as much tooling as possible for this, but there's sometimes still a need to manually add a call to sleep into some engines.

## How to build for Webassembly
This folder contains a script to help build scummvm with Emscripten, it automatically downloads the correct emsdk version and also takes care of bundling the data and setting up a few demo games.

### Running build.sh

`build.sh` needs to be run from the root of the project. 
```Shell
./dists/emscripten/build.sh [Tasks] [Options]
```

**Tasks:** space separated list of tasks to run. These can be:  
* `build`: Run all tasks to build the complete app. These tasks are:
* `configure`: Run the configure script with emconfigure with the recommended settings for a simple demo page 
* `make`: Run the make scripts with emmake
* `dist`: Copy all files into a single build-emscripten folder to bring it all together
* `clean`: Cleanup build artifacts (keeps libs + emsdk in place)
* `run`: Start webserver and launch ScummVM in Chrome  
  
**Options:**
*  `-h`, `--help`: print a short help text
*  `-v`, `--verbose`: print all commands run by the script
*  `--*`: all other options are passed on to the scummvm configure script

Independent of the command executed, the script sets up a pre-defined emsdk environment in the subfolder `./dists/emscripten/build.sh`

**Example:**

See e.g. [chkuendig/scummvm-demo/.github/workflows/main.yml](https://github.com/chkuendig/scummvm-demo/blob/main/.github/workflows/main.yml) for an example on how do deploy this.

## Current Status of Port
In general, ScummVM runs in the browser sufficiently to run all demos and freeware games.

* All engines compile (though I didn't test all of them), including ResidualVM with WebGL acceleration and shaders and run as plugins (which means the initial page load is somewhat limited)
* Audio works and 3rd-party libraries for sound and video decoding are integrated.
* All data can be downloaded on demand (or in the case of the testbed generated as part of the build script)

## Known Issues + Possible Improvements

### Emscripten Asyncify Optimizations
ScummVM relies heavily on Asyncify (see note above), and this comes with a quite heavy performance penalty. Possible optimizations in this regard could be:
*   Specify a `ASYNCIFY_ONLY` list in `configure` to  make asyncify only instrument functions in the call path as described in [emscripten.org: Asyncify](https://emscripten.org/docs/porting/asyncify.html)
*   Limit asyncify overhead by having a more specific setting for `ASYNCIFY_IMPORTS` in `configure`. This is especailly critical for plugins as when plugins are enabled, we currently add all functions as imports. 
*   Don't use asyncify but rewrite main loop to improve performance.
*   Look into Stack Switching (emscripten-core/emscripten#16779) or multithreading as an alternative to Asyncify.

### MIDI/MT32 Sound Options
MIDI currently works a few ways:
- Fluidlite can be used as a software synthesizer (using the Roland_SC-55 soundfont by default)
- Integrated Yamaha OPL emulators (DosBOX, Nuked etc.) work fine 
- Roland MT-32 emulation works once MT32 roms are provided (can be drag&dropped into the browser window)
### Storage Integration
*   BrowserFS seems abandoned and never did a stable 2.0.0 release. It's worth replacing it.  
    * `scummvm_fs.js` is an early prototype for a custom FS which can be adopted for ScummVM specific needs, i.e.
      * Download all game assets in background once the game has started
      * Persist last game and last plugin for offline use
      * Pre-load assets asynchronously (not blocking) - i.e. rest of the data of a game which has been launched
      * Loading indicators (doesn't work with the current synchronous/blocking filesystem)
*   Add support for save games (and game data?) on personal cloud storage (Dropbox, Google Drive).

Emscripten is currently re-doing their filesystem code, which could help address some of the above issues ( emscripten-core/emscripten#15041 ).

### UI Integration
*   Build a nice webpage around the canvas.
    *   Allow showing/hiding of console (at the moment there's only the browser console)
    *   Bonus: Adapt page padding/background color to theme (black when in game)
*   Automatically show console in case of exceptions
* 🐞 Aspect Ratio is broken when starting a game until the window is resized once. Good starting points might be  https://github.com/emscripten-ports/SDL2/issues/47 or https://github.com/emscripten-core/emscripten/issues/10285
    * doesn't seem to affect 3D engines in opengl mode
    * definitely affects testbed in OpenGL or other modes