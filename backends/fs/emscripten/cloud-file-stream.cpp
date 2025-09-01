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

#include "backends/fs/emscripten/cloud-file-stream.h"
#include "backends/cloud/downloadrequest.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/debug.h"
#include "common/system.h"

CloudFileStream::CloudFileStream(Cloud::Storage *storage, const Common::String &fileId,
								 const Common::String &displayName, const Common::String &cachePath,
								 uint64 fileSize)
	: NetworkFileStream(displayName, cachePath, fileSize), _storage(storage), _fileId(fileId) {
}

CloudFileStream::~CloudFileStream() {
	// Nothing special to clean up
}

void CloudFileStream::fileDownloadedCallback(const Cloud::Storage::BoolResponse &response) {
	warning("CloudFileStream::fileDownloadedCallback %s", _displayName.c_str());
}

void CloudFileStream::fileDownloadedErrorCallback(const Networking::ErrorResponse &_error) {
	error("CloudFileStream::fileDownloadedErrorCallback ErrorResponse %ld: %s", _error.httpResponseCode, _error.response.c_str());
}

void CloudFileStream::downloadChunk(uint32 chunkIndex) {
	if (chunkIndex >= _numChunks)
		return;

	Common::String chunkPath = getChunkPath(chunkIndex);

	POSIXFilesystemNode *chunkFile = new POSIXFilesystemNode(chunkPath);
	// Calculate chunk range
	uint64 chunkStart = chunkIndex * CHUNK_SIZE;
	uint64 chunkLength = CHUNK_SIZE;
	if (chunkStart + chunkLength > _fileSize) {
		chunkLength = _fileSize - chunkStart; // Last chunk may be smaller
	}

	debug(5, "CloudFileStream: Downloading chunk %u: bytes %llu-%llu",
		  chunkIndex + 1, chunkStart, chunkStart + chunkLength - 1);

	// Show progress bar with chunk info
	Common::String progressText = _displayName.c_str();
	if (_numChunks > 1)
		progressText = Common::String::format("%s - part %u/%u", _displayName.c_str(), chunkIndex + 1, _numChunks);
	httpShowProgressBar(progressText.c_str());

	// Download the chunk
	Cloud::DownloadRequest *downloadRequest = dynamic_cast<Cloud::DownloadRequest *>(_storage->downloadById(
		_fileId,
		Common::Path(chunkPath),
		new Common::Callback<CloudFileStream, const Cloud::Storage::BoolResponse &>(this, &CloudFileStream::fileDownloadedCallback),
		new Common::Callback<CloudFileStream, const Networking::ErrorResponse &>(this, &CloudFileStream::fileDownloadedErrorCallback),
		chunkStart,
		chunkLength));

	// Wait for download to complete with progress updates
	while (!chunkFile->exists()) {
		httpUpdateProgressBar(downloadRequest->getProgress() * chunkLength, chunkLength);
		g_system->delayMillis(10);
	}

	// Mark chunk as downloaded
	_downloadedChunks[chunkIndex] = true;
	debug(5, "CloudFileStream: Chunk %u completed", chunkIndex + 1);

	httpHideProgressBar();
}

#endif // EMSCRIPTEN
