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

#include "backends/fs/emscripten/virtual-fs.h"
#include "backends/fs/fs-factory.h"
#include "common/debug.h"
#include "common/system.h"

/**
 * Implementation of VirtualFileSystemNode - a base class for virtual file system nodes
 * backed by remote files accessed over the network.
 */

// Accessor for the shared directory cache
Common::HashMap<Common::String, AbstractFSList> &VirtualFileSystemNode::virtualFolders() {
	static Common::HashMap<Common::String, AbstractFSList> *folders = nullptr;
	if (!folders)
		folders = new Common::HashMap<Common::String, AbstractFSList>();
	return *folders;
}

VirtualFileSystemNode::VirtualFileSystemNode(const Common::String &path, const Common::String &displayName,
											 bool isValid, bool isDirectory, uint32 size, bool isRoot)
	: _path(path), _displayName(displayName), _isValid(isValid), _isDirectory(isDirectory), _size(size), _timestamp(0), _isRoot(isRoot) {
}

VirtualFileSystemNode::VirtualFileSystemNode(const Common::String &path, bool isRoot)
	: _path(path), _isRoot(isRoot), _isValid(false), _isDirectory(false), _size(0), _timestamp(0) {
	assert(path.size() > 0);

	// Normalize the path (that is, remove unneeded slashes etc.)
	_path = Common::normalizePath(_path, '/');
	_displayName = Common::lastPathComponent(_path, '/');

	if (_isRoot) {
		// Special case for handling the root of the virtual filesystem
		_displayName = "[" + Common::lastPathComponent(_path, '/') + "]";
		_isDirectory = true;
		_isValid = true;
	} else {
		// Initialize properties from parent directory
		initializeFromParent();
	}

	// Note: For directories, size may not be meaningful, so we don't assert on it
	debug(5, "VirtualFileSystemNode::VirtualFileSystemNode(%s) - isValid %s isDirectory %s",
		  path.c_str(), _isValid ? "true" : "false", _isDirectory ? "true" : "false");
}

VirtualFileSystemNode::VirtualFileSystemNode()
	: _isValid(false), _isDirectory(false), _size(0), _timestamp(0) {
}

AbstractFSNode *VirtualFileSystemNode::getChild(const Common::String &n) const {
	assert(!_path.empty());
	assert(_isDirectory);

	// Make sure the string contains no slashes
	assert(!n.contains('/'));

	// We assume here that _path is already normalized (hence don't bother to call
	// Common::normalizePath on the final path).
	Common::String newPath(_path);
	if (_path.lastChar() != '/')
		newPath += '/';
	newPath += n;

	return makeNode(newPath);
}

bool VirtualFileSystemNode::getChildren(AbstractFSList &myList, ListMode mode, bool hidden) const {
	if (!_isValid) {
		return false;
	}
	assert(_isDirectory);

	if (!virtualFolders().contains(_path)) {
		// If we don't have a children list yet, we need to fetch it
		debug(5, "VirtualFileSystemNode::getChildren - Fetching children for: %s", _path.c_str());
		fetchDirectoryContents();
		waitForDirectoryCache();
		debug(5, "VirtualFileSystemNode::getChildren - %s size %u", _path.c_str(), virtualFolders()[_path].size());
	}
	if (_isValid == false) { // couldn't fetch directory contents
		return false;
	}
	for (AbstractFSList::iterator i = virtualFolders()[_path].begin(); i != virtualFolders()[_path].end(); ++i) {
		VirtualFileSystemNode *node = static_cast<VirtualFileSystemNode *>(*i);

		// Apply the requested filter mode
		bool includeNode = false;
		switch (mode) {
		case Common::FSNode::kListAll:
			includeNode = true;
			break;
		case Common::FSNode::kListFilesOnly:
			includeNode = !node->isDirectory();
			break;
		case Common::FSNode::kListDirectoriesOnly:
			includeNode = node->isDirectory();
			break;
		}

		if (includeNode && node->exists()) {
			// Create a copy of the node as FSNode will take ownership
			myList.push_back(node->copy());
		} else {
			debug(5, "VirtualFileSystemNode::getChildren - skipping %s", node->getPath().c_str());
		}
	}
	return true;
}

AbstractFSNode *VirtualFileSystemNode::getParent() const {
	if (_path == "/")
		return 0; // The filesystem root has no parent

	const char *start = _path.c_str();
	const char *end = start + _path.size();

	// Strip off the last component. We make use of the fact that at this
	// point, _path is guaranteed to be normalized
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

Common::SeekableWriteStream *VirtualFileSystemNode::createWriteStream(bool atomic) {
	return 0; // Virtual filesystems are read-only
}

bool VirtualFileSystemNode::createDirectory() {
	return false; // Virtual filesystems are read-only
}

bool VirtualFileSystemNode::exists() const {
	return _isValid;
}

bool VirtualFileSystemNode::isReadable() const {
	return exists();
}

bool VirtualFileSystemNode::isWritable() const {
	return false; // Virtual filesystems are read-only
}

void VirtualFileSystemNode::waitForDirectoryCache() const {
	while (!virtualFolders().contains(_path) && _isValid) {
		g_system->delayMillis(10);
	}
}

void VirtualFileSystemNode::initializeFromParent() {
	// We need to peek in the parent folder to see if the node exists and if it's a directory
	AbstractFSNode *parent = getParent();
	if (!parent) {
		return;
	}

	AbstractFSList tmp = AbstractFSList();
	parent->getChildren(tmp, Common::FSNode::kListAll, true);

	for (AbstractFSList::iterator i = tmp.begin(); i != tmp.end(); ++i) {
		AbstractFSNode *child = *i;
		if (child->getPath() == _path) {
			_isDirectory = child->isDirectory();
			_isValid = true;
			// Try to cast to VirtualFileSystemNode to get size
			VirtualFileSystemNode *virtualChild = dynamic_cast<VirtualFileSystemNode *>(child);
			if (virtualChild) {
				_size = virtualChild->getSize();
			}
			break;
		}
	}
}

#endif // EMSCRIPTEN
