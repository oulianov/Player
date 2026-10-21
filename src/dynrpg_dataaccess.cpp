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
#include "dynrpg_dataaccess.h"
#include <cstdlib>
#include <optional>
#include <lcf/data.h>
#include <lcf/reader_util.h>
#include "game_actor.h"
#include "game_battle.h"
#include "game_enemy.h"
#include "game_enemyparty.h"
#include "game_map.h"
#include "game_party.h"
#include "main_data.h"
#include "output.h"
#include "scene.h"
#include "scene_battle_rpg2k3.h"
#include "utils.h"

namespace {
	constexpr std::string_view prefix = "dyndataaccess_";
	constexpr int max_actors = 4;
	constexpr int death_state = 1;

	/** @return (int) of a number argument of DynRPG, 0 when missing or not a number */
	int Arg(dyn_arg_list args, size_t i) {
		if (i >= args.size() || args[i].empty()) {
			return 0;
		}
		return static_cast<int>(std::strtod(args[i].c_str(), nullptr));
	}

	std::string TextArg(dyn_arg_list args, size_t i) {
		return i < args.size() ? args[i] : std::string();
	}

	/** RPG::Actor::partyMember: none for an empty slot (1 to 4) */
	Game_Actor* PartyMember(int slot) {
		auto actors = Main_Data::game_party->GetActors();
		return slot >= 1 && slot <= static_cast<int>(actors.size()) ? actors[slot - 1] : nullptr;
	}

	Game_Actor* NeedPartyMember(std::string_view func, int slot) {
		auto* actor = PartyMember(slot);
		if (!actor) {
			Output::Warning("{}: No party member {}", func, slot);
		}
		return actor;
	}

	/** RPG::monsters: the troop members in battle (1 to 8) */
	Game_Enemy* NeedEnemy(std::string_view func, int slot) {
		std::vector<Game_Enemy*> enemies;
		if (Main_Data::game_enemyparty) {
			enemies = Main_Data::game_enemyparty->GetEnemies();
		}
		if (slot < 1 || slot > static_cast<int>(enemies.size())) {
			Output::Warning("{}: No enemy {}", func, slot);
			return nullptr;
		}
		return enemies[slot - 1];
	}

	template <typename T>
	T* NeedElement(std::string_view func, std::vector<T>& list, int id, std::string_view what) {
		auto* element = lcf::ReaderUtil::GetElement(list, id);
		if (!element) {
			Output::Warning("{}: Invalid {} ID {}", func, what, id);
		}
		return element;
	}

	/** @return the rank (0-4: A-E) of a database actor or enemy for an attribute or a condition (C when unset) */
	int Rank(const std::vector<uint8_t>& ranks, int id) {
		return id >= 1 && id <= static_cast<int>(ranks.size()) ? ranks[id - 1] : 2;
	}

	void SetRank(std::vector<uint8_t>& ranks, int id, int rank) {
		if (id < 1) {
			return;
		}
		if (id > static_cast<int>(ranks.size())) {
			ranks.resize(id, 2);
		}
		ranks[id - 1] = static_cast<uint8_t>(rank);
	}

	/** @return the rate of a rank (dmgA to dmgE of an attribute, susA to susE of a condition), none out of A-E */
	template <typename T>
	std::optional<int> RateOfRank(const T& data, int rank) {
		switch (rank) {
			case 0:
				return data.a_rate;
			case 1:
				return data.b_rate;
			case 2:
				return data.c_rate;
			case 3:
				return data.d_rate;
			case 4:
				return data.e_rate;
			default:
				return {};
		}
	}

	/** @return the file of a folder of the game as the Player loads it: without the folder and the extension */
	std::optional<std::string> FileOfFolder(std::string_view path, std::string_view folder) {
		std::string name(path);
		for (auto& c: name) {
			if (c == '\\') {
				c = '/';
			}
		}
		const std::string dir = ToString(folder) + "/";
		if (!StartsWith(Utils::LowerCase(name), Utils::LowerCase(dir))) {
			return {};
		}
		name = name.substr(dir.size());
		auto dot = name.rfind('.');
		if (dot != std::string::npos && name.find('/', dot) == std::string::npos) {
			name.resize(dot);
		}
		return name;
	}

	Scene_Battle_Rpg2k3* BattleScene() {
		if (!Scene::instance || Scene::instance->type != Scene::Battle) {
			return nullptr;
		}
		return dynamic_cast<Scene_Battle_Rpg2k3*>(Scene::instance.get());
	}

	using EnemyGetter = int (*)(const Game_Enemy&);
	using EnemySetter = void (*)(Game_Enemy&, int);

