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

#define FORBIDDEN_SYMBOL_EXCEPTION_asctime
#define FORBIDDEN_SYMBOL_EXCEPTION_clock
#define FORBIDDEN_SYMBOL_EXCEPTION_ctime
#define FORBIDDEN_SYMBOL_EXCEPTION_difftime
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_getdate
#define FORBIDDEN_SYMBOL_EXCEPTION_gmtime
#define FORBIDDEN_SYMBOL_EXCEPTION_localtime
#define FORBIDDEN_SYMBOL_EXCEPTION_mktime
#define FORBIDDEN_SYMBOL_EXCEPTION_strcpy
#define FORBIDDEN_SYMBOL_EXCEPTION_strdup
#define FORBIDDEN_SYMBOL_EXCEPTION_time
#include <emscripten.h>
#include <emscripten/fetch.h>

#include "backends/networking/emscripten/connectionmanager-emscripten.h"
#include "backends/networking/emscripten/networkreadstream-emscripten.h"
#include "backends/networking/emscripten/slist.h"
#include "base/version.h"
#include "common/debug.h"

namespace Networking {

/* Default Constructor*/
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, curl_slist *headersList, bool keepAlive, long keepAliveIdle, long keepAliveInterval)
	: _emscripten_fetch_attr(new emscripten_fetch_attr_t()),

	  _emscripten_fetch_url(url),
	  _backingStream(DisposeAfterUse::YES), _keepAlive(keepAlive),
	  _keepAliveIdle(keepAliveIdle),
	  _keepAliveInterval(keepAliveInterval),
	  _errorBuffer(nullptr),
	  _errorCode(CURLE_OK) {

	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten default constructor url: %s", url);
	resetStream();
	emscripten_fetch_attr_init(_emscripten_fetch_attr);

	// convert header linked list
	// first get size of list
	curl_slist *p = headersList;
	int size = 0;
	while (p != NULL) {

		debug(5, "_emscripten_request_headers %s", p->data);
		p = p->next;
		size = size + 1;
	}
	p = headersList; // reset pointer to beginning of list
	_emscripten_request_headers = new char *[size * 2 + 1];
	_emscripten_request_headers[size * 2] = 0; // header array needs to be null-terminated.

	int i = 0;
	while (p != NULL) {
		// see https://stackoverflow.com/a/49258912
		char *dup = strdup(p->data);
		char *tok = strtok(dup, ":");

		while (tok != NULL) {
			_emscripten_request_headers[i++] = tok;
			debug(5, "_emscripten_request_headers %s", tok);
			tok = strtok(NULL, ":");
		}

		p = p->next;
		// free(dup);
		// TODO: We should figure out why we can't free this (or why we need to copy in the first place... )
	}

	// debug(5,"_emscripten_request_headers %s", _emscripten_request_headers);
	_emscripten_fetch_attr->requestHeaders = _emscripten_request_headers;
	strcpy(_emscripten_fetch_attr->requestMethod, "GET"); // todo: move down to setup buffer contents
	_emscripten_fetch_attr->attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	_emscripten_fetch_attr->onsuccess = emscriptenDownloadSucceeded;
	_emscripten_fetch_attr->onprogress = emscriptenDownloadProgress;
	_emscripten_fetch_attr->onerror = emscriptenDownloadFailed;
	_emscripten_fetch_attr->userData = this;

	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::initCurl %s", _emscripten_fetch_url);
}

/** Send <postFields>, using POST by default. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, curl_slist *headersList, Common::String postFields, bool uploading, bool usingPatch, bool keepAlive, long keepAliveIdle, long keepAliveInterval)
	: NetworkReadStreamEmscripten(url, headersList, keepAlive, keepAliveIdle, keepAliveInterval) {
	setupBufferContents((const byte *)postFields.c_str(), postFields.size(), uploading, usingPatch, false);
}
/** Send <formFields>, <formFiles>, using POST multipart/form. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, curl_slist *headersList, Common::HashMap<Common::String, Common::String> formFields, Common::HashMap<Common::String, Common::Path> formFiles, bool keepAlive, long keepAliveIdle, long keepAliveInterval)
	: NetworkReadStreamEmscripten(url, headersList, keepAlive, keepAliveIdle, keepAliveInterval) {
	setupFormMultipart(formFields, formFiles);
}

/** Send <buffer>, using POST by default. */
NetworkReadStreamEmscripten::NetworkReadStreamEmscripten(const char *url, curl_slist *headersList, const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post, bool keepAlive, long keepAliveIdle, long keepAliveInterval)
	: NetworkReadStreamEmscripten(url, headersList, keepAlive, keepAliveIdle, keepAliveInterval) {
	setupBufferContents(buffer, bufferSize, uploading, usingPatch, post);
}

