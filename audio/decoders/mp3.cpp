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

#include "audio/decoders/mp3.h"

#ifdef USE_MP3

#include "common/debug.h"
#include "common/mutex.h"
#include "common/ptr.h"
#include "common/queue.h"
#include "common/stream.h"
#include "common/substream.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "audio/audiostream.h"

#ifdef USE_MAD
#include <mad.h>
#elif defined(USE_MPG123)
#include <mpg123.h>
#endif

#if defined(__PSP__)
	#include "backends/platform/psp/mp3.h"
#endif
namespace Audio {

#ifdef USE_MPG123
// Global mpg123 initialization
static bool g_mpg123Initialized = false;
static int g_mpg123RefCount = 0;

static void initMpg123() {
	if (!g_mpg123Initialized) {
		if (mpg123_init() == MPG123_OK) {
			g_mpg123Initialized = true;
		} else {
			error("Failed to initialize mpg123");
		}
	}
	g_mpg123RefCount++;
}

static void deinitMpg123() {
	g_mpg123RefCount--;
	if (g_mpg123RefCount == 0 && g_mpg123Initialized) {
		mpg123_exit();
		g_mpg123Initialized = false;
	}
}
#endif

#pragma mark -
#pragma mark --- MP3 stream ---
#pragma mark -

class BaseMP3Stream : public virtual AudioStream {
public:
	BaseMP3Stream();
	virtual ~BaseMP3Stream();

	bool endOfData() const override { return _state == MP3_STATE_EOS; }
	bool isStereo() const override { return _channels == 2; }
	int getRate() const override { return _rate; }

protected:
#ifdef USE_MAD
	void decodeMP3Data(Common::ReadStream &stream);
	void readMP3Data(Common::ReadStream &stream);

	void initStream(Common::ReadStream &stream);
	void readHeader(Common::ReadStream &stream);

	int fillBuffer(Common::ReadStream &stream, int16 *buffer, const int numSamples);
#elif defined(USE_MPG123)
	void initStream();
	
	int fillBuffer(int16 *buffer, const int numSamples);
	bool feedData(const byte *data, size_t size);
	void drainDecoder();

#endif
	void deinitStream();
	enum State {
		MP3_STATE_INIT,	// Need to init the decoder
		MP3_STATE_READY,	// ready for processing data
		MP3_STATE_EOS		// end of data reached (may need to loop)
	};

	State _state;
#ifdef USE_MAD
	uint _posInFrame;

	mad_timer_t _curTime;

	mad_stream _stream;
	mad_frame _frame;
	mad_synth _synth;

	uint _channels;
	uint _rate;

	enum {
		BUFFER_SIZE = 5 * 8192
	};

	// This buffer contains a slab of input data
	byte _buf[BUFFER_SIZE + MAD_BUFFER_GUARD];
#elif defined(USE_MPG123)
	mpg123_handle *_handle;
	
	int _channels;
	long _rate;
	int _encoding;

	// Buffer for decoded audio
	static const size_t BUFFER_SIZE = 8192;
	unsigned char _decodeBuffer[BUFFER_SIZE];
	size_t _decodeBufferPos;
	size_t _decodeBufferUsed;
#endif
};

class MP3Stream : private BaseMP3Stream, public SeekableAudioStream {
public:
	MP3Stream(Common::SeekableReadStream *inStream,
	               DisposeAfterUse::Flag dispose);


	int readBuffer(int16 *buffer, const int numSamples) override;
	bool seek(const Timestamp &where) override;
	Timestamp getLength() const override { return _length; }

protected:
	Common::ScopedPtr<Common::SeekableReadStream> _inStream;

	Timestamp _length;
#ifdef USE_MPG123
	off_t _totalSamples;
#endif
private:
	static Common::SeekableReadStream *skipID3(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose);
};

class PacketizedMP3Stream : private BaseMP3Stream, public PacketizedAudioStream {
public:
	PacketizedMP3Stream(Common::SeekableReadStream &firstPacket);
	PacketizedMP3Stream(uint channels, uint rate);
	~PacketizedMP3Stream();

	// AudioStream API
	int readBuffer(int16 *buffer, const int numSamples) override;
	bool endOfData() const override;
	bool endOfStream() const override;

