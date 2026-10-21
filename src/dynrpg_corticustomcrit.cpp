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
#include "dynrpg_corticustomcrit.h"
#include "async_handler.h"
#include "battle_animation.h"
#include "bitmap.h"
#include "filefinder.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_battlealgorithm.h"
#include "game_enemy.h"
#include "game_enemyparty.h"
#include "game_party.h"
#include "game_switches.h"
#include "game_system.h"
#include "game_variables.h"
#include "main_data.h"
#include "output.h"
#include "rand.h"
#include "scene.h"
#include <lcf/data.h>
#include <lcf/reader_util.h>

#include <algorithm>

namespace {
	using Action = DynRpg::CortiCustomCritPlugin::Action;

	const lcf::rpg::Item* Item(int id) {
		return id > 0 ? lcf::ReaderUtil::GetElement(lcf::Data::items, id) : nullptr;
	}

	/** RPG::DBActor curves: value of the level (1 to 99), 0 outside the curve */
	int Curve(const std::vector<int16_t>& curve, int level) {
		return level >= 1 && level <= static_cast<int>(curve.size()) ? curve[level - 1] : 0;
	}

	int Var(int id) {
		return Main_Data::game_variables->Get(id);
	}

	/** RPG::Action of the action a battler starts */
	Action ActionOf(const Game_BattleAlgorithm::AlgorithmBase& algo) {
		Action action;
		if (algo.GetType() == Game_BattleAlgorithm::Type::Normal) {
			action.attack = true;
		} else if (algo.GetType() == Game_BattleAlgorithm::Type::Skill) {
			auto& skill_algo = static_cast<const Game_BattleAlgorithm::Skill&>(algo);
			if (!skill_algo.GetItem()) { // an item calling a skill is AK_ITEM
				action.skill_id = skill_algo.GetSkill().ID;
			}
		}
		if (auto* party = algo.GetOriginalPartyTarget()) {
			action.target = party == Main_Data::game_party.get() ? Action::AllActors : Action::AllMonsters;
		} else if (auto* target = algo.GetOriginalSingleTarget()) {
			if (target->GetType() == Game_Battler::Type_Ally) {
				action.target = Action::Actor;
				action.target_index = Main_Data::game_party->GetActorPositionInParty(target->GetId());
			} else {
				action.target = Action::Monster;
				action.target_index = Main_Data::game_enemyparty->GetEnemyPositionInParty(static_cast<Game_Enemy*>(target));
			}
		}
		return action;
	}

	Game_Actor* PartyMember(int index) {
		auto actors = Main_Data::game_party->GetActors();
		return index >= 0 && index < static_cast<int>(actors.size()) ? actors[index] : nullptr;
	}

	/** RPG::monsters[index]: the monster in that place of the troop, none outside of battle */
	Game_Enemy* Monster(int index) {
		if (Scene::instance && Scene::instance->type != Scene::Battle) {
			return nullptr;
		}
		auto& party = *Main_Data::game_enemyparty;
		return index >= 0 && index < party.GetBattlerCount() ? &party[index] : nullptr;
	}

	/** Whether an armor (not a weapon) prevents critical hits */
	bool PreventsCrit(int item_id) {
		auto* item = Item(item_id);
		return item && item->type != lcf::rpg::Item::Type_weapon && item->prevent_critical;
	}

	bool HeroPreventsCrit(const Game_Actor& actor) {
		return PreventsCrit(actor.GetShieldId()) || PreventsCrit(actor.GetArmorId())
			|| PreventsCrit(actor.GetHelmetId()) || PreventsCrit(actor.GetAccessoryId());
	}

	/** Whether the skill changes HP. As the DLL: the last skill of the database never does */
	bool CanSkillDamageOrHeal(int skill_id) {
		if (skill_id > 0 && skill_id < static_cast<int>(lcf::Data::skills.size())) {
			return lcf::Data::skills[skill_id - 1].affect_hp;
		}
		return false;
	}
}

DynRpg::CortiCustomCritPlugin::CortiCustomCritPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {
	// CortiLog: onStartup
	log_enabled = instance.GetConfigInt(log_section, "LogToConsole") > 0 || instance.GetConfigInt(log_section, "LogToFile") > 0;
	Log("CortiLog Startup");

	// onInitFinished
	if (instance.HasConfigSection(dll_name)) {
		Output::Warning("CortiCustomCrit: Configuration contains section [CortiXCustomCrit] ( with an X ). In the current version, the section name is [CortiCustomCrit] ( without the X ).");
	}
	Log("CortiCustomCrit: Initializing.Started");
	LoadSettings();
	LoadVisualEffects();
	LoadSkills();
	if (calc_type == 1) {
		LoadType1();
	} else if (calc_type == 2) {
		t2_source = Config("t2CritSourceStat");
		if (t2_source <= 0 || t2_source > 4) {
			Output::Warning("CortiCustomCrit: t2CritSourceStat:{}. This replaced the parameter 'critSource' from older versions.", t2_source);
			valid = false;
		}
	} else if (calc_type == 3) {
		t3_source = Config("t3CritSourceStat");
		if (t3_source <= 0 || t3_source > 2) {
			Output::Warning("CortiCustomCrit: t3CritSourceStat:{}. This replaced the parameter 'critSource' from older versions.", t3_source);
			valid = false;
		}
		t3_coefficient_hero = Config("t3RatingCoefficientHeroId");
		if (t3_coefficient_hero <= 0 || t3_coefficient_hero > static_cast<int>(lcf::Data::actors.size())) {
			Output::Warning("CortiCustomCrit: t3RatingCoefficientHeroId '{}' is not a hero of the database. See readme.", t3_coefficient_hero);
			valid = false;
		}
	} else {
		Output::Warning("CortiCustomCrit: CritCalcType is not validly set");
	}
	LoadMonsterCrit();
	hero_atk.assign(lcf::Data::actors.size() + 1, 0);
	ResetEffects();
	Log("CortiCustomCrit: Initializing.Completed");
}

