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

#ifndef BACKENDS_FS_EMSCRIPTEN_CLOUD_FILE_STREAM_H
#define BACKENDS_FS_EMSCRIPTEN_CLOUD_FILE_STREAM_H

#ifdef EMSCRIPTEN

#include "backends/fs/emscripten/network-file-stream.h"
#include "backends/cloud/storage.h"

/**
 * Cloud storage implementation of NetworkFileStream.
 * Downloads file chunks using Cloud::Storage API.
 */
class CloudFileStream : public NetworkFileStream {
private:
	Cloud::Storage *_storage;
	Common::String _fileId;

	void fileDownloadedCallback(const Cloud::Storage::BoolResponse &response);
	void fileDownloadedErrorCallback(const Networking::ErrorResponse &error);

protected:
	virtual void downloadChunk(uint32 chunkIndex) override;

public:
	CloudFileStream(Cloud::Storage *storage, const Common::String &fileId, 
	               const Common::String &displayName, const Common::String &cachePath, 
	               uint64 fileSize);
	virtual ~CloudFileStream();
};

#endif // EMSCRIPTEN

#endif // BACKENDS_FS_EMSCRIPTEN_CLOUD_FILE_STREAM_H
