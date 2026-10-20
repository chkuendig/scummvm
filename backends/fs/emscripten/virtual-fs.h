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

#ifndef BACKENDS_FS_EMSCRIPTEN_VIRTUAL_FS_H
#define BACKENDS_FS_EMSCRIPTEN_VIRTUAL_FS_H

#ifdef EMSCRIPTEN

#include "backends/fs/abstract-fs.h"
#include "common/hashmap.h"
#include "common/str.h"

/**
 * Abstract base class for a virtual file system node backed by remote files accessed over the network.
 *
 * This class provides common functionality for filesystem implementations that access files remotely
 * (such as HTTP and Cloud storage), including directory listing caching and asynchronous content fetching.
 */
class VirtualFileSystemNode : public AbstractFSNode {
protected:
	Common::String _path;
	Common::String _displayName;
	bool _isValid;
	bool _isDirectory;
	bool _isRoot;
	uint32 _size;
	uint32 _timestamp;

	// Accessor for the shared directory listing cache
	static Common::HashMap<Common::String, AbstractFSList> &virtualFolders();

	/**
	 * Constructor for creating nodes with known properties
	 */
	VirtualFileSystemNode(const Common::String &path, const Common::String &displayName,
						  bool isValid, bool isDirectory, uint32 size, bool isRoot);

	/**
	 * Constructor for creating nodes from path - will determine properties
	 */
	VirtualFileSystemNode(const Common::String &path, bool isRoot);

	/**
	 * Default constructor for subclass use
	 */
	VirtualFileSystemNode();

public:
	virtual ~VirtualFileSystemNode() {}

	// AbstractFSNode interface
	virtual AbstractFSNode *getChild(const Common::String &n) const override;
	virtual bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override;

	// Pure virtual method for copying nodes - each subclass implements with its copy constructor
	virtual VirtualFileSystemNode *copy() const = 0;
	virtual AbstractFSNode *getParent() const override;
	virtual Common::SeekableWriteStream *createWriteStream(bool atomic) override;
	virtual bool createDirectory() override;
	virtual bool exists() const override;
	virtual bool isReadable() const override;
	virtual bool isWritable() const override;

	virtual Common::U32String getDisplayName() const override { return _displayName; }
	virtual Common::String getName() const override { return _displayName; }
	virtual Common::String getPath() const override { return _path; }
	virtual bool isDirectory() const override { return _isDirectory; }

	// Size accessor (not part of AbstractFSNode interface)
	uint32 getSize() const { return _size; }

protected:
	/**
	 * Create a new filesystem node for the given path.
	 * Must be implemented by subclasses to return the correct node type.
	 */
	virtual AbstractFSNode *makeNode(const Common::String &path) const = 0;

	/**
	 * Initiate asynchronous fetching of directory contents.
	 * Must be implemented by subclasses to populate the cache.
	 */
	virtual void fetchDirectoryContents() const = 0;

	/**
	 * Wait for directory contents to be cached.
	 * Can be overridden by subclasses if different waiting logic is needed.
	 */
	virtual void waitForDirectoryCache() const;

	/**
	 * Initialize node properties from parent directory.
	 * Used when creating a node from a path.
	 */
	void initializeFromParent();
};

#endif // EMSCRIPTEN

#endif
