/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef EMSCRIPTEN

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "backends/fs/emscripten/emscripten-posix-fs.h"
#include "backends/fs/emscripten/http-fs.h"
#ifdef USE_CLOUD
#include "backends/fs/emscripten/cloud-fs.h"
#endif

#include <emscripten.h>
extern "C" {
  extern void _initIDBFS(em_proxying_ctx* ctx, const char *pathPtr);
}


EM_JS(void, _persistIDBFS, (), { 
	// Making this synchronous with EM_ASYNC_JS breaks  (somewhere in Asyncify) when syncing saves from cloud 
	// which is why we run this now asynchronous. Emscripten already handles multiple in-flight FS.syncfs calls, 
	// so this isn't an issue and might actually increase performance as the native code isn't blocked. 
	console.error("Persisted IDBFS call");
	FS.syncfs((err) => {
		if (err) {
			console.error("Persisted IDBFS error");
			throw err;
		}
		console.error("Persisted IDBFS done");
	});
});

void EmscriptenFilesystemFactory::persistIDBFS() {
	_persistIDBFS();
}
EmscriptenFilesystemFactory::EmscriptenFilesystemFactory() {
	// see https://github.com/emscripten-core/emscripten/blob/main/test/pthread/test_pthread_proxying_cpp.cpp
	_initIDBFS(getenv("HOME"));
}

AbstractFSNode *EmscriptenFilesystemFactory::makeCurrentDirectoryFileNode() const {
	// getcwd() defaults to root on emscripten and ScummVM doesn't use setcwd()
	return makeRootFileNode();
}

AbstractFSNode *EmscriptenFilesystemFactory::makeRootFileNode() const {
	return new EmscriptenPOSIXFilesystemNode("/");
}

AbstractFSNode *EmscriptenFilesystemFactory::makeFileNodePath(const Common::String &path) const {
	assert(!path.empty());
	if (path.hasPrefix(DATA_PATH)) {
		return new HTTPFilesystemNode(path);
#ifdef USE_CLOUD
	} else if (path.hasPrefix(CLOUD_FS_PATH) && CloudMan.isStorageEnabled()) {
		return new CloudFilesystemNode(path);
#endif
	} else {
		return new EmscriptenPOSIXFilesystemNode(path);
	}
}
#endif