void NetworkReadStreamEmscripten::emscriptenHandleDownload(emscripten_fetch_t *fetch, bool success) {
	NetworkReadStreamEmscripten *stream = (NetworkReadStreamEmscripten *)fetch->userData;
	if (success)
		debug(5, "NetworkReadStreamEmscripten::emscriptenHandleDownload Finished downloading %llu bytes from URL %s .", fetch->numBytes, fetch->url);
	else
		debug(5, "NetworkReadStreamEmscripten::emscriptenHandleDownload Downloading %s failed, HTTP failure status code: %d.", fetch->url, fetch->status);
	if (fetch->numBytes > 0) {
		stream->_backingStream.write(fetch->data, fetch->numBytes); // todo: maybe this could be done continously during onprogress?
	}
	stream->setProgress(fetch->numBytes, fetch->numBytes);
	stream->finished(CURLE_OK); // todo: actually pass the result code from emscripten_fetch
	if (stream->_request) {
		
		int64 readPos = stream->_backingStream.pos();
		bool dataRead = true;
		Request *request = stream->_request;
		while (stream->_request && stream->_request->state() != FINISHED && dataRead && !ConnMan.timerStarted()) {

			debug(9, "NetworkReadStreamEmscripten::emscriptenHandleDownload state %d %s", request->state(), fetch->url);
			if (request->state() == PROCESSING) {
				request->handle();
			} else if (request->state() == RETRY) {
				request->handleRetry();
			}

			debug(9, "NetworkReadStreamEmscripten::emscriptenHandleDownload state updated %d %s ", request->state(), fetch->url);

			dataRead = (stream->_backingStream.pos() != readPos); // we check if handle is reading any data, if so we continue calling it (this is a workaround as emscripten doesn't have a timer)
			readPos = stream->_backingStream.pos();

		}
	}
}

void NetworkReadStreamEmscripten::emscriptenDownloadSucceeded(emscripten_fetch_t *fetch) {
	// debug(5,"NetworkReadStreamEmscripten::emscriptenDownloadSucceeded %s", fetch->url);
	emscriptenHandleDownload(fetch, true);
}

void NetworkReadStreamEmscripten::emscriptenDownloadProgress(emscripten_fetch_t *fetch) {
	/*
	if (fetch->totalBytes) {
		debug(5,"Downloading %s.. %.2f percent complete.", fetch->url, fetch->dataOffset * 100.0 / fetch->totalBytes);
	} else {
		debug(5,"Downloading %s.. %lld bytes complete.", fetch->url, fetch->dataOffset + fetch->numBytes);
	}
	debug(5,"Downloading %s.. %.2f %s complete. HTTP readyState: %hu. HTTP status: %hu - "
			"HTTP statusText: %s. Received chunk [%llu, %llu]",
			fetch->url,
			fetch->totalBytes > 0 ? (fetch->dataOffset + fetch->numBytes) * 100.0 / fetch->totalBytes : (fetch->dataOffset + fetch->numBytes),
			fetch->totalBytes > 0 ? "percent" : " bytes",
			fetch->readyState,
			fetch->status,
			fetch->statusText,
			fetch->dataOffset,
			fetch->dataOffset + fetch->numBytes);
	*/
	NetworkReadStreamEmscripten *stream = (NetworkReadStreamEmscripten *)fetch->userData;
	if (stream) {
		stream->setProgress(fetch->dataOffset, fetch->totalBytes);
	}
	
	if (stream->_request) {
		Request *request = stream->_request;
			if (request->state() == PROCESSING)
				request->handle();
	}
}

void NetworkReadStreamEmscripten::emscriptenDownloadFailed(emscripten_fetch_t *fetch) {
	emscriptenHandleDownload(fetch, false);
}

