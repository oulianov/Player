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

#ifndef EP_GAME_DYNRPG_H
#define EP_GAME_DYNRPG_H

#include <cstdint>
#include <locale>
#include <memory>
#include <optional>
#include <vector>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include "output.h"
#include "scene.h"
#include "utils.h"
#include <lcf/rpg/eventcommand.h>

// Headers
namespace lcf {
	class INIReader;
}
namespace lcf::rpg {
	class SaveEventExecFrame;
}

class Bitmap;
class Drawable;
class DynRpgPlugin;
class Game_Battler;
class Game_Interpreter;
class Rect;
namespace Game_BattleAlgorithm {
	class AlgorithmBase;
}

using dyn_arg_list = const Span<std::string>;
using dynfunc = bool(*)(dyn_arg_list);

/** Contains helper functions for parsing */
namespace DynRpg {
	class EasyRpgPlugin;

	std::string ParseVarArg(std::string_view func_name, dyn_arg_list args, int index, bool& parse_okay);
	std::string ParseCommand(std::string command, std::vector<std::string>& params);

	namespace detail {
		template <typename T>
		inline bool parse_arg(std::string_view, dyn_arg_list, const int, T&, bool&) {
			static_assert(sizeof(T) == -1, "Only parsing int, float and std::string supported");
			return false;
		}

		// FIXME: Extracting floats that are followed by chars behaviour varies depending on the C++ library
		// see https://bugs.llvm.org/show_bug.cgi?id=17782
		template <>
		inline bool parse_arg(std::string_view func_name, dyn_arg_list args, const int i, float& value, bool& parse_okay) {
			if (!parse_okay) return false;
			value = 0.0;
			if (args[i].empty()) {
				parse_okay = true;
				return parse_okay;
			}
			std::istringstream iss(args[i]);
			iss.imbue(std::locale::classic());
			iss >> value;
			parse_okay = !iss.fail();
			if (!parse_okay) {
				Output::Warning("{}: Arg {} ({}) is not numeric", func_name, i, args[i]);
				parse_okay = false;
			}
			return parse_okay;
		}

		template <>
		inline bool parse_arg(std::string_view func_name, dyn_arg_list args, const int i, int& value, bool& parse_okay) {
			if (!parse_okay) return false;
			value = 0;
			if (args[i].empty()) {
				parse_okay = true;
				return parse_okay;
			}
			std::istringstream iss(args[i]);
			iss >> value;
			parse_okay = !iss.fail();
			if (!parse_okay) {
				Output::Warning("{}: Arg {} ({}) is not an integer", func_name, i, args[i]);
				parse_okay = false;
			}
			return parse_okay;
		}

		template <>
		inline bool parse_arg(std::string_view, dyn_arg_list args, const int i, std::string& value, bool& parse_okay) {
			if (!parse_okay) return false;
			value = args[i];
			parse_okay = true;
			return parse_okay;
		}

		template <typename Tuple, std::size_t... I>
		inline void parse_args(std::string_view func_name, dyn_arg_list in, Tuple& value, bool& parse_okay, std::index_sequence<I...>) {
			(void)std::initializer_list<bool>{parse_arg(func_name, in, I, std::get<I>(value), parse_okay)...};
		}
	}


	template <typename... Targs>
	std::tuple<Targs...> ParseArgs(std::string_view func_name, dyn_arg_list args, bool* parse_okay = nullptr) {
		std::tuple<Targs...> t;
		if (args.size() < sizeof...(Targs)) {
			if (parse_okay)
				*parse_okay = false;
			Output::Warning("{}: Got {} args (needs {} or more)", func_name, args.size(), sizeof...(Targs));
			return t;
		}
		bool okay = true;
		detail::parse_args(func_name, args, t, okay, std::make_index_sequence<sizeof...(Targs)>{});
		if (parse_okay)
			*parse_okay = okay;
		return t;
	}
}

/**
 * Implements DynRPG Patch (kinda, plugins cannot be executed directly and must be reimplemented)
 */
class Game_DynRpg {
public:
	bool Invoke(std::string_view command, Game_Interpreter* interpreter = nullptr);
	void Update();

