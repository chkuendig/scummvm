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
#define FORBIDDEN_SYMBOL_EXCEPTION_unlink

#include <unistd.h>

#include "backends/fs/emscripten/virtual-readstream.h"
#include "backends/fs/posix/posix-iostream.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/std/algorithm.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#ifdef USE_CLOUD
#include "backends/cloud/cloudmanager.h"
#endif
VirtualReadStream::VirtualReadStream(const Common::String &displayName, const Common::String &cachePath, uint64 fileSize)
	: _displayName(displayName), _baseCachePath(cachePath), _size(fileSize), _pos(0), _eos(false),
	  _singleFullFile(false) {
	if (fileSize == 0) {
		_singleFullFile = true;
		debug(5, "VirtualReadStream::VirtualReadStream file empty %s", _displayName.c_str());
		Common::DumpFile *_localFile = new Common::DumpFile();
		if (!_localFile->open(Common::Path(_baseCachePath), true)) {
			warning("Storage: unable to open file to download into %s", _baseCachePath.c_str());
			return;
		}
		_localFile->close();
		return;
	}

	// Calculate number of chunks needed
	_numChunks = (_size + CHUNK_SIZE - 1) / CHUNK_SIZE; // Round up division
	_downloadedChunks.resize(_numChunks);
	debug(5, "VirtualReadStream: Initialized for file %s, size %llu, chunks %u",
		  _displayName.c_str(), _size, _numChunks);

	// Check if the full file already exists
	debug(5, "VirtualReadStream: Checking if full file exists at: %s", _baseCachePath.c_str());
	if (isFullFileDownloaded()) {
		debug(5, "VirtualReadStream: Full file already exists - setting singleFullFile=true");
		_singleFullFile = true;
		return;
	}

	// Check which chunks already exist
	for (uint32 i = 0; i < _numChunks; i++) {
		_downloadedChunks[i] = isChunkDownloaded(i);
		if (_downloadedChunks[i]) {
			debug(5, "VirtualReadStream: Chunk %u already exists", i + 1);
			// Account pre-existing chunks (e.g. from an earlier stream on the
			// same file) in the session-wide cache registry.
			Common::String chunkPath = getChunkPath(i);
			Common::SeekableReadStream *chunk = PosixIoStream::makeFromPath(chunkPath, StdioStream::WriteMode_Read);
			if (chunk) {
				chunkCache().noteChunk(chunkPath, (uint64)chunk->size());
				delete chunk;
			}
		}
	}
}

VirtualReadStream::~VirtualReadStream() {
	// Nothing special to clean up
}

VirtualReadStream::ChunkCacheRegistry &VirtualReadStream::chunkCache() {
	static ChunkCacheRegistry registry;
	return registry;
}

uint64 VirtualReadStream::ChunkCacheRegistry::cacheLimit() const {
	// Default cap for the MEMFS chunk cache. Overridable (in megabytes) via
	// the config file for testing or constrained deployments; 0 disables
	// eviction entirely.
	static const uint64 kDefaultLimit = 96 * 1024 * 1024;
	if (ConfMan.hasKey("vfs_cache_limit"))
		return (uint64)ConfMan.getInt("vfs_cache_limit") * 1024 * 1024;
	return kDefaultLimit;
}

void VirtualReadStream::ChunkCacheRegistry::noteChunk(const Common::String &path, uint64 size) {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_entries[i].path == path) {
			_totalSize += size - _entries[i].size;
			_entries[i].size = size;
			_entries[i].lastUse = ++_tick;
			enforceCap(path);
			return;
		}
	}
	Entry e;
	e.path = path;
	e.size = size;
	e.lastUse = ++_tick;
	_entries.push_back(e);
	_totalSize += size;
	enforceCap(path);
}

void VirtualReadStream::ChunkCacheRegistry::touch(const Common::String &path) {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_entries[i].path == path) {
			_entries[i].lastUse = ++_tick;
			return;
		}
	}
}

void VirtualReadStream::ChunkCacheRegistry::forget(const Common::String &path) {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_entries[i].path == path) {
			_totalSize -= _entries[i].size;
			_entries.remove_at(i);
			return;
		}
	}
}

