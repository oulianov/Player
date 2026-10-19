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

#ifndef EP_DYNRPG_GAMECLOCK_H
#define EP_DYNRPG_GAMECLOCK_H

#include <cstdint>
#include <string>
#include "game_dynrpg.h"
#include "memory_management.h"

namespace DynRpg {
	/**
	 * Main Menu Game Playtime Clock v4.1 by PepsiOtaku (game_clock.dll).
	 *
	 * Counts the playtime in variables, every frame and in every scene:
	 *
	 *   [game_clock]
	 *   SessionStart=4003      seconds of the clock when the game was started or loaded
	 *   SessionCurrent=4004    seconds of the clock now
	 *   SessionSecs=4005       the difference
	 *   PlaytimeSecs=4006      playtime of the save, in seconds
	 *   PlaytimeHH=4007        its hours, minutes and seconds
	 *   PlaytimeMM=4008
	 *   PlaytimeSS=4009
	 *   RestartTrigger=4010    writing it sets the playtime to the value written
	 *   ForegroundVar=4011     N > 0 uses ForegroundImageN instead of ForegroundImage
	 *   BackgroundVar=4012     same for BackgroundImageN
	 *   ForegroundImage=Picture\menu-time_fg.png   drawn over the main menu (mask color)
	 *   BackgroundImage=Picture\Pixel.png          drawn over the menus' background
	 *   TimeTextDisplay=Time   label of the clock
	 *
	 * In the main menu, it draws the foreground image and the clock (label at
	 * 8,168 in color 1, "H:MM:SS" at 184) over the windows, redrawn every 11 frames.
	 * The clock is the one of the computer (GetTickCount in RPG_RT), not the frames.
	 */
	class GameClockPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "game_clock";

		explicit GameClockPlugin(Game_DynRpg& instance);

		bool Invoke(std::string_view, dyn_arg_list, bool&, Game_Interpreter*) override { return false; }
		void OnFrame(Scene::SceneType scene) override;
		bool OnSetVariable(int id, int value) override;
		void OnNewGame() override;
		void OnLoadGame() override;
		void OnDrawScene(Scene::SceneType scene, Bitmap& dst) override;
		void OnSystemBackgroundDrawn(Bitmap& dst, const Rect& rect) override;

	private:
		void StartSession();
		void DrawClock();
		std::string ImageKey(const char* key, int var_id) const;

		int var_session_start = 0;
		int var_session_current = 0;
		int var_session_secs = 0;
		int var_playtime_secs = 0;
		int var_playtime_hh = 0;
		int var_playtime_mm = 0;
		int var_playtime_ss = 0;
		int var_restart_trigger = 0;
		int var_foreground = 0;
		int var_background = 0;

		uint32_t playtime_secs = 0;
		uint8_t frame_timer = 0;

		BitmapRef foreground;
		BitmapRef clock;
		BitmapRef background;
		bool foreground_tried = false;
		bool background_tried = false;
	};
}

#endif
