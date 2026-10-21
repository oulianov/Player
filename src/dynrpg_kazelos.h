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

#ifndef EP_DYNRPG_KAZELOS_H
#define EP_DYNRPG_KAZELOS_H

#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * Kaze Line of Sight 1.01 by Kazesui (KazeLoS.dll): sets a switch to
	 * whether a source sees a target: in its sight field, no wall between.
	 *
	 *   @hero_see_event Switch, Event, Width, Radius
	 *   @hero_see_point Switch, X, Y, Width, Radius
	 *   @this_see_hero Switch, Width, Radius
	 *   @this_see_event Switch, Event, Width, Radius
	 *   @this_see_point Switch, X, Y, Width, Radius
	 *   @event_see_hero Switch, Source, Width, Radius
	 *   @event_see_event Switch, Source, Target, Width, Radius
	 *   @event_see_point Switch, Source, X, Y, Width, Radius
	 *   @point_see_point Switch, X, Y, SourceX, SourceY, Width, Radius, Direction
	 *
	 * Width: the angle of the sight field in degrees, half on each side of
	 * the direction the source faces (point_see_point: Direction in degrees,
	 * 0 is right and 90 down). Radius in pixels. The points are screen
	 * pixels, the characters are at the center of their tile, where they are
	 * while moving. The line from the source is followed every 8 pixels up to
	 * the target: a tile where the map allows no move at all blocks it,
	 * partly passable tiles and events don't.
	 *
	 * The readme gives point_see_point the source first and says a width of
	 * 180 sees everything around: the DLL takes the target first and sees
	 * 90 degrees on each side, like the Player.
	 *
	 * The DLL reads the tiles from the map file when the hero changes maps:
	 * the Player uses the map as it is (changed tiles count) and wraps the
	 * line on looping maps.
	 */
	class KazeLosPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "KazeLoS";

		explicit KazeLosPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {}

		bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) override;

		/**
		 * The line of sight of the DLL, in pixels of the map.
		 *
		 * @param target_x target
		 * @param target_y target
		 * @param viewer_x viewer
		 * @param viewer_y viewer
		 * @param fov angle of the field of view (degrees)
		 * @param range farthest distance seen
		 * @param direction where the viewer looks (radians, 0 is right, pi/2 down)
		 * @return whether the viewer sees the target
		 */
		static bool Sees(int target_x, int target_y, int viewer_x, int viewer_y, float fov, float range, float direction);
	};
}

#endif
