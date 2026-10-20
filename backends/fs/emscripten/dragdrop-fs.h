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

#ifndef BACKENDS_FS_EMSCRIPTEN_DRAGDROP_FS_H
#define BACKENDS_FS_EMSCRIPTEN_DRAGDROP_FS_H

#ifdef EMSCRIPTEN

#include "backends/fs/abstract-fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/types.h"

#define DRAGDROP_FS_PATH "/drag&drop"

// C functions to interface with JavaScript without emscripten::val
extern "C" {
// These match the function names in our JavaScript library
void DragDropFilesystemNode_addDragEventListeners();
void DragDropFilesystemNode_removeDragEventListeners();
int DragDropFilesystemNode_exists(const char *path);
char **DragDropFilesystemNode_getChildren(const char *path);
int32 DragDropFilesystemNode_size(const char *path);
bool DragDropFilesystemNode_isDirectory(const char *path);
uint32 DragDropFilesystemNode_readFile(const char *pathPtr, uint32 offset, uint32 size, void *bufferPtr);
}

class DragDropFilesystemNode : public AbstractFSNode {
public:
	explicit DragDropFilesystemNode(const Common::String &path);

	bool exists() const override;
	Common::U32String getDisplayName() const override;
	Common::String getName() const override;
	Common::String getPath() const override { return DRAGDROP_FS_PATH + _relPath; }
	bool isDirectory() const override;
	bool isReadable() const override;
	bool isWritable() const override { return false; }

	AbstractFSNode *getChild(const Common::String &n) const override;
	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override;
	AbstractFSNode *getParent() const override;

	Common::SeekableReadStream *createReadStream() override;
	Common::SeekableReadStream *createReadStreamForAltStream(Common::AltStreamType altStreamType) override;
	Common::SeekableWriteStream *createWriteStream(bool atomic) override;
	bool createDirectory() override;

private:
	Common::String _relPath;
	Common::String _displayName;
	bool _isDirectory = false;
	bool _exists = false;
	bool _isRoot = false;
	int64 _size = -1;
};


#endif // EMSCRIPTEN

#endif
