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

#ifndef EP_MOVIE_DECODER_FFMPEG_H
#define EP_MOVIE_DECODER_FFMPEG_H

#ifdef HAVE_FFMPEG

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "movie_decoder.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

/**
 * Plays the movies FFmpeg reads: MP4, WebM, MKV, MOV, MPEG, compressed AVI...  The
 * pictures are decoded as they are shown, at the size of the movie on screen; the sound
 * all at once when the movie opens.
 */
class MovieDecoderFFmpeg final : public MovieDecoder {
public:
	/**
	 * Opens a movie: GetError() tells why FFmpeg can't play it.
	 *
	 * @param file the movie, as FileFinder::OpenMovie finds it
	 * @param width width of the pictures (the native one when 0)
	 * @param height height of the pictures (the native one when 0)
	 */
	MovieDecoderFFmpeg(std::string_view file, int width, int height);
	~MovieDecoderFFmpeg() override;

	/** @return why the movie can't be played, empty if it can */
	const std::string& GetError() const;

	bool HasVideo() const override;
	int GetWidth() const override;
	int GetHeight() const override;
	bool ReadFrame(int64_t time, uint8_t* rgba) override;
	bool HasAudio() const override;
	std::vector<uint8_t> ReadAudio() override;
	bool IsDone(int64_t time) const override;

	/** An opened file (its stream and its FFmpeg contexts) */
	struct Input;

private:
	/** Decodes the next picture into frame: false after the last one */
	bool DecodeVideoFrame();
	void DecodeAudio(std::string_view file);

	std::string error;
	std::unique_ptr<Input> input;
	AVCodecContext* video = nullptr;
	AVFrame* frame = nullptr;
	AVFrame* shown = nullptr;
	AVPacket* packet = nullptr;
	SwsContext* scaler = nullptr;
	int video_index = -1;
	int width = 0;
	int height = 0;
	int64_t duration = 0;
	int64_t frame_time = 0;
	int64_t frame_step = 0;
	bool frame_waiting = false;
	bool video_ended = false;
	bool flushing = false;

	std::vector<uint8_t> audio;
	int64_t audio_duration = 0;
};

#endif

#endif
