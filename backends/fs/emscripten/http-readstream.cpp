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

namespace {
// Chunk downloads share the hosting behind this demo with everything else on
// the CDN/origin, so a transient 429 (rate limit) or 502/503/504 (upstream
// hiccup) - or the connection being interrupted before headers arrive, coded
// here as HTTP code 0 - is expected occasionally, not exceptional. Retrying a
// handful of times with a short linear backoff is enough for those to clear;
// anything else (404 etc.) is a real failure and stays fatal below.
const int kMaxDownloadAttempts = 4;
const uint32 kRetryBaseDelayMs = 500;

bool isRetryableHttpStatus(long httpResponseCode) {
	switch (httpResponseCode) {
	case 0: // request interrupted/aborted before a status was received
	case 429:
	case 500:
	case 502:
	case 503:
	case 504:
		return true;
	default:
		return false;
	}
}
} // End of anonymous namespace

HttpReadStream::HttpReadStream(const Common::String &url, const Common::String &displayName,
							   const Common::String &cachePath, uint64 fileSize)
	: VirtualReadStream(displayName, cachePath, fileSize), _url(url),
	  _lastDownloadFailed(false), _lastDownloadRetryable(false), _lastHttpResponseCode(0) {
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
	_lastDownloadFailed = true;
	_lastHttpResponseCode = errorResponse.httpResponseCode;
	_lastErrorMessage = errorResponse.response;
	_lastDownloadRetryable = isRetryableHttpStatus(errorResponse.httpResponseCode);
	warning("HttpReadStream::errorCallbackDownloadFile called - %s (HTTP %ld)%s",
			errorResponse.response.c_str(), errorResponse.httpResponseCode,
			_lastDownloadRetryable ? ", will retry" : "");
	// Fatal handling (if not retried) happens in downloadChunk() once the
	// request has fully settled, so we don't abort mid-callback here.
}

void HttpReadStream::downloadChunk(uint32 chunkIndex, uint64 chunkStart, uint64 chunkLength) {
	assert(chunkIndex < _numChunks);

	Common::String chunkPath = getChunkPath(chunkIndex);

	for (int attempt = 1; attempt <= kMaxDownloadAttempts; attempt++) {
		debug(5, "HttpReadStream: Downloading chunk %u: bytes %llu-%llu (attempt %d/%d)",
			  chunkIndex + 1, chunkStart, chunkStart + chunkLength - 1, attempt, kMaxDownloadAttempts);

		_lastDownloadFailed = false;
		_lastDownloadRetryable = false;

		Networking::DataCallback callback = new Common::Callback<HttpReadStream, const Networking::DataResponse &>((HttpReadStream *)this, &HttpReadStream::downloadFileCallback);
		Networking::ErrorCallback errorCallback = new Common::Callback<HttpReadStream, const Networking::ErrorResponse &>((HttpReadStream *)this, &HttpReadStream::errorCallbackDownloadFile);

		// Create and start the download request using SessionRequest
		Networking::SessionRequest *request = new Networking::SessionRequest(_url, Common::Path(chunkPath), callback, errorCallback);

		// Add Range header for chunk download
		Common::String rangeHeaderLine = Common::String::format("Range: bytes=%llu-%llu", chunkStart, chunkStart + chunkLength - 1);
		request->addHeader(rangeHeaderLine);

		// Start the download - this sets state to PROCESSING
		request->start();

		startDownloadProgress(chunkIndex, chunkLength);
		uint32 downloadStartTime = g_system->getMillis();
		while (request->state() == Networking::RequestState::PROCESSING) {
			updateDownloadProgress(request->getProgress() * chunkLength, chunkLength, downloadStartTime, chunkIndex);
			g_system->delayMillis(10);
		}
		debug(5, "HTTPFilesystemNode::createReadStream() download completed for %s", chunkPath.c_str());

		bool ok = request->success() && !_lastDownloadFailed;
		request->close();

		if (ok) {
			completeDownloadProgress(chunkLength);
			return;
		}

		if (!_lastDownloadRetryable || attempt == kMaxDownloadAttempts) {
			completeDownloadProgress(chunkLength);
			error("HttpReadStream: Failed to download chunk %u: %s (HTTP %ld)",
				  chunkIndex + 1, _lastErrorMessage.c_str(), _lastHttpResponseCode);
		}

		warning("HttpReadStream: Chunk %u download failed (HTTP %ld), retrying (%d/%d)...",
				chunkIndex + 1, _lastHttpResponseCode, attempt, kMaxDownloadAttempts);
		g_system->delayMillis(kRetryBaseDelayMs * attempt);
	}
}

#endif // EMSCRIPTEN