	/**
	 * Loads the plugins of the game (DynRPG loads them at start-up), when the
	 * game objects are made: their hooks then see every write of a variable.
	 * The calls that need the plugins load them too.
	 */
	void LoadPlugins() { InitPlugins(); }
	void Load(int slot);
	void Save(int slot);

	/**
	 * DynRPG onEventCommand: called before each event command is executed.
	 * Plugins (e.g. DynParams) may rewrite the command, like DynRPG plugins
	 * rewrite the script line in RPG_RT. The rewritten command replaces the
	 * current command of the frame.
	 *
	 * @param interpreter interpreter running the command
	 * @param frame its current frame, at the command about to be executed
	 * @return the command as it was before a plugin rewrote it, none when
	 *   unchanged. DynRPG restores it once the command was executed.
	 */
	std::optional<lcf::rpg::EventCommand> OnEventCommand(const Game_Interpreter& interpreter, lcf::rpg::SaveEventExecFrame& frame);

	/**
	 * The frames of an interpreter from first_frame on are replaced
	 * (a frame is pushed at first_frame, the stack is cleared or the
	 * interpreter destroyed): plugins drop the state of those scripts.
	 *
	 * @param interpreter the interpreter
	 * @param first_frame index of the first replaced frame
	 */
	void OnFramesReset(const Game_Interpreter* interpreter, int first_frame);

	/** @return raw text of the DynRPG comment being invoked (e.g. "@func 1, 2") */
	std::string_view GetCurrentComment() const { return current_comment; }

	/**
	 * DynRPG onFrame: called once per frame, in every scene.
	 *
	 * @param scene the current scene
	 */
	void OnFrame(Scene::SceneType scene);

	/**
	 * DynRPG onSetVariable: called before the engine writes a variable,
	 * whatever writes it (event commands, battle, input...).
	 * Plugins writing variables themselves don't trigger it, see SetVariable.
	 *
	 * @param id variable written
	 * @param value its new value
	 * @return false when a plugin prevents the write
	 */
	bool OnSetVariable(int id, int value);

	/** DynRPG onNewGame: a new game was started. */
	void OnNewGame();

	/**
	 * DynRPG onDoBattlerAction: a battler starts an action in battle, before
	 * its message, movement and animation.
	 *
	 * @param action the action
	 */
	void OnDoBattlerAction(const Game_BattleAlgorithm::AlgorithmBase& action);

	/**
	 * Whether the normal attacks of a battler may land RPG_RT's critical hits:
	 * plugins turn them off (with memory patches in RPG_RT).
	 *
	 * @param source the attacker
	 */
	bool HasStandardCriticalHits(const Game_Battler& source);

	/**
	 * Draws what plugins draw over the scene, after it was drawn
	 * (RPG::screen->canvas in DynRPG). See CreateSceneOverlay.
	 */
	void OnDrawScene(Scene::SceneType scene, Bitmap& dst);

	/**
	 * DynRPG onSystemBackgroundDrawn: the background behind the windows of
	 * a menu was drawn, plugins may draw their own over it.
	 */
	void OnSystemBackgroundDrawn(Scene::SceneType scene, Bitmap& dst);

	/**
	 * @return a drawable, above the windows of the scene, that draws the
	 *   plugins' OnDrawScene. It belongs to the scene that creates it.
	 */
	std::unique_ptr<Drawable> CreateSceneOverlay(Scene::SceneType scene);

	/**
	 * Writes a variable as a plugin does (RPG::variables[id] = value):
	 * without calling OnSetVariable, without the limits of the engine.
	 */
	void SetVariable(int id, int value);

	/** @return value of a key in DynRPG.ini, empty when it isn't there */
	std::string_view GetConfig(std::string_view section, std::string_view key) const;

	/** @return whether DynRPG.ini has the key */
	bool HasConfig(std::string_view section, std::string_view key) const;

	/** @return whether DynRPG.ini has the section (case-insensitive), even empty */
	bool HasConfigSection(std::string_view section) const;

	/** @return a key of DynRPG.ini read as a number like atoi does (0 when missing or not a number) */
	int GetConfigInt(std::string_view section, std::string_view key) const;

	/** @return whether the game has the plugin DynPlugins/<name>.dll (it isn't needed: see HasConfigSection) */
	static bool HasPluginDll(std::string_view name);

