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
#include "dynrpg_msgcommands.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_variables.h"
#include "main_data.h"

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <lcf/data.h>
#include <lcf/reader_util.h>
#include <lcf/rpg/saveeventexecframe.h>

namespace {
	using Cmd = lcf::rpg::EventCommand::Code;
	using Term = lcf::DBString lcf::rpg::Terms::*;

	// \R[n]: the terms in the order of RPG_RT 1.08's vocabulary, 36-40 are not strings
	constexpr Term terms[] = {
		nullptr,
		&lcf::rpg::Terms::special_combat, &lcf::rpg::Terms::escape_failure, &lcf::rpg::Terms::victory,
		&lcf::rpg::Terms::defeat, &lcf::rpg::Terms::exp_received, &lcf::rpg::Terms::gold_recieved_a,
		&lcf::rpg::Terms::gold_recieved_b, &lcf::rpg::Terms::item_recieved, &lcf::rpg::Terms::attacking,
		&lcf::rpg::Terms::enemy_critical, &lcf::rpg::Terms::actor_critical, &lcf::rpg::Terms::defending,
		&lcf::rpg::Terms::observing, &lcf::rpg::Terms::focus, &lcf::rpg::Terms::autodestruction,
		&lcf::rpg::Terms::enemy_escape, &lcf::rpg::Terms::enemy_transform, &lcf::rpg::Terms::enemy_damaged,
		&lcf::rpg::Terms::enemy_undamaged, &lcf::rpg::Terms::actor_damaged, &lcf::rpg::Terms::actor_undamaged,
		&lcf::rpg::Terms::skill_failure_a, &lcf::rpg::Terms::skill_failure_b, &lcf::rpg::Terms::skill_failure_c,
		&lcf::rpg::Terms::dodge, &lcf::rpg::Terms::use_item, &lcf::rpg::Terms::hp_recovery,
		&lcf::rpg::Terms::parameter_increase, &lcf::rpg::Terms::parameter_decrease, &lcf::rpg::Terms::enemy_hp_absorbed,
		&lcf::rpg::Terms::actor_hp_absorbed, &lcf::rpg::Terms::resistance_increase, &lcf::rpg::Terms::resistance_decrease,
		&lcf::rpg::Terms::level_up, &lcf::rpg::Terms::skill_learned,
		nullptr, nullptr, nullptr, nullptr, nullptr,
		&lcf::rpg::Terms::possessed_items, &lcf::rpg::Terms::equipped_items, &lcf::rpg::Terms::gold,
		&lcf::rpg::Terms::battle_fight, &lcf::rpg::Terms::battle_auto, &lcf::rpg::Terms::battle_escape,
		&lcf::rpg::Terms::command_attack, &lcf::rpg::Terms::command_defend, &lcf::rpg::Terms::command_item,
		&lcf::rpg::Terms::command_skill, &lcf::rpg::Terms::menu_equipment, &lcf::rpg::Terms::menu_save,
		&lcf::rpg::Terms::menu_quit, &lcf::rpg::Terms::new_game, &lcf::rpg::Terms::load_game,
		&lcf::rpg::Terms::exit_game, &lcf::rpg::Terms::level, &lcf::rpg::Terms::health_points,
		&lcf::rpg::Terms::spirit_points, &lcf::rpg::Terms::normal_status, &lcf::rpg::Terms::exp_short,
		&lcf::rpg::Terms::lvl_short, &lcf::rpg::Terms::hp_short, &lcf::rpg::Terms::sp_short,
		&lcf::rpg::Terms::sp_cost, &lcf::rpg::Terms::attack, &lcf::rpg::Terms::defense,
		&lcf::rpg::Terms::spirit, &lcf::rpg::Terms::agility, &lcf::rpg::Terms::weapon,
		&lcf::rpg::Terms::shield, &lcf::rpg::Terms::armor, &lcf::rpg::Terms::helmet,
		&lcf::rpg::Terms::accessory, &lcf::rpg::Terms::save_game_message, &lcf::rpg::Terms::load_game_message,
		&lcf::rpg::Terms::exit_game_message, &lcf::rpg::Terms::file, &lcf::rpg::Terms::yes,
		&lcf::rpg::Terms::no, &lcf::rpg::Terms::status, &lcf::rpg::Terms::order,
		&lcf::rpg::Terms::row, &lcf::rpg::Terms::wait_on, &lcf::rpg::Terms::wait_off,
		&lcf::rpg::Terms::battle_start, &lcf::rpg::Terms::miss,
	};
	static_assert(std::size(terms) == 88);

	/** Name of entry id of a database list ("" when there is none: RPG::getItemName...) */
	template <typename T, typename F>
	std::string Name(const std::vector<T>& list, int id, F&& field) {
		const auto* entry = lcf::ReaderUtil::GetElement(list, id);
		return entry ? ToString(field(*entry)) : std::string();
	}

	/** The DLL's "0 < n < count": the last entry of the list is rejected */
	template <typename T>
	bool InRange(const std::vector<T>& list, int n) {
		return n > 0 && n < static_cast<int>(list.size());
	}
}

