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

#ifndef EP_DYNRPG_ATBWAITONLY_H
#define EP_DYNRPG_ATBWAITONLY_H

#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * ATBWaitOnly.dll: the ATB mode is Wait after a new game and after a
	 * game was loaded (RPG::system->atbMode = ATBM_WAIT). Nothing else: the
	 * mode can still be changed afterwards. No settings, no comments.
	 */
	class AtbWaitOnlyPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "ATBWaitOnly";
		/** It reads no settings: an empty section tells the Player that the game uses it */
		static constexpr const char* section = dll_name;

		explicit AtbWaitOnlyPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {}

		bool Invoke(std::string_view, dyn_arg_list, bool&, Game_Interpreter*) override { return false; }
		void OnNewGame() override;
		void OnLoadGame() override;
	};
}

#endif
