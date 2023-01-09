#wasm-objdump -x -j Export build-emscripten/scummvm.wasm > dists/emscripten/main-module-exports.txt
#wasm-objdump -x -j Global build-emscripten/scummvm.wasm > dists/emscripten/main-module-exports.txt
wasm-objdump -x -j Function build-emscripten/scummvm.wasm > dists/emscripten/main-module-exports.txt
cat dists/emscripten/main-module-exports.txt


python3 dists/emscripten/main-module-exports.py