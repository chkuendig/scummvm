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
#include "backends/platform/sdl/sdl.h"
#include "common/array.h"
#include "common/debug.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/system.h"
#include <SDL3/SDL_stdinc.h>
#include <emscripten.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE bool DragDropFilesystemNode_pushDropFileEvent(const char *path) {
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_DROP_FILE;
	event.common.timestamp = 0;
	Common::String full_path = Common::normalizePath(Common::String(DRAGDROP_FS_PATH) + "/" + path, '/');
	char *path_copy = new char[full_path.size() + 1];
	SDL_strlcpy(path_copy, full_path.c_str(), full_path.size() + 1);
	event.drop.data = path_copy;
	event.drop.windowID = 0; // we don't need this
	debug(5, "Pushing drop file event for path: %s", event.drop.data);
	return SDL_PushEvent(&event);
}
}
namespace {

}

DragDropFilesystemNode::DragDropFilesystemNode(const Common::String &path) : _isRoot(path == DRAGDROP_FS_PATH) {

	if (_isRoot) {
		// Special case for handling the root of the virtual filesystem
		_relPath = "/";
		_displayName = "[" + Common::lastPathComponent(DRAGDROP_FS_PATH, '/') + "]";
		_isDirectory = true;
		_exists = true;
	} else {
		// Normalize the path (that is, remove unneeded slashes etc.)
		_relPath = Common::normalizePath(path.substr(strlen(DRAGDROP_FS_PATH)), '/');
		_displayName = Common::lastPathComponent(_relPath, '/');
		_exists = DragDropFilesystemNode_exists(_relPath.c_str()) != 0;
		_isDirectory = DragDropFilesystemNode_isDirectory(_relPath.c_str()) != 0;
		if (!_isDirectory && _exists) {
			_size = DragDropFilesystemNode_size(_relPath.c_str());
			assert(_size >= 0);
		}
	}
}

bool DragDropFilesystemNode::exists() const {
	return _exists;
}
bool DragDropFilesystemNode::isReadable() const {
	return exists();
}

bool DragDropFilesystemNode::isDirectory() const {
	return _isDirectory;
}

Common::U32String DragDropFilesystemNode::getDisplayName() const {
	return Common::U32String(_displayName);
}

Common::String DragDropFilesystemNode::getName() const {
	return _displayName;
}

bool DragDropFilesystemNode::getChildren(AbstractFSList &list, ListMode mode, bool hidden) const {
	if (!isDirectory())
		return false;
	char **children_arr = DragDropFilesystemNode_getChildren(_relPath.c_str());
	char **iter = children_arr;
	Common::Array<char *> names;
	while (strcmp(*iter, "") != 0) {
		char *childPath = *iter++;

		if (!childPath)
			continue;

		DragDropFilesystemNode *child = new DragDropFilesystemNode(Common::String(getPath() + "/" + childPath));
		bool include = false;
		switch (mode) {
		case Common::FSNode::kListAll:
			include = true;
			break;
		case Common::FSNode::kListFilesOnly:
			include = !child->isDirectory();
			break;
		case Common::FSNode::kListDirectoriesOnly:
			include = child->isDirectory();
			break;
		}

		if (include) {
			list.push_back(child);
		} else {
			delete child;
		}

		// Free the allocated string from JavaScript
		free(childPath);
	}
	free(children_arr);

	return true;
}

AbstractFSNode *DragDropFilesystemNode::getParent() const {

	const char *start = getPath().c_str();
	const char *end = start + getPath().size();

	// Strip off the last component. We make use of the fact that at this
	// point, _relPath is guaranteed to be normalized
	while (end > start && *(end - 1) != '/')
		end--;

	if (end == start) {
		// This only happens if we were called with a relative path, for which
		// there simply is no parent.
		return 0;
	}

	Common::String parentPath = Common::normalizePath(Common::String(start, end), '/');
	FilesystemFactory *factory = g_system->getFilesystemFactory();
	return factory->makeFileNodePath(parentPath);
}

AbstractFSNode *DragDropFilesystemNode::getChild(const Common::String &n) const {
	assert(!_relPath.empty());
	assert(_isDirectory);

	// Make sure the string contains no slashes
	assert(!n.contains('/'));

	// We assume here that _path is already normalized (hence don't bother to call
	// Common::normalizePath on the final path).
	Common::String newPath(getPath());
	if (getPath().lastChar() != '/')
		newPath += '/';
	newPath += n;

	return new DragDropFilesystemNode(newPath);
}

Common::SeekableReadStream *DragDropFilesystemNode::createReadStream() {
	debug(5, "DragDropFilesystemNode::createReadStream called for path: %s", _relPath.c_str());
	if (isDirectory() || !exists()) {
		warning("DragDropFilesystemNode::createReadStream - could not read file %s", _relPath.c_str());
		return nullptr;
	}
	return new DragDropReadStream(_relPath);
}

Common::SeekableReadStream *DragDropFilesystemNode::createReadStreamForAltStream(Common::AltStreamType altStreamType) {
	return nullptr; // No alt streams supported
}

Common::SeekableWriteStream *DragDropFilesystemNode::createWriteStream(bool atomic) {
	return nullptr; // Read-only filesystem
}

bool DragDropFilesystemNode::createDirectory() {
	return false; // Read-only filesystem
}

#endif // EMSCRIPTEN