bool DynRpg::MsgCommandsPlugin::Invoke(std::string_view func, dyn_arg_list, bool&, Game_Interpreter* interpreter) {
	if (func != "process_cmd") {
		return false;
	}
	if (!interpreter) {
		return true;
	}

	auto& frame = Game_DynRpg::GetFrame(*interpreter);
	auto& list = frame.commands;
	const size_t line = frame.current_command;

	// the command after the whole comment must be a message
	size_t next = line + 1;
	while (next < list.size() && list[next].code == static_cast<int32_t>(Cmd::Comment_2)) {
		++next;
	}
	if (next >= list.size() || list[next].code != static_cast<int32_t>(Cmd::ShowMessage)) {
		return true;
	}

	flag_v = flag_m = false;
	list[next].string = lcf::DBString(ProcessLine(ToString(list[next].string)));
	// the DLL counts its 3 other lines from the comment's first line
	for (size_t i = line + 2; i < line + 5 && i < list.size(); ++i) {
		if (list[i].code != static_cast<int32_t>(Cmd::ShowMessage_2)) {
			break;
		}
		list[i].string = lcf::DBString(ProcessLine(ToString(list[i].string)));
	}
	return true;
}

std::string DynRpg::MsgCommandsPlugin::ProcessLine(const std::string& in) {
	std::string out;
	if (in.empty()) {
		return out;
	}
	auto at = [&](size_t k) { return k < in.size() ? in[k] : '\0'; };

	size_t i = 0;
	do {
		const char ch = at(i + 1);
		if (in[i] == '\\' && ch != '\0' && std::strchr("ABDIKMORTVabdikmortv", ch)) {
			out += Convert(in.substr(i + 1));
			// skip to the first "]", and the one of the code around a nested \V or \m
			size_t end = in.find(']', i + 1);
			i = end == std::string::npos ? in.size() + 1 : end;
			bool past_end = false;
			if (flag_v) {
				if (i >= in.size()) {
					past_end = true;
				} else if (at(i + 1) == ']') {
					++i;
				}
			}
			if (flag_m && !past_end && i < in.size() && at(i + 1) == ']') {
				++i;
			}
			if ((ch & 0xDF) == 'V') {
				flag_v = true;
			}
			if (ch == 'm') {
				flag_m = true;
			}
		} else {
			out += in[i];
			flag_v = flag_m = false;
		}
		++i;
	} while (i < in.size());
	return out;
}

std::string DynRpg::MsgCommandsPlugin::Convert(const std::string& s) {
	// s starts at the code letter: the argument ends at the first "]" from index 2
	size_t p = 2;
	while (p < s.size()) {
		if (s[p++] == ']') {
			break;
		}
	}
	if (s.size() < 2) {
		// the DLL throws std::out_of_range here and RPG_RT stops
		return {};
	}
	const int n = std::atoi(ProcessLine(s.substr(2, p - 2)).c_str());
	// a wrong id: the rest of the line from the letter, minus its last character
	const std::string fallback = s.substr(0, s.size() - 1);

	switch (s[0]) {
		case 'V':
		case 'v':
			return n > 0 ? std::to_string(Main_Data::game_variables->Get(n)) : "0";
		case 'I':
			return Name(lcf::Data::items, n, [](auto& e) { return e.name; });
		case 'i':
			return Name(lcf::Data::items, n, [](auto& e) { return e.description; });
		case 'K':
			return Name(lcf::Data::skills, n, [](auto& e) { return e.name; });
		case 'k':
			return Name(lcf::Data::skills, n, [](auto& e) { return e.description; });
		case 'O':
		case 'o':
			return Name(lcf::Data::states, n, [](auto& e) { return e.name; });
		case 'M':
			return InRange(lcf::Data::enemies, n) ? Name(lcf::Data::enemies, n, [](auto& e) { return e.name; }) : fallback;
		case 'm': {
			// RPG_RT crashes on an enemy that doesn't exist
			const auto* enemy = lcf::ReaderUtil::GetElement(lcf::Data::enemies, n);
			return std::to_string(enemy ? enemy->drop_id : 0);
		}
		case 'A':
		case 'a':
			return InRange(lcf::Data::attributes, n) ? Name(lcf::Data::attributes, n, [](auto& e) { return e.name; }) : fallback;
		case 'T':
		case 't':
			return InRange(lcf::Data::terrains, n) ? Name(lcf::Data::terrains, n, [](auto& e) { return e.name; }) : fallback;
		case 'D':
		case 'd': {
			// the actor's current title
			auto* actor = InRange(lcf::Data::actors, n) ? Main_Data::game_actors->GetActor(n) : nullptr;
			return actor ? ToString(actor->GetTitle()) : fallback;
		}
		case 'B':
		case 'b': {
			// 0-based index in the list of battle commands
			const auto& commands = lcf::Data::battlecommands.commands;
			return InRange(commands, n) ? ToString(commands[n].name) : fallback;
		}
		case 'R':
		case 'r':
			if (n >= 1 && n < static_cast<int>(std::size(terms)) && terms[n]) {
				return ToString(lcf::Data::terms.*terms[n]);
			}
			return fallback;
	}
	return fallback;
}
