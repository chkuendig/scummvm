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
#include "backends/fs/emscripten/network-file-stream.h"
#include "backends/fs/posix/posix-iostream.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/translation.h"

NetworkFileStream::NetworkFileStream(const Common::String &displayName, const Common::String &cachePath, uint64 fileSize)
	: _displayName(displayName), _baseCachePath(cachePath), _fileSize(fileSize), _currentPos(0), _eos(false),
	  _singleFullFile(false) {
	if (fileSize == 0) {
		_singleFullFile = true;
		debug(5, "NetworkFileStream::NetworkFileStream file empty %s", _displayName.c_str());
		Common::DumpFile *_localFile = new Common::DumpFile();
		if (!_localFile->open(Common::Path(_baseCachePath), true)) {
			warning("Storage: unable to open file to download into %s", _baseCachePath.c_str());
			return;
		}
		_localFile->close();
		return;
	}

	// Calculate number of chunks needed
	_numChunks = (_fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE; // Round up division
	_downloadedChunks.resize(_numChunks);
	debug(5, "NetworkFileStream: Initialized for file %s, size %llu, chunks %u",
		  _displayName.c_str(), _fileSize, _numChunks);

	// Check if the full file already exists
	debug(5, "NetworkFileStream: Checking if full file exists at: %s", _baseCachePath.c_str());
	if (isFullFileDownloaded()) {
		debug(5, "NetworkFileStream: Full file already exists - setting singleFullFile=true");
		_singleFullFile = true;
		return;
	}

	// Check which chunks already exist
	for (uint32 i = 0; i < _numChunks; i++) {
		_downloadedChunks[i] = isChunkDownloaded(i);
		if (_downloadedChunks[i]) {
			debug(5, "NetworkFileStream: Chunk %u already exists", i + 1);
		}
	}
}

NetworkFileStream::~NetworkFileStream() {
	// Nothing special to clean up
}

uint32 NetworkFileStream::getChunkIndex(uint64 pos) const {
	return (uint32)(pos / CHUNK_SIZE);
}

Common::String NetworkFileStream::getChunkPath(uint32 chunkIndex) const {
	return Common::String::format("%s.%03d", _baseCachePath.c_str(), chunkIndex + 1);
}

bool NetworkFileStream::isChunkDownloaded(uint32 chunkIndex) const {
	if (chunkIndex >= _numChunks)
		return false;

	Common::String chunkPath = getChunkPath(chunkIndex);
	POSIXFilesystemNode chunkFile(chunkPath);
	return chunkFile.exists();
}

bool NetworkFileStream::isFullFileDownloaded() const {
	POSIXFilesystemNode fullFile(_baseCachePath);
	if (!fullFile.exists())
		return false;
	int64 size = (PosixIoStream::makeFromPath(_baseCachePath, StdioStream::WriteMode_Read))->size();
	return size == (int64)_fileSize;
}

void NetworkFileStream::ensureChunkDownloaded(uint32 chunkIndex) {
	if (_singleFullFile) {
		debug(5, "NetworkFileStream: Chunk %u not needed - full file available", chunkIndex + 1);
		return; // No need to set individual chunk flags
	}

	if (!_downloadedChunks[chunkIndex]) {
		// Re-check if chunk exists on disk (might have been downloaded by another stream)
		if (isChunkDownloaded(chunkIndex)) {
			_downloadedChunks[chunkIndex] = true;
		} else {
			// Calculate chunk range
			uint64 chunkStart = chunkIndex * CHUNK_SIZE;
			uint64 chunkLength = CHUNK_SIZE;
			if (chunkStart + chunkLength > _fileSize) {
				chunkLength = _fileSize - chunkStart; // Last chunk may be smaller
			}
			downloadChunk(chunkIndex, chunkStart, chunkLength);

			Common::String chunkPath = getChunkPath(chunkIndex);
			POSIXFilesystemNode chunkFile(chunkPath);
			uint64 actualSize = (PosixIoStream::makeFromPath(chunkPath, StdioStream::WriteMode_Read))->size();

			if (actualSize == chunkLength) {
				_downloadedChunks[chunkIndex] = true;
				debug(5, "NetworkFileStream: Chunk %u completed successfully (%llu bytes)", chunkIndex + 1, actualSize);
			} else if (actualSize == _fileSize) {
				// Server sent the full file instead of just the chunk
				debug(5, "NetworkFileStream: Server sent full file (%llu bytes) instead of chunk %u (%llu bytes)",
					  actualSize, chunkIndex + 1, chunkLength);
				// Rename the chunk file to the full file path
				Common::String fullPath = _baseCachePath;
				rename(chunkPath.c_str(), fullPath.c_str());
				_singleFullFile = true;
				debug(5, "NetworkFileStream: Marked as single full file download");
			} else if (actualSize == chunkLength) {
				// Server sent expected chunk size, normal chunked download
				_downloadedChunks[chunkIndex] = true;
				debug(5, "NetworkFileStream: Chunk %u completed successfully (%llu bytes)", chunkIndex + 1, actualSize);
			} else {
				// Unexpected size, error out
				error("NetworkFileStream: Downloaded chunk %u has unexpected size: got %llu, expected %llu or %llu",
					  chunkIndex + 1, actualSize, chunkLength, _fileSize);
			}
			debug(5, "NetworkFileStream: Ensured chunk %u is downloaded for %s", chunkIndex + 1, _baseCachePath.c_str());
		}
	}
}

bool NetworkFileStream::eos() const {
	return _eos;
}

bool NetworkFileStream::err() const {
	return false; // We don't track error state for now
}

void NetworkFileStream::clearErr() {
	// Nothing to clear
}

uint32 NetworkFileStream::read(void *dataPtr, uint32 dataSize) {
	if (!dataPtr)
		return 0;

	// Read at most as many bytes as are still available...
	if (dataSize > _fileSize - _currentPos) {
		dataSize = _fileSize - _currentPos;
		_eos = true;
	}

	if (!_singleFullFile) {
		uint32 chunkIndex = getChunkIndex(_currentPos);
		// Ensure the chunk is downloaded
		ensureChunkDownloaded(chunkIndex);
	}
	// If we have the full file, read from it directly
	if (_singleFullFile) {
		Common::String fullPath = _baseCachePath;
		Common::SeekableReadStream *fullStream = PosixIoStream::makeFromPath(fullPath, StdioStream::WriteMode_Read);
		if (fullStream) {
			if (fullStream->seek(_currentPos)) {
				uint32 bytesRead = fullStream->read(dataPtr, dataSize);
				_currentPos += bytesRead;
				delete fullStream;
				return bytesRead;
			}
			delete fullStream;
		}
		warning("NetworkFileStream: Failed to read from full file, falling back to chunk method");
		_singleFullFile = false; // Fall back to chunk reading
	}

	uint32 totalBytesRead = 0;
	uint8 *outputPtr = (uint8 *)dataPtr;
	while (totalBytesRead < dataSize) {
		// Figure out which chunk we need
		uint32 chunkIndex = getChunkIndex(_currentPos);
		uint64 chunkStart = chunkIndex * CHUNK_SIZE;
		uint64 offsetInChunk = _currentPos - chunkStart;

		// Open the chunk file
		Common::String chunkPath = getChunkPath(chunkIndex);
		Common::SeekableReadStream *chunkStream = PosixIoStream::makeFromPath(chunkPath, StdioStream::WriteMode_Read);
		if (!chunkStream) {
			warning("NetworkFileStream: Failed to open chunk file: %s", chunkPath.c_str());
			break;
		}
		// Seek to the correct position in the chunk
		if (!chunkStream->seek(offsetInChunk)) {
			warning("NetworkFileStream: Failed to seek in chunk file");
			delete chunkStream;
			break;
		}
		// Calculate how much to read from this chunk
		uint64 chunkSize = (chunkIndex == _numChunks - 1) ? (_fileSize - chunkStart) : CHUNK_SIZE;
		uint64 bytesAvailableInChunk = chunkSize - offsetInChunk;
		uint32 bytesToReadFromChunk = MIN(dataSize - totalBytesRead, (uint32)bytesAvailableInChunk);
		// Read from the chunk
		uint32 bytesReadFromChunk = chunkStream->read(outputPtr + totalBytesRead, bytesToReadFromChunk);
		delete chunkStream;
		if (bytesReadFromChunk == 0) {
			break; // EOF or error
		}

		totalBytesRead += bytesReadFromChunk;
		_currentPos += bytesReadFromChunk;
		// If we read less than expected, we're done
		if (bytesReadFromChunk < bytesToReadFromChunk) {
			break;
		}
	}

	return totalBytesRead;
}

int64 NetworkFileStream::pos() const {
	return _currentPos;
}

int64 NetworkFileStream::size() const {
	return _fileSize;
}

bool NetworkFileStream::seek(int64 offset, int whence) {
	_eos = false; // seeking always cancels EOS
	int64 newPos = _currentPos;

	switch (whence) {
	case SEEK_SET:
		newPos = offset;
		break;
	case SEEK_CUR:
		newPos = _currentPos + offset;
		break;
	case SEEK_END:
		newPos = _fileSize + offset;
		break;
	default:
		return false;
	}

	if (newPos < 0 || newPos > (int64)_fileSize) {
		return false;
	}

	_currentPos = newPos;
	return true;
}

void NetworkFileStream::startDownloadProgress(uint32 chunkIndex, uint64 chunkLength) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}

	if (!system->isOverlayVisible()) {
		_downloadProgressShowedOverlay = true;
		system->showOverlay(true);
	}
	Common::U32String osdMessage = Common::U32String(Common::U32String::format(_("Downloading %s"), _displayName.c_str()));
	if (_numChunks > 1) {
		osdMessage = Common::U32String(Common::U32String::format(_("Downloading %s\n Part %u of %u"), _displayName.c_str(), chunkIndex + 1, _numChunks));
	}
	system->displayMessageOnOSD(osdMessage);
}

void NetworkFileStream::updateDownloadProgress(double progress, uint64 chunkLength, uint32 downloadStartTime) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}

	system->updateDownloadProgress(progress * chunkLength, chunkLength, system->getMillis() - downloadStartTime);
	if (system->getCloudIcon()->needsUpdate()) {
		system->getCloudIcon()->show(Cloud::CloudIcon::kSyncing);
		system->getCloudIcon()->update();
	}
	system->updateScreen();
}

void NetworkFileStream::completeDownloadProgress(uint64 chunkLength) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}
	if (_downloadProgressShowedOverlay) {
		_downloadProgressShowedOverlay = false;
		system->hideOverlay();
	}
	system->getCloudIcon()->show(Cloud::CloudIcon::kNone);
}

#endif // EMSCRIPTEN
