/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EP_AVI_READER_H
#define EP_AVI_READER_H

#include <cstdint>
#include <string>
#include <vector>
#include "filesystem_stream.h"
#include "movie_decoder.h"

/**
 * Reads the movies of the Play Movie command that need no codec: AVI files with
 * uncompressed video (YUV: IYUV/I420, YV12, YUY2, UYVY; RGB: 8-bit with a palette,
 * 16, 24 or 32-bit) and their sound.
 *
 * RPG_RT plays movies through DirectShow, so any format Windows has a codec for.
 * The video frames are read from the stream when they are shown, the sound all at once.
 */
class AviReader final : public MovieDecoder {
public:
	/**
	 * Reads the headers and finds the chunks of the movie.
	 *
	 * @param stream the AVI file, kept open to read the frames
	 */
	explicit AviReader(Filesystem_Stream::InputStream stream);

	/** @return why the movie can't be played, empty if it can */
	const std::string& GetError() const;

	/** @return why the video can't be decoded (the sound may be), empty if it can */
	const std::string& GetVideoError() const;

	/** @return the length of the movie, its video or its sound whichever is longer, in microseconds */
	int64_t GetDuration() const;

	bool HasVideo() const override;
	int GetWidth() const override;
	int GetHeight() const override;
	bool ReadFrame(int64_t time, uint8_t* rgba) override;
	bool HasAudio() const override;

	/**
	 * @return the sound as a file the audio decoders read: a WAV file with the AVI's
	 * wave format, or the MP3 stream itself
	 */
	std::vector<uint8_t> ReadAudio() override;

	bool IsDone(int64_t time) const override;

private:
	struct Chunk {
		uint32_t offset;
		uint32_t size;
	};

	enum class Video {
		None,
		Planar420, // Y plane, then U and V planes at half width and height (IYUV/I420)
		Planar420VU, // the same with V before U (YV12)
		YUYV, // packed 4:2:2: Y0 U Y1 V (YUY2)
		UYVY, // packed 4:2:2: U Y0 V Y1
		RGB // BI_RGB, rows bottom-up unless the height is negative
	};

	/**
	 * Decodes a video frame.
	 *
	 * @return false when the frame has no picture (a dropped frame repeats the one before)
	 */
	bool DecodeFrame(int index, uint8_t* rgba);

	void ReadHeaders(uint32_t end);
	void ReadStreamHeaders(uint32_t end);
	void ReadChunks(uint32_t end);
	bool Seek(uint32_t offset);
	bool ReadBytes(void* dst, uint32_t size);
	uint32_t ReadU32();

	Filesystem_Stream::InputStream stream;
	std::string error;
	std::string video_error;

	// AVI streams: their type and format, in the order of the stream numbers of the chunks
	struct StreamInfo {
		uint32_t type = 0;
		uint32_t scale = 1;
		uint32_t rate = 0;
		std::vector<uint8_t> format;
	};
	std::vector<StreamInfo> streams;
	int video_stream = -1;
	int audio_stream = -1;

	Video video = Video::None;
	int width = 0;
	int height = 0;
	int bits = 0;
	bool bottom_up = false;
	uint32_t palette[256] = {};
	int64_t frame_duration = 0;
	int frame_index = -1;

	std::vector<Chunk> frames;
	std::vector<Chunk> audio;
	std::vector<uint8_t> chunk_data;
};

#endif
