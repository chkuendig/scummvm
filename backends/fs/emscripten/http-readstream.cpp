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

#include "backends/fs/emscripten/http-readstream.h"
#include "backends/networking/http/connectionmanager.h"
#include "backends/networking/http/sessionrequest.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/debug.h"
#include "common/system.h"

HttpReadStream::HttpReadStream(const Common::String &url, const Common::String &displayName,
							   const Common::String &cachePath, uint64 fileSize)
	: VirtualReadStream(displayName, cachePath, fileSize), _url(url) {
}

HttpReadStream::~HttpReadStream() {
	// Nothing special to clean up
}
void HttpReadStream::downloadFileCallback(const Networking::DataResponse &response) {
	debug(5, "HttpReadStream::downloadFileCallback called - processing download progress");
	// SessionRequest handles the file writing automatically
	// No need to track completion here
}

void HttpReadStream::errorCallbackDownloadFile(const Networking::ErrorResponse &errorResponse) {
	error("HttpReadStream::errorCallbackDownloadFile called - %s", errorResponse.response.c_str());
	// Error is handled by SessionRequest
}

void HttpReadStream::downloadChunk(uint32 chunkIndex, uint64 chunkStart, uint64 chunkLength) {
	assert(chunkIndex < _numChunks);

	Common::String chunkPath = getChunkPath(chunkIndex);

	POSIXFilesystemNode *chunkFile = new POSIXFilesystemNode(chunkPath);

	debug(5, "HttpReadStream: Downloading chunk %u: bytes %llu-%llu",
		  chunkIndex + 1, chunkStart, chunkStart + chunkLength - 1);

	// Create callbacks following the ScummVMCloud::startDownloadAsync pattern
	Networking::DataCallback callback = new Common::Callback<HttpReadStream, const Networking::DataResponse &>((HttpReadStream *)this, &HttpReadStream::downloadFileCallback);
	Networking::ErrorCallback errorCallback = new Common::Callback<HttpReadStream, const Networking::ErrorResponse &>((HttpReadStream *)this, &HttpReadStream::errorCallbackDownloadFile);

	// Create and start the download request using SessionRequest
	Networking::SessionRequest *request = new Networking::SessionRequest(_url, Common::Path(chunkPath), callback, errorCallback);

	// Add Range header for chunk download
	Common::String rangeHeaderLine = Common::String::format("Range: bytes=%llu-%llu", chunkStart, chunkStart + chunkLength - 1);
	request->addHeader(rangeHeaderLine);

	// Start the download - this sets state to PROCESSING
	request->start();

	// Wait for completion by checking both request state AND file existence/size
	// This matches the original HTTPFilesystemNode pattern

	startDownloadProgress(chunkIndex, chunkLength);
	uint32 downloadStartTime = g_system->getMillis();
	while (!chunkFile->exists()) {
		updateDownloadProgress(request->getProgress() * chunkLength, chunkLength, downloadStartTime);
		g_system->delayMillis(10);
	}
	debug(5, "HTTPFilesystemNode::createReadStream() download completed for %s", chunkPath.c_str());
	if (!request->success()) {
		error("HttpReadStream: Failed to download chunk %u", chunkIndex + 1);
	}
	completeDownloadProgress(chunkLength);

	request->close();
}

#endif // EMSCRIPTEN
