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
#include "dynrpg_pathfinder.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>
#include <lcf/rpg/movecommand.h>
#include <lcf/rpg/moveroute.h>
#include <lcf/rpg/saveeventexecframe.h>
#include "game_character.h"
#include "game_interpreter.h"
#include "game_map.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "main_data.h"

namespace {
	using Cmd = lcf::rpg::EventCommand::Code;
	using Move = lcf::rpg::MoveCommand::Code;

	constexpr int MoveCode(Move code) {
		return static_cast<int>(code);
	}

	/** @return a number argument of DynRPG (a double), none for a token, a string or a missing one */
	std::optional<double> Number(dyn_arg_list args, size_t i) {
		if (i >= args.size() || args[i].empty()) {
			return {};
		}
		const char* begin = args[i].c_str();
		char* end = nullptr;
		double value = std::strtod(begin, &end);
		if (end == begin || *end != '\0') {
			return {};
		}
		return value;
	}

	/** (UINT) of the DLL: through a 64 bit integer, negative numbers wrap */
	uint32_t ToUint(double value) {
		return static_cast<uint32_t>(static_cast<int64_t>(value));
	}

	bool IsToken(dyn_arg_list args, size_t i, std::string_view token) {
		return i < args.size() && args[i] == token;
	}

	/** @return eventId of DynRPG, 0 without an interpreter */
	int DynRpgEventId(Game_Interpreter* interpreter) {
		return interpreter ? Game_DynRpg::GetEventId(*interpreter) : 0;
	}

	/** RPG::map->events[id]: no event for the IDs of common events */
	Game_Character* MapEvent(int id) {
		return id > 0 ? Game_Map::GetEvent(id) : nullptr;
	}
}

