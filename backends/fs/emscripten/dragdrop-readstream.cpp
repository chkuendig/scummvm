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

#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#include "backends/fs/emscripten/dragdrop-fs.h"
#include "backends/fs/emscripten/dragdrop-readstream.h"
#include "backends/fs/fs-factory.h"
#include "common/array.h"
#include "common/debug.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/system.h"
#include <emscripten.h>

// DragDropReadStream implementation
DragDropReadStream::DragDropReadStream(const Common::String &path) : _path(path), _pos(0), _eos(false) {
	_size = DragDropFilesystemNode_size(path.c_str());
}

DragDropReadStream::~DragDropReadStream() {
}

uint32 DragDropReadStream::read(void *dataPtr, uint32 dataSize) {
	if (!dataPtr)
		return 0;

	// Read at most as many bytes as are still available...
	if (dataSize > _size - _pos) {
		dataSize = _size - _pos;
		_eos = true;
	}

	uint32 bytesRead = DragDropFilesystemNode_readFile(_path.c_str(), _pos, dataSize, dataPtr);
	_pos += bytesRead;
	return bytesRead;
}

bool DragDropReadStream::seek(int64 offset, int whence) {
	int64 newPos = _pos;

	switch (whence) {
	case SEEK_SET:
		newPos = offset;
		break;
	case SEEK_CUR:
		newPos = _pos + offset;
		break;
	case SEEK_END:
		newPos = _size + offset;
		break;
	default:
		debug(5, "DragDropReadStream::seek - invalid whence: %d", whence);
		return false;
	}

	if (newPos < 0 || newPos > _size) {
		return false;
	}

	_pos = newPos;
	_eos = (_pos >= _size);
	return true;
}

#endif // EMSCRIPTEN
