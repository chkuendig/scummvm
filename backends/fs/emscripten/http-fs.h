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

#ifndef HTTP_FILESYSTEM_H
#define HTTP_FILESYSTEM_H

#ifdef EMSCRIPTEN

#include "backends/fs/emscripten/virtual-fs.h"
#include "backends/networking/http/httpjsonrequest.h"
#include "backends/networking/http/sessionrequest.h"

#define DATA_PATH "/data"

/**
 * Implementation of the ScummVM file system API based on HTTP.
 *
 * Parts of this class are documented in the base interface class, AbstractFSNode.
 */
class HTTPFilesystemNode : public VirtualFileSystemNode {
private:
	Common::String _url;

	// Callback methods for HTTP requests
	void jsonCallbackGetChildren(const Networking::JsonResponse &response);
	void errorCallbackGetChildren(const Networking::ErrorResponse &error);

protected:
	/**
	 * Full constructor, for internal use only (hence protected).
	 */
	HTTPFilesystemNode(const Common::String &path, const Common::String &displayName, const Common::String &baseUrl, bool isValid, bool isDirectory, uint32 size);

	virtual AbstractFSNode *makeNode(const Common::String &path) const override {
		return new HTTPFilesystemNode(path);
	}

	virtual void fetchDirectoryContents() const override;

	virtual VirtualFileSystemNode *copy() const override {
		return new HTTPFilesystemNode(*this);
	}

public:
	/**
	 * Creates a HTTPFilesystemNode for a given path.
	 *
	 * @param path the path the new node should point to.
	 */
	HTTPFilesystemNode(const Common::String &path);

	virtual Common::SeekableReadStream *createReadStream() override;
};

#endif // EMSCRIPTEN

#endif