	// PacketizedAudioStream API
	void queuePacket(Common::SeekableReadStream *packet) override;
	void finish() override;

private:
	Common::Mutex _mutex;
	Common::Queue<Common::SeekableReadStream *> _queue;
	bool _finished;
};


BaseMP3Stream::BaseMP3Stream() :
#ifdef USE_MAD
	_posInFrame(0),
	_curTime(mad_timer_zero) ,
#elif defined(USE_MPG123)
	_handle(nullptr),
	_channels(0),
	_rate(0),
	_encoding(0),
	_decodeBufferPos(0),
	_decodeBufferUsed(0),
#endif
	_state(MP3_STATE_INIT) {
#ifdef USE_MAD
	// The MAD_BUFFER_GUARD must always contain zeros (the reason
	// for this is that the Layer III Huffman decoder of libMAD
	// may read a few bytes beyond the end of the input buffer).
	memset(_buf + BUFFER_SIZE, 0, MAD_BUFFER_GUARD);
#elif defined(USE_MPG123)
	initMpg123();
#endif
}

BaseMP3Stream::~BaseMP3Stream() {
	deinitStream();
#ifdef USE_MPG123
	deinitMpg123();
#endif
}

#ifdef USE_MAD
void BaseMP3Stream::decodeMP3Data(Common::ReadStream &stream) {
	do {
		if (_state == MP3_STATE_INIT)
			initStream(stream);

		if (_state == MP3_STATE_EOS)
			return;

		// If necessary, load more data into the stream decoder
		if (_stream.error == MAD_ERROR_BUFLEN)
			readMP3Data(stream);

		while (_state == MP3_STATE_READY) {
			_stream.error = MAD_ERROR_NONE;

			// Decode the next frame
			if (mad_frame_decode(&_frame, &_stream) == -1) {
				if (_stream.error == MAD_ERROR_BUFLEN) {
					break; // Read more data
				} else if (MAD_RECOVERABLE(_stream.error)) {
					// Note: we will occasionally see MAD_ERROR_BADDATAPTR errors here.
					// These are normal and expected (caused by our frame skipping (i.e. "seeking")
					// code above).
					debug(6, "MP3Stream: Recoverable error in mad_frame_decode (%s)", mad_stream_errorstr(&_stream));
					continue;
				} else {
					warning("MP3Stream: Unrecoverable error in mad_frame_decode (%s)", mad_stream_errorstr(&_stream));
					break;
				}
			}

			// Sum up the total playback time so far
			mad_timer_add(&_curTime, _frame.header.duration);
			// Synthesize PCM data
			mad_synth_frame(&_synth, &_frame);
			_posInFrame = 0;
			break;
		}
	} while (_state != MP3_STATE_EOS && _stream.error == MAD_ERROR_BUFLEN);

	if (_stream.error != MAD_ERROR_NONE)
		_state = MP3_STATE_EOS;
}

void BaseMP3Stream::readMP3Data(Common::ReadStream &stream) {
	uint32 remaining = 0;
	// Give up immediately if we already used up all data in the stream
	if (stream.eos()) {
		_state = MP3_STATE_EOS;
		return;
	}
	if (_stream.next_frame) {
		// If there is still data in the MAD stream, we need to preserve it.
		// Note that we use memmove, as we are reusing the same buffer,
		// and hence the data regions we copy from and to may overlap.
		remaining = _stream.bufend - _stream.next_frame;
		assert(remaining < BUFFER_SIZE);	// Paranoia check
		memmove(_buf, _stream.next_frame, remaining);
	}
	// Try to read the next block
	uint32 size = stream.read(_buf + remaining, BUFFER_SIZE - remaining);
	if (size <= 0) {
		_state = MP3_STATE_EOS;
		return;
	}
	// Feed the data we just read into the stream decoder
	_stream.error = MAD_ERROR_NONE;
	mad_stream_buffer(&_stream, _buf, size + remaining);
}
void BaseMP3Stream::initStream(Common::ReadStream &stream) {
	if (_state != MP3_STATE_INIT)
		deinitStream();

	// Init MAD
	mad_stream_init(&_stream);
	mad_frame_init(&_frame);
	mad_synth_init(&_synth);

	// Reset the stream data
	_curTime = mad_timer_zero;
	_posInFrame = 0;
	// Update state
	_state = MP3_STATE_READY;
	// Read the first few sample bytes
	readMP3Data(stream);
}
void BaseMP3Stream::readHeader(Common::ReadStream &stream) {
	if (_state != MP3_STATE_READY)
		return;
	// If necessary, load more data into the stream decoder
	if (_stream.error == MAD_ERROR_BUFLEN)
		readMP3Data(stream);

	while (_state != MP3_STATE_EOS) {
		_stream.error = MAD_ERROR_NONE;

		// Decode the next header. Note: mad_frame_decode would do this for us, too.
		// However, for seeking we don't want to decode the full frame (else it would
		// be far too slow). Hence we perform this explicitly in a separate step.
		if (mad_header_decode(&_frame.header, &_stream) == -1) {
			if (_stream.error == MAD_ERROR_BUFLEN) {
				readMP3Data(stream);  // Read more data
				continue;
			} else if (MAD_RECOVERABLE(_stream.error)) {
				debug(6, "MP3Stream: Recoverable error in mad_header_decode (%s)", mad_stream_errorstr(&_stream));
				continue;
			} else {
				warning("MP3Stream: Unrecoverable error in mad_header_decode (%s)", mad_stream_errorstr(&_stream));
				break;
			}
		}

		// Sum up the total playback time so far
		mad_timer_add(&_curTime, _frame.header.duration);
		break;
	}

	if (_stream.error != MAD_ERROR_NONE)
		_state = MP3_STATE_EOS;
}

void BaseMP3Stream::deinitStream() {
	if (_state == MP3_STATE_INIT)
		return;

	// Deinit MAD
	mad_synth_finish(&_synth);
	mad_frame_finish(&_frame);
	mad_stream_finish(&_stream);

	_state = MP3_STATE_EOS;
}

static inline int scaleSample(mad_fixed_t sample) {
	// round
	sample += (1L << (MAD_F_FRACBITS - 16));

	// clip
	if (sample > MAD_F_ONE - 1)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;

	// quantize and scale to not saturate when mixing a lot of channels
	return sample >> (MAD_F_FRACBITS + 1 - 16);
}

int BaseMP3Stream::fillBuffer(Common::ReadStream &stream, int16 *buffer, const int numSamples) {
	int samples = 0;
	// Keep going as long as we have input available
	while (samples < numSamples && _state != MP3_STATE_EOS) {
		const int len = MIN(numSamples, samples + (int)(_synth.pcm.length - _posInFrame) * MAD_NCHANNELS(&_frame.header));
		while (samples < len) {
			*buffer++ = (int16)scaleSample(_synth.pcm.samples[0][_posInFrame]);
			samples++;
			if (MAD_NCHANNELS(&_frame.header) == 2) {
				*buffer++ = (int16)scaleSample(_synth.pcm.samples[1][_posInFrame]);
				samples++;
			}
			_posInFrame++;
		}
		if (_posInFrame >= _synth.pcm.length) {
			// We used up all PCM data in the current frame -- read & decode more
			decodeMP3Data(stream);
		}
	}
	return samples;
}
MP3Stream::MP3Stream(Common::SeekableReadStream *inStream, DisposeAfterUse::Flag dispose) :
		BaseMP3Stream(),
		_inStream(skipID3(inStream, dispose)),
		_length(0, 1000) {

	// Initialize the stream with some data and set the channels and rate
	// variables
	decodeMP3Data(*_inStream);
	_channels = MAD_NCHANNELS(&_frame.header);
	_rate = _frame.header.samplerate;

	// Calculate the length of the stream
	while (_state != MP3_STATE_EOS)
		readHeader(*_inStream);

	// To rule out any invalid sample rate to be encountered here, say in case the
	// MP3 stream is invalid, we just check the MAD error code here.
	// We need to assure this, since else we might trigger an assertion in Timestamp
	// (When getRate() returns 0 or a negative number to be precise).
	// Note that we allow "MAD_ERROR_BUFLEN" as error code here, since according
	// to mad.h it is also set on EOF.
	if ((_stream.error == MAD_ERROR_NONE || _stream.error == MAD_ERROR_BUFLEN) && getRate() > 0)
		_length = Timestamp(mad_timer_count(_curTime, MAD_UNITS_MILLISECONDS), getRate());

	deinitStream();

	// Reinit stream
	_state = MP3_STATE_INIT;
	_inStream->seek(0);

	// Decode the first chunk of data to set up the stream again.
	decodeMP3Data(*_inStream);
}

int MP3Stream::readBuffer(int16 *buffer, const int numSamples) {
	return fillBuffer(*_inStream, buffer, numSamples);
}
#elif defined(USE_MPG123)

void BaseMP3Stream::initStream() {
	if (_state != MP3_STATE_INIT)
		deinitStream();

	// Create new mpg123 handle
	int err;
	_handle = mpg123_new(nullptr, &err);
	if (!_handle) {
		error("Failed to create mpg123 handle: %s", mpg123_plain_strerror(err));
		return;
	}

	// Set format to 16-bit signed
	mpg123_format_none(_handle);
	mpg123_format(_handle, 44100, MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);

	// Open feeder (for feeding data manually)
	if (mpg123_open_feed(_handle) != MPG123_OK) {
		error("Failed to open mpg123 feeder: %s", mpg123_strerror(_handle));
		mpg123_delete(_handle);
		_handle = nullptr;
		return;
	}

	_state = MP3_STATE_READY;
	_decodeBufferPos = 0;
	_decodeBufferUsed = 0;
}

void BaseMP3Stream::deinitStream() {
	if (_handle) {
		mpg123_close(_handle);
		mpg123_delete(_handle);
		_handle = nullptr;
	}
	_state = MP3_STATE_INIT;
}

bool BaseMP3Stream::feedData(const byte *data, size_t size) {
	if (!_handle || _state != MP3_STATE_READY)
		return false;

	int ret = mpg123_feed(_handle, data, size);
	return (ret == MPG123_OK);
}

void BaseMP3Stream::drainDecoder() {
	if (!_handle)
		return;

	while (_state == MP3_STATE_READY) {
		size_t done;
		int ret = mpg123_read(_handle, _decodeBuffer, BUFFER_SIZE, &done);
		
		if (ret == MPG123_NEW_FORMAT) {
			// Get format information
			int encoding;
			if (mpg123_getformat(_handle, &_rate, &_channels, &encoding) == MPG123_OK) {
				_encoding = encoding;
			}
			continue;
		}
		
		if (ret == MPG123_OK) {
			_decodeBufferUsed = done;
			_decodeBufferPos = 0;
			break;
		} else if (ret == MPG123_NEED_MORE) {
			break;
		} else if (ret == MPG123_DONE) {
			_state = MP3_STATE_EOS;
			break;
		} else {
			warning("mpg123_read error: %s", mpg123_strerror(_handle));
			_state = MP3_STATE_EOS;
			break;
		}
	}
}

int BaseMP3Stream::fillBuffer(int16 *buffer, const int numSamples) {
	int samples = 0;

	while (samples < numSamples && _state != MP3_STATE_EOS) {
		// If we have data in the decode buffer, use it first
		if (_decodeBufferPos < _decodeBufferUsed) {
			int16 *srcBuffer = (int16 *)(_decodeBuffer + _decodeBufferPos);
			int availSamples = (_decodeBufferUsed - _decodeBufferPos) / 2; // 16-bit samples
			int copySamples = MIN(numSamples - samples, availSamples);
			
			memcpy(buffer + samples, srcBuffer, copySamples * 2);
			samples += copySamples;
			_decodeBufferPos += copySamples * 2;
		} else {
			// Need to decode more data
			drainDecoder();
			
			// If we still don't have data after decoding, we're done
			if (_decodeBufferPos >= _decodeBufferUsed && _state != MP3_STATE_EOS) {
				break;
			}
		}
	}

	return samples;
}

MP3Stream::MP3Stream(Common::SeekableReadStream *inStream, DisposeAfterUse::Flag dispose) :
		BaseMP3Stream(),
		_inStream(skipID3(inStream, dispose)),
		_length(0, 1000),
		_totalSamples(0) {

	// Initialize the decoder
	initStream();

	// Feed some data to get format information
	byte buffer[4096];
	int size = _inStream->read(buffer, sizeof(buffer));
	if (size > 0) {
		feedData(buffer, size);
		drainDecoder();
	}

	// Calculate the length of the stream by seeking to the end and back
	if (_rate > 0) {
		off_t currentPos = _inStream->pos();
		_inStream->seek(0, SEEK_END);
		off_t fileSize = _inStream->pos();
		_inStream->seek(currentPos);

		// Estimate total samples (this is approximate)
		// Typical bitrate estimation: fileSize * 8 / (bitrate in bps)
		// For now, use a rough estimate
		_totalSamples = (fileSize * 8 * _rate) / (128000); // Assume 128kbps
		_length = Timestamp(_totalSamples, _rate);
	}
}

int MP3Stream::readBuffer(int16 *buffer, const int numSamples) {
	// Read from stream and feed to decoder as needed
	byte inputBuffer[4096];
	int samples = 0;
	
	while (samples < numSamples && _state != MP3_STATE_EOS) {
		// Try to fill buffer with already decoded data
		int decodedSamples = fillBuffer(buffer + samples, numSamples - samples);
		samples += decodedSamples;
		
		// If we didn't get enough samples and we're not at EOS, read more data
		if (samples < numSamples && _state != MP3_STATE_EOS) {
			int readBytes = _inStream->read(inputBuffer, sizeof(inputBuffer));
			if (readBytes > 0) {
				feedData(inputBuffer, readBytes);
			} else {
				_state = MP3_STATE_EOS;
			}
		}
	}
	
	return samples;
}
#endif

bool MP3Stream::seek(const Timestamp &where) {
	if (where == _length) {
		_state = MP3_STATE_EOS;
		return true;
	} else if (where > _length) {
		return false;
	}
#ifdef USE_MAD
	const uint32 time = where.msecs();

	mad_timer_t destination;
	mad_timer_set(&destination, time / 1000, time % 1000, 1000);

	if (_state != MP3_STATE_READY || mad_timer_compare(destination, _curTime) < 0) {
		_inStream->seek(0);
		initStream(*_inStream);
	}
	
	while (mad_timer_compare(destination, _curTime) > 0 && _state != MP3_STATE_EOS)
		readHeader(*_inStream);

	decodeMP3Data(*_inStream);

#elif defined(USE_MPG123)
	// For now, just seek to beginning and decode until we reach the target
	// A more sophisticated implementation would use mpg123_seek
	_inStream->seek(0, SEEK_SET);
	deinitStream();
	initStream();
	
	// Calculate target sample
	uint32 targetSample = where.convertToFramerate(_rate).totalNumberOfFrames();
	uint32 currentSample = 0;
	
	// Read and discard samples until we reach the target
	int16 tempBuffer[1024];
	while (currentSample < targetSample && _state != MP3_STATE_EOS) {
		int samplesToRead = MIN((int)(targetSample - currentSample), 1024);
		int samplesRead = readBuffer(tempBuffer, samplesToRead);
		currentSample += samplesRead;
		if (samplesRead == 0) {
			break;
		}
	}
#endif

	return (_state != MP3_STATE_EOS);
}

Common::SeekableReadStream *MP3Stream::skipID3(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose) {
	// Skip ID3 TAG if any
	// ID3v1 (beginning with with 'TAG') is located at the end of files. So we can ignore those.
	// ID3v2 can be located at the start of files and begins with a 10 bytes header, the first 3 bytes being 'ID3'.
	// The tag size is coded on the last 4 bytes of the 10 bytes header as a 32 bit synchsafe integer.
	// See http://id3.org/id3v2.4.0-structure for details.
	char data[10];
	stream->read(data, sizeof(data));

	uint32 offset = 0;
	if (!stream->eos() && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
		uint32 size = data[9] + 128 * (data[8] + 128 * (data[7] + 128 * data[6]));
		// This size does not include an optional 10 bytes footer. Check if it is present.
		if (data[5] & 0x10)
			size += 10;

		// Add in the 10 bytes we read in
		size += sizeof(data);
		debug(0, "Skipping ID3 TAG (%d bytes)", size);
		offset = size;
	}

	return new Common::SeekableSubReadStream(stream, offset, stream->size(), dispose);
}

PacketizedMP3Stream::PacketizedMP3Stream(Common::SeekableReadStream &firstPacket) :
		BaseMP3Stream(),
		_finished(false) {
#ifdef USE_MAD
	// Load some data to get the channels/rate
	_queue.push(&firstPacket);
	decodeMP3Data(firstPacket);
	_channels = MAD_NCHANNELS(&_frame.header);
	_rate = _frame.header.samplerate;

	// Clear everything
	deinitStream();
	_state = MP3_STATE_INIT;
	_queue.clear();

#elif defined(USE_MPG123)
	// Initialize the decoder first
	initStream();

	// Feed the first packet to get channel/rate info
	byte buffer[4096];
	firstPacket.seek(0, SEEK_SET);
	int size = firstPacket.read(buffer, sizeof(buffer));
	if (size > 0) {
		feedData(buffer, size);
		drainDecoder();
	}

	// Reset for later use
	firstPacket.seek(0, SEEK_SET);
	_queue.push(&firstPacket);
#endif
}

PacketizedMP3Stream::PacketizedMP3Stream(uint channels, uint rate) :
		BaseMP3Stream(),
		_finished(false) {
	_channels = channels;
	_rate = rate;
}

PacketizedMP3Stream::~PacketizedMP3Stream() {
	Common::StackLock lock(_mutex);
	while (!_queue.empty()) {
		delete _queue.front();
		_queue.pop();
	}
}

int PacketizedMP3Stream::readBuffer(int16 *buffer, const int numSamples) {
	int samples = 0;

	Common::StackLock lock(_mutex);
	while (samples < numSamples) {
		// Empty? Bail out for now, and mark the stream as ended
		if (_queue.empty()) {
			// EOS state is only valid once a packet has been received at least
			// once
			if (_state == MP3_STATE_READY)
				_state = MP3_STATE_EOS;
			return samples;
		}

		Common::SeekableReadStream *packet = _queue.front();

		if (_state == MP3_STATE_INIT) {
			// Initialize everything
#ifdef USE_MAD
			decodeMP3Data(*packet);
#elif defined(USE_MPG123)
			initStream();
			// Feed the packet data
			byte tempBuffer[4096];
			packet->seek(0, SEEK_SET);
			int size = packet->read(tempBuffer, sizeof(tempBuffer));
			if (size > 0) {
				feedData(tempBuffer, size);
				drainDecoder();
			}
			packet->seek(0, SEEK_SET);
#endif
			_state = MP3_STATE_READY;
		} else if (_state == MP3_STATE_EOS) {
			// Reset the end-of-stream setting
			_state = MP3_STATE_READY;
		}
#ifdef USE_MAD
		samples += fillBuffer(*packet, buffer + samples, numSamples - samples);
#elif defined(USE_MPG123)
		// Feed remaining data from packet
		byte tempBuffer[4096];
		int readBytes = packet->read(tempBuffer, sizeof(tempBuffer));
		if (readBytes > 0) {
			feedData(tempBuffer, readBytes);
		}

		samples += fillBuffer(buffer + samples, numSamples - samples);
#endif
		// If the stream is done, kill it
		if (packet->pos() >= packet->size()) {
			_queue.pop();
			delete packet;
		}
	}

	// This will happen if the audio runs out just as the last sample is
	// decoded. But there may still be more audio queued up.
	if (_state == MP3_STATE_EOS && !_queue.empty()) {
		_state = MP3_STATE_READY;
	}

	return samples;
}

bool PacketizedMP3Stream::endOfData() const {
	Common::StackLock lock(_mutex);
	return BaseMP3Stream::endOfData();
}

bool PacketizedMP3Stream::endOfStream() const {
	Common::StackLock lock(_mutex);

	if (!endOfData())
		return false;

	if (!_queue.empty())
		return false;

	return _finished;
}

void PacketizedMP3Stream::queuePacket(Common::SeekableReadStream *packet) {
	Common::StackLock lock(_mutex);
	assert(!_finished);
	_queue.push(packet);

	// If the audio had finished (buffer underrun?), there is more to
	// decode now.
	if (_state == MP3_STATE_EOS) {
		_state = MP3_STATE_READY;
	}
}

void PacketizedMP3Stream::finish() {
	Common::StackLock lock(_mutex);
	_finished = true;
}


#pragma mark -
#pragma mark --- MP3 factory functions ---
#pragma mark -

SeekableAudioStream *makeMP3Stream(
	Common::SeekableReadStream *stream,
	DisposeAfterUse::Flag disposeAfterUse) {

#if defined(__PSP__)
	SeekableAudioStream *s = 0;

	if (Mp3PspStream::isOkToCreateStream())
		s = new Mp3PspStream(stream, disposeAfterUse);

	if (!s)	// go to regular MAD mp3 stream if ME fails
		s = new MP3Stream(stream, disposeAfterUse);
#else
	SeekableAudioStream *s = new MP3Stream(stream, disposeAfterUse);
#endif
	if (s && s->endOfData()) {
		delete s;
		return nullptr;
	} else {
		return s;
	}
}

PacketizedAudioStream *makePacketizedMP3Stream(Common::SeekableReadStream &firstPacket) {
	return new PacketizedMP3Stream(firstPacket);
}

PacketizedAudioStream *makePacketizedMP3Stream(uint channels, uint rate) {
	return new PacketizedMP3Stream(channels, rate);
}


} // End of namespace Audio

#endif // #ifdef USE_MP3