DynRpg::CortiCustomCritPlugin::~CortiCustomCritPlugin() = default;

void DynRpg::CortiCustomCritPlugin::Log(std::string_view text) const {
	if (log_enabled) {
		Output::Debug("{}", text);
	}
}

int DynRpg::CortiCustomCritPlugin::Config(std::string_view key) const {
	return instance.GetConfigInt(section, key);
}

void DynRpg::CortiCustomCritPlugin::LoadSettings() {
	valid = true;
	const int actors = static_cast<int>(lcf::Data::actors.size());

	calc_type = Config("CritCalcType");
	if (calc_type <= 0 || calc_type > 3) {
		Output::Warning("CortiCustomCrit: CritCalcType:{} Try to add a configuration", calc_type);
		valid = false;
	}

	attack_bonus_type = Config("CritAttackBonusType");
	if (attack_bonus_type <= 0 || attack_bonus_type > 2) {
		Output::Warning("CortiCustomCrit: CritAttackBonusType:{}", attack_bonus_type);
		valid = false;
	}

	attack_bonus_value = Config("CritAttackBonusTypeValue");
	if (attack_bonus_value <= 0) {
		Output::Warning("CortiCustomCrit: CritAttackBonusTypeValue:{}", attack_bonus_value);
		valid = false;
	} else if (attack_bonus_type == 2 && attack_bonus_value > actors) {
		Output::Warning("CortiCustomCrit: CritAttackBonusTypeValue: HeroId {} Invalid", attack_bonus_value);
		valid = false;
	}

	calc_with_level = Config("CritCalcWithLevel");
	if (calc_with_level < 0 || calc_with_level > 2) {
		Output::Warning("CortiCustomCrit: CritCalcWithLevel:{}", calc_with_level);
		valid = false;
	}

	calc_with_level_variable = Config("CritCalcWithLevelVariable");
	if (calc_with_level == 2 && calc_with_level_variable <= 0) {
		Output::Warning("CortiCustomCrit: CritCalcWithLevelVariable:{} invalid", calc_with_level_variable);
		valid = false;
	}

	// 1: RPG_RT's critical hits of heroes are turned off (a memory patch), see HasStandardCriticalHits
	weapon_critical_type = Config("CritCalcWeaponCriticalType");
	if (weapon_critical_type == 0) {
		Output::Warning("CortiCustomCrit: CritCalcWeaponCriticalType:{} invalid.", weapon_critical_type);
		valid = false;
	}

	weapon_bonus_application = Config("CritCalcWeaponCritBonusApplication");
	if (weapon_bonus_application != 1 && weapon_bonus_application != 2) {
		Output::Warning("CortiCustomCrit: CritCalcWeaponCritBonusApplication:{} invalid.", weapon_bonus_application);
		valid = false;
	}

	skills_disabled = Config("CritCalcOnSkillsDisabled") > 0;
	heroes_disabled = Config("CritCalcOnHerosDisabled") > 0;

	if (int removed = Config("CritCalcOnHerosLevel"); removed != 0) {
		Output::Warning("CortiCustomCrit: CritCalcOnHerosLevel:{} Parameter was removed in 0.9", removed);
	}
	Log(fmt::format("CritCalcType:{} CritAttackBonusType:{} CritAttackBonusTypeValue:{} CritCalcWithLevel:{} CritCalcWeaponCriticalType:{} CritCalcWeaponCritBonusApplication:{} CritCalcOnSkillsDisabled:{} CritCalcOnHerosDisabled:{}",
		calc_type, attack_bonus_type, attack_bonus_value, calc_with_level, weapon_critical_type, weapon_bonus_application, skills_disabled, heroes_disabled));
}

void DynRpg::CortiCustomCritPlugin::LoadType1() {
	t1_default = Config("t1CritChanceDefault");
	if (t1_default < 0 || t1_default > 100) {
		Output::Warning("CortiCustomCrit: t1CritChanceDefault: {}% Ungünstig", t1_default);
		valid = false;
	}
	for (int i = 1;; ++i) {
		int hero = Config(fmt::format("t1HeroCritHero_{}_Id", i));
		if (hero <= 0) {
			break;
		}
		int fixed = Config(fmt::format("t1HeroCritHero_{}_FixCrit", i));
		int variable = Config(fmt::format("t1HeroCritHero_{}_Variable", i));
		// as the DLL: a missing FixCrit is 0, so a variable is only read with a negative FixCrit
		if (fixed >= 0) {
			t1_fixed[hero] = fixed;
			Log(fmt::format("Hero Id{} Constant Critchance: {}%", hero, fixed));
		} else if (variable > 0) {
			t1_variable[hero] = variable;
			Log(fmt::format("Hero Id{} Critchance from Variable ID: {}", hero, variable));
		}
	}
}

