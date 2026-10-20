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

#include "movie.h"
#include "audio.h"
#include "audio_secache.h"
#include "bitmap.h"
#include "drawable_mgr.h"
#include "movie_decoder.h"
#include "output.h"
#include "pixel_format.h"

Movie::Movie(std::string_view name, Rect rect) : Drawable(Priority_Movie), name(name), rect(rect) {
	DrawableMgr::Register(this);

	FileRequestAsync* request = AsyncHandler::RequestFile("Movie", name);
	request_id = request->Bind(&Movie::OnFileReady, this);
	request->Start();
}

Movie::~Movie() {
	if (started && !done) {
		// the game ended or went to the title meanwhile
		Audio().SE_Stop();
		Stop();
	}
}

void Movie::OnFileReady(FileRequestResult* result) {
	if (!result->success) {
		Output::Warning("Movie not found: {}", name);
		done = true;
		return;
	}

	std::string error;
	decoder = MovieDecoder::Create(result->file, rect.width, rect.height, error);
	if (!decoder) {
		Output::Warning("Couldn't play movie {}: {}", name, error);
		done = true;
		return;
	}
	Start();
}

void Movie::Start() {
	Audio().SE_Stop();
	if (Audio().BGM_GetType() != "midi") {
		Audio().BGM_Pause();
		music_paused = true;
	}

	if (decoder->HasAudio()) {
		auto buffer = new Filesystem_Stream::InputMemoryStreamBuf(decoder->ReadAudio());
		Filesystem_Stream::InputStream stream(buffer, name);
		auto se = AudioSeCache::Create(std::move(stream), "Movie/" + name);
		if (se) {
			Audio().SE_Play(std::move(se), 100, 100, 50);
		} else {
			Output::Warning("Movie {}: its sound format is not supported", name);
		}
	}

	if (decoder->HasVideo() && rect.width > 0 && rect.height > 0) {
		int width = decoder->GetWidth();
		int height = decoder->GetHeight();
		pixels.resize(static_cast<size_t>(width) * height * 4);
		frame = Bitmap::Create(pixels.data(), width, height, width * 4, format_R8G8B8A8_n().format());
	}

	start_time = Game_Clock::GetFrameTime();
	started = true;
	decoder->Play(Audio().SE_GetGlobalVolume());
	Update();
}

void Movie::Stop() {
	done = true;
	decoder.reset();
	if (music_paused) {
		Audio().BGM_Resume();
	}
}

void Movie::Update() {
	if (!started || done) {
		return;
	}

	if (music_paused) {
		// the Player resumes the music when the window gets the focus back
		Audio().BGM_Pause();
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Game_Clock::GetFrameTime() - start_time).count();
	if (decoder->IsDone(elapsed)) {
		Stop();
		return;
	}

	if (frame && decoder->ReadFrame(elapsed, pixels.data())) {
		has_picture = true;
	}
}

void Movie::Draw(Bitmap& dst) {
	if (!frame || !has_picture || done) {
		return;
	}
	dst.StretchBlit(rect, *frame, frame->GetRect(), Opacity::Opaque());
}
