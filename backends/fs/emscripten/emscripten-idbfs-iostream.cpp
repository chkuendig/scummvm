
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