void NetworkReadStreamEmscripten::resetStream() {
	_eos = _requestComplete = false;
	// todo: implement emscripten_fetch_close
	// debug(5,"EMSCRIPTEN CURL: NetworkReadStreamEmscripten::resetStream url set %s", _emscripten_fetch_url);
	_sendingContentsSize = _sendingContentsPos = 0;
	_progressDownloaded = _progressTotal = 0;
	_bufferCopy = nullptr;
}

bool NetworkReadStreamEmscripten::reuseCurl(const char *url, curl_slist *headersList) {
	if (!_keepAlive) {
		debug(5, "NetworkReadStreamEmscripten: Can't reuse curl handle (was not setup as keep-alive)");
		return false;
	}

	resetStream();

	// debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::reuseCurl");
	return true;
}

void NetworkReadStreamEmscripten::setupBufferContents(const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post) {

	if (uploading) {
		strcpy(_emscripten_fetch_attr->requestMethod, "PUT");
		_emscripten_fetch_attr->requestDataSize = bufferSize;
		_emscripten_fetch_attr->requestData = (const char *)buffer;

	} else if (usingPatch) {
		strcpy(_emscripten_fetch_attr->requestMethod, "PATCH");
	} else {
		if (post || bufferSize != 0) {

			strcpy(_emscripten_fetch_attr->requestMethod, "POST");
			_emscripten_fetch_attr->requestDataSize = bufferSize;
			_emscripten_fetch_attr->requestData = (const char *)buffer;
		}
	}

	_emscripten_fetch = emscripten_fetch(_emscripten_fetch_attr, _emscripten_fetch_url);
	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::setupBufferContents");
}

void NetworkReadStreamEmscripten::setupFormMultipart(const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles) {
	// set POST multipart upload form fields/files

	error("EMSCRIPTEN CURL: NetworkReadStreamEmscripten::setupFormMultipart");
}

bool NetworkReadStreamEmscripten::reuse(const char *url, curl_slist *headersList, Common::String postFields, bool uploading, bool usingPatch) {
	debug(5, "NetworkReadStreamEmscripten::reuse 1 %s", url);

	if (!reuseCurl(url, headersList))
		return false;

	_backingStream = Common::MemoryReadWriteStream(DisposeAfterUse::YES);
	setupBufferContents((const byte *)postFields.c_str(), postFields.size(), uploading, usingPatch, false);
	return true;
}

bool NetworkReadStreamEmscripten::reuse(const char *url, curl_slist *headersList, const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles) {

	debug(5, "NetworkReadStreamEmscripten::reuse 2 %s", url);
	if (!reuseCurl(url, headersList))
		return false;

	_backingStream = Common::MemoryReadWriteStream(DisposeAfterUse::YES);
	setupFormMultipart(formFields, formFiles);
	return true;
}

bool NetworkReadStreamEmscripten::reuse(const char *url, curl_slist *headersList, const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post) {
	debug(5, "NetworkReadStreamEmscripten::reuse 3 %s", url);
	if (!reuseCurl(url, headersList))
		return false;

	_backingStream = Common::MemoryReadWriteStream(DisposeAfterUse::YES);
	setupBufferContents(buffer, bufferSize, uploading, usingPatch, post);
	return true;
}

NetworkReadStream::~NetworkReadStream() {}

NetworkReadStreamEmscripten::~NetworkReadStreamEmscripten() {

	if (_emscripten_fetch)
		// emscripten_fetch_close(_emscripten_fetch);
		debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten emscripten_fetch_close");
	free(_bufferCopy);
	free(_errorBuffer);
}

bool NetworkReadStreamEmscripten::eos() const {
	return _eos;
}

uint32 NetworkReadStreamEmscripten::read(void *dataPtr, uint32 dataSize) {
	uint32 actuallyRead = _backingStream.read(dataPtr, dataSize);

	// debug(5,"NetworkReadStreamEmscripten::read %u %s %s %s", actuallyRead, _eos ? "_eos" : "not _eos", _requestComplete ? "_requestComplete" : "_request not Complete", _emscripten_fetch->url);

	if (actuallyRead == 0) {
		if (_requestComplete) {
			_eos = true;
			if (this->_request) {
				// debug(5,"NetworkReadStreamEmscripten::read delete request %s ", _emscripten_fetch_url);

				// delete _request;
				//_request = nullptr;
			}
		}
		return 0;
	}

	return actuallyRead;
}

