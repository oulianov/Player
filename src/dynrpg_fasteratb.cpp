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

// Headers
#include "dynrpg_fasteratb.h"
#include "game_variables.h"
#include "main_data.h"

namespace {
	// onStartup runs once in RPG_RT, the plugin objects live as long as a game
	bool started = false;
}

DynRpg::FasterAtbPlugin::FasterAtbPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {
	variable_id = instance.GetConfigInt(dll_name, "BattleSpeedVariable");
	default_speed = instance.GetConfigInt(dll_name, "DefaultSpeed");
	if (!started) {
		started = true;
		Game_DynRpg::SetBattleSpeed(default_speed);
	}
}

void DynRpg::FasterAtbPlugin::Apply(int value) {
	// faster_atb_advanced.cpp: battlePercents, indexed from -4 (paused) to 16
	if (value >= -4 && value <= -1) {
		Game_DynRpg::SetBattleSpeed((value + 4) * 25);
	} else if (value >= 1 && value <= 16) {
		Game_DynRpg::SetBattleSpeed(value * 100);
	} else if (value == 0 || value > 16) {
		Game_DynRpg::SetBattleSpeed(default_speed);
	}
	// below -4: the speed doesn't change
}

bool DynRpg::FasterAtbPlugin::OnSetVariable(int id, int value) {
	if (id == variable_id) {
		Apply(value);
	}
	return true;
}

void DynRpg::FasterAtbPlugin::OnLoadGame() {
	if (variable_id > 0) {
		Apply(Main_Data::game_variables->Get(variable_id));
	}
}
