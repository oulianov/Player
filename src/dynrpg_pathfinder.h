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

#ifndef EP_DYNRPG_PATHFINDER_H
#define EP_DYNRPG_PATHFINDER_H

#include <optional>
#include <vector>
#include "game_dynrpg.h"

class Game_Character;

namespace DynRpg {
	/**
	 * Pathfinder.dll by anti-freak (DynRPG plugin contest, 2012): moves an
	 * event or the hero to a tile, around the obstacles.
	 *
	 *   @findpath Event, X, Y, Speed, Frequency, MaxTiles, Switch
	 *   @findpathornearestspot Event, X, Y, Speed, Frequency, MaxTiles, Switch
	 *     Event: an event ID, this or hero; the tile must be on the map.
	 *     Speed: 1 to 6 (3 when missing), Frequency: 1 to 8 (8 when missing),
	 *     or default for the character's own.
	 *     MaxTiles: the move route stops after that many steps (0: the whole path).
	 *     Switch: turned ON when a path was found, OFF otherwise (0: none).
	 *     The nearest spot variant goes to the closest reachable tile when the
	 *     target can't be reached.
	 *   @findpath Speed, Frequency, MaxTiles, Switch
	 *     followed by a Set Event Location: moves its event to its tile (and
	 *     facing), the Set Event Location is skipped. Same for the nearest spot.
	 *   @getcureventid Var             ID of the running event (common events:
	 *                                  minus their ID, battle events: 0)
	 *   @waituntilmove Event           does nothing (unfinished in the DLL)
	 *
	 * The search is the DLL's best-first A*: 4 directions, the passability of
	 * the character's moves, cost 1 per step plus 16 per tile of Manhattan
	 * distance to the target, the first of the cheapest tiles in (x, y) order.
	 * The path goes into a move route that waits when blocked, the speed is set.
	 */
	class PathfinderPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "Pathfinder";

		explicit PathfinderPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {}

		bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) override;

		/**
		 * The path of the DLL from the tile of a character to another tile.
		 *
		 * @param character the character moving
		 * @param x target tile
		 * @param y target tile
		 * @param nearest when the target can't be reached, the path to the
		 *   reached tile of lowest cost (it may be the starting tile: no step)
		 * @return the steps (lcf::rpg::MoveCommand codes), none when no path
		 *   or the character is on the target already
		 */
		static std::optional<std::vector<int>> FindPath(Game_Character& character, int x, int y, bool nearest);

	private:
		void FindPathCommand(std::string_view func, dyn_arg_list args, Game_Interpreter* interpreter);
	};
}

#endif
