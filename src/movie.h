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

#ifndef EP_MOVIE_H
#define EP_MOVIE_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "async_handler.h"
#include "drawable.h"
#include "game_clock.h"
#include "memory_management.h"
#include "rect.h"

class MovieDecoder;

/**
 * A movie of the Play Movie command (MovieDecoder), stretched to its rectangle over the
 * whole game, an erased screen included, as RPG_RT shows it in a window of its own.  The
 * game stops until it ends (Scene_Map).
 *
 * Its frames follow the clock, like its sound, which plays as a sound effect (or in the
 * browser).  As RPG_RT frees its wave player for the movie, the sound effects stop and the
 * music is paused, but MIDI music, which RPG_RT plays on another device.
 */
class Movie : public Drawable {
public:
	/**
	 * Loads the movie and starts it.
	 *
	 * @param name file of the Movie folder
	 * @param rect where it is shown, in screen pixels
	 */
	Movie(std::string_view name, Rect rect);
	~Movie() override;

	void Draw(Bitmap& dst) override;

	/** Shows the frame of the time elapsed, and ends the movie after its last one. */
	void Update();

	/** @return whether the movie ended, or could not be played */
	bool IsDone() const;

private:
	void OnFileReady(FileRequestResult* result);
	void Start();
	void Stop();

	std::string name;
	Rect rect;
	FileRequestBinding request_id;
	std::unique_ptr<MovieDecoder> decoder;
	std::vector<uint8_t> pixels;
	BitmapRef frame;
	bool has_picture = false;
	Game_Clock::time_point start_time;
	bool started = false;
	bool done = false;
	bool music_paused = false;
};

inline bool Movie::IsDone() const {
	return done;
}

#endif