void DynRpg::CortiCustomCritPlugin::LoadSkills() {
	for (int i = 1;; ++i) {
		int skill = Config(fmt::format("SkillId{}", i));
		if (skill <= 0) {
			break;
		}
		if (int value = Config(fmt::format("SkillAddCon{}", i)); value > 0) {
			skill_add_constant[skill] = value;
		}
		if (int value = Config(fmt::format("SkillMulCon{}", i)); value >= 0) {
			skill_mul_constant[skill] = value;
		}
		if (int value = Config(fmt::format("SkillAddVar{}", i)); value > 0) {
			skill_add_variable[skill] = value;
		}
		if (int value = Config(fmt::format("SkillMulVar{}", i)); value > 0) {
			skill_mul_variable[skill] = value;
		}
		if (int value = Config(fmt::format("SkillDelay{}", i)); value > 0) {
			skill_delay[skill] = value;
		}
	}
}

void DynRpg::CortiCustomCritPlugin::LoadMonsterCrit() {
	monster_enabled = true;

	monster_source = Config("MonsterCritSourceStat");
	if (monster_source <= 0 || monster_source > 4) {
		Output::Warning("CortiCustomCrit: MonsterCritSourceStat: {} Invalid Value. Consult readme for valid settings.", monster_source);
		monster_enabled = false;
	}
	if (monster_source == 1 && calc_with_level == 1) {
		Output::Warning("CortiCustomCrit: MonsterCritSourceStat setting uses int as monster crit source while 'CritCalcWithLevel' uses int for the monster level.");
		monster_enabled = false;
	}

	// 2: RPG_RT's critical hits of monsters are turned off (a memory patch), see HasStandardCriticalHits
	monster_on_attack = Config("MonsterCritOnAttack");
	if (monster_on_attack <= 0 || monster_on_attack > 3) {
		Output::Warning("CortiCustomCrit: MonsterCritOnAttack: {} Invalid Value. Consult readme for valid settings.", monster_on_attack);
		monster_enabled = false;
	}

	monster_on_skills = Config("MonsterCritOnSkills");
	if (monster_on_skills <= 0 || monster_on_skills > 3) {
		Output::Warning("CortiCustomCrit: MonsterCritOnSkills: {} Invalid Value. Consult readme for valid settings.", monster_on_skills);
		monster_enabled = false;
	}

	monster_bonus = Config("MonsterCritAttackBonus");
	if (monster_bonus <= 0 || monster_bonus > 2) {
		Output::Warning("CortiCustomCrit: MonsterCritAttackBonus: {} Invalid Value. Consult readme for valid settings.", monster_bonus);
		monster_enabled = false;
	} else if (monster_bonus == 1) {
		monster_bonus_percentage = Config("MonsterCritAttackBonusPercentage");
		if (monster_bonus_percentage <= 0) {
			Output::Warning("CortiCustomCrit: MonsterCritAttackBonusPercentage: {} invalid value.", monster_bonus_percentage);
			monster_enabled = false;
		}
	} else {
		monster_bonus_hero = Config("MonsterCritAttackBonusHeroDbId");
		if (monster_bonus_hero < 1 || monster_bonus_hero > static_cast<int>(lcf::Data::actors.size())) {
			Output::Warning("CortiCustomCrit: MonsterCritAttackBonusHeroDbId: {} invalid value.", monster_bonus_hero);
			monster_enabled = false;
		}
		if (calc_with_level == 0) {
			Output::Warning("CortiCustomCrit: MonsterCritAttackBonus: {} Requires a monsterLevel.", monster_bonus);
		}
	}
	Log(fmt::format("MonsterCritSourceStat: {} MonsterCritOnAttack: {} MonsterCritOnSkills: {} MonsterCritAttackBonus: {}",
		monster_source, monster_on_attack, monster_on_skills, monster_bonus));
}

void DynRpg::CortiCustomCritPlugin::LoadVisualEffects() {
	bool enabled = true;
	int type = Config("VisualEffectType");
	if (type < 1 || type > 2) {
		Output::Warning("CortiCustomCrit: VisualEffectType: {} Invalid Value. Consult readme for valid settings.", type);
		enabled = false;
	}
	if (type == 2) {
		animation_id = Config("VisualEffectAnimationDatabaseId");
		if (animation_id < 2 || animation_id > static_cast<int>(lcf::Data::animations.size())) {
			Output::Warning("CortiCustomCrit: VisualEffectAnimationDatabaseId: {} Invalid Value. Consult readme for valid settings.", animation_id);
			enabled = false;
		}
	}
	delay_heroes = Config("DelayFramesHeroes");
	if (delay_heroes < 0) {
		Output::Warning("CortiCustomCrit: DelayFramesHeroes: {} Invalid Value. Consult readme for valid settings.", delay_heroes);
		enabled = false;
	}
	delay_monsters = Config("DelayFramesMonsters");
	if (delay_monsters < 0) {
		Output::Warning("CortiCustomCrit: DelayFramesMonsters: {} Invalid Value. Consult readme for valid settings.", delay_monsters);
		enabled = false;
	}
	splash_enabled = enabled && type == 1;
	animation_enabled = enabled && type == 2;
	Log(fmt::format("VisualEffectType: {} VisualEffectAnimationDatabaseId: {} DelayFramesHeroes: {} DelayFramesMonsters: {}",
		type, animation_id, delay_heroes, delay_monsters));
}

