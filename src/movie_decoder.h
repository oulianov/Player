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

#ifndef EP_MOVIE_DECODER_H
#define EP_MOVIE_DECODER_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * Decodes a movie of the Play Movie command for Movie: its pictures and its sound.
 *
 * AviReader, built in, reads uncompressed AVI files.  The other formats (MP4, WebM,
 * MPEG, compressed AVI...) need FFmpeg (MovieDecoderFFmpeg) or, in the web build,
 * the browser (MovieDecoderBrowser).
 */
class MovieDecoder {
public:
	virtual ~MovieDecoder() = default;

	/**
	 * Opens a movie with the first decoder that plays it.
	 *
	 * @param file the movie, as FileFinder::OpenMovie finds it
	 * @param width width of the pictures wanted (the place of the movie on screen):
	 *   the decoders that can scale give them at this size
	 * @param height height of the pictures wanted
	 * @param error why no decoder plays it
	 * @return the decoder, or null
	 */
	static std::unique_ptr<MovieDecoder> Create(std::string_view file, int width, int height, std::string& error);

	/** @return whether the movie has pictures */
	virtual bool HasVideo() const = 0;

	/** @return width of the pictures ReadFrame writes */
	virtual int GetWidth() const = 0;

	/** @return height of the pictures ReadFrame writes */
	virtual int GetHeight() const = 0;

	/**
	 * Writes the picture shown at a time of the movie.
	 *
	 * @param time since the start, in microseconds; it only goes forward
	 * @param rgba GetWidth() * GetHeight() pixels, 4 bytes each (R, G, B, 255)
	 * @return whether the picture changed
	 */
	virtual bool ReadFrame(int64_t time, uint8_t* rgba) = 0;

	/** @return whether the movie has sound for the audio decoders (ReadAudio) */
	virtual bool HasAudio() const = 0;

	/** @return the sound as a file the audio decoders read (a WAV file...) */
	virtual std::vector<uint8_t> ReadAudio() = 0;

	/**
	 * Starts the movie, for the decoders that play it themselves with its sound.
	 *
	 * @param volume volume of the sound, 0 to 100
	 */
	virtual void Play(int volume);

	/**
	 * @param time since the start, in microseconds
	 * @return whether the movie ended
	 */
	virtual bool IsDone(int64_t time) const = 0;

	/**
	 * @param format the wave format (WAVEFORMATEX)
	 * @param samples the sound data in that format
	 * @return a WAV file of them
	 */
	static std::vector<uint8_t> MakeWav(const std::vector<uint8_t>& format, const std::vector<uint8_t>& samples);
};

#endif
