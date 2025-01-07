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
#include "backends/cloud/downloadrequest.h"
#include "backends/fs/fs-factory.h"
#include "backends/fs/posix/posix-fs.h"
#include "backends/fs/posix/posix-iostream.h"
#include "common/debug.h"
#include "common/file.h"

Common::HashMap<Common::String, AbstractFSList> HTTPFilesystemNode::_httpFolders = Common::HashMap<Common::String, AbstractFSList>();

HTTPFilesystemNode::HTTPFilesystemNode(const Common::String &path, const Common::String &displayName, const Common::String &baseUrl, bool isValid, bool isDirectory, int size) : _path(path), _displayName(displayName), _baseUrl(baseUrl), _isValid(isValid), _isDirectory(isDirectory), _size(size) {
	debug(5, "HTTPFilesystemNode::HTTPFilesystemNode(%s, %s)", path.c_str(), baseUrl.c_str());
	assert(path.size() > 0);
	assert(isDirectory || size >= 0 || !isValid);
}

HTTPFilesystemNode::HTTPFilesystemNode(const Common::String &p) : _path(p), _isValid(false), _isDirectory(false), _size(-1) {
	debug(5, "HTTPFilesystemNode::HTTPFilesystemNode(%s)", p.c_str());
	assert(p.size() > 0);

	// Normalize the path (that is, remove unneeded slashes etc.)
	_path = Common::normalizePath(_path, '/');
	_displayName = Common::lastPathComponent(_path, '/');
	if (_path == DATA_PATH) { // need special case for handling the root of the http-filesystem
		_displayName = "[" + Common::lastPathComponent(_path, '/') + "]";
		_isDirectory = true;
		_isValid = true;
		_baseUrl = _path;
	} else { // we need to peek in the parent folder to see if file exists and if it's a directory
		AbstractFSNode *parent = getParent();
		AbstractFSList tmp = AbstractFSList();
		parent->getChildren(tmp, Common::FSNode::kListAll, true);
		for (AbstractFSList::iterator i = tmp.begin(); i != tmp.end(); ++i) {
			AbstractFSNode *child = *i;
			if (child->getPath() == _path) {
				_isDirectory = child->isDirectory();
				_isValid = true;
				_baseUrl = ((HTTPFilesystemNode *)child)->_baseUrl;
				_size = ((HTTPFilesystemNode *)child)->_size;
				break;
			}
		}
	}
	assert(_isDirectory || _size >= 0 || !_isValid);
	debug(5, "HTTPFilesystemNode::HTTPFilesystemNode(%s) -  %s isValid %s isDirectory %s", p.c_str(), _baseUrl.c_str(), _isValid ? "True" : "false", _isDirectory ? "True" : "false");
}

AbstractFSNode *HTTPFilesystemNode::getChild(const Common::String &n) const {
	assert(!_path.empty());
	assert(_isDirectory);

	// Make sure the string contains no slashes
	assert(!n.contains('/'));

	// We assume here that _path is already normalized (hence don't bother to call
	//  Common::normalizePath on the final path).
	Common::String newPath(_path);
	if (_path.lastChar() != '/')
		newPath += '/';
	newPath += n;

	return makeNode(newPath);
}

void HTTPFilesystemNode::directoryListedCallback(const Networking::JsonResponse &response) {
	Common::JSONObject jsonObj = Common::JSON::parse(response.value->stringify().c_str())->asObject();
	AbstractFSList *dirList = new AbstractFSList();
	for (typename Common::HashMap<Common::String, Common::JSONValue *>::iterator i = jsonObj.begin(); i != jsonObj.end(); ++i) {
		Common::String name = i->_key;
		bool isDir = false;
		int size = -1;
		Common::String baseUrl = _baseUrl + "/" + ConnMan.urlEncode(name);

		if (i->_value->isObject()) {
			isDir = true;
			if (i->_value->asObject().contains("baseUrl")) {
				debug(5, "HTTPFilesystemNode::directoryListedCallback - Directory with baseUrl %s", name.c_str());
				baseUrl = i->_value->asObject()["baseUrl"]->asString();
			}
		} else if (i->_value->isIntegerNumber()) {
			size = i->_value->asIntegerNumber();
		}
		HTTPFilesystemNode *file_node = new HTTPFilesystemNode(_path + "/" + name, name, baseUrl, true, isDir, size);
		dirList->push_back(file_node);
	}
	_httpFolders[_path] = *dirList;
}

void HTTPFilesystemNode::requestErrorCallback(const Networking::ErrorResponse &_error) {
	debug(5, "Response %ld: %s", _error.httpResponseCode, _error.response.c_str());
	error("HTTPFilesystemNode::requestErrorCallback");
}

void HTTPFilesystemNode::fileDownloadedCallback(const Networking::MemoryWriteStreamDynamicResponse &response) {
	debug(5, "HTTPFilesystemNode::fileDownloadedCallback %u", response.request->state());
	Common::String fsCachePath = Common::normalizePath("/.cache/" + _path, '/');
	Common::DumpFile *_localFile = new Common::DumpFile();
	if (!_localFile->open(Common::Path(fsCachePath), true)) {
		error("Storage: unable to open file to download into");
	}
	debug(5, "HTTPFilesystemNode::fileDownloadedCallback() file downloaded %s", _path.c_str());

	Common::MemoryWriteStreamDynamic *_memoryStream;
	_memoryStream = response.value;
	byte *data = _memoryStream->getData();
	uint32 size = _memoryStream->size();

	_localFile->write(data, size);
	free(data); // CurlFileRequest doesn't free this.

	_localFile->close();
}

