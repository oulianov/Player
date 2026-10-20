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

#ifndef EP_MOVIE_DECODER_BROWSER_H
#define EP_MOVIE_DECODER_BROWSER_H

#ifdef __EMSCRIPTEN__

#include <vector>
#include "movie_decoder.h"

/**
 * Plays a movie with the browser (web build), which decodes what it supports: MP4
 * (H.264, AAC), WebM (VP8, VP9, AV1, Opus)...  It plays the sound itself; the pictures
 * are copied from its video element, at the size of the movie on screen.
 */
class MovieDecoderBrowser final : public MovieDecoder {
public:
	/**
	 * @param data the movie file
	 * @param width width of the pictures
	 * @param height height of the pictures
	 */
	MovieDecoderBrowser(std::vector<uint8_t> data, int width, int height);
	~MovieDecoderBrowser() override;

	bool HasVideo() const override;
	int GetWidth() const override;
	int GetHeight() const override;
	bool ReadFrame(int64_t time, uint8_t* rgba) override;
	bool HasAudio() const override;
	std::vector<uint8_t> ReadAudio() override;
	void Play(int volume) override;
	bool IsDone(int64_t time) const override;

private:
	int id = 0;
	int width = 0;
	int height = 0;
};

#endif

#endif