	// Commands V, Enemy
	const std::pair<std::string_view, EnemyGetter> enemy_getters[] = {
		{"get_enemy_database_id", [](const Game_Enemy& e) { return e.GetId(); }},
		{"get_enemy_current_hp", [](const Game_Enemy& e) { return e.GetHp(); }},
		{"get_enemy_current_mp", [](const Game_Enemy& e) { return e.GetSp(); }},
		{"get_enemy_max_hp", [](const Game_Enemy& e) { return e.GetMaxHp(); }},
		{"get_enemy_max_mp", [](const Game_Enemy& e) { return e.GetMaxSp(); }},
		{"get_enemy_attack", [](const Game_Enemy& e) { return e.GetAtk(); }},
		{"get_enemy_defense", [](const Game_Enemy& e) { return e.GetDef(); }},
		{"get_enemy_intelligence", [](const Game_Enemy& e) { return e.GetSpi(); }},
		{"get_enemy_agility", [](const Game_Enemy& e) { return e.GetAgi(); }},
		{"get_enemy_atb", [](const Game_Enemy& e) { return e.GetAtbGauge(); }},
	};

	// Commands N, Enemy (attack and the others relative to the database value)
	const std::pair<std::string_view, EnemySetter> enemy_setters[] = {
		{"set_enemy_current_hp", [](Game_Enemy& e, int n) { e.SetHp(n); }},
		{"set_enemy_current_mp", [](Game_Enemy& e, int n) { e.SetSp(n); }},
		{"set_enemy_attack", [](Game_Enemy& e, int n) { e.SetAtkModifier(n - e.GetBaseAtk()); }},
		{"set_enemy_defense", [](Game_Enemy& e, int n) { e.SetDefModifier(n - e.GetBaseDef()); }},
		{"set_enemy_intelligence", [](Game_Enemy& e, int n) { e.SetSpiModifier(n - e.GetBaseSpi()); }},
		{"set_enemy_agility", [](Game_Enemy& e, int n) { e.SetAgiModifier(n - e.GetBaseAgi()); }},
		{"set_enemy_atb", [](Game_Enemy& e, int n) { e.SetAtbGauge(n); }},
	};
}

