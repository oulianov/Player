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

#include "movie_decoder.h"
#include "avi_reader.h"
#include "filefinder.h"
#include "movie_decoder_browser.h"
#include "movie_decoder_ffmpeg.h"
#include "output.h"

std::unique_ptr<MovieDecoder> MovieDecoder::Create(std::string_view file, int width, int height, std::string& error) {
	auto avi = std::make_unique<AviReader>(FileFinder::OpenMovie(file));
	if (avi->GetError().empty() && avi->GetVideoError().empty()) {
		return avi;
	}
	error = avi->GetError().empty() ? avi->GetVideoError() : avi->GetError();

#ifdef HAVE_FFMPEG
	auto ffmpeg = std::make_unique<MovieDecoderFFmpeg>(file, width, height);
	if (ffmpeg->GetError().empty()) {
		return ffmpeg;
	}
	error = ffmpeg->GetError();
#endif

#ifdef __EMSCRIPTEN__
	// the browser plays what it can, or says it can't later (IsDone)
	auto stream = FileFinder::OpenMovie(file);
	std::vector<uint8_t> data;
	if (stream) {
		data.resize(static_cast<size_t>(stream.GetSize()));
		stream.read(reinterpret_cast<char*>(data.data()), data.size());
		data.resize(static_cast<size_t>(stream.gcount()));
	}
	if (!data.empty()) {
		return std::make_unique<MovieDecoderBrowser>(std::move(data), width, height);
	}
#endif
	(void)width;
	(void)height;

	if (avi->GetError().empty()) {
		// an AVI with compressed video and a sound the audio decoders play
		Output::Warning("Movie {}: {}, playing the sound only", file, avi->GetVideoError());
		return avi;
	}
	return nullptr;
}

void MovieDecoder::Play(int) {
}

std::vector<uint8_t> MovieDecoder::MakeWav(const std::vector<uint8_t>& format, const std::vector<uint8_t>& samples) {
	auto put_id = [](std::vector<uint8_t>& data, const char* id) {
		data.insert(data.end(), id, id + 4);
	};
	auto put_u32 = [](std::vector<uint8_t>& data, uint32_t value) {
		for (int i = 0; i < 4; ++i) {
			data.push_back(static_cast<uint8_t>(value >> (8 * i)));
		}
	};

	uint32_t format_size = static_cast<uint32_t>(format.size());
	uint32_t data_size = static_cast<uint32_t>(samples.size());
	std::vector<uint8_t> wav;
	wav.reserve(44 + format_size + data_size);
	put_id(wav, "RIFF");
	put_u32(wav, 4 + 8 + format_size + (format_size & 1) + 8 + data_size + (data_size & 1));
	put_id(wav, "WAVE");
	put_id(wav, "fmt ");
	put_u32(wav, format_size);
	wav.insert(wav.end(), format.begin(), format.end());
	if (format_size & 1) {
		wav.push_back(0);
	}
	put_id(wav, "data");
	put_u32(wav, data_size);
	wav.insert(wav.end(), samples.begin(), samples.end());
	if (data_size & 1) {
		wav.push_back(0);
	}
	return wav;
}