	/** @return the frame running the comment of a plugin call (RPG::EventScriptData) */
	static lcf::rpg::SaveEventExecFrame& GetFrame(Game_Interpreter& interpreter);

	/**
	 * @return eventId of the DynRPG callbacks for the frame running a plugin call:
	 *   the map event, minus the ID of a common event, 0 for a battle event
	 */
	static int GetEventId(Game_Interpreter& interpreter);

	/**
	 * GetExpForLevel of RPG_RT, for plugins replacing the EXP curve.
	 *
	 * @param level level to reach
	 * @param base EXP parameters of the class (or of the actor)
	 * @param inflation
	 * @param correction
	 * @return total EXP needed to reach level, none when no plugin changes the curve
	 */
	std::optional<int> GetExpForLevel(int level, int base, int inflation, int correction);

	/**
	 * RPG::battleSpeed of the DynRPG patch: the ATB gauges of all battlers
	 * fill at this percentage of their speed. Like in RPG_RT, it is kept
	 * until the Player exits, across games.
	 */
	static int GetBattleSpeed() { return battle_speed; }
	static void SetBattleSpeed(int speed) { battle_speed = speed; }

private:
	friend DynRpg::EasyRpgPlugin;

	std::string current_comment;

	bool Invoke(std::string_view func, dyn_arg_list args, Game_Interpreter* interpreter = nullptr);
	void InitPlugins();
	void LoadPluginData(int slot);

	std::unique_ptr<lcf::INIReader> config;
	std::vector<std::string> config_sections; // lower case
	static int battle_speed;

	using dyn_rpg_func = std::unordered_map<std::string, dynfunc>;

	bool plugins_loaded = false;

	// Registered DynRpg Plugins
	std::vector<std::unique_ptr<DynRpgPlugin>> plugins;

	// DynRpg Function table
	dyn_rpg_func dyn_rpg_functions;
};

/** Base class for implementing a DynRpg Plugins */
class DynRpgPlugin {
public:
	explicit DynRpgPlugin(std::string identifier, Game_DynRpg& instance) : instance(instance), identifier(std::move(identifier)) {}
	DynRpgPlugin() = delete;
	virtual ~DynRpgPlugin() = default;

	std::string_view GetIdentifier() const { return identifier; }
	virtual bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) = 0;
	virtual void Update() {}
	virtual void Load(const std::vector<uint8_t>&) {}
	virtual std::vector<uint8_t> Save() { return {}; }
	/**
	 * DynRPG onEventCommand, see Game_DynRpg::OnEventCommand
	 *
	 * @return the rewritten command, none to leave it unchanged
	 */
	virtual std::optional<lcf::rpg::EventCommand> OnEventCommand(const Game_Interpreter&, lcf::rpg::SaveEventExecFrame&) { return {}; }
	/** See Game_DynRpg::OnFramesReset */
	virtual void OnFramesReset(const Game_Interpreter*, int) {}
	/** See Game_DynRpg::OnFrame */
	virtual void OnFrame(Scene::SceneType) {}
	/** See Game_DynRpg::OnSetVariable, false prevents the write */
	virtual bool OnSetVariable(int, int) { return true; }
	/** See Game_DynRpg::OnNewGame */
	virtual void OnNewGame() {}
	/** See Game_DynRpg::OnDoBattlerAction */
	virtual void OnDoBattlerAction(const Game_BattleAlgorithm::AlgorithmBase&) {}
	/** See Game_DynRpg::HasStandardCriticalHits, false turns them off */
	virtual bool HasStandardCriticalHits(const Game_Battler&) const { return true; }
	/** DynRPG onLoadGame: a game was loaded, after Load() got the plugin's save data */
	virtual void OnLoadGame() {}
	/** See Game_DynRpg::OnDrawScene */
	virtual void OnDrawScene(Scene::SceneType, Bitmap&) {}
	/** See Game_DynRpg::OnSystemBackgroundDrawn, rect is the area that was drawn */
	virtual void OnSystemBackgroundDrawn(Bitmap&, const Rect&) {}
	/** See Game_DynRpg::GetExpForLevel */
	virtual std::optional<int> GetExpForLevel(int, int, int, int) { return {}; }

protected:
	Game_DynRpg& instance;

private:
	std::string identifier;
};

#endif
