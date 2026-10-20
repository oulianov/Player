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

#include "movie_decoder_browser.h"

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <emscripten.h>

// The movies are video elements in Module.easyrpgMovies, by id.  state: 0 playing (or
// loading), 1 ended, 2 the browser can't play it.
EM_JS(int, movie_browser_open, (const uint8_t* data, int size, int width, int height), {
	const movies = Module.easyrpgMovies || (Module.easyrpgMovies = { next: 1 });
	const video = document.createElement("video");
	video.playsInline = true;
	video.preload = "auto";
	video.src = URL.createObjectURL(new Blob([HEAPU8.slice(data, data + size)]));
	const canvas = document.createElement("canvas");
	canvas.width = Math.max(width, 1);
	canvas.height = Math.max(height, 1);
	const movie = { video, canvas, context: canvas.getContext("2d", { willReadFrequently: true }), state: 0 };
	video.addEventListener("ended", () => { movie.state = 1; });
	video.addEventListener("error", () => {
		console.warn("Movie: the browser can't play it", video.error);
		movie.state = 2;
	});
	const id = movies.next++;
	movies[id] = movie;
	return id;
});

EM_JS(void, movie_browser_play, (int id, double volume), {
	const movie = Module.easyrpgMovies[id];
	movie.video.volume = volume;
	movie.video.play().catch(() => {
		// no sound allowed before the player clicked or pressed a key in the page
		movie.video.muted = true;
		movie.video.play().catch(() => { movie.state = 2; });
	});
});

EM_JS(int, movie_browser_frame, (int id, uint8_t* rgba), {
	const movie = Module.easyrpgMovies[id];
	const video = movie.video;
	if (video.readyState < 2 || video.videoWidth === 0) {
		return 0;
	}
	const canvas = movie.canvas;
	movie.context.drawImage(video, 0, 0, canvas.width, canvas.height);
	HEAPU8.set(movie.context.getImageData(0, 0, canvas.width, canvas.height).data, rgba);
	return 1;
});

EM_JS(int, movie_browser_state, (int id), {
	return Module.easyrpgMovies[id].state;
});

EM_JS(void, movie_browser_close, (int id), {
	const movie = Module.easyrpgMovies[id];
	movie.video.pause();
	URL.revokeObjectURL(movie.video.src);
	movie.video.removeAttribute("src");
	movie.video.load();
	delete Module.easyrpgMovies[id];
});

MovieDecoderBrowser::MovieDecoderBrowser(std::vector<uint8_t> data, int width, int height) :
	width(std::max(width, 0)), height(std::max(height, 0)) {
	id = movie_browser_open(data.data(), static_cast<int>(data.size()), this->width, this->height);
}

MovieDecoderBrowser::~MovieDecoderBrowser() {
	movie_browser_close(id);
}

bool MovieDecoderBrowser::HasVideo() const {
	return width > 0 && height > 0;
}

int MovieDecoderBrowser::GetWidth() const {
	return width;
}

int MovieDecoderBrowser::GetHeight() const {
	return height;
}

bool MovieDecoderBrowser::ReadFrame(int64_t, uint8_t* rgba) {
	// the browser keeps the time of its movie
	return HasVideo() && movie_browser_frame(id, rgba) != 0;
}

bool MovieDecoderBrowser::HasAudio() const {
	return false; // the browser plays it
}

std::vector<uint8_t> MovieDecoderBrowser::ReadAudio() {
	return {};
}

void MovieDecoderBrowser::Play(int volume) {
	movie_browser_play(id, volume / 100.0);
}

bool MovieDecoderBrowser::IsDone(int64_t) const {
	return movie_browser_state(id) != 0;
}

#endif
