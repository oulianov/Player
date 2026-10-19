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

#ifndef EP_DYNRPG_FASTERATB_H
#define EP_DYNRPG_FASTERATB_H

#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * CMS Tools: Advanced Faster ATB v2.0 by PepsiOtaku (faster_atb_advanced.dll):
	 * a variable sets the battle speed (Game_DynRpg::SetBattleSpeed).
	 *
	 *   [faster_atb_advanced]
	 *   BattleSpeedVariable=4001   the variable
	 *   DefaultSpeed=200           speed (%) when the variable is 0 or out of range
	 *
	 * Variable value: -4 to -1 = 0, 25, 50, 75 %; 1 to 16 = 100 to 1600 %.
	 * The speed changes when the engine writes the variable and when a game is
	 * loaded, it starts at DefaultSpeed when the Player starts.
	 */
	class FasterAtbPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "faster_atb_advanced";
		static constexpr const char* section = dll_name;

		explicit FasterAtbPlugin(Game_DynRpg& instance);

		bool Invoke(std::string_view, dyn_arg_list, bool&, Game_Interpreter*) override { return false; }
		bool OnSetVariable(int id, int value) override;
		void OnLoadGame() override;

	private:
		void Apply(int value);

		int variable_id = 0;
		int default_speed = 0;
	};
}

#endif
