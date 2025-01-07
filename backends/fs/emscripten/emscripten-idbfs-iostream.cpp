
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/fs/emscripten/emscripten-idbfs-iostream.h"
#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "common/system.h"
#include <sys/stat.h>

EmscriptenIdbfsIoStream::EmscriptenIdbfsIoStream(void *handle) : PosixIoStream(handle) {}

EmscriptenIdbfsIoStream::~EmscriptenIdbfsIoStream() {
	this->flush();
	EmscriptenFilesystemFactory *fs_factory = dynamic_cast<EmscriptenFilesystemFactory *>(g_system->getFilesystemFactory());
	fs_factory->persistIDBFS();
	warning("Persist IDBFS done");
}

EmscriptenIdbfsIoStream *EmscriptenIdbfsIoStream::makeFromPath(const Common::String &path, bool writeMode) {
	warning("EmscriptenIdbfsIoStream::makeFromPath %s", path.c_str());
#if defined(HAS_FSEEKO64)
	FILE *handle = fopen64(path.c_str(), writeMode ? "wb" : "rb");
#else
	FILE *handle = fopen(path.c_str(), writeMode ? "wb" : "rb");
#endif
	if (handle)
		return new EmscriptenIdbfsIoStream(handle);
	return nullptr;
}
