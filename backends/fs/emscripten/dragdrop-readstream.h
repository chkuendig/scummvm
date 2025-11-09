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

#ifndef BACKENDS_FS_EMSCRIPTEN_DRAGDROP_READSTREAM_H
#define BACKENDS_FS_EMSCRIPTEN_DRAGDROP_READSTREAM_H

#ifdef EMSCRIPTEN

#include "backends/fs/abstract-fs.h"
#include "backends/fs/emscripten/dragdrop-fs.h"
#include "backends/fs/emscripten/dragdrop-readstream.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/types.h"

class DragDropReadStream : public Common::SeekableReadStream {
public:
	DragDropReadStream(const Common::String &path);
	~DragDropReadStream();

	bool eos() const override { return _eos; }
	bool err() const override { return false; }
	void clearErr() override {}
	uint32 read(void *dataPtr, uint32 dataSize) override;
	int64 pos() const override { return _pos; }
	int64 size() const override { return _size; }
	bool seek(int64 offset, int whence = SEEK_SET) override;

private:
	Common::String _path;
	int64 _pos;
	int64 _size;
	bool _eos;
};

#endif // EMSCRIPTEN

#endif
