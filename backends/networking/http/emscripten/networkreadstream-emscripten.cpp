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

#include "backends/networking/http/emscripten/networkreadstream-emscripten.h"
#include "backends/networking/http/networkreadstream.h"
#include "base/version.h"
#include "common/debug.h"

namespace Networking {

NetworkReadStream *NetworkReadStream::make(const char *url, RequestHeaders *headersList, const Common::String &postFields, bool uploading, bool usingPatch, bool keepAlive, long keepAliveIdle, long keepAliveInterval, uint64 rangeStart, uint64 rangeLength) {
	// Create a copy of headers list that we can modify
	RequestHeaders *newHeadersList = nullptr;
	if (headersList)
		newHeadersList = new RequestHeaders(*headersList);
	else
		newHeadersList = new RequestHeaders();

	// Add Range header if specified
	if (rangeStart > 0 || rangeLength > 0) {
		Common::String rangeHeader = Common::String::format("Range: bytes=%llu-%s",
															rangeStart,
															rangeLength > 0 ? Common::String::format("%llu", rangeStart + rangeLength - 1).c_str() : "");

		newHeadersList->push_back(rangeHeader);
		debug(5, "Adding range header: %s", rangeHeader.c_str());
	}

	NetworkReadStreamEmscripten *stream = new NetworkReadStreamEmscripten(url, newHeadersList, postFields, uploading, usingPatch, keepAlive, keepAliveIdle, keepAliveInterval);

	// We created our own copy of headers, so delete it
	// delete newHeadersList;

	return stream;
}

NetworkReadStream *NetworkReadStream::make(const char *url, RequestHeaders *headersList, const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles, bool keepAlive, long keepAliveIdle, long keepAliveInterval, uint64 rangeStart, uint64 rangeLength) {
	// Create a copy of headers list that we can modify
	RequestHeaders *newHeadersList = nullptr;
	if (headersList)
		newHeadersList = new RequestHeaders(*headersList);
	else
		newHeadersList = new RequestHeaders();

	// Add Range header if specified
	if (rangeStart > 0 || rangeLength > 0) {
		Common::String rangeHeader = Common::String::format("Range: bytes=%llu-%s",
															rangeStart,
															rangeLength > 0 ? Common::String::format("%llu", rangeStart + rangeLength - 1).c_str() : "");

		newHeadersList->push_back(rangeHeader);
		debug(5, "Adding range header: %s", rangeHeader.c_str());
	}

	NetworkReadStreamEmscripten *stream = new NetworkReadStreamEmscripten(url, newHeadersList, formFields, formFiles, keepAlive, keepAliveIdle, keepAliveInterval);

	// We created our own copy of headers, so delete it
	// delete newHeadersList;

	return stream;
}

NetworkReadStream *NetworkReadStream::make(const char *url, RequestHeaders *headersList, const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post, bool keepAlive, long keepAliveIdle, long keepAliveInterval, uint64 rangeStart, uint64 rangeLength) {
	// Create a copy of headers list that we can modify
	RequestHeaders *newHeadersList = nullptr;
	if (headersList)
		newHeadersList = new RequestHeaders(*headersList);
	else
		newHeadersList = new RequestHeaders();

	// Add Range header if specified
	if (rangeStart > 0 || rangeLength > 0) {
		Common::String rangeHeader = Common::String::format("Range: bytes=%llu-%s",
															rangeStart,
															rangeLength > 0 ? Common::String::format("%llu", rangeStart + rangeLength - 1).c_str() : "");

		newHeadersList->push_back(rangeHeader);
		debug(5, "Adding range header: %s", rangeHeader.c_str());
	}

	NetworkReadStreamEmscripten *stream = new NetworkReadStreamEmscripten(url, headersList, buffer, bufferSize, uploading, usingPatch, post, keepAlive, keepAliveIdle, keepAliveInterval);

	// We created our own copy of headers, so delete it
	// delete newHeadersList;

	return stream;
}

void NetworkReadStreamEmscripten::resetStream() {
	_eos = false;
	// Note: httpFetchIsSuccessful is a function, not assignable
	_sendingContentsSize = _sendingContentsPos = 0;
	// _progressDownloaded = _progressTotal = 0;
	_readPos = 0; // Reset read position
	_fetchId = 0; // Reset to invalid ID
	//_headersList = nullptr;
}

void NetworkReadStreamEmscripten::initFetch() {
	static bool initialized = false;
	if (!initialized) {
		httpFetchInit();
		initialized = true;
	}

	// resetStream();
}

void NetworkReadStreamEmscripten::setupBufferContents(const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post) {
	const char *method = "GET";

	if (uploading) {
		method = "PUT";
	} else if (usingPatch) {
		method = "PATCH";
	} else if (post || bufferSize != 0) {
		method = "POST";
	}

	debug(5, "NetworkReadStreamEmscripten::setupBufferContents uploading %s usingPatch %s post %s -> method %s",
		  uploading ? "true" : "false", usingPatch ? "true" : "false", post ? "true" : "false",
		  method);

	// Convert header list
	int size = 0;
	char **_request_headers = nullptr;
	if (_headersList) {
		size = _headersList->size();
		debug(5, "_request_headers count: %d", size);

		_request_headers = new char *[size * 2 + 1];
		_request_headers[size * 2] = nullptr; // Null-terminate the array

		int i = 0;
		for (const Common::String &header : *_headersList) {
			// Find the colon separator
			uint colonPos = header.findFirstOf(':');
			if (colonPos == Common::String::npos) {
				warning("NetworkReadStreamEmscripten: Malformed header (no colon): %s", header.c_str());
				continue;
			}

			// Split into key and value parts
			Common::String key = header.substr(0, colonPos);
			Common::String value = header.substr(colonPos + 1);

			// Trim whitespace
			key.trim();
			value.trim();

			// Store key and value as separate strings
			_request_headers[i++] = scumm_strdup(key.c_str());
			_request_headers[i++] = scumm_strdup(value.c_str());
			debug(5, "_request_headers key='%s' value='%s'", key.c_str(), value.c_str());
		}
	}
	debug(5, "Starting fetch: %s %s", method, _url.c_str());
	// Start the fetch with individual parameters
	_fetchId = httpFetchStart(method, _url.c_str(),
							  (const char *)buffer, bufferSize,
							  _request_headers);
}

void NetworkReadStreamEmscripten::setupFormMultipart(const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles) {
	// Not implemented yet - this would require multipart/form-data construction
	error("NetworkReadStreamEmscripten::setupFormMultipart not implemented");
}
double NetworkReadStreamEmscripten::getProgress() const {
	uint64 numBytes = httpFetchGetNumBytes(_fetchId);
	if(numBytes == 0){ 
		return 0.0; // avoid division by zero in case totalBytes zero
	}
	uint64 totalBytes = httpFetchGetTotalBytes(_fetchId);
	debug(5, "NetworkReadStreamEmscripten::getProgress - Progress: %llu / %llu for %s", numBytes, totalBytes, _url.c_str());
	return (double)numBytes / (double)totalBytes;
}

/** Send <postFields>, using POST by default. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, RequestHeaders *headersList, const Common::String &postFields,
														 bool uploading, bool usingPatch, bool keepAlive, long keepAliveIdle, long keepAliveInterval, uint64 startPos, uint64 length) : _fetchId(0), _url(url), _headersList(headersList),
																																														_readPos(0),
																																														_startPos(startPos), _length(length),
																																														NetworkReadStream(keepAlive, keepAliveIdle, keepAliveInterval) {

	initFetch();
	setupBufferContents((const byte *)postFields.c_str(), postFields.size(), uploading, usingPatch, false);
}

/** Send <formFields>, <formFiles>, using POST multipart/form. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, RequestHeaders *headersList, const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles,
														 bool keepAlive, long keepAliveIdle, long keepAliveInterval, uint64 startPos, uint64 length) : _fetchId(0), _url(url), _headersList(headersList),
																																					   _readPos(0),
																																					   NetworkReadStream(keepAlive, keepAliveIdle, keepAliveInterval) {

	initFetch();
	setupFormMultipart(formFields, formFiles);
}

/** Send <buffer>, using POST by default. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, RequestHeaders *headersList, const byte *buffer, uint32 bufferSize,
														 bool uploading, bool usingPatch, bool post, bool keepAlive, long keepAliveIdle, long keepAliveInterval,
														 uint64 startPos, uint64 length) : _fetchId(0), _url(url), _headersList(headersList),
																						   _readPos(0),
																						   NetworkReadStream(keepAlive, keepAliveIdle, keepAliveInterval) {

	initFetch();
	setupBufferContents(buffer, bufferSize, uploading, usingPatch, post);
}

NetworkReadStreamEmscripten::~NetworkReadStreamEmscripten() {
	debug(5, "NetworkReadStreamEmscripten::~NetworkReadStreamEmscripten %s", _url.c_str());
	if (_fetchId) {
		debug(5, "~NetworkReadStreamEmscripten: httpFetchClose");
		httpFetchClose(_fetchId);
		_fetchId = 0;
	}
}

uint32 NetworkReadStreamEmscripten::read(void *dataPtr, uint32 dataSize) {
	if (!_fetchId || _eos || dataSize == 0) {
		warning("NetworkReadStreamEmscripten::read - Invalid state");
		return 0;
	}

	// Get direct pointer to JS buffer
	char *jsBuffer = httpFetchGetDataPointer(_fetchId);
	if (!jsBuffer) {
		//debug("NetworkReadStreamEmscripten::read - No JS buffer available (yet) for %s", _url.c_str());
		return 0;
	}

	// Get current number of bytes available
	uint32 numBytes = httpFetchGetNumBytes(_fetchId);

	// Calculate how many bytes we can actually read
	uint32 availableBytes = numBytes - _readPos;
	uint32 bytesToRead = (dataSize < availableBytes) ? dataSize : availableBytes;
	uint32 totalBytes = httpFetchGetTotalBytes(_fetchId);
	debug(5, "NetworkReadStreamEmscripten::read - Progress: %u / %u for %s, currently at %u Trying to read %u bytes", numBytes, totalBytes, _url.c_str(), _readPos, bytesToRead);

	if (bytesToRead == 0) {
		// Check if transfer is complete
		if (httpFetchIsCompleted(_fetchId))
			_eos = true;
		return 0;
	}

	// Copy data directly from JS buffer
	memcpy(dataPtr, jsBuffer + _readPos, bytesToRead);
	_readPos += bytesToRead;

	return bytesToRead;
}

bool NetworkReadStreamEmscripten::hasError() const {
	return !httpFetchIsSuccessful(_fetchId) && httpFetchIsCompleted(_fetchId);
}

const char *NetworkReadStreamEmscripten::getError() const {
	if (!hasError() || _fetchId == 0) {
		return "No error";
	}

	// Get error message directly from JavaScript side
	// TODO: This should be freed after use, but how?

	return httpFetchGetErrorMessage(_fetchId);
}

bool NetworkReadStreamEmscripten::eos() const {
	// Process any new data before checking end of stream
	return _eos;
}

long NetworkReadStreamEmscripten::httpResponseCode() const {
	// Process any new data before returning status code
	return _fetchId ? httpFetchGetStatus(_fetchId) : 0;
}

Common::String NetworkReadStreamEmscripten::currentLocation() const {
	return Common::String(_url);
}

Common::HashMap<Common::String, Common::String> NetworkReadStreamEmscripten::responseHeadersMap() const {

	Common::HashMap<Common::String, Common::String> headers;

	if (!_fetchId)
		return headers;

	char **responseHeaders = httpFetchGetResponseHeadersArray(_fetchId);
	if (!responseHeaders)
		return headers;

	for (int i = 0; responseHeaders[i * 2]; ++i) {
		headers[responseHeaders[i * 2]] = responseHeaders[(i * 2) + 1];
	}

	// Note: No need to free responseHeaders - it's managed by JavaScript side
	return headers;
}

} // namespace Networking