void NetworkReadStreamEmscripten::finished(uint32 errorCode) {
	_requestComplete = true;

	const char *url = _emscripten_fetch_url;
	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::finished %i %s ", errorCode, url);

	_errorCode = errorCode;

	if (_errorCode == CURLE_OK) {
		debug(9, "NetworkReadStreamEmscripten: %s - Request succeeded", url);
	} else {
		debug(5,"NetworkReadStreamEmscripten: %s - Request failed (%d - %s)", url, _errorCode, getError());
	}
}

bool NetworkReadStreamEmscripten::hasError() const {
	return _errorCode != CURLE_OK;
}

const char *NetworkReadStreamEmscripten::getError() const {
	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::getError");
	return _errorBuffer;
}

long NetworkReadStreamEmscripten::httpResponseCode() const {
	// return 200;
	long responseCode = -1;
	if (_emscripten_fetch)
		responseCode = _emscripten_fetch->status;
	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::httpResponseCode %hu %hu ", _emscripten_fetch->status, responseCode);
	return responseCode;
}

Common::String NetworkReadStreamEmscripten::currentLocation() const {
	Common::String result = "";
	debug(5, "EMSCRIPTEN CURL: NetworkReadStreamEmscripten::currentLocation");
	return result;
}

Common::String NetworkReadStreamEmscripten::responseHeaders() const {
	return _responseHeaders;
}

Common::HashMap<Common::String, Common::String> NetworkReadStreamEmscripten::responseHeadersMap() const {
	// HTTP headers are described at RFC 2616: https://datatracker.ietf.org/doc/html/rfc2616#section-4.2
	// this implementation tries to follow it, but for simplicity it does not support multi-line header values

	Common::HashMap<Common::String, Common::String> headers;
	Common::String headerName, headerValue, trailingWhitespace;
	char c;
	bool readingName = true;

	for (uint i = 0; i < _responseHeaders.size(); ++i) {
		c = _responseHeaders[i];

		if (readingName) {
			if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
				// header names should not contain any whitespace, this is invalid
				// ignore what's been before
				headerName = "";
				continue;
			}
			if (c == ':') {
				if (!headerName.empty()) {
					readingName = false;
				}
				continue;
			}
			headerName += c;
			continue;
		}

		// reading value:
		if (c == ' ' || c == '\t') {
			if (headerValue.empty()) {
				// skip leading whitespace
				continue;
			} else {
				// accumulate trailing whitespace
				trailingWhitespace += c;
				continue;
			}
		}

		if (c == '\r' || c == '\n') {
			// not sure if RFC allows empty values, we'll ignore such
			if (!headerName.empty() && !headerValue.empty()) {
				// add header value
				// RFC allows header with the same name to be sent multiple times
				// and requires it to be equivalent of just listing all header values separated with comma
				// so if header already was met, we'll add new value to the old one
				headerName.toLowercase();
				if (headers.contains(headerName)) {
					headers[headerName] += "," + headerValue;
				} else {
					headers[headerName] = headerValue;
				}
			}

			headerName = "";
			headerValue = "";
			trailingWhitespace = "";
			readingName = true;
			continue;
		}

		// if we meet non-whitespace character, turns out those "trailing" whitespace characters were not so trailing
		headerValue += trailingWhitespace;
		trailingWhitespace = "";
		headerValue += c;
	}

	return headers;
}

uint32 NetworkReadStreamEmscripten::fillWithSendingContents(char *bufferToFill, uint32 maxSize) {
	uint32 sendSize = _sendingContentsSize - _sendingContentsPos;
	if (sendSize > maxSize)
		sendSize = maxSize;
	for (uint32 i = 0; i < sendSize; ++i) {
		bufferToFill[i] = _sendingContentsBuffer[_sendingContentsPos + i];
	}
	_sendingContentsPos += sendSize;
	return sendSize;
}

uint32 NetworkReadStreamEmscripten::addResponseHeaders(char *buffer, uint32 bufferSize) {
	_responseHeaders += Common::String(buffer, bufferSize);
	return bufferSize;
}

double NetworkReadStreamEmscripten::getProgress() const {
	if (_progressTotal < 1)
		return 0;
	return (double)_progressDownloaded / (double)_progressTotal;
}

void NetworkReadStreamEmscripten::setProgress(uint64 downloaded, uint64 total) {
	_progressDownloaded = downloaded;
	_progressTotal = total;
}

} // namespace Networking