void VirtualReadStream::ChunkCacheRegistry::enforceCap(const Common::String &protectPath) {
	const uint64 limit = cacheLimit();
	if (limit == 0)
		return;
	while (_totalSize > limit) {
		// Find the least-recently-used chunk, never the one just written/read.
		int victim = -1;
		for (uint i = 0; i < _entries.size(); i++) {
			if (_entries[i].path == protectPath)
				continue;
			if (victim < 0 || _entries[i].lastUse < _entries[(uint)victim].lastUse)
				victim = (int)i;
		}
		if (victim < 0)
			return; // Nothing evictable (single oversized chunk).
		debug(2, "VirtualReadStream: cache over limit (%llu > %llu), evicting %s (%llu bytes)",
			  _totalSize, limit, _entries[(uint)victim].path.c_str(), _entries[(uint)victim].size);
		unlink(_entries[(uint)victim].path.c_str());
		_totalSize -= _entries[(uint)victim].size;
		_entries.remove_at((uint)victim);
	}
}

uint32 VirtualReadStream::getChunkIndex(uint64 pos) const {
	return (uint32)(pos / CHUNK_SIZE);
}

Common::String VirtualReadStream::getChunkPath(uint32 chunkIndex) const {
	return Common::String::format("%s.%03d", _baseCachePath.c_str(), chunkIndex + 1);
}

bool VirtualReadStream::isChunkDownloaded(uint32 chunkIndex) const {
	assert(chunkIndex < _numChunks);

	Common::String chunkPath = getChunkPath(chunkIndex);
	POSIXFilesystemNode chunkFile(chunkPath);
	return chunkFile.exists();
}

bool VirtualReadStream::isFullFileDownloaded() const {
	POSIXFilesystemNode fullFile(_baseCachePath);
	if (!fullFile.exists())
		return false;
	Common::SeekableReadStream *full = PosixIoStream::makeFromPath(_baseCachePath, StdioStream::WriteMode_Read);
	if (!full)
		return false;
	int64 size = full->size();
	delete full;
	return size == (int64)_size;
}

void VirtualReadStream::ensureChunkDownloaded(uint32 chunkIndex) {
	if (_singleFullFile) {
		debug(5, "VirtualReadStream: Chunk %u not needed - full file available", chunkIndex + 1);
		return; // No need to set individual chunk flags
	}

	if (!_downloadedChunks[chunkIndex]) {
		// Re-check if chunk exists on disk (might have been downloaded by another stream instance in the meantime)
		if (isChunkDownloaded(chunkIndex)) {
			_downloadedChunks[chunkIndex] = true;
		} else {
			// Calculate chunk range
			uint64 chunkStart = chunkIndex * CHUNK_SIZE;
			uint64 chunkLength = CHUNK_SIZE;
			if (chunkStart + chunkLength > _size) {
				chunkLength = _size - chunkStart; // Last chunk may be smaller
			}
			debug(5, "VirtualReadStream: Downloading chunk %u: bytes %llu-%llu for file %s",
				  chunkIndex + 1, chunkStart, chunkStart + chunkLength - 1, _displayName.c_str());
			downloadChunk(chunkIndex, chunkStart, chunkLength);
			TimeDate curTime;
			g_system->getTimeAndDate(curTime);
			debug(5, "VirtualReadStream: Downloaded chunk %u for file %s at %02d:%02d", chunkIndex + 1, _displayName.c_str(), curTime.tm_min, curTime.tm_sec);
			Common::String chunkPath = getChunkPath(chunkIndex);
			Common::SeekableReadStream *downloaded = PosixIoStream::makeFromPath(chunkPath, StdioStream::WriteMode_Read);
			if (!downloaded) {
				// The chunk file could not be opened after downloading (e.g. the
				// local cache write failed because the cache directory does not
				// exist yet). Degrade gracefully rather than dereferencing null:
				// leave the chunk unmarked so read() returns the bytes it has.
				warning("VirtualReadStream: chunk %u unavailable after download for %s", chunkIndex + 1, _baseCachePath.c_str());
				return;
			}
			uint64 actualSize = downloaded->size();
			delete downloaded;

			if (actualSize == chunkLength) {
				_downloadedChunks[chunkIndex] = true;
				chunkCache().noteChunk(chunkPath, actualSize);
				debug(5, "VirtualReadStream: Chunk %u completed successfully (%llu bytes)", chunkIndex + 1, actualSize);
			} else if (actualSize == _size) {
				// Server sent the full file instead of just the chunk
				debug(5, "VirtualReadStream: Server sent full file (%llu bytes) instead of chunk %u (%llu bytes)",
					  actualSize, chunkIndex + 1, chunkLength);
				// Rename the chunk file to the full file path
				Common::String fullPath = _baseCachePath;
				rename(chunkPath.c_str(), fullPath.c_str());
				chunkCache().forget(chunkPath);
				_singleFullFile = true;
				debug(5, "VirtualReadStream: Marked as single full file download");
			} else if (actualSize == chunkLength) {
				// Server sent expected chunk size, normal chunked download
				_downloadedChunks[chunkIndex] = true;
				debug(5, "VirtualReadStream: Chunk %u completed successfully (%llu bytes)", chunkIndex + 1, actualSize);
			} else {
				// Unexpected size, error out
				error("VirtualReadStream: Downloaded chunk %u has unexpected size: got %llu, expected %llu or %llu",
					  chunkIndex + 1, actualSize, chunkLength, _size);
			}
			debug(5, "VirtualReadStream: Ensured chunk %u is downloaded for %s", chunkIndex + 1, _baseCachePath.c_str());
		}
	}
}

