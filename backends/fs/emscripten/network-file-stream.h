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

#ifndef BACKENDS_FS_EMSCRIPTEN_NETWORK_FILE_STREAM_H
#define BACKENDS_FS_EMSCRIPTEN_NETWORK_FILE_STREAM_H

#ifdef EMSCRIPTEN

#include "common/stream.h"
#include "backends/fs/posix/posix-fs.h"

/**
 * Abstract base class for network-based file streams that download files in chunks.
 * Provides common functionality for chunked downloading and caching.
 */
class NetworkFileStream : public Common::SeekableReadStream {
protected:
	Common::String _displayName;
	Common::String _baseCachePath;
	uint64 _fileSize;
	uint64 _currentPos;
	bool _eos;  // True when a read operation has hit EOF
	static const uint64 CHUNK_SIZE = 10 * 1024 * 1024; // 10MB chunks
	/*
	 * Note on CHUNK_SIZE: 10MB is probably too big, but avoids the following issue
	 * 
	 * When hosting on GitHub Pages, this needs to be high enough to not trigger any chunked downloads
	 * for files hosted there, as their servers don't work properly with range requests
	 * https://stackoverflow.com/questions/55914486/issue-making-range-requests-in-some-browsers
	 * https://github.com/bdon/ghpages-firefox-range-bug?tab=readme-ov-file
	 * NOTE: This breaks for all browser when proxying through Cloudflare, as they also can't handle
	 * 		 the responses (0 bytes for range requests from GH Pages)
	 */

	// Cache management
	Common::Array<bool> _downloadedChunks;
	uint32 _numChunks;
	bool _singleFullFile;  // True if we have the entire file in one download

	// Helper methods (implemented in base class)
	uint32 getChunkIndex(uint64 pos) const;
	Common::String getChunkPath(uint32 chunkIndex) const;
	bool isChunkDownloaded(uint32 chunkIndex) const;
	bool isFullFileDownloaded() const;
	void ensureChunkDownloaded(uint32 chunkIndex);

	// Abstract method to be implemented by subclasses
	virtual void downloadChunk(uint32 chunkIndex, uint64 chunkStart, uint64 chunkLength) = 0;

public:
	NetworkFileStream(const Common::String &displayName, const Common::String &cachePath, uint64 fileSize);
	virtual ~NetworkFileStream();

	// SeekableReadStream interface
	virtual bool eos() const override;
	virtual bool err() const override;
	virtual void clearErr() override;
	virtual uint32 read(void *dataPtr, uint32 dataSize) override;
	virtual int64 pos() const override;
	virtual int64 size() const override;
	virtual bool seek(int64 offset, int whence = SEEK_SET) override;
};

#endif // EMSCRIPTEN

#endif // BACKENDS_FS_EMSCRIPTEN_NETWORK_FILE_STREAM_H
