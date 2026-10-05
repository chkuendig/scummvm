// Regenerates dists/emscripten/plugin-asyncify-imports.json — the narrowed
// ASYNCIFY_IMPORTS list used for emscripten plugin (SIDE_MODULE) links.
//
// The list = { main-module functions that can suspend (per -sASYNCIFY_ADVISE) }
//          ∩ { direct imports of any plugin }  ∪  { invoke_* }  ∪  { existing list }
//
// Indirect (virtual) calls are instrumented by asyncify regardless, so this
// only needs to cover DIRECT import calls into suspend-capable main functions.
// The result is UNIONED with the existing checked-in list: single advise
// harvests have been observed to omit real suspend paths (e.g.
// Common::File::read missing from one otherwise-fine harvest), and a missing
// entry corrupts asyncify state at runtime, while an extra entry only costs a
// little plugin size. To intentionally shrink the list, delete the JSON first.
//
// Matching is alias-aware: binaryen's duplicate-function elimination folds
// identical bodies (observed: ~1600 multi-alias export groups), and the advise
// log only names the surviving function - so advise names are resolved through
// the main wasm's name section to function indices, and ALL export aliases of
// an instrumented index are considered suspend-capable.
//
// Invoked by: ./dists/emscripten/build.sh asyncify-imports
// Usage: node gen-asyncify-imports.js <adviseLog> <mainWasm> <pluginDir> <cxxfilt> <outJson>
"use strict";
const fs = require("fs");
const { execSync } = require("child_process");

const [, , adviseFile, mainWasm, pluginDir, cxxfilt, outFile] = process.argv;
if (!outFile) {
	console.error("usage: node gen-asyncify-imports.js <adviseLog> <mainWasm> <pluginDir> <cxxfilt> <outJson>");
	process.exit(1);
}

// --- parse main wasm: export table (name -> func idx) + name section (idx -> internal name)
function parseMainWasm(file) {
	const buf = fs.readFileSync(file);
	let p = 8;
	function leb() { let r = 0, s = 0, b; do { b = buf[p++]; r |= (b & 127) << s; s += 7; } while (b & 128); return r >>> 0; }
	function str() { const n = leb(); const s = buf.toString("utf8", p, p + n); p += n; return s; }
	const exportsByIdx = new Map(), idxByInternalName = new Map();
	while (p < buf.length) {
		const id = buf[p++], size = leb(), end = p + size;
		if (id === 7) { // export section
			const n = leb();
			for (let i = 0; i < n; i++) { const nm = str(); const kind = buf[p++]; const idx = leb();
				if (kind === 0) { if (!exportsByIdx.has(idx)) exportsByIdx.set(idx, []); exportsByIdx.get(idx).push(nm); } }
		} else if (id === 0) { // custom section
			const nm = str();
			if (nm === "name") {
				while (p < end) { const sub = buf[p++], ssz = leb(), send = p + ssz;
					if (sub === 1) { const cnt = leb(); for (let i = 0; i < cnt; i++) { const idx = leb(); const fn = str(); idxByInternalName.set(fn, idx); } }
					p = send; }
			}
			p = end;
		} else p = end;
	}
	return { exportsByIdx, idxByInternalName };
}

// 1. Parse the advise log; unescape binaryen's \XX hex escapes in names.
const advise = new Set();
for (const line of fs.readFileSync(adviseFile, "latin1").split("\n")) {
	const m = /^\[asyncify\] (.*?) can (?:change the state|unwind)/.exec(line);
	if (m) advise.add(m[1].replace(/\\([0-9a-f]{2})/gi, (_, h) => String.fromCharCode(parseInt(h, 16))));
}
console.log("advise: instrumented main functions:", advise.size);
if (advise.size < 1000) {
	console.error("ERROR: advise set implausibly small - was the main module relinked with -sASYNCIFY_ADVISE?");
	process.exit(1);
}

// 2. Union of all plugins' function imports (mangled names).
const imports = new Set();
let nPlugins = 0;
for (const f of fs.readdirSync(pluginDir).filter((f) => f.endsWith(".so"))) {
	try {
		const mod = new WebAssembly.Module(fs.readFileSync(pluginDir + "/" + f));
		for (const i of WebAssembly.Module.imports(mod)) if (i.kind === "function") imports.add(i.name);
		nPlugins++;
	} catch (e) {
		console.error("skipping", f, "-", e.message.slice(0, 80));
	}
}
console.log(`plugins scanned: ${nPlugins}; unique function imports: ${imports.size}`);
if (nPlugins < 10) {
	console.error("ERROR: too few plugins present - build the plugins first (they are the import source).");
	process.exit(1);
}

// 3. Resolve advise names to function indices, expand to ALL export aliases.
const { exportsByIdx, idxByInternalName } = parseMainWasm(mainWasm);
const suspendExports = new Set(); // mangled export names that can suspend
let resolved = 0;
for (const name of advise) {
	const idx = idxByInternalName.get(name);
	if (idx === undefined) continue;
	resolved++;
	for (const exp of exportsByIdx.get(idx) || []) suspendExports.add(exp);
}
console.log(`advise names resolved to indices: ${resolved}; suspend-capable export aliases: ${suspendExports.size}`);

// 4. Keep imports that are suspend-capable exports (alias-aware), or whose
//    raw/demangled name matches the advise set directly (belt & braces).
const list = [...imports];
const dem = execSync(cxxfilt, { input: list.join("\n"), maxBuffer: 1 << 28 }).toString().split("\n");
const keep = new Set();
for (let i = 0; i < list.length; i++)
	if (suspendExports.has(list[i]) || advise.has(list[i]) || advise.has(dem[i])) keep.add(list[i]);
console.log("suspend-capable direct imports (this harvest):", keep.size);

// 5. Union with the existing list (additive-only; see header).
let prior = 0;
try {
	for (const e of JSON.parse(fs.readFileSync(outFile))) { keep.add(e); prior++; }
} catch (e) { /* no existing list */ }
keep.add("invoke_*"); // exception/setjmp shims can transitively suspend
const out = [...keep].sort();
fs.writeFileSync(outFile, JSON.stringify(out));
console.log(`wrote ${outFile}: ${out.length} entries (prior list: ${prior})`);