bool DynRpg::CortiCustomCritPlugin::HasStandardCriticalHits(const Game_Battler& source) const {
	if (source.GetType() == Game_Battler::Type_Ally) {
		return weapon_critical_type != 1;
	}
	return monster_on_attack != 2;
}

void DynRpg::CortiCustomCritPlugin::OnDoBattlerAction(const Game_BattleAlgorithm::AlgorithmBase& algo) {
	auto* source = algo.GetSource();
	if (!source) {
		return;
	}
	Log("----------------------------------------------------------------------");
	const Action action = ActionOf(algo);
	if (source->GetType() == Game_Battler::Type_Enemy) {
		if (monster_on_attack) {
			auto* enemy = static_cast<Game_Enemy*>(source);
			OnMonsterAction(*enemy, Main_Data::game_enemyparty->GetEnemyPositionInParty(enemy), action);
		}
	} else if (valid) {
		OnHeroAction(static_cast<Game_Actor&>(*source), action);
	}
}

bool DynRpg::CortiCustomCritPlugin::CanHeroCrit(const Action& action) const {
	if (action.skill_id) {
		if (!CanSkillDamageOrHeal(action.skill_id)) {
			return false;
		}
		if (skills_disabled && !skill_mul_constant.count(action.skill_id)) {
			return false;
		}
		if (heroes_disabled && (action.target == Action::AllActors || action.target == Action::Actor)) {
			return false;
		}
		return true;
	}
	return action.attack && (weapon_critical_type == 1 || weapon_critical_type == 2);
}

bool DynRpg::CortiCustomCritPlugin::CanMonsterCrit(const Action& action) const {
	if (action.skill_id) {
		if (!CanSkillDamageOrHeal(action.skill_id)) {
			return false;
		}
		if (monster_on_skills == 1 && !skill_mul_constant.count(action.skill_id)) {
			return false;
		}
		if (monster_on_skills == 2 && (action.target == Action::AllMonsters || action.target == Action::Monster)) {
			return false;
		}
		return true;
	}
	return action.attack && (monster_on_attack == 2 || monster_on_attack == 3);
}

int DynRpg::CortiCustomCritPlugin::ApplySkillspecific(int chance, int skill_id) const {
	if (auto it = skill_mul_constant.find(skill_id); it != skill_mul_constant.end()) {
		chance = chance * it->second / 100;
	} else if (skills_disabled) {
		chance = 0;
	}
	if (auto it = skill_mul_variable.find(skill_id); it != skill_mul_variable.end()) {
		chance = chance * Var(it->second) / 100;
	}
	if (auto it = skill_add_variable.find(skill_id); it != skill_add_variable.end()) {
		chance += Var(it->second);
	}
	if (auto it = skill_add_constant.find(skill_id); it != skill_add_constant.end()) {
		chance += it->second;
	}
	Log(fmt::format("SkillId: {} Critchance.AfterSkillModifier: {}%", skill_id, chance));
	return chance;
}

int DynRpg::CortiCustomCritPlugin::ApplyWeaponCritBonus(const Game_Actor& actor, const Action& action) const {
	if (action.skill_id && weapon_bonus_application == 2) {
		return 0;
	}
	int bonus = 0;
	const int weapon1 = actor.GetWeaponId();
	auto* item1 = Item(weapon1);
	if (item1 && item1->type == lcf::rpg::Item::Type_weapon && item1->critical_hit > 0) {
		bonus += item1->critical_hit;
	}
	// the second hand counts when the first one holds something
	auto* item2 = weapon1 > 0 ? Item(actor.GetShieldId()) : nullptr;
	if (item2 && item2->type == lcf::rpg::Item::Type_weapon && item2->critical_hit > 0) {
		bonus += item2->critical_hit;
	}
	return bonus;
}

int DynRpg::CortiCustomCritPlugin::ApplyAntiCrit(int chance, const Action& action) const {
	if (action.target == Action::AllActors) {
		// as the DLL: the living ones are counted on top of the party's size
		auto actors = Main_Data::game_party->GetActors();
		int living = static_cast<int>(actors.size());
		int unprotected = 0;
		for (auto* actor : actors) {
			if (actor->GetHp() > 0) {
				++living;
				if (!HeroPreventsCrit(*actor)) {
					++unprotected;
				}
			}
		}
		if (living > 0) {
			chance = chance * unprotected / living;
		}
	} else if (action.target == Action::Actor) {
		auto* actor = PartyMember(action.target_index);
		if (actor && actor->GetHp() > 0 && HeroPreventsCrit(*actor)) {
			return 0;
		}
	}
	return chance;
}

