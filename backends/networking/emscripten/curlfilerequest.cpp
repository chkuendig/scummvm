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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#ifdef USE_LIBCURL
#include <curl/curl.h>
#elif defined(EMSCRIPTEN)
#include "backends/networking/emscripten/slist.h"
#endif
#include "backends/networking/curl/connectionmanager.h"
#include "backends/networking/curl/networkreadstream.h"
#include "backends/networking/emscripten/curlfilerequest.h"
#include "common/debug.h"

namespace Networking {

CurlFileRequest::CurlFileRequest(MemoryWriteStreamDynamicCallback cb, ErrorCallback ecb, Common::String url) : CurlRequest(nullptr, ecb, url), _networkReadStreamCallback(cb), _contentsStream(DisposeAfterUse::NO),
																											   _buffer(new byte[CURL_FILE_REQUEST_BUFFER_SIZE]) {}

CurlFileRequest::~CurlFileRequest() {
	delete _networkReadStreamCallback;
	delete[] _buffer;
}

void CurlFileRequest::handle() {

	debug(8, "CurlFileRequest::handle() ");
	if (!_stream)
		_stream = makeStream();

	if (_stream) {
		uint32 readBytes = _stream->read(_buffer, CURL_FILE_REQUEST_BUFFER_SIZE);
		if (readBytes != 0)
			if (_contentsStream.write(_buffer, readBytes) != readBytes)
				warning("CurlFileRequest::handle - unable to write all the bytes into MemoryWriteStreamDynamic");

		if (_stream->eos()) {

			if (_contentsStream.size() > 0) {
				finishStream(&_contentsStream); // it's JSON even if's not 200 OK? That's fine!..
			} else {
				if (_stream->httpResponseCode() == 200) // no JSON, but 200 OK? That's fine!..
					finishStream(nullptr);
				else
					finishError(ErrorResponse(this, false, true, "", _stream->httpResponseCode()));
			}
		}
	}
}

void CurlFileRequest::restart() {
	if (_stream)
		delete _stream;
	_stream = nullptr;
	_contentsStream = Common::MemoryWriteStreamDynamic(DisposeAfterUse::YES);
	// with no stream available next handle() will create another one
}

void CurlFileRequest::finishStream(Common::MemoryWriteStreamDynamic *stream) {
	Request::finishSuccess();
	if (_networkReadStreamCallback)
		(*_networkReadStreamCallback)(MemoryWriteStreamDynamicResponse(this, stream)); // potential memory leak, free it in your callbacks!
}

} // End of namespace Networking