bool VirtualReadStream::eos() const {
	return _eos;
}

uint32 VirtualReadStream::readFullFile(void *dataPtr, uint32 dataSize) {
	Common::SeekableReadStream *fullStream = PosixIoStream::makeFromPath(_baseCachePath, StdioStream::WriteMode_Read);
	if (fullStream && fullStream->seek(_pos)) {
		uint32 bytesRead = fullStream->read(dataPtr, dataSize);
		_pos += bytesRead;
		delete fullStream;
		return bytesRead;
	}
	delete fullStream;
	return 0;
}

uint32 VirtualReadStream::read(void *dataPtr, uint32 dataSize) {
	if (!dataPtr)
		return 0;

	// Read at most as many bytes as are still available...
	if (dataSize > _size - _pos) {
		dataSize = _size - _pos;
		_eos = true;
	}

	// If we have the full file, read from it directly
	if (_singleFullFile) {
		return readFullFile(dataPtr, dataSize);
	}

	uint32 bytesRead = 0;
	uint32 retriedChunk = (uint32)-1; // one re-download retry per chunk per call
	uint8 *outputPtr = (uint8 *)dataPtr;
	while (bytesRead < dataSize) {
		// Figure out which chunk we need
		uint32 chunkIndex = getChunkIndex(_pos);
		uint64 chunkStart = chunkIndex * CHUNK_SIZE;
		uint64 offsetInChunk = _pos - chunkStart;

		// Ensure the chunk is downloaded
		ensureChunkDownloaded(chunkIndex);
		if (_singleFullFile) {
			// Server sent full file  (either because whole file is smaller than chunk or server doesn't support range requests)
			return readFullFile(dataPtr, dataSize);
		}
		// Open the chunk file
		Common::String chunkPath = getChunkPath(chunkIndex);
		Common::SeekableReadStream *chunkStream = PosixIoStream::makeFromPath(chunkPath, StdioStream::WriteMode_Read);
		if (!chunkStream) {
			// The chunk may have been evicted by the cache LRU since this
			// stream last saw it; clear the flag and retry (re-download) once.
			if (_downloadedChunks[chunkIndex] && chunkIndex != retriedChunk) {
				debug(2, "VirtualReadStream: chunk %u missing (evicted?), re-downloading", chunkIndex + 1);
				_downloadedChunks[chunkIndex] = false;
				retriedChunk = chunkIndex;
				continue;
			}
			warning("VirtualReadStream: Failed to open chunk file: %s", chunkPath.c_str());
			break;
		}
		chunkCache().touch(chunkPath);
		// Seek to the correct position in the chunk
		if (!chunkStream->seek(offsetInChunk)) {
			warning("VirtualReadStream: Failed to seek in chunk file");
			delete chunkStream;
			break;
		}
		// Calculate how much to read from this chunk
		uint64 chunkSize = (chunkIndex == _numChunks - 1) ? (_size - chunkStart) : CHUNK_SIZE;
		uint64 bytesAvailableInChunk = chunkSize - offsetInChunk;
		uint32 bytesToReadFromChunk = MIN(dataSize - bytesRead, (uint32)bytesAvailableInChunk);
		// Read from the chunk
		uint32 bytesReadFromChunk = chunkStream->read(outputPtr + bytesRead, bytesToReadFromChunk);
		delete chunkStream;
		if (bytesReadFromChunk == 0) {
			break; // EOF or error
		}

		bytesRead += bytesReadFromChunk;
		_pos += bytesReadFromChunk;
		// If we read less than expected, we're done
		if (bytesReadFromChunk < bytesToReadFromChunk) {
			break;
		}
	}
	return bytesRead;
}