std::optional<std::vector<int>> DynRpg::PathfinderPlugin::FindPath(Game_Character& character, int x, int y, bool nearest) {
	// The tiles are Point<UINT> in the DLL, ordered by x, then y
	using Point = std::pair<uint32_t, uint32_t>;
	struct Node {
		Point pos;
		int step; // move code from the previous node, -1 for the start
		int previous; // index in nodes, -1 for the start
		uint32_t g;
		uint32_t h;

		uint32_t F() const { return g + h; }
	};

	const Point start(character.GetX(), character.GetY());
	const Point end(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
	if (start == end) {
		return {};
	}

	auto distance = [&](Point pos) {
		const uint32_t dx = pos.first > end.first ? pos.first - end.first : end.first - pos.first;
		const uint32_t dy = pos.second > end.second ? pos.second - end.second : end.second - pos.second;
		// TILE_SIZE_X and TILE_SIZE_Y
		return dx * 16 + dy * 16;
	};

	std::vector<Node> nodes;
	std::map<Point, int> open_nodes;
	std::map<Point, int> closed_nodes;
	// The DLL takes the first open node of lowest F in (x, y) order: the lowest (F, x, y)
	std::set<std::pair<uint32_t, Point>> open_by_f;

	nodes.push_back({start, -1, -1, 1, distance(start)});
	closed_nodes.emplace(start, 0);
	int current = 0;

	struct Step {
		int dx;
		int dy;
		int code;
	};
	// top, left, bottom, right
	static constexpr Step steps[] = {
		{0, -1, MoveCode(Move::move_up)}, {-1, 0, MoveCode(Move::move_left)},
		{0, 1, MoveCode(Move::move_down)}, {1, 0, MoveCode(Move::move_right)}
	};
	const auto width = static_cast<uint32_t>(Game_Map::GetTilesX());
	const auto height = static_cast<uint32_t>(Game_Map::GetTilesY());

	for (;;) {
		for (const auto& step: steps) {
			const Node cur = nodes[current];
			const Point pos(cur.pos.first + step.dx, cur.pos.second + step.dy);
			// Off the map (only passable on looping maps), the DLL would go on forever
			if (pos.first >= width || pos.second >= height) {
				continue;
			}
			if (!character.CheckWay(static_cast<int>(cur.pos.first), static_cast<int>(cur.pos.second),
					static_cast<int>(pos.first), static_cast<int>(pos.second))) {
				continue;
			}
			if (closed_nodes.count(pos)) {
				continue;
			}

			const Node node{pos, step.code, current, cur.g + 1, distance(pos)};
			auto it = open_nodes.find(pos);
			if (it == open_nodes.end()) {
				open_nodes.emplace(pos, static_cast<int>(nodes.size()));
				open_by_f.emplace(node.F(), pos);
				nodes.push_back(node);
			} else if (node.g < nodes[it->second].g) {
				auto& stored = nodes[it->second];
				open_by_f.erase({stored.F(), pos});
				stored = node;
				open_by_f.emplace(stored.F(), pos);
			}
		}

		if (open_nodes.empty()) {
			if (!nearest) {
				return {};
			}
			// The closed node of lowest F, the first in (x, y) order
			int best = -1;
			for (const auto& closed: closed_nodes) {
				if (best < 0 || nodes[closed.second].F() < nodes[best].F()) {
					best = closed.second;
				}
			}
			current = best;
			break;
		}

		const Point pos = open_by_f.begin()->second;
		open_by_f.erase(open_by_f.begin());
		current = open_nodes[pos];
		open_nodes.erase(pos);
		closed_nodes.emplace(pos, current);

		if (pos == end) {
			break;
		}
	}

	std::vector<int> path;
	for (int i = current; nodes[i].previous >= 0; i = nodes[i].previous) {
		path.push_back(nodes[i].step);
	}
	std::reverse(path.begin(), path.end());
	return path;
}

void DynRpg::PathfinderPlugin::FindPathCommand(std::string_view func, dyn_arg_list args, Game_Interpreter* interpreter) {
	Game_Character* character = nullptr;
	int x = 0;
	int y = 0;
	// first argument after the event and the tile
	size_t param = 0;
	std::optional<int> face;

	// The next command after the comment (and its other lines)
	lcf::rpg::SaveEventExecFrame* frame = interpreter ? &Game_DynRpg::GetFrame(*interpreter) : nullptr;
	size_t next = 0;
	if (frame) {
		next = frame->current_command + 1;
		while (next < frame->commands.size() && frame->commands[next].code == static_cast<int32_t>(Cmd::Comment_2)) {
			++next;
		}
	}

	if (frame && next < frame->commands.size() && frame->commands[next].code == static_cast<int32_t>(Cmd::ChangeEventLocation)) {
		const auto& com = frame->commands[next];
		auto com_param = [&](size_t i) { return i < com.parameters.size() ? com.parameters[i] : 0; };

		int event_id = com_param(0);
		if (event_id == Game_Character::CharThisEvent) {
			event_id = DynRpgEventId(interpreter);
		}
		character = MapEvent(event_id);

		if (com_param(1) == 0) {
			x = com_param(2);
			y = com_param(3);
		} else {
			x = Main_Data::game_variables->Get(com_param(2));
			y = Main_Data::game_variables->Get(com_param(3));
		}

		switch (com_param(4)) {
			case 1:
				face = MoveCode(Move::face_up);
				break;
			case 2:
				face = MoveCode(Move::face_right);
				break;
			case 3:
				face = MoveCode(Move::face_down);
				break;
			case 4:
				face = MoveCode(Move::face_left);
				break;
			default:
				break;
		}

		// The Set Event Location is not executed
		frame->current_command = static_cast<int>(next) + 1;
	} else if (args.size() >= 3 && Number(args, 1) && Number(args, 2)) {
		const double target_x = *Number(args, 1);
		const double target_y = *Number(args, 2);
		if (target_x > Game_Map::GetTilesX() - 1 || target_x < 0 || target_y > Game_Map::GetTilesY() - 1 || target_y < 0) {
			return;
		}
		x = static_cast<int>(ToUint(target_x));
		y = static_cast<int>(ToUint(target_y));

		if (auto id = Number(args, 0)) {
			if (*id <= 0) {
				return;
			}
			character = MapEvent(static_cast<int>(*id));
		} else if (IsToken(args, 0, "this")) {
			character = MapEvent(DynRpgEventId(interpreter));
		} else if (IsToken(args, 0, "hero")) {
			character = Main_Data::game_player.get();
		}

		param = 3;
	} else {
		return;
	}

	int speed = 3;
	if (auto value = Number(args, param)) {
		speed = static_cast<int>(std::clamp<uint32_t>(ToUint(*value), 1, 6));
	} else if (character && IsToken(args, param, "default")) {
		speed = character->GetMoveSpeed();
	}

	int frequency = 8;
	if (auto value = Number(args, param + 1)) {
		frequency = static_cast<int>(std::clamp<uint32_t>(ToUint(*value), 1, 8));
	} else if (character && IsToken(args, param + 1, "default")) {
		frequency = character->GetMoveFrequency();
	}

	uint32_t max_steps = 0;
	if (auto value = Number(args, param + 2)) {
		max_steps = ToUint(*value);
	}

	std::optional<std::vector<int>> path;
	if (character) {
		path = FindPath(*character, x, y, func == "findpathornearestspot");
	}

	if (path) {
		lcf::rpg::MoveRoute route;
		route.repeat = false;
		route.skippable = false;
		for (int code: *path) {
			lcf::rpg::MoveCommand move;
			move.command_id = code;
			route.move_commands.push_back(move);
			if (max_steps != 0 && route.move_commands.size() >= max_steps) {
				break;
			}
		}
		if (face) {
			lcf::rpg::MoveCommand move;
			move.command_id = *face;
			route.move_commands.push_back(move);
		}

		// RPG::Character::stop, then move
		character->CancelMoveRoute();
		character->ForceMoveRoute(route, frequency);
		character->SetMoveSpeed(speed);
	}

	auto switch_id = Number(args, param + 3);
	if (switch_id && *switch_id > 0) {
		Main_Data::game_switches->Set(static_cast<int>(*switch_id), path.has_value());
	}
}

bool DynRpg::PathfinderPlugin::Invoke(std::string_view func, dyn_arg_list args, bool&, Game_Interpreter* interpreter) {
	if (func == "findpath" || func == "findpathornearestspot") {
		FindPathCommand(func, args, interpreter);
		return true;
	}

	if (func == "getcureventid") {
		if (auto var = Number(args, 0)) {
			instance.SetVariable(static_cast<int>(ToUint(*var)), DynRpgEventId(interpreter));
		}
		return true;
	}

	if (func == "waituntilmove") {
		// The DLL looks the event up and does nothing more
		return true;
	}

	return false;
}