int DynRpg::CortiCustomCritPlugin::MonsterLevel(int index) const {
	if (calc_with_level == 1) {
		// the intelligence of the monster, at most 99
		auto* enemy = Monster(index);
		return enemy ? std::min(enemy->GetSpi(), 99) : -1;
	} else if (calc_with_level == 2) {
		int level = Var(calc_with_level_variable);
		return level < 1 || level > 99 ? 1 : level;
	}
	return 1;
}

int DynRpg::CortiCustomCritPlugin::HeroTargetLevel(const Game_Actor& actor, const Action& action) const {
	int level = actor.GetLevel();
	if (calc_with_level == 1) {
		if (action.target == Action::Monster) {
			auto* enemy = Monster(action.target_index);
			if (enemy) {
				level = std::min(enemy->GetSpi(), 99);
			}
		} else if (action.target == Action::AllMonsters) {
			// the average intelligence of the monsters shown and alive, leaving out those above 99
			int sum = 0;
			int count = 0;
			for (int i = 0; i < 8; ++i) {
				auto* enemy = Monster(i);
				if (enemy && enemy->GetHp() > 0 && !enemy->IsHidden() && enemy->GetSpi() <= 99) {
					sum += enemy->GetSpi();
					++count;
				}
			}
			if (count > 0) {
				level = sum / count;
			}
		}
	} else if (calc_with_level == 2) {
		level = Var(calc_with_level_variable);
	}
	return level;
}

int DynRpg::CortiCustomCritPlugin::HeroCritAdaptToLevel(int chance, const Game_Actor& actor, const Action& action) const {
	if (calc_with_level != 1 && calc_with_level != 2) {
		return chance;
	}
	const int difference = actor.GetLevel() - HeroTargetLevel(actor, action);
	if (difference > 0) {
		chance += chance * (difference * 10) / 100;
	} else if (difference < 0) {
		chance = std::max(0, chance + chance * (difference * 5) / 100);
	}
	return chance;
}

int DynRpg::CortiCustomCritPlugin::MonsterCritAdaptToLevel(int chance, int index, const Action& action) const {
	if (calc_with_level != 1 && calc_with_level != 2) {
		return chance;
	}
	const int level = MonsterLevel(index);
	int target_level = level;
	if (action.target == Action::Actor) {
		auto* actor = PartyMember(action.target_index);
		target_level = actor ? actor->GetLevel() : -1;
	} else if (action.target == Action::AllActors) {
		auto actors = Main_Data::game_party->GetActors();
		int sum = 0;
		for (auto* actor : actors) {
			sum += actor->GetLevel();
		}
		target_level = actors.empty() ? -1 : sum / static_cast<int>(actors.size());
	}
	const int difference = level - target_level;
	if (difference > 0) {
		chance += chance * (difference * 10) / 100;
	} else if (difference < 0) {
		chance = std::max(0, chance + chance * (difference * 5) / 100);
	}
	return chance;
}

int DynRpg::CortiCustomCritPlugin::HeroChance(Game_Actor& actor, const Action& action) const {
	const int id = actor.GetId();
	const auto* db = lcf::ReaderUtil::GetElement(lcf::Data::actors, id);
	int chance = 0;
	if (calc_type == 1) {
		// a fixed chance, or one in a variable
		chance = t1_default;
		if (auto it = t1_variable.find(id); it != t1_variable.end()) {
			chance = Var(it->second);
		} else if (auto it = t1_fixed.find(id); it != t1_fixed.end()) {
			chance = it->second;
		}
		chance = HeroCritAdaptToLevel(chance, actor, action);
	} else if (calc_type == 2) {
		// a stat as chance
		if (t2_source == 1) {
			chance = actor.GetSpi();
		} else if (t2_source == 2) {
			chance = actor.GetAgi();
		} else if (t2_source == 3 && db && db->critical_hit && db->critical_hit_chance > 0) {
			chance = 100 / db->critical_hit_chance;
		} else if (t2_source == 4 && db && db->critical_hit) {
			chance = db->critical_hit_chance;
		}
		chance = HeroCritAdaptToLevel(chance, actor, action);
	} else if (calc_type == 3) {
		// a stat as rating: chance = rating * 100 / max HP curve of a hero at the target's level
		int rating = t3_source == 1 ? actor.GetAgi() : actor.GetSpi();
		auto* coefficient_hero = lcf::ReaderUtil::GetElement(lcf::Data::actors, t3_coefficient_hero);
		int coefficient = coefficient_hero ? Curve(coefficient_hero->parameters.maxhp, HeroTargetLevel(actor, action)) : 0;
		chance = coefficient != 0 ? rating * 100 / coefficient : 0;
	}
	Log(fmt::format("HeroId {} Critchance.Hero: {}", id, chance));
	return chance;
}