void HTTPFilesystemNode::requestFinishedCallback(Networking::Request *invalidRequestPointer) {
	debug(5, " HTTPFilesystemNode::requestFinishedCallback");
}

bool HTTPFilesystemNode::getChildren(AbstractFSList &myList, ListMode mode, bool hidden) const {
	if (!_isValid) {
		return false;
	}
	assert(_isDirectory);
	if (!_httpFolders.contains(_path)) {
		debug(5, "HTTPFilesystemNode::getChildren Fetching Children: %s at %s", _path.c_str(), _baseUrl.c_str());
		Common::String url = _baseUrl + "/index.json";

		Networking::CurlJsonRequest *request = new Networking::CurlJsonRequest(
			new Common::Callback<HTTPFilesystemNode, const Networking::JsonResponse &>((HTTPFilesystemNode *)this, &HTTPFilesystemNode::directoryListedCallback),
			new Common::Callback<HTTPFilesystemNode, const Networking::ErrorResponse &>((HTTPFilesystemNode *)this, &HTTPFilesystemNode::requestErrorCallback),
			url);
		ConnMan.addRequest(
			request,
			new Common::Callback<HTTPFilesystemNode, Networking::Request *>((HTTPFilesystemNode *)this, &HTTPFilesystemNode::requestFinishedCallback));

		while (!_httpFolders.contains(_path)) {
			g_system->delayMillis(10);
		}
		debug(5, "HTTPFilesystemNode::getChildren %s size %u", _path.c_str(), _httpFolders[_path].size());
	}

	for (AbstractFSList::iterator i = _httpFolders[_path].begin(); i != _httpFolders[_path].end(); ++i) {
		// TODO: Respect "ListMode mode" and "bool hidden"
		HTTPFilesystemNode *node = (HTTPFilesystemNode *)*i;
		// we need to copy node here as FSNode will take ownership of the pointer and destroy it after use
		HTTPFilesystemNode *file_node = new HTTPFilesystemNode(*node);
		myList.push_back(file_node);
	}
	return true;
}

AbstractFSNode *HTTPFilesystemNode::getParent() const {
	if (_path == "/")
		return 0; // The filesystem root has no parent

	const char *start = _path.c_str();
	const char *end = start + _path.size();

	// Strip of the last component. We make use of the fact that at this
	// point, _path is guaranteed to be normalized
	while (end > start && *(end - 1) != '/')
		end--;

	if (end == start) {
		// This only happens if we were called with a relative path, for which
		// there simply is no parent.
		// TODO: We could also resolve this by assuming that the parent is the
		//       current working directory, and returning a node referring to that.
		return 0;
	}

	Common::String _parent_path = Common::normalizePath(Common::String(start, end), '/');
	FilesystemFactory *factory = g_system->getFilesystemFactory();
	return factory->makeFileNodePath(_parent_path);
}

Common::SeekableReadStream *HTTPFilesystemNode::createReadStream() {
	debug(5, "*HTTPFilesystemNode::createReadStream() %s (size %d) ", _path.c_str(), _size);
	Common::String fsCachePath = Common::normalizePath("/.cache/" + _path, '/');
	POSIXFilesystemNode *cacheFile = new POSIXFilesystemNode(fsCachePath);
	if (!cacheFile->exists() && _size > 0) {
		Common::String url = _baseUrl;
		debug(5, "HTTPFilesystemNode::createReadStream - get file %s at %s ", _path.c_str(), _baseUrl.c_str());
		Networking::CurlFileRequest *request = new Networking::CurlFileRequest(
			new Common::Callback<HTTPFilesystemNode, const Networking::MemoryWriteStreamDynamicResponse &>(this, &HTTPFilesystemNode::fileDownloadedCallback),
			new Common::Callback<HTTPFilesystemNode, const Networking::ErrorResponse &>(this, &HTTPFilesystemNode::requestErrorCallback),
			url);
		ConnMan.addRequest(
			request,
			new Common::Callback<HTTPFilesystemNode, Networking::Request *>(this, &HTTPFilesystemNode::requestFinishedCallback));
		while (!cacheFile->exists()) {
			g_system->delayMillis(10);
		}
		debug(5, "*HTTPFilesystemNode::createReadStream() file written %s", fsCachePath.c_str());
	} else if (_size == 0) {
		debug(5, "HTTPFilesystemNode::createReadStream() file empty %s", _path.c_str());
		Common::String fsCachePath = Common::normalizePath("/.cache/" + _path, '/');
		Common::DumpFile *_localFile = new Common::DumpFile();
		if (!_localFile->open(Common::Path(fsCachePath), true)) {
			error("Storage: unable to open file to download into");
		}
		_localFile->close();
	}
	return PosixIoStream::makeFromPath(fsCachePath, StdioStream::WriteMode_Read);
}

Common::SeekableWriteStream *HTTPFilesystemNode::createWriteStream(bool atomic) {
	return 0;
}

bool HTTPFilesystemNode::createDirectory() {
	return false;
}
bool HTTPFilesystemNode::exists() const {
	return _isValid;
}

bool HTTPFilesystemNode::isReadable() const {
	return exists();
}

bool HTTPFilesystemNode::isWritable() const {
	return false;
}

#endif // #if defined(EMSCRIPTEN)
