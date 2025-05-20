

// TODO: This shouldn't be async. see https://github.com/emscripten-core/emscripten/blob/2c3c493386f0da51f0eed5af331dd39417fb7135/system/lib/wasmfs/thread_utils.h
// https://github.com/emscripten-core/emscripten/discussions/21929#discussioncomment-9424051
addToLibrary({
  _initIDBFS: async function(ctx, pathPtr) {
   const path = UTF8ToString(pathPtr);
	console.log("Mount IDBFS at "+path);
	FS.mount(IDBFS, { autoPersist: true }, path);
	// sync from persisted state into memory 
	await (new Promise((resolve, reject) => {
			FS.syncfs(true, (err) => {
				if (err) {
					reject(err);
				} else if(!FS.analyzePath("/home/web_user/scummvm.ini").exists) {  //TODO: This path shold be based on the pathPtr passed 
					// if we don't have a settings file yet, we try to download it
					// this allows setting up a default scummvm.ini with some games already added 
					fetch("scummvm.ini")
						.then((response) => response.ok ? response.text() : "")
						.then((text) => {
							FS.writeFile('/home/web_user/scummvm.ini', text);
							resolve(true);
						})
						.catch((err) => reject);
				} else {
					resolve(true);
				}
			});
		})).catch(err => {
			alert("Error initializing files: " + err);
                _emscripten_proxy_finish(ctx);
			throw err;
	});
	console.log("Data synced from IDBFS");
        _emscripten_proxy_finish(ctx);
  },
});

