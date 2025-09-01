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

#ifndef CLOUD_FILESYSTEM_H
#define CLOUD_FILESYSTEM_H

#ifdef EMSCRIPTEN

#include "backends/fs/emscripten/virtual-fs.h"

#ifdef USE_CLOUD
#include "backends/cloud/cloudmanager.h"
#include "backends/cloud/storage.h"
#include "backends/cloud/storagefile.h"
#include "backends/networking/http/connectionmanager.h"
#include "backends/networking/http/httpjsonrequest.h"
#include "backends/networking/http/request.h"
#endif

#define CLOUD_FS_PATH "/cloud"

/**
 * Implementation of the ScummVM file system API based on Cloud storage.
 *
 * Parts of this class are documented in the base interface class, AbstractFSNode.
 */
class CloudFilesystemNode : public VirtualFileSystemNode {
private:
	Common::String _storageFileId;
	Common::String _storageDirectoryPath;
	static Common::String *_lastAccessToken;

	/**
	 * Callbacks for network calls
	 */
	void directoryListedCallback(const Cloud::Storage::ListDirectoryResponse &response);
	void directoryListedErrorCallback(const Networking::ErrorResponse &error);

protected:
	/**
	 * Constructor for creating nodes with known properties (for internal use only).
	 */
	CloudFilesystemNode(const Common::String &path, const Common::String &displayName,
						bool isDirectory, bool isValid, uint32 size,
						const Common::String &storageFileId, const Common::String &storageDirectoryPath);

	virtual AbstractFSNode *makeNode(const Common::String &path) const override {
		return new CloudFilesystemNode(path);
	}

	virtual void fetchDirectoryContents() const override;

	virtual VirtualFileSystemNode *copy() const override {
		return new CloudFilesystemNode(*this);
	}

public:
	/**
	 * Creates a CloudFilesystemNode for a given path.
	 *
	 * @param path the path the new node should point to.
	 */
	CloudFilesystemNode(const Common::String &path);

	virtual Common::SeekableReadStream *createReadStream() override;

	/**
	 * Invalidate the cache for all cloud folders if the access token has changed,
	 * to make sure we don't show outdated folder contents after a user switches
	 * cloud accounts.
	 */
	static void invalidateFoldersCache();
};

#endif // EMSCRIPTEN

#endif
