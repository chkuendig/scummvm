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

#include "backends/fs/emscripten/cloud-fs.h"
#include "backends/cloud/basestorage.h"
#include "backends/cloud/cloudmanager.h"
#include "backends/cloud/storage.h"
#include "backends/fs/emscripten/cloud-readstream.h"
#include "common/system.h"

Common::String *CloudFilesystemNode::_lastAccessToken = nullptr;

CloudFilesystemNode::CloudFilesystemNode(const Common::String &path, const Common::String &displayName,
										 bool isDirectory, bool isValid, uint32 size,
										 const Common::String &storageFileId, const Common::String &storageDirectoryPath)
	: VirtualFileSystemNode(path, displayName, isValid, isDirectory, size, path == CLOUD_FS_PATH),
	  _storageFileId(storageFileId), _storageDirectoryPath(storageDirectoryPath) {
}

CloudFilesystemNode::CloudFilesystemNode(const Common::String &path)
	: VirtualFileSystemNode(path, path == CLOUD_FS_PATH) {
	if (_isRoot) {
		_storageDirectoryPath = "";
	} else {
		AbstractFSNode *parent = getParent();
		if (!parent) {
			_isValid = false;
			return;
		}
		for (AbstractFSList::iterator i = virtualFolders()[parent->getPath()].begin();
			 i != virtualFolders()[parent->getPath()].end(); ++i) {
			CloudFilesystemNode *node = (CloudFilesystemNode *)*i;
			if (node->getPath() == _path) {
				_storageFileId = node->_storageFileId;
				_storageDirectoryPath = node->_storageDirectoryPath;
				break;
			}
		}
	}
}

void CloudFilesystemNode::directoryListedCallback(const Cloud::Storage::ListDirectoryResponse &response) {
	debug(5, "CloudFilesystemNode::directoryListedCallback %s", _path.c_str());
	Common::Array<Cloud::StorageFile> storageFiles = response.value;
	AbstractFSList *dirList = new AbstractFSList();

	for (Common::Array<Cloud::StorageFile>::iterator i = storageFiles.begin(); i != storageFiles.end(); ++i) {
		Cloud::StorageFile storageFile = *i;
		CloudFilesystemNode *file_node = new CloudFilesystemNode(
			_path + "/" + storageFile.name(),
			storageFile.name(),
			storageFile.isDirectory(),
			true,
			storageFile.size(),
			storageFile.isDirectory() ? "" : storageFile.id(),
			storageFile.isDirectory() ? storageFile.path() : "");
		dirList->push_back(file_node);
	}
	virtualFolders()[_path] = *dirList;
}

void CloudFilesystemNode::directoryListedErrorCallback(const Networking::ErrorResponse &error) {
	warning("CloudFilesystemNode::directoryListedErrorCallback - Response %ld: %s",
			error.httpResponseCode, error.response.c_str());
	_isValid = false;
}

void CloudFilesystemNode::fetchDirectoryContents() const {
	debug(5, "CloudFilesystemNode::fetchDirectoryContents - Fetching children for %s", _path.c_str());
	CloudMan.listDirectory(
		_storageDirectoryPath,
		new Common::Callback<CloudFilesystemNode, const Cloud::Storage::ListDirectoryResponse &>(
			const_cast<CloudFilesystemNode *>(this), &CloudFilesystemNode::directoryListedCallback),
		new Common::Callback<CloudFilesystemNode, const Networking::ErrorResponse &>(
			const_cast<CloudFilesystemNode *>(this), &CloudFilesystemNode::directoryListedErrorCallback),
		false);
}

Common::SeekableReadStream *CloudFilesystemNode::createReadStream() {
	debug(5, "CloudFilesystemNode::createReadStream() %s", _path.c_str());

	// Create cache path for this file
	Common::String baseCachePath = Common::normalizePath("/.cache/" + _path, '/');

	// Get current storage
	Cloud::Storage *storage = CloudMan.getCurrentStorage();
	if (!storage) {
		warning("CloudFilesystemNode::createReadStream: No storage available");
		return nullptr;
	}

	// Create and return the CloudReadStream
	return new CloudReadStream(storage, _storageFileId, _displayName, baseCachePath, _size);
}

void CloudFilesystemNode::invalidateFoldersCache() {
	if (!_lastAccessToken) {
		_lastAccessToken = new Common::String();
	}
	Cloud::BaseStorage *baseStorage = dynamic_cast<Cloud::BaseStorage *>(CloudMan.getCurrentStorage());

	Common::String newToken = baseStorage->accessToken();
	if (newToken != *_lastAccessToken) {
		// Collect keys to erase first, then erase them
		Common::Array<Common::String> keysToErase;
		for (Common::HashMap<Common::String, AbstractFSList>::iterator it = virtualFolders().begin();
			 it != virtualFolders().end(); ++it) {
			if (it->_key.hasPrefix(CLOUD_FS_PATH)) {
				keysToErase.push_back(it->_key);
			}
		}

		// Now erase the collected keys
		for (uint i = 0; i < keysToErase.size(); ++i) {
			virtualFolders().erase(keysToErase[i]);
		}

		*_lastAccessToken = newToken;
	}
}

#endif // EMSCRIPTEN
