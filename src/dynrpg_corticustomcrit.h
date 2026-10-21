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

#ifndef EP_DYNRPG_CORTICUSTOMCRIT_H
#define EP_DYNRPG_CORTICUSTOMCRIT_H

#include <array>
#include <map>
#include <memory>
#include <vector>
#include "game_dynrpg.h"
#include "memory_management.h"

class BattleAnimationBattle;
class Game_Actor;
class Game_Enemy;

namespace DynRpg {
	/**
	 * CortiCustomCrit V0.9.2 by Corti (CortiXCustomCrit.dll,
	 * https://github.com/CortiWins/CortiPluginCollection): critical hits of its own.
	 *
	 * When a battler starts an action that may crit (a normal attack, or a skill that
	 * changes HP), it rolls its chance; a critical action raises the battler's attack
	 * for that action, and shows an effect on the targets. The bonus is removed at the
	 * battler's next action that doesn't crit, at the end of the battle, or by comment.
	 * Read from DynRPG.ini [CortiCustomCrit]:
	 *
	 *   CritCalcType                 heroes' chance: 1 fixed (t1*), 2 a stat (t2CritSourceStat),
	 *                                3 a stat as rating (t3CritSourceStat, t3RatingCoefficientHeroId)
	 *   t1CritChanceDefault          type 1: % of the heroes not listed
	 *   t1HeroCritHero_N_Id          type 1: hero N's database ID, its % in
	 *   t1HeroCritHero_N_FixCrit     or t1HeroCritHero_N_Variable (N = 1, 2... until an Id is missing)
	 *   CritCalcWeaponCriticalType   1: weapon attacks crit with the plugin, RPG_RT's critical hits
	 *                                of heroes are off; 2: both; 3: weapon attacks don't
	 *   CritCalcWeaponCritBonusApplication  the weapons' critical bonus: 1 attacks and skills, 2 attacks
	 *   CritCalcOnSkillsDisabled     1: only the skills with SkillMulConN crit
	 *   CritCalcOnHerosDisabled      1: the heroes' skills on allies don't crit
	 *   CritAttackBonusType          1: +CritAttackBonusTypeValue % of the attack, 2: + the attack
	 *                                curve of hero CritAttackBonusTypeValue at the hero's level
	 *   CritCalcWithLevel            0, 1 the enemies' intelligence is their level, 2 their level is
	 *                                in variable CritCalcWithLevelVariable: +10% per level above
	 *                                the target, -5% per level below
	 *   MonsterCritSourceStat        monsters' chance: 1 int, 2 agi, 3 1 in N of the database, 4 its N %
	 *   MonsterCritOnAttack          1 no plugin crits; 2 plugin crits, RPG_RT's critical hits of
	 *                                monsters are off; 3 both
	 *   MonsterCritOnSkills          1 none (but SkillMulConN), 2 attacks, 3 attacks and healing
	 *   MonsterCritAttackBonus       1: +MonsterCritAttackBonusPercentage % of the database attack,
	 *                                2: + the attack curve of hero MonsterCritAttackBonusHeroDbId
	 *   SkillIdN, SkillAddConN, SkillAddVarN, SkillMulConN, SkillMulVarN, SkillDelayN
	 *                                a skill's chance: (chance * MulCon / 100 * v[MulVar] / 100)
	 *                                + AddCon + v[AddVar]; SkillDelay delays its effect (frames)
	 *   VisualEffectType             1: DynRessource/Splash.png fades in and out over the target,
	 *                                2: battle animation VisualEffectAnimationDatabaseId
	 *   DelayFramesHeroes, DelayFramesMonsters  frames before the effect
	 *
	 * [CortiLog] LogToConsole=1 or LogToFile=1 writes what it calculates to the Player's log.
	 * The chances are rolled from a shuffled deck of 1 to 100 (one for the heroes, one for the
	 * monsters). After loading, it sets the ATB mode to Wait. Comments:
	 *
	 *   @ccrit_getstatus heroId, switchId          the switch is on while the hero has the bonus
	 *   @ccrit_resetstatus heroId                  removes it
	 *   @ccrit_monster_getstatus monster, switchId the same for the monster in place 1 to 8
	 *   @ccrit_monster_resetstatus monster, x      (the DLL needs a second number)
	 *   @ccrit_skill_addcon skillId, value         sets SkillAddCon of the skill
	 *   @ccrit_skill_addvar, _mulcon, _mulvar, _delay  the other settings of a skill
	 */
	class CortiCustomCritPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "CortiXCustomCrit";
		/** The section doesn't depend on the name of the DLL */
		static constexpr const char* section = "CortiCustomCrit";
		/** Its log settings */
		static constexpr const char* log_section = "CortiLog";