int DynRpg::CortiCustomCritPlugin::MonsterChance(Game_Enemy& enemy, int index, const Action& action) const {
	const auto& db = enemy.GetDbEnemy();
	int chance = 0;
	if (monster_source == 1) {
		chance = enemy.GetSpi();
	} else if (monster_source == 2) {
		chance = enemy.GetAgi();
	} else if (monster_source == 3 && db.critical_hit && db.critical_hit_chance > 0) {
		chance = 100 / db.critical_hit_chance;
	} else if (monster_source == 4 && db.critical_hit) {
		chance = db.critical_hit_chance;
	}
	chance = MonsterCritAdaptToLevel(chance, index, action);
	Log(fmt::format("Monster {} Monster.Critchance: {}", index + 1, chance));
	return chance;
}

int DynRpg::CortiCustomCritPlugin::Roll(std::array<int, 100>& deck, int& next) const {
	// a shuffled deck of 1 to 100, shuffled again once it was drawn
	if (next >= static_cast<int>(deck.size())) {
		next = 0;
		for (int i = 0; i < static_cast<int>(deck.size()); ++i) {
			deck[i] = i + 1;
		}
		for (int i = 0; i < static_cast<int>(deck.size()); ++i) {
			std::swap(deck[i], deck[Rand::GetRandomNumber(0, static_cast<int>(deck.size()) - 1)]);
		}
	}
	return deck[next++];
}

void DynRpg::CortiCustomCritPlugin::OnHeroAction(Game_Actor& actor, const Action& action) {
	const int id = actor.GetId();
	if (!CanHeroCrit(action)) {
		Log(fmt::format("HeroId {} DoBattlerAction - Action can not crit", id));
		ResetHeroBonus(id);
		return;
	}
	int chance = HeroChance(actor, action);
	chance += ApplyWeaponCritBonus(actor, action);
	if (action.skill_id) {
		chance = ApplySkillspecific(chance, action.skill_id);
	}
	const int roll = Roll(deck_heroes, deck_heroes_next);
	Log(fmt::format("HeroId {} Critchance: {} Roll: {}", id, chance, roll));
	if (chance > 0 && roll <= chance) {
		Log(fmt::format("HeroId {} Action CRITICAL", id));
		AddHeroBonus(actor);
		AddEffects(action, delay_heroes);
	} else {
		ResetHeroBonus(id);
	}
}

void DynRpg::CortiCustomCritPlugin::OnMonsterAction(Game_Enemy& enemy, int index, const Action& action) {
	if (!CanMonsterCrit(action)) {
		Log(fmt::format("Monster {} DoBattlerAction - Action can not crit", index + 1));
		ResetMonsterBonus(index);
		return;
	}
	int chance = MonsterChance(enemy, index, action);
	if (action.skill_id) {
		chance = ApplySkillspecific(chance, action.skill_id);
	}
	chance = ApplyAntiCrit(chance, action);
	const int roll = Roll(deck_monsters, deck_monsters_next);
	Log(fmt::format("Monster {} Critchance: {} Roll: {}", index + 1, chance, roll));
	if (chance > 0 && roll <= chance) {
		Log(fmt::format("Monster {} Action CRITICAL", index + 1));
		AddMonsterBonus(index);
		AddEffects(action, delay_monsters);
	} else {
		ResetMonsterBonus(index);
	}
}

void DynRpg::CortiCustomCritPlugin::AddHeroBonus(Game_Actor& actor) {
	const int id = actor.GetId();
	if (!valid || id <= 0 || id >= static_cast<int>(hero_atk.size()) || hero_atk[id - 1] != 0) {
		return;
	}
	int bonus = 0;
	if (attack_bonus_type == 1) {
		bonus = actor.GetAtk() * attack_bonus_value / 100;
	} else if (attack_bonus_type == 2) {
		auto* curve_hero = lcf::ReaderUtil::GetElement(lcf::Data::actors, attack_bonus_value);
		bonus = curve_hero ? Curve(curve_hero->parameters.attack, actor.GetLevel()) : 0;
	}
	hero_atk[id - 1] = bonus;
	// RPG::Actor::attackDiff
	actor.SetBaseAtk(actor.GetBaseAtk() + bonus);
	Log(fmt::format("Hero {} Add Critbonus: ATK Bonus {}", id, bonus));
}

void DynRpg::CortiCustomCritPlugin::ResetHeroBonus(int actor_id) {
	if (actor_id <= 0 || actor_id >= static_cast<int>(hero_atk.size()) || hero_atk[actor_id - 1] <= 0) {
		return;
	}
	if (auto* actor = Main_Data::game_actors->GetActor(actor_id)) {
		actor->SetBaseAtk(actor->GetBaseAtk() - hero_atk[actor_id - 1]);
	}
	hero_atk[actor_id - 1] = 0;
	Log(fmt::format("Hero {} Reset ATK Bonus", actor_id));
}

