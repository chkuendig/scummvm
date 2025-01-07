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

#ifndef BACKENDS_NETWORKING_CURL_CURLFILEREQUEST_H
#define BACKENDS_NETWORKING_CURL_CURLFILEREQUEST_H

#include "backends/networking/curl/curlrequest.h"
#include "common/memstream.h"
#include "common/formats/json.h"

namespace Networking {
#define CURL_FILE_REQUEST_BUFFER_SIZE 1 * 1024 * 1024


typedef Response<Common::MemoryWriteStreamDynamic *> MemoryWriteStreamDynamicResponse;
typedef Common::BaseCallback<const MemoryWriteStreamDynamicResponse &> *MemoryWriteStreamDynamicCallback;

class CurlFileRequest: public CurlRequest {
	/*
	Simple helper to use curl to download a file. Similar to CurlJsonRequest but returning binary stream. Necessary as CurlRequest on it's own doesn't finish
	*/
protected:
	MemoryWriteStreamDynamicCallback _networkReadStreamCallback;
	Common::MemoryWriteStreamDynamic _contentsStream;
	byte *_buffer;
	virtual void finishStream(Common::MemoryWriteStreamDynamic *stream);
public:
	CurlFileRequest(MemoryWriteStreamDynamicCallback cb, ErrorCallback ecb, Common::String url);
	virtual ~CurlFileRequest();

	virtual void handle();
	virtual void restart();

};

} // End of namespace Networking

#endif