		explicit CortiCustomCritPlugin(Game_DynRpg& instance);
		~CortiCustomCritPlugin() override;

		bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) override;
		void OnFrame(Scene::SceneType scene) override;
		void OnNewGame() override;
		void OnLoadGame() override;
		void OnDrawScene(Scene::SceneType scene, Bitmap& dst) override;
		void OnDoBattlerAction(const Game_BattleAlgorithm::AlgorithmBase& action) override;
		bool HasStandardCriticalHits(const Game_Battler& source) const override;

		/** What the DLL reads of the action a battler starts (RPG::Action) */
		struct Action {
			enum Target { None, Actor, AllActors, Monster, AllMonsters };
			/** a normal or double attack (AK_BASIC, BA_ATTACK or BA_DOUBLE_ATTACK) */
			bool attack = false;
			/** the skill used (AK_SKILL), 0 for another action */
			int skill_id = 0;
			Target target = None;
			/** place of the target in its party, for Actor and Monster */
			int target_index = 0;
		};

	private:
		void Log(std::string_view text) const;
		void LoadSettings();
		void LoadType1();
		void LoadSkills();
		void LoadMonsterCrit();
		void LoadVisualEffects();
		int Config(std::string_view key) const;

		void OnHeroAction(Game_Actor& actor, const Action& action);
		void OnMonsterAction(Game_Enemy& enemy, int index, const Action& action);
		bool CanHeroCrit(const Action& action) const;
		bool CanMonsterCrit(const Action& action) const;
		int HeroChance(Game_Actor& actor, const Action& action) const;
		int MonsterChance(Game_Enemy& enemy, int index, const Action& action) const;
		int ApplySkillspecific(int chance, int skill_id) const;
		int ApplyWeaponCritBonus(const Game_Actor& actor, const Action& action) const;
		int ApplyAntiCrit(int chance, const Action& action) const;
		int HeroTargetLevel(const Game_Actor& actor, const Action& action) const;
		int MonsterLevel(int index) const;
		int HeroCritAdaptToLevel(int chance, const Game_Actor& actor, const Action& action) const;
		int MonsterCritAdaptToLevel(int chance, int index, const Action& action) const;
		int Roll(std::array<int, 100>& deck, int& next) const;

		void AddHeroBonus(Game_Actor& actor);
		void ResetHeroBonus(int actor_id);
		void AddMonsterBonus(int index);
		void ResetMonsterBonus(int index);
		void OnSceneChange();
		void OnResetGame();

		void AddEffects(const Action& action, int delay);
		void ResetEffects();

		bool log_enabled = false;

		// [CortiCustomCrit]
		bool valid = false;
		int calc_type = 0;
		int attack_bonus_type = 0;
		int attack_bonus_value = 0;
		int calc_with_level = 0;
		int calc_with_level_variable = 0;
		int weapon_critical_type = 0;
		int weapon_bonus_application = 0;
		bool skills_disabled = false;
		bool heroes_disabled = false;
		int t1_default = 0;
		std::map<int, int> t1_fixed;
		std::map<int, int> t1_variable;
		int t2_source = 0;
		int t3_source = 0;
		int t3_coefficient_hero = 0;
		std::map<int, int> skill_add_constant;
		std::map<int, int> skill_mul_constant;
		std::map<int, int> skill_add_variable;
		std::map<int, int> skill_mul_variable;
		std::map<int, int> skill_delay;
		bool monster_enabled = false;
		int monster_source = 0;
		int monster_on_attack = 0;
		int monster_on_skills = 0;
		int monster_bonus = 0;
		int monster_bonus_percentage = 0;
		int monster_bonus_hero = 0;
		bool splash_enabled = false;
		bool animation_enabled = false;
		int animation_id = 0;
		int delay_heroes = 0;
		int delay_monsters = 0;

		// state
		std::vector<int> hero_atk; // by database ID - 1
		std::array<int, 8> monster_atk = {};
		mutable std::array<int, 100> deck_heroes = {};
		mutable int deck_heroes_next = 100;
		mutable std::array<int, 100> deck_monsters = {};
		mutable int deck_monsters_next = 100;
		Scene::SceneType current_scene = Scene::Null;

		// VisualEffectType 2: the animations waiting for their delay, then playing
		struct Effect {
			std::vector<Game_Battler*> targets;
			int delay = 0;
			std::unique_ptr<BattleAnimationBattle> animation;
		};
		std::vector<Effect> effects;

		// VisualEffectType 1: frame of the splash of each place (70: none)
		static constexpr int splash_frames = 70;
		std::array<int, 4> hero_splash;
		std::array<int, 8> monster_splash;
		BitmapRef splash;
		bool splash_tried = false;
	};
}

#endif