void DynRpg::CortiCustomCritPlugin::AddMonsterBonus(int index) {
	if (!monster_enabled || index < 0 || index >= static_cast<int>(monster_atk.size())) {
		return;
	}
	auto* enemy = Monster(index);
	if (!enemy || monster_atk[index] != 0) {
		return;
	}
	int bonus = 0;
	if (monster_bonus == 1) {
		bonus = enemy->GetDbEnemy().attack * monster_bonus_percentage / 100;
	} else if (monster_bonus == 2) {
		auto* curve_hero = lcf::ReaderUtil::GetElement(lcf::Data::actors, monster_bonus_hero);
		bonus = curve_hero ? Curve(curve_hero->parameters.attack, MonsterLevel(index)) : 0;
	}
	monster_atk[index] = bonus;
	// RPG::Monster::attackDiff
	enemy->SetAtkModifier(enemy->GetAtkModifier() + bonus);
	Log(fmt::format("MonsterIndex {} Add Critbonus: ATK Bonus {}", index, bonus));
}

void DynRpg::CortiCustomCritPlugin::ResetMonsterBonus(int index) {
	if (index < 0 || index >= static_cast<int>(monster_atk.size()) || monster_atk[index] <= 0) {
		return;
	}
	if (auto* enemy = Monster(index)) {
		enemy->SetAtkModifier(enemy->GetAtkModifier() - monster_atk[index]);
	}
	monster_atk[index] = 0;
	Log(fmt::format("MonsterIndex {} Reset ATK Bonus", index));
}

void DynRpg::CortiCustomCritPlugin::OnSceneChange() {
	// entering or leaving a battle: the bonuses are removed
	for (int id = 1; id < static_cast<int>(hero_atk.size()); ++id) {
		ResetHeroBonus(id);
	}
	for (int i = 0; i < static_cast<int>(monster_atk.size()); ++i) {
		ResetMonsterBonus(i);
	}
	ResetEffects();
}

void DynRpg::CortiCustomCritPlugin::OnResetGame() {
	// the game was reset: only the flags, the actors are new
	std::fill(hero_atk.begin(), hero_atk.end(), 0);
	monster_atk.fill(0);
	ResetEffects();
}

void DynRpg::CortiCustomCritPlugin::OnNewGame() {
	OnResetGame();
	// onInitFinished sets it before the title: a new game keeps it, a loaded game has its own
	Main_Data::game_system->SetAtbMode(lcf::rpg::SaveSystem::AtbMode_atb_wait);
}

void DynRpg::CortiCustomCritPlugin::OnLoadGame() {
	OnResetGame();
}

void DynRpg::CortiCustomCritPlugin::OnFrame(Scene::SceneType scene) {
	if (scene != current_scene) {
		if (scene == Scene::Battle && current_scene != Scene::Debug) {
			OnSceneChange();
		} else if (scene != Scene::Debug && current_scene == Scene::Battle) {
			OnSceneChange();
		} else if (scene == Scene::Title) {
			OnResetGame();
		}
		current_scene = scene;
	}
	if (scene != Scene::Battle) {
		return;
	}

	for (auto it = effects.begin(); it != effects.end();) {
		if (it->delay > 0) {
			--it->delay;
			++it;
			continue;
		}
		if (!it->animation) {
			auto* anim = lcf::ReaderUtil::GetElement(lcf::Data::animations, animation_id);
			if (!anim) {
				it = effects.erase(it);
				continue;
			}
			it->animation = std::make_unique<BattleAnimationBattle>(*anim, it->targets);
		}
		it->animation->Update();
		if (it->animation->IsDone()) {
			it = effects.erase(it);
		} else {
			++it;
		}
	}

	for (auto& frame : hero_splash) {
		frame = std::min(frame + 1, splash_frames);
	}
	for (auto& frame : monster_splash) {
		frame = std::min(frame + 1, splash_frames);
	}
}

void DynRpg::CortiCustomCritPlugin::AddEffects(const Action& action, int delay) {
	if (action.skill_id) {
		if (auto it = skill_delay.find(action.skill_id); it != skill_delay.end()) {
			delay = it->second;
		}
	}

	// the targets of the action, by their place in their party
	std::vector<std::pair<bool, int>> places; // (monster, index)
	if (action.target == Action::Monster) {
		places.emplace_back(true, action.target_index);
	} else if (action.target == Action::AllMonsters) {
		for (int i = 0; i < 8; ++i) {
			auto* enemy = Monster(i);
			if (enemy && !enemy->IsHidden() && enemy->GetHp() > 0) {
				places.emplace_back(true, i);
			}
		}
	} else if (action.target == Action::AllActors) {
		for (int i = 0; i < 4; ++i) {
			auto* actor = PartyMember(i);
			if (actor && actor->GetHp() > 0) {
				places.emplace_back(false, i);
			}
		}
	} else if (action.target == Action::Actor) {
		places.emplace_back(false, action.target_index);
	}

	if (splash_enabled) {
		// as the DLL: a skill's delay starts one frame earlier
		const int start = action.skill_id && skill_delay.count(action.skill_id) ? -delay : 1 - delay;
		for (auto& [monster, index] : places) {
			if (monster && index >= 0 && index < static_cast<int>(monster_splash.size())) {
				monster_splash[index] = start;
			} else if (!monster && index >= 0 && index < static_cast<int>(hero_splash.size())) {
				hero_splash[index] = start;
			}
		}
	} else if (animation_enabled) {
		// one animation for all the targets: its sounds and screen effects once
		Effect effect;
		for (auto& [monster, index] : places) {
			Game_Battler* battler = monster ? static_cast<Game_Battler*>(Monster(index)) : PartyMember(index);
			if (battler) {
				effect.targets.push_back(battler);
			}
		}
		if (!effect.targets.empty()) {
			effect.delay = delay;
			effects.push_back(std::move(effect));
		}
	}
}

