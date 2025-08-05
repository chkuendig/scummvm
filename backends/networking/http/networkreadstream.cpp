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

#include "networkreadstream.h"
#include "common/tokenizer.h"

#ifdef USE_LIBCURL
#include "backends/networking/http/curl/networkreadstream-curl.h"
#elif defined(EMSCRIPTEN)
#include "backends/networking/http/emscripten/networkreadstream-emscripten.h"
#endif

namespace Networking {

// Constructor implementations
NetworkReadStream::NetworkReadStream(const char *url, RequestHeaders *headersList, const Common::String &postFields, bool uploading, bool usingPatch, bool keepAlive, long keepAliveIdle, long keepAliveInterval) {
#ifdef USE_LIBCURL
	_impl = new NetworkReadStreamCurl(url, headersList, postFields, uploading, usingPatch, keepAlive, keepAliveIdle, keepAliveInterval);
#elif defined(EMSCRIPTEN)
	_impl = new NetworkReadStreamEmscripten(url, headersList, postFields, uploading, usingPatch, keepAlive, keepAliveIdle, keepAliveInterval);
#endif
}

NetworkReadStream::NetworkReadStream(const char *url, RequestHeaders *headersList, const Common::HashMap<Common::String, Common::String> &formFields, const Common::HashMap<Common::String, Common::Path> &formFiles, bool keepAlive, long keepAliveIdle, long keepAliveInterval) {
#ifdef USE_LIBCURL
	_impl = new NetworkReadStreamCurl(url, headersList, formFields, formFiles, keepAlive, keepAliveIdle, keepAliveInterval);
#elif defined(EMSCRIPTEN)
	_impl = new NetworkReadStreamEmscripten(url, headersList, formFields, formFiles, keepAlive, keepAliveIdle, keepAliveInterval);

#endif
}

NetworkReadStream::NetworkReadStream(const char *url, RequestHeaders *headersList, const byte *buffer, uint32 bufferSize, bool uploading, bool usingPatch, bool post, bool keepAlive, long keepAliveIdle, long keepAliveInterval) {
#ifdef USE_LIBCURL
	_impl = new NetworkReadStreamCurl(url, headersList, buffer, bufferSize, uploading, usingPatch, post, keepAlive, keepAliveIdle, keepAliveInterval);
#elif defined(EMSCRIPTEN)
	_impl = new NetworkReadStreamEmscripten(url, headersList, buffer, bufferSize, uploading, usingPatch, post, keepAlive, keepAliveIdle, keepAliveInterval);

#endif
}

NetworkReadStream::~NetworkReadStream() {

	delete _impl;
}

uint32 NetworkReadStreamImplementation::fillWithSendingContents(char *bufferToFill, uint32 maxSize) {
	uint32 sendSize = _sendingContentsSize - _sendingContentsPos;
	if (sendSize > maxSize)
		sendSize = maxSize;
	for (uint32 i = 0; i < sendSize; ++i) {
		bufferToFill[i] = _sendingContentsBuffer[_sendingContentsPos + i];
	}
	_sendingContentsPos += sendSize;
	return sendSize;
}

uint32 NetworkReadStreamImplementation::addResponseHeaders(char *buffer, uint32 bufferSize) {
	_responseHeaders += Common::String(buffer, bufferSize);
	return bufferSize;
}

double NetworkReadStreamImplementation::getProgress() const {
	if (_progressTotal < 1)
		return 0;
	return (double)_progressDownloaded / (double)_progressTotal;
}

void NetworkReadStreamImplementation::setProgress(uint64 downloaded, uint64 total) {
	_progressDownloaded = downloaded;
	_progressTotal = total;
}

uint32 NetworkReadStreamImplementation::read(void *dataPtr, uint32 dataSize) {
	uint32 actuallyRead = _backingStream.read(dataPtr, dataSize);

	if (actuallyRead == 0) {
		if (_requestComplete)
			_eos = true;
		return 0;
	}

	return actuallyRead;
}

} // End of namespace Networking