bool DynRpg::DataAccessPlugin::Invoke(std::string_view func, dyn_arg_list args, bool&, Game_Interpreter*) {
	if (!StartsWith(func, prefix)) {
		return false;
	}
	const std::string_view cmd = func.substr(prefix.size());
	auto arg = [&](size_t i) { return Arg(args, i); };
	auto set_var = [&](int var, int value) { instance.SetVariable(var, value); };

	for (const auto& getter: enemy_getters) {
		if (cmd == getter.first) {
			if (auto* enemy = NeedEnemy(func, arg(1))) {
				set_var(arg(0), getter.second(*enemy));
			}
			return true;
		}
	}
	for (const auto& setter: enemy_setters) {
		if (cmd == setter.first) {
			if (auto* enemy = NeedEnemy(func, arg(1))) {
				setter.second(*enemy, arg(0));
			}
			return true;
		}
	}

	// Actors
	if (cmd == "get_party_member_id") {
		auto* actor = PartyMember(arg(1));
		set_var(arg(0), actor ? actor->GetId() : 0);
	} else if (cmd == "get_party_member_all_ids") {
		for (int i = 0; i < max_actors; ++i) {
			auto* actor = PartyMember(i + 1);
			set_var(arg(0) + i, actor ? actor->GetId() : 0);
		}
	} else if (cmd == "set_party_member_critical_rate") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			if (auto* db = NeedElement(func, lcf::Data::actors, actor->GetId(), "actor")) {
				db->critical_hit_chance = arg(0);
			}
		}
	} else if (cmd == "set_party_member_guard_type") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			actor->SetStrongDefense(arg(0) != 0);
		}
	} else if (cmd == "get_party_member_database_attribute_resistance" || cmd == "set_party_member_database_attribute_resistance"
			|| cmd == "get_party_member_database_condition_resistance" || cmd == "set_party_member_database_condition_resistance") {
		auto* actor = NeedPartyMember(func, arg(1));
		auto* db = actor ? NeedElement(func, lcf::Data::actors, actor->GetId(), "actor") : nullptr;
		if (!db) {
			return true;
		}
		const bool attribute = cmd.find("attribute") != std::string_view::npos;
		auto& ranks = attribute ? db->attribute_ranks : db->state_ranks;
		const int id = arg(2);
		if (StartsWith(cmd, "set_")) {
			if (arg(0) >= 0 && arg(0) <= 4) {
				SetRank(ranks, id, arg(0));
			}
		} else if (attribute) {
			if (auto* data = NeedElement(func, lcf::Data::attributes, id, "attribute")) {
				if (auto rate = RateOfRank(*data, Rank(ranks, id))) {
					set_var(arg(0), *rate);
				}
			}
		} else if (auto* data = NeedElement(func, lcf::Data::states, id, "condition")) {
			if (auto rate = RateOfRank(*data, Rank(ranks, id))) {
				set_var(arg(0), *rate);
			}
		}
	} else if (cmd == "get_party_member_current_attribute_resistance") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			set_var(arg(0), actor->GetAttributeRateShift(arg(2)) + 1);
		}
	} else if (cmd == "set_party_member_current_attribute_resistance") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			const int id = arg(2);
			actor->ShiftAttributeRate(id, arg(0) - 1 - actor->GetAttributeRateShift(id));
		}
	} else if (cmd == "get_party_member_condition_turns") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			const auto& states = actor->GetStates();
			const int id = arg(2);
			set_var(arg(0), id >= 1 && id <= static_cast<int>(states.size()) ? states[id - 1] : 0);
		}
	} else if (cmd == "get_party_member_condition_turns_total" || cmd == "get_party_member_condition_total") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			const bool turns = cmd == "get_party_member_condition_turns_total";
			const auto& states = actor->GetStates();
			int total = 0;
			for (int i = 0; i < static_cast<int>(lcf::Data::states.size()) && i < static_cast<int>(states.size()); ++i) {
				if (lcf::Data::states[i].priority >= arg(2) && states[i] > 0) {
					total += turns ? states[i] : 1;
				}
			}
			set_var(arg(0), total);
		}
	} else if (cmd == "set_party_member_combo") {
		if (auto* actor = NeedPartyMember(func, arg(1))) {
			actor->SetBattleCombo(arg(0), arg(2));
		}
	} else if (cmd == "get_party_member_animation2" || cmd == "set_party_member_animation2") {
		auto* actor = NeedPartyMember(func, arg(1));
		if (auto* db = actor ? NeedElement(func, lcf::Data::actors, actor->GetId(), "actor") : nullptr) {
			if (cmd == "get_party_member_animation2") {
				set_var(arg(0), db->battler_animation);
			} else {
				db->battler_animation = arg(0);
			}
		}
	} else if (cmd == "get_party_member_defeated_count") {
		int count = 0;
		for (int i = 1; i <= max_actors; ++i) {
			auto* actor = PartyMember(i);
			count += actor && actor->HasState(death_state);
		}
		set_var(arg(0), count);
	}

	// Battle, troop and items
	else if (cmd == "set_battle_bg") {
		const auto path = TextArg(args, 0);
		if (auto name = FileOfFolder(path, "Backdrop")) {
			Game_Battle::ChangeBackground(*name);
		} else {
			Output::Warning("{}: Only the files of Backdrop are supported: {}", func, path);
		}
	} else if (cmd == "get_troop_initial_size") {
		if (const auto* troop = Game_Battle::GetActiveTroop()) {
			set_var(arg(0), static_cast<int>(troop->members.size()));
		} else {
			Output::Warning("{}: No battle", func);
		}
	} else if (cmd == "get_item_attribute") {
		if (auto* item = NeedElement(func, lcf::Data::items, arg(1), "item")) {
			const int id = arg(2);
			set_var(arg(0), id >= 1 && id <= static_cast<int>(item->attribute_set.size()) && item->attribute_set[id - 1]);
		}
	}

	// Enemies
	else if (cmd == "get_enemy_all_stats") {
		if (auto* enemy = NeedEnemy(func, arg(1))) {
			const int stats[] = {
				enemy->GetId(), enemy->GetHp(), enemy->GetSp(), enemy->GetMaxHp(), enemy->GetMaxSp(),
				enemy->GetAtk(), enemy->GetDef(), enemy->GetSpi(), enemy->GetAgi()
			};
			for (int i = 0; i < static_cast<int>(std::size(stats)); ++i) {
				set_var(arg(0) + i, stats[i]);
			}
		}
	} else if (cmd == "get_enemy_database_stats") {
		if (auto* enemy = NeedEnemy(func, arg(1))) {
			const int stats[] = {enemy->GetBaseAtk(), enemy->GetBaseDef(), enemy->GetBaseSpi(), enemy->GetBaseAgi()};
			for (int i = 0; i < static_cast<int>(std::size(stats)); ++i) {
				set_var(arg(0) + i, stats[i]);
			}
		}
	} else if (cmd == "get_enemy_attribute_resistance" || cmd == "get_enemy_condition_resistance") {
		auto* enemy = NeedEnemy(func, arg(1));
		auto* db = enemy ? NeedElement(func, lcf::Data::enemies, enemy->GetId(), "enemy") : nullptr;
		const int id = arg(2);
		std::optional<int> rate;
		if (!db) {
			return true;
		} else if (cmd == "get_enemy_attribute_resistance") {
			if (auto* data = NeedElement(func, lcf::Data::attributes, id, "attribute")) {
				rate = RateOfRank(*data, Rank(db->attribute_ranks, id));
			}
		} else if (auto* data = NeedElement(func, lcf::Data::states, id, "condition")) {
			rate = RateOfRank(*data, Rank(db->state_ranks, id));
		}
		if (rate) {
			set_var(arg(0), *rate);
		}
	} else if (cmd == "get_enemy_condition_turns") {
		if (auto* enemy = NeedEnemy(func, arg(1))) {
			const auto& states = enemy->GetStates();
			const int id = arg(2);
			set_var(arg(0), id >= 1 && id <= static_cast<int>(states.size()) ? states[id - 1] : 0);
		}
	} else if (cmd == "force_enemy_condition") {
		auto* enemy = NeedEnemy(func, arg(0));
		auto* db = enemy ? NeedElement(func, lcf::Data::enemies, enemy->GetId(), "enemy") : nullptr;
		const int id = arg(1);
		if (db && Rank(db->state_ranks, id) < arg(2)) {
			enemy->AddState(id, true);
			if (id == death_state) {
				enemy->SetHp(0);
			}
		}
	} else if (cmd == "enemy_text_popup" || cmd == "enemy_number_popup") {
		auto* enemy = NeedEnemy(func, arg(1));
		auto* scene = BattleScene();
		if (enemy && scene) {
			if (cmd == "enemy_text_popup") {
				scene->ShowBattlerPopup(enemy, 0, TextArg(args, 0));
			} else {
				scene->ShowBattlerPopup(enemy, arg(2), std::to_string(arg(0)));
			}
		}
	} else if (cmd == "enemy_flash") {
		if (auto* enemy = NeedEnemy(func, arg(0))) {
			enemy->Flash(arg(1), arg(2), arg(3), arg(4), arg(5));
		}
	} else if (cmd == "set_enemy_sprite") {
		const auto path = TextArg(args, 0);
		if (auto* enemy = NeedEnemy(func, arg(1))) {
			if (auto name = FileOfFolder(path, "Monster")) {
				enemy->SetSpriteName(*name);
			} else {
				Output::Warning("{}: Only the files of Monster are supported: {}", func, path);
			}
		}
	} else if (cmd == "get_enemy_defeated_count" || cmd == "get_enemy_undefeated_count") {
		const bool defeated = cmd == "get_enemy_defeated_count";
		int count = 0;
		if (Main_Data::game_enemyparty) {
			for (auto* enemy: Main_Data::game_enemyparty->GetEnemies()) {
				count += defeated ? enemy->GetHp() < 1 : enemy->GetHp() > 0;
			}
		}
		set_var(arg(0), count);
	}

	// Map, skills and terrains
	else if (cmd == "get_encounter_rate_current") {
		set_var(arg(0), Game_Map::GetEncounterSteps());
	} else if (cmd == "set_encounter_rate_current") {
		Game_Map::SetEncounterSteps(arg(0));
	} else if (cmd == "get_database_encounter_rate") {
		set_var(arg(0), Game_Map::GetOriginalEncounterSteps());
	} else if (cmd == "get_skill_cost") {
		if (auto* skill = NeedElement(func, lcf::Data::skills, arg(1), "skill")) {
			set_var(arg(0), skill->sp_cost);
		}
	} else if (cmd == "set_skill_cost" || cmd == "set_skill_attack_influence" || cmd == "set_skill_effect_rating") {
		if (auto* skill = NeedElement(func, lcf::Data::skills, arg(1), "skill")) {
			auto& value = cmd == "set_skill_cost" ? skill->sp_cost
				: cmd == "set_skill_attack_influence" ? skill->physical_rate : skill->power;
			value = arg(0);
		}
	} else if (cmd == "set_terrain_initiative_rate") {
		if (auto* terrain = NeedElement(func, lcf::Data::terrains, arg(1), "terrain")) {
			// Initiative: the party attacks the back of the enemies
			terrain->special_back_party = arg(0);
		}
	} else {
		return false;
	}
	return true;
}