void DynRpg::CortiCustomCritPlugin::ResetEffects() {
	effects.clear();
	hero_splash.fill(splash_frames);
	monster_splash.fill(splash_frames);
}

void DynRpg::CortiCustomCritPlugin::OnDrawScene(Scene::SceneType scene, Bitmap& dst) {
	if (!splash_enabled || scene != Scene::Battle) {
		return;
	}
	if (!splash_tried) {
		auto* request = AsyncHandler::RequestFile("DynRessource", "Splash");
		request->SetGraphicFile(true);
		request->Start();
		if (!request->IsReady()) {
			return;
		}
		splash_tried = true;
		if (auto is = FileFinder::OpenImage("DynRessource", "Splash")) {
			splash = Bitmap::Create(std::move(is), true);
		} else {
			Output::Warning("CortiCustomCrit: DynRessource\\Splash.png: File does not exist.");
		}
	}
	if (!splash) {
		return;
	}

	// 20 frames nothing, 20 fading in, 10 shown, 20 fading out; its lower right corner on the battler
	auto draw = [&](int frame, const Game_Battler& battler, int offset) {
		int opacity = 0;
		if (frame >= 20 && frame < 40) {
			opacity = 255 * (frame - 20) / 20;
		} else if (frame >= 40 && frame < 50) {
			opacity = 255;
		} else if (frame >= 50 && frame < splash_frames) {
			opacity = 255 - 255 * (frame - 50) / 20;
		}
		if (opacity > 0) {
			const int x = battler.GetDisplayX() + offset - splash->GetWidth();
			const int y = battler.GetDisplayY() + offset - splash->GetHeight();
			dst.Blit(x, y, *splash, splash->GetRect(), Opacity(opacity));
		}
	};
	for (int i = 0; i < static_cast<int>(hero_splash.size()); ++i) {
		if (auto* actor = PartyMember(i)) {
			draw(hero_splash[i], *actor, 0);
		}
	}
	for (int i = 0; i < static_cast<int>(monster_splash.size()); ++i) {
		if (auto* enemy = Monster(i)) {
			draw(monster_splash[i], *enemy, 16);
		}
	}
}

bool DynRpg::CortiCustomCritPlugin::Invoke(std::string_view func, dyn_arg_list args, bool&, Game_Interpreter*) {
	const int count = static_cast<int>(args.size());
	auto number = [&](int i) {
		return count > i ? std::atoi(args[i].c_str()) : 0;
	};

	if (func == "ccrit_getstatus" || func == "ccrit_resetstatus") {
		const bool get = func == "ccrit_getstatus";
		if (count != (get ? 2 : 1)) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid number of parameters {}", func, count);
			return true;
		}
		// as the DLL: the last hero of the database is refused
		const int hero = number(0);
		if (hero < 1 || hero >= static_cast<int>(lcf::Data::actors.size())) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid hero databaseId {}", func, hero);
			return true;
		}
		if (!get) {
			ResetHeroBonus(hero);
			return true;
		}
		const int switch_id = number(1);
		if (switch_id < 1) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid switchId {}", func, switch_id);
			return true;
		}
		Main_Data::game_switches->Set(switch_id, hero < static_cast<int>(hero_atk.size()) && hero_atk[hero - 1] > 0);
		return true;
	}

	if (func == "ccrit_monster_getstatus" || func == "ccrit_monster_resetstatus") {
		// as the DLL: resetstatus needs 2 parameters as well
		if (count != 2) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid number of parameters {}", func, count);
			return true;
		}
		const int monster = number(0);
		if (monster < 1 || monster > 8) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid monster Group Id {}", func, monster);
			return true;
		}
		if (func == "ccrit_monster_resetstatus") {
			ResetMonsterBonus(monster - 1);
			return true;
		}
		const int switch_id = number(1);
		if (switch_id < 1) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid switchId {}", func, switch_id);
			return true;
		}
		Main_Data::game_switches->Set(switch_id, monster_atk[monster - 1] > 0);
		return true;
	}

	std::map<int, int>* skill_setting = nullptr;
	if (func == "ccrit_skill_addcon") {
		skill_setting = &skill_add_constant;
	} else if (func == "ccrit_skill_addvar") {
		skill_setting = &skill_add_variable;
	} else if (func == "ccrit_skill_mulcon") {
		skill_setting = &skill_mul_constant;
	} else if (func == "ccrit_skill_mulvar") {
		skill_setting = &skill_mul_variable;
	} else if (func == "ccrit_skill_delay") {
		skill_setting = &skill_delay;
	}
	if (skill_setting) {
		if (count != 2) {
			Output::Warning("CortiCustomCrit: CommentCommand: {}: invalid number of parameters {}", func, count);
			return true;
		}
		(*skill_setting)[number(0)] = number(1);
		Log(fmt::format("CommentCommand: '{}' skill: '{}' Value: '{}'", func, number(0), number(1)));
		return true;
	}
	return false;
}
