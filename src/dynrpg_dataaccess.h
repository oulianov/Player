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

#ifndef EP_DYNRPG_DATAACCESS_H
#define EP_DYNRPG_DATAACCESS_H

#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * DynDataAccess.dll by Aubrey the Bard, DJC and xshobux
	 * (AubreyTheBard/DynDataAccess): reads and writes data that the event
	 * commands can't, with @dyndataaccess_<command> comments.
	 *
	 * Party members are 1 to 4, enemies 1 to 8 (troop order); the commands
	 * returning a value take the variable first:
	 *
	 *   get_party_member_id V, Member              actor ID, 0 for an empty slot
	 *   get_party_member_all_ids V                 the 4 of them into V to V+3
	 *   set_party_member_critical_rate N, Member   critical hits 1 out of N (database)
	 *   set_party_member_guard_type N, Member      mighty guard (1) or not (0)
	 *   get/set_party_member_database_attribute_resistance V|N, Member, Attribute
	 *       get: damage rate of the actor's rank in the database, set: the rank (0-4: A-E)
	 *   get/set_party_member_current_attribute_resistance V|N, Member, Attribute
	 *       the shift of the rank in battle plus 1 (0: one rank down, 2: one up)
	 *   get_party_member_condition_turns V, Member, Condition   0: not inflicted
	 *   get_party_member_condition_turns_total V, Member, Priority   turns of the
	 *       conditions of that priority or higher
	 *   get_party_member_condition_total V, Member, Priority     their number
	 *   get/set_party_member_database_condition_resistance V|N, Member, Condition
	 *       get: the rate of the actor's rank in the database, set: the rank
	 *   set_party_member_combo Command, Member, Times
	 *   get/set_party_member_animation2 V|N, Member   battle animation in the database
	 *   get_party_member_defeated_count V
	 *   set_battle_bg "Backdrop\file.png"
	 *   get_troop_initial_size V                   members of the troop
	 *   get_item_attribute V, Item, Attribute      1 when the item has the attribute
	 *   get_enemy_database_id, get_enemy_current_hp/mp, get_enemy_max_hp/mp,
	 *   get_enemy_attack/defense/intelligence/agility V, Enemy
	 *   set_enemy_current_hp/mp N, Enemy
	 *   get_enemy_all_stats V, Enemy               ID, HP, MP, max HP, max MP,
	 *       attack, defense, intelligence and agility into V to V+8
	 *   get_enemy_database_stats V, Enemy          base attack, defense,
	 *       intelligence and agility into V to V+3
	 *   set_enemy_attack/defense/intelligence/agility N, Enemy
	 *   get_enemy_attribute/condition_resistance V, Enemy, Attribute|Condition
	 *       the rate of the enemy's rank in the database
	 *   get_enemy_condition_turns V, Enemy, Condition
	 *   force_enemy_condition Enemy, Condition, Rank   inflicts it when the
	 *       enemy's rank is lower (5: always)
	 *   get/set_enemy_atb V|N, Enemy               the ATB gauge (300000: full)
	 *   enemy_text_popup "text", Enemy
	 *   enemy_number_popup N, Enemy, Color
	 *   enemy_flash Enemy, Red, Green, Blue, Intensity, Frames
	 *   set_enemy_sprite "Monster\file.png", Enemy
	 *   get_enemy_defeated_count V, get_enemy_undefeated_count V
	 *   get/set_encounter_rate_current V|N         encounter steps of the map
	 *   get_database_encounter_rate V              those of the map tree
	 *   get/set_skill_cost V|N, Skill
	 *   set_skill_attack_influence N, Skill
	 *   set_skill_effect_rating N, Skill
	 *   set_terrain_initiative_rate N, Terrain
	 *
	 * The database changes last until the Player is closed, like RPG_RT's.
	 * Where RPG_RT would crash (no such party member or enemy...) a warning
	 * is shown. The Player keeps attribute shifts within one rank of the
	 * base, and only shows files of Backdrop and Monster.
	 */
	class DataAccessPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "DynDataAccess";

		explicit DataAccessPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {}

		bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) override;
	};
}

#endif