int64 VirtualReadStream::pos() const {
	return _pos;
}

int64 VirtualReadStream::size() const {
	return _size;
}

bool VirtualReadStream::seek(int64 offset, int whence) {
	_eos = false; // seeking always cancels EOS
	int64 newPos = _pos;

	switch (whence) {
	case SEEK_SET:
		newPos = offset;
		break;
	case SEEK_CUR:
		newPos = _pos + offset;
		break;
	case SEEK_END:
		newPos = _size + offset;
		break;
	default:
		return false;
	}

	if (newPos < 0 || newPos > (int64)_size) {
		return false;
	}

	_pos = newPos;
	return true;
}

void VirtualReadStream::startDownloadProgress(uint32 chunkIndex, uint64 chunkLength) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}

	if (!system->isOverlayVisible()) {
		_downloadProgressShowedOverlay = true;
		system->showOverlay(true);
	}
}

void VirtualReadStream::updateDownloadProgress(uint64 currentBytes, uint64 totalBytes, uint32 downloadStartTime, uint32 chunkIndex) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}

	int elapsedMs = g_system->getMillis() - downloadStartTime;
	if (totalBytes <= 0 || elapsedMs <= 0) {
		return;
	}

	GraphicsManager *_graphicsManager = system->getGraphicsManager();
	double progress = (double)currentBytes / totalBytes;

	// Get overlay pixel format for proper color handling
	Graphics::PixelFormat overlayFormat = _graphicsManager->getOverlayFormat();

	// Get font for sizing calculations
	const Graphics::Font *localizedFont = FontMan.getFontByUsage(Graphics::FontManager::kLocalizedFont);
	int fontHeight = localizedFont->getFontHeight();

	debug(5, "updateDownloadProgress: currentBytes=%llu totalBytes=%llu elapsedMs=%d progress=%.2f", currentBytes, totalBytes, elapsedMs, progress);
	// Make progress bar appropriately sized but within overlay bounds
	int wRect = _graphicsManager->getOverlayWidth() * 0.7;
	int lRect = fontHeight * 3;                // Height is 3 times the font height
	int borderWidth = Std::max(1, lRect / 15); // At least 1 or height / 15

	// Create a temporary surface for drawing
	Graphics::Surface tempSurface;
	tempSurface.create(wRect, lRect, overlayFormat);

	int wProgressFill = (int)((wRect - 2 * borderWidth) * progress);
	uint32 bgColor = overlayFormat.ARGBToColor(128, 60, 60, 60); // Semi-transparent dark gray
	uint32 borderColor = overlayFormat.RGBToColor(0, 0, 0);      // Black border
	uint32 fillColor = overlayFormat.RGBToColor(255, 255, 255);  // White fill

	// Fill background
	tempSurface.fillRect(Common::Rect(0, 0, wRect, lRect), bgColor);
	// Draw border
	for (int b = 0; b < borderWidth; b++) {
		tempSurface.frameRect(Common::Rect(b, b, wRect - b, lRect - b), borderColor);
	}
	// Draw progress fill
	if (wProgressFill > 0) {
		tempSurface.fillRect(Common::Rect(borderWidth, borderWidth, borderWidth + wProgressFill, lRect - borderWidth), fillColor);
	}

	// Draw text elements

	// Calculate text positioning for centering both strings
	Common::String percentText = Common::String::format("%.0f%%", progress * 100);
	Common::U32String progressMessage = Common::U32String(Common::U32String::format(_("Loading %s"), _displayName.c_str()));
	if (_numChunks > 1) {
		progressMessage += Common::U32String::format(" (part #%d/%d)", chunkIndex + 1, _numChunks);
	}
	int percentWidth = localizedFont->getStringWidth(percentText);
	int textWidth = localizedFont->getStringWidth(progressMessage);

	// Calculate total height for both text lines
	int totalTextHeight = fontHeight * 2; // Two lines of text

	// Center the text group vertically
	int textGroupY = (lRect - totalTextHeight) / 2;

	// Draw progressMessage above percentage
	int textX = (wRect - textWidth) / 2;
	int textY = textGroupY;
	uint32 textColor = (textX < borderWidth + wProgressFill) && (textX + textWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
						   overlayFormat.RGBToColor(255, 255, 255);                                                                     // White text on dark background
	localizedFont->drawString(&tempSurface, progressMessage, textX, textY, textWidth, textColor, Graphics::kTextAlignCenter);

	// Draw percentage text (centered)
	int percentX = (wRect - percentWidth) / 2;
	int percentY = textGroupY + fontHeight;
	uint32 percentColor = (percentX < borderWidth + wProgressFill) && (percentX + percentWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
							  overlayFormat.RGBToColor(255, 255, 255);                                                                              // White text on dark background
	localizedFont->drawString(&tempSurface, percentText, percentX, percentY, percentWidth, percentColor, Graphics::kTextAlignCenter);

	// Progress bytes text (bottom-left aligned)
	const char *currentUnits, *totalUnits;
	Common::String currentBytesStr = Common::getHumanReadableBytes((uint64)currentBytes, currentUnits);
	Common::String totalBytesStr = Common::getHumanReadableBytes((uint64)totalBytes, totalUnits);
	Common::String progressText = Common::String::format("%s%s/%s%s", currentBytesStr.c_str(), currentUnits, totalBytesStr.c_str(), totalUnits);
	int progressWidth = localizedFont->getStringWidth(progressText);
	int progressX = borderWidth + 3;                                                                                                                    // Left aligned with small margin
	int progressY = lRect - localizedFont->getFontHeight() - borderWidth - 1;                                                                           // Bottom aligned with small margin
	uint32 progressColor = (progressX < borderWidth + wProgressFill) && (progressX + progressWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
							   overlayFormat.RGBToColor(255, 255, 255);                                                                                 // White text on dark background
	localizedFont->drawString(&tempSurface, progressText, progressX, progressY, progressWidth, progressColor, Graphics::kTextAlignLeft);

	// Speed text (bottom-right aligned)
	if (currentBytes > 0) {
		double bytesPerSecond = (double)currentBytes / (elapsedMs / 1000.0);
		const char *speedUnits;
		Common::String speed = Common::getHumanReadableBytes((uint64)bytesPerSecond, speedUnits);
		Common::String speedText = Common::String::format("%s %s/s", speed.c_str(), speedUnits);
		int speedWidth = localizedFont->getStringWidth(speedText);
		int speedX = wRect - speedWidth - borderWidth - 2;                                                                                      // Right aligned with small margin
		int speedY = lRect - localizedFont->getFontHeight() - borderWidth - 3;                                                                  // Bottom aligned with small margin
		uint32 speedColor = (speedX < borderWidth + wProgressFill) && (speedX + speedWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
								overlayFormat.RGBToColor(255, 255, 255);                                                                        // White text on dark background
		localizedFont->drawString(&tempSurface, speedText, speedX, speedY, speedWidth, speedColor, Graphics::kTextAlignLeft);
	}

	// Copy to overlay
	int x = Std::max(0, (_graphicsManager->getOverlayWidth() - wRect) / 2); // Center horizontally
	int y = Std::max(10, _graphicsManager->getOverlayHeight() - 2 * lRect); // Bottom with spacing equal to height (40px from bottom)
	// A blocking download keeps the launcher event loop from repainting the
	// overlay, so clear it first; otherwise a previous (completed) bar persists
	// and the two stack up.
	_graphicsManager->clearOverlay();
	_graphicsManager->copyRectToOverlay(tempSurface.getPixels(), tempSurface.pitch, x, y, wRect, lRect);
	_graphicsManager->updateScreen();

	// Clean up
	tempSurface.free();
}

void VirtualReadStream::completeDownloadProgress(uint64 chunkLength) {
	OSystem_Emscripten *system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (!system->getGraphicsManager()) {
		return;
	}
	if (_downloadProgressShowedOverlay) {
		_downloadProgressShowedOverlay = false;
		system->hideOverlay();
	}
}

#endif // EMSCRIPTEN
