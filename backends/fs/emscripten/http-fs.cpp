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

#include "backends/fs/emscripten/http-fs.h"
#include "backends/fs/emscripten/http-readstream.h"
#include "backends/networking/http/httpjsonrequest.h"
#include "common/debug.h"
#include "common/formats/json.h"
#include "common/system.h"

HTTPFilesystemNode::HTTPFilesystemNode(const Common::String &path, const Common::String &displayName,
									   const Common::String &baseUrl, bool isValid, bool isDirectory, uint32 size)
	: VirtualFileSystemNode(path, displayName, isValid, isDirectory, size, path == DATA_PATH),
	  _url(baseUrl) {
}

HTTPFilesystemNode::HTTPFilesystemNode(const Common::String &p)
	: VirtualFileSystemNode(p, p == DATA_PATH) {
	if (_isRoot) {
		_url = _path;
	} else {
		AbstractFSNode *parent = getParent();
		if (!parent) {
			_isValid = false;
			return;
		}
		for (AbstractFSList::iterator i = virtualFolders()[parent->getPath()].begin();
			 i != virtualFolders()[parent->getPath()].end(); ++i) {
			HTTPFilesystemNode *node = (HTTPFilesystemNode *)*i;
			if (node->getPath() == _path) {
				_url = node->_url;
				break;
			}
		}
	}
}

// Instance callback methods for HTTP requests
void HTTPFilesystemNode::jsonCallbackGetChildren(const Networking::JsonResponse &response) {
	debug(5, "HTTPFilesystemNode::jsonCallbackGetChildren called - processing response for %s", _path.c_str());
	const Common::JSONValue *json = response.value;

	Common::JSONObject jsonObj = json->asObject();
	AbstractFSList *dirList = new AbstractFSList();

	for (typename Common::HashMap<Common::String, Common::JSONValue *>::iterator i = jsonObj.begin(); i != jsonObj.end(); ++i) {
		Common::String name = i->_key;
		bool isDir = false;
		uint32 size = 0;
		Common::String baseUrl = _url + "/" + name;

		if (i->_value->isObject()) {
			isDir = true;
			if (i->_value->asObject().contains("baseUrl")) {
				debug(5, "HTTPFilesystemNode::jsonCallbackGetChildren - Directory with baseUrl %s", name.c_str());
				baseUrl = i->_value->asObject()["baseUrl"]->asString();
			}
		} else if (i->_value->isIntegerNumber()) {
			size = i->_value->asIntegerNumber();
		}
		HTTPFilesystemNode *file_node = new HTTPFilesystemNode(_path + "/" + name, name, baseUrl, true, isDir, size);
		dirList->push_back(file_node);
	}
	virtualFolders()[_path] = *dirList;
}

void HTTPFilesystemNode::errorCallbackGetChildren(const Networking::ErrorResponse &errorResponse) {
	warning("HTTPFilesystemNode::errorCallbackGetChildren called - %s", errorResponse.response.c_str());
	_isValid = false;
}

void HTTPFilesystemNode::fetchDirectoryContents() const {
	debug(5, "HTTPFilesystemNode::fetchDirectoryContents - Fetching children for %s at %s", _path.c_str(), _url.c_str());
	Common::String url = _url + "/index.json";
	debug(5, "HTTPFilesystemNode::fetchDirectoryContents - Starting HTTP request to %s", url.c_str());

	Networking::JsonCallback callback = new Common::Callback<HTTPFilesystemNode, const Networking::JsonResponse &>(
		const_cast<HTTPFilesystemNode *>(this), &HTTPFilesystemNode::jsonCallbackGetChildren);
	Networking::ErrorCallback failureCallback = new Common::Callback<HTTPFilesystemNode, const Networking::ErrorResponse &>(
		const_cast<HTTPFilesystemNode *>(this), &HTTPFilesystemNode::errorCallbackGetChildren);

	Networking::HttpJsonRequest *request = new Networking::HttpJsonRequest(callback, failureCallback, url);
	request->execute();
}

Common::SeekableReadStream *HTTPFilesystemNode::createReadStream() {
	debug(5, "HTTPFilesystemNode::createReadStream() %s (size %u)", _path.c_str(), _size);
	// Create cache path for this file
	Common::String baseCachePath = Common::normalizePath("/.cache/" + _path, '/');
	// Create and return the HttpReadStream for chunked downloading
	return new HttpReadStream(_url, _displayName, baseCachePath, _size);
}

#endif // EMSCRIPTEN
