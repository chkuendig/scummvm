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

#include "backends/fs/emscripten/cloud-readstream.h"
#include "backends/cloud/downloadrequest.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/debug.h"
#include "common/system.h"

CloudReadStream::CloudReadStream(Cloud::Storage *storage, const Common::String &fileId,
								 const Common::String &displayName, const Common::String &cachePath,
								 uint64 fileSize)
	: VirtualReadStream(displayName, cachePath, fileSize), _storage(storage), _fileId(fileId) {
}

CloudReadStream::~CloudReadStream() {
	// Nothing special to clean up
}

void CloudReadStream::fileDownloadedCallback(const Cloud::Storage::BoolResponse &response) {
	debug(5, "CloudReadStream::fileDownloadedCallback %s", _displayName.c_str());
}

void CloudReadStream::fileDownloadedErrorCallback(const Networking::ErrorResponse &_error) {
	error("CloudReadStream::fileDownloadedErrorCallback ErrorResponse %ld: %s", _error.httpResponseCode, _error.response.c_str());
}

void CloudReadStream::downloadChunk(uint32 chunkIndex, uint64 chunkStart, uint64 chunkLength) {
	if (chunkIndex >= _numChunks)
		return;

	Common::String chunkPath = getChunkPath(chunkIndex);

	POSIXFilesystemNode *chunkFile = new POSIXFilesystemNode(chunkPath);

	debug(5, "CloudReadStream: Downloading chunk %u: bytes %llu-%llu",
		  chunkIndex + 1, chunkStart, chunkStart + chunkLength - 1);

		  
	startDownloadProgress(chunkIndex, chunkLength);
	uint32 downloadStartTime = g_system->getMillis();

	// Download the chunk
	Cloud::DownloadRequest *downloadRequest = dynamic_cast<Cloud::DownloadRequest *>(_storage->downloadById(
		_fileId,
		Common::Path(chunkPath),
		new Common::Callback<CloudReadStream, const Cloud::Storage::BoolResponse &>(this, &CloudReadStream::fileDownloadedCallback),
		new Common::Callback<CloudReadStream, const Networking::ErrorResponse &>(this, &CloudReadStream::fileDownloadedErrorCallback),
		chunkStart,
		chunkLength));

	// Wait for download to complete with progress updates
	while (!chunkFile->exists()) {
		// Update download progress
		updateDownloadProgress(downloadRequest->getProgress() * chunkLength, chunkLength, downloadStartTime);
		g_system->delayMillis(10);
	}

	if (!chunkFile->exists()) {
		error("CloudReadStream: Failed to download chunk %u", chunkIndex + 1);
	}

	completeDownloadProgress(chunkLength);
}

#endif // EMSCRIPTEN
