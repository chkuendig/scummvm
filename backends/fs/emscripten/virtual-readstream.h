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

#ifndef BACKENDS_FS_EMSCRIPTEN_VIRTUAL_READSTREAM_H
#define BACKENDS_FS_EMSCRIPTEN_VIRTUAL_READSTREAM_H

#ifdef EMSCRIPTEN

#include "backends/fs/posix/posix-fs.h"
#include "common/stream.h"

/**
 * Abstract base class for read streams for virtual file nodes backed by files accessed over the network.
 *
 * Provides common functionality for chunked downloading and caching of remote files,
 * including progress reporting and local file system caching.
 */
class VirtualReadStream : public Common::SeekableReadStream {
protected:
	Common::String _displayName;
	Common::String _baseCachePath;
	uint64 _size;
	uint64 _pos;
	bool _eos; // True when a read operation has hit EOF
	bool _downloadProgressShowedOverlay = false;
	static const uint64 CHUNK_SIZE = 5 * 1024 * 1024; // 5MB chunks
	/*
	 * Note on CHUNK_SIZE: This needs to be higher than the largest file hosted on Github Pages
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
	bool _singleFullFile; // True if we have the entire file in one download

	POSIXFilesystemNode *_cachedFileNode;          // Node for cached version of the file
	Common::SeekableReadStream *_cachedFileStream; // Stream to cached file

	// Helper methods (implemented in base class)
	uint32 getChunkIndex(uint64 pos) const;
	Common::String getChunkPath(uint32 chunkIndex) const;
	bool isChunkDownloaded(uint32 chunkIndex) const;
	bool isFullFileDownloaded() const;
	void ensureChunkDownloaded(uint32 chunkIndex);

	/**
	 * Session-wide LRU accounting for downloaded chunk files. The chunk cache
	 * lives in MEMFS (i.e. on the wasm heap), so without a bound a long session
	 * streaming a large game (voice files alone can be several hundred MB)
	 * grows the heap towards MAXIMUM_MEMORY and eventually OOMs the tab.
	 * Chunks are only opened for the duration of a single read(), so evicted
	 * chunks are simply re-downloaded on the next access (see read()).
	 * Full-file downloads are not tracked: they only occur for files smaller
	 * than CHUNK_SIZE or servers without range support, and evicting them
	 * could not be recovered mid-stream.
	 */
	class ChunkCacheRegistry {
	public:
		/** Record (or refresh) a chunk file and enforce the cache cap. */
		void noteChunk(const Common::String &path, uint64 size);
		/** Mark a chunk as recently used. */
		void touch(const Common::String &path);
		/** Forget a path without unlinking (e.g. chunk renamed to full file). */
		void forget(const Common::String &path);

	private:
		struct Entry {
			Common::String path;
			uint64 size;
			uint32 lastUse;
		};
		void enforceCap(const Common::String &protectPath);
		uint64 cacheLimit() const;

		Common::Array<Entry> _entries;
		uint64 _totalSize = 0;
		uint32 _tick = 0;
	};
	static ChunkCacheRegistry &chunkCache();

	// Abstract method to be implemented by subclasses
	virtual void downloadChunk(uint32 chunkIndex, uint64 chunkStart, uint64 chunkLength) = 0;

	// Progress management methods
	void startDownloadProgress(uint32 chunkIndex, uint64 chunkLength);
	void updateDownloadProgress(uint64 currentBytes, uint64 totalBytes, uint32 downloadStartTime, uint32 chunkIndex);
	void completeDownloadProgress(uint64 chunkLength);

	// Helper methods
	bool createCacheDirectory();
	void hideDownloadProgress();
	void reportDownloadProgress(const Common::String &fileName, uint64 downloadedBytes, uint64 totalBytes);

public:
	VirtualReadStream(const Common::String &displayName, const Common::String &baseCachePath, uint64 fileSize);
	virtual ~VirtualReadStream();

	// SeekableReadStream interface
	virtual bool eos() const override;
	virtual uint32 read(void *dataPtr, uint32 dataSize) override;
	virtual int64 pos() const override;
	virtual int64 size() const override;
	virtual bool seek(int64 offset, int whence = SEEK_SET) override;

private:
	uint32 readFullFile(void *dataPtr, uint32 dataSize);
};

#endif // EMSCRIPTEN

#endif
