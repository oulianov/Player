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
#include "dynrpg_kazelos.h"
#include <cmath>
#include <cstdlib>
#include "game_character.h"
#include "game_map.h"
#include "game_player.h"
#include "game_switches.h"
#include "main_data.h"
#include "map_data.h"
#include "output.h"

namespace {
	constexpr double pi = 3.14159265358979323846;

	/** @return a number argument of DynRPG, 0 when missing or not a number */
	double Arg(dyn_arg_list args, size_t i) {
		if (i >= args.size() || args[i].empty()) {
			return 0;
		}
		return std::strtod(args[i].c_str(), nullptr);
	}

	/** @return whether the line of sight stops at a tile: the map allows no move there */
	bool IsWall(int x, int y) {
		if (Game_Map::LoopHorizontal()) {
			x = Game_Map::RoundX(x);
		}
		if (Game_Map::LoopVertical()) {
			y = Game_Map::RoundY(y);
		}
		const int any_direction = Passable::Down | Passable::Left | Passable::Right | Passable::Up;
		return !Game_Map::IsValid(x, y) || !Game_Map::IsPassableTile(nullptr, any_direction, x, y, false, true);
	}

	/** A viewer or a target, in pixels of the map */
	struct Spot {
		int x = 0;
		int y = 0;
	};

	/** @return the center of the tile of a character (getScreenX and getScreenY - 8), where it is while moving */
	Spot CharacterSpot(const Game_Character& character) {
		return {
			character.GetSpriteX() / TILE_SIZE + TILE_SIZE / 2,
			character.GetSpriteY() / TILE_SIZE + TILE_SIZE / 2 - character.GetJumpHeight()
		};
	}

	/** @return a point of the screen on the map */
	Spot ScreenSpot(int x, int y) {
		return {x + Game_Map::GetDisplayX() / TILE_SIZE, y + Game_Map::GetDisplayY() / TILE_SIZE};
	}

	/** @return the direction a character faces, in radians (0 is right, pi/2 down) */
	float Facing(const Game_Character& character) {
		return static_cast<float>(((character.GetFacing() + 3) & 3) * (pi / 2));
	}

	enum class Kind {
		Hero,
		This,
		Event,
		Point
	};

	struct Command {
		std::string_view name;
		Kind viewer;
		Kind target;
	};

	constexpr Command commands[] = {
		{"hero_see_event", Kind::Hero, Kind::Event},
		{"hero_see_point", Kind::Hero, Kind::Point},
		{"this_see_hero", Kind::This, Kind::Hero},
		{"this_see_event", Kind::This, Kind::Event},
		{"this_see_point", Kind::This, Kind::Point},
		{"event_see_hero", Kind::Event, Kind::Hero},
		{"event_see_event", Kind::Event, Kind::Event},
		{"event_see_point", Kind::Event, Kind::Point},
		{"point_see_point", Kind::Point, Kind::Point},
	};
}

bool DynRpg::KazeLosPlugin::Sees(int target_x, int target_y, int viewer_x, int viewer_y, float fov, float range, float direction) {
	const auto dx = static_cast<float>(target_x - viewer_x);
	const auto dy = static_cast<float>(target_y - viewer_y);

	// In the field of view: the direction of the target in [0, 2 pi), its angle with the view in (-pi, pi)
	auto to_target = static_cast<float>(std::atan2(dy, dx));
	if (to_target < 0) {
		to_target = static_cast<float>(to_target + 2 * pi);
	}
	double angle = direction - to_target;
	if (angle >= pi) {
		angle -= 2 * pi;
	}
	if (angle <= -pi) {
		angle += 2 * pi;
	}
	if (std::abs(angle) > fov * (pi / 360)) {
		return false;
	}

	// In range
	const float distance = std::sqrt(dx * dx + dy * dy);
	if (distance > range) {
		return false;
	}

	// No wall at the points of the line every 8 pixels, from the first after the viewer up to the target
	const float steps = distance * 0.125f;
	if (steps <= 0) {
		return true;
	}
	const auto step_x = static_cast<float>(std::cos(to_target) * 8);
	const auto step_y = static_cast<float>(std::sin(to_target) * 8);
	float x = viewer_x + step_x;
	float y = viewer_y + step_y;
	for (float i = 0;;) {
		if (IsWall(static_cast<int>(std::floor(x / TILE_SIZE)), static_cast<int>(std::floor(y / TILE_SIZE)))) {
			return false;
		}
		i += 1;
		if (i >= steps) {
			return true;
		}
		x += step_x;
		y += step_y;
	}
}

bool DynRpg::KazeLosPlugin::Invoke(std::string_view func, dyn_arg_list args, bool&, Game_Interpreter* interpreter) {
	const Command* command = nullptr;
	for (const auto& com: commands) {
		if (func == com.name) {
			command = &com;
			break;
		}
	}
	if (!command) {
		return false;
	}

	auto event = [&](int id) -> Game_Character* {
		Game_Character* character = id > 0 ? Game_Map::GetEvent(id) : nullptr;
		if (!character) {
			Output::Warning("{}: No event {}", func, id);
		}
		return character;
	};
	auto character = [&](Kind kind, size_t& arg) -> Game_Character* {
		switch (kind) {
			case Kind::Hero:
				return Main_Data::game_player.get();
			case Kind::This:
				return event(interpreter ? Game_DynRpg::GetEventId(*interpreter) : 0);
			case Kind::Event:
				return event(static_cast<int>(Arg(args, arg++)));
			case Kind::Point:
				break;
		}
		return nullptr;
	};
	auto point = [&](size_t& arg) {
		const int x = static_cast<int>(Arg(args, arg++));
		const int y = static_cast<int>(Arg(args, arg++));
		return ScreenSpot(x, y);
	};

	// Switch, the viewer event, the target, the viewer point, FOV, Range, Direction (viewer point)
	size_t arg = 1;
	Game_Character* viewer = nullptr;
	if (command->viewer != Kind::Point && !(viewer = character(command->viewer, arg))) {
		return true;
	}

	Spot target;
	if (command->target == Kind::Point) {
		target = point(arg);
	} else if (auto* target_character = character(command->target, arg)) {
		target = CharacterSpot(*target_character);
	} else {
		return true;
	}

	Spot from;
	if (viewer) {
		from = CharacterSpot(*viewer);
	} else {
		from = point(arg);
	}

	const auto fov = static_cast<float>(Arg(args, arg++));
	const auto range = static_cast<float>(Arg(args, arg++));
	const float direction = viewer ? Facing(*viewer) : static_cast<float>(Arg(args, arg) * (pi / 180));

	const bool sees = Sees(target.x, target.y, from.x, from.y, fov, range, direction);
	Main_Data::game_switches->Set(static_cast<int>(Arg(args, 0)), sees);
	return true;
}
