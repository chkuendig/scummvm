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
#include "common/system.h"

NetworkFileStream::NetworkFileStream(const Common::String &displayName, const Common::String &cachePath, uint64 fileSize)
	: _displayName(displayName), _baseCachePath(cachePath), _fileSize(fileSize), _currentPos(0), _eos(false) {
	
	// Calculate number of chunks needed
	_numChunks = (_fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE; // Round up division
	_downloadedChunks.resize(_numChunks);
	
	debug(5, "NetworkFileStream: Initialized for file %s, size %llu, chunks %u", 
	      _displayName.c_str(), _fileSize, _numChunks);
	
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
	if (chunkIndex >= _numChunks) return false;
	
	Common::String chunkPath = getChunkPath(chunkIndex);
	POSIXFilesystemNode chunkFile(chunkPath);
	return chunkFile.exists();
}

void NetworkFileStream::ensureChunkDownloaded(uint32 chunkIndex) {
	if (chunkIndex >= _numChunks) return;
	
	if (!_downloadedChunks[chunkIndex]) {
		// Re-check if chunk exists on disk (might have been downloaded by another stream)
		if (isChunkDownloaded(chunkIndex)) {
			_downloadedChunks[chunkIndex] = true;
		} else {
			warning("NetworkFileStream: Ensuring chunk %u is downloaded", chunkIndex + 1);
			downloadChunk(chunkIndex);
			warning("NetworkFileStream: Ensured chunk %u is downloaded", chunkIndex + 1);
		}
	}
}

bool NetworkFileStream::eos() const {
	return _eos ;
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

	uint32 totalBytesRead = 0;
	uint8 *outputPtr = (uint8*)dataPtr;
	
	while (totalBytesRead < dataSize) {
		// Figure out which chunk we need
		uint32 chunkIndex = getChunkIndex(_currentPos);
		uint64 chunkStart = chunkIndex * CHUNK_SIZE;
		uint64 offsetInChunk = _currentPos - chunkStart;
		
		// Ensure the chunk is downloaded
		ensureChunkDownloaded(chunkIndex);
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

#endif // EMSCRIPTEN
