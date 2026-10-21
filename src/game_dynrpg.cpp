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
#include "game_dynrpg.h"

#include <algorithm>
#include "bitmap.h"
#include "drawable.h"
#include "drawable_mgr.h"
#include "filefinder.h"
#include "game_actors.h"
#include "game_strings.h"
#include "game_variables.h"
#include "main_data.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "rect.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <lcf/inireader.h>
#include <lcf/rpg/saveeventexecframe.h>

#include "dynrpg_atbwaitonly.h"
#include "dynrpg_corticustomcrit.h"
#include "dynrpg_dataaccess.h"
#include "dynrpg_easyrpg.h"
#include "dynrpg_expcurves.h"
#include "dynrpg_fasteratb.h"
#include "dynrpg_gameclock.h"
#include "dynrpg_msgcommands.h"
#include "dynrpg_textplugin.h"
#include "dynrpg_params.h"
#include "dynrpg_pathfinder.h"
#include "game_interpreter.h"
#include "game_interpreter_shared.h"

int Game_DynRpg::battle_speed = 100;

namespace {
	/** Draws the plugins' OnDrawScene above the windows of a scene */
	class DynRpgSceneOverlay : public Drawable {
	public:
		explicit DynRpgSceneOverlay(Scene::SceneType scene) : Drawable(Priority_Timer), scene(scene) {
			DrawableMgr::Register(this);
		}

		void Draw(Bitmap& dst) override {
			if (Main_Data::game_dynrpg) {
				Main_Data::game_dynrpg->OnDrawScene(scene, dst);
			}
		}

	private:
		Scene::SceneType scene;
	};
}

enum DynRpg_ParseMode {
	ParseMode_Function,
	ParseMode_WaitForComma,
	ParseMode_WaitForArg,
	ParseMode_String,
	ParseMode_Token
};

// Var arg referenced by $n
std::string DynRpg::ParseVarArg(std::string_view func_name, dyn_arg_list args, int index, bool& parse_okay) {
	parse_okay = true;
	if (index >= static_cast<int>(args.size())) {
		parse_okay = false;
		Output::Warning("{}: Vararg {} out of range", func_name, index);
		return {};
	}

	std::string::iterator text_index, end;
	std::string text = args[index];
	text_index = text.begin();
	end = text.end();

	std::stringstream msg;

	for (; text_index != end; ++text_index) {
		char chr = *text_index;

		// Test for "" -> append "
		// otherwise end of string
		if (chr == '$' && std::distance(text_index, end) > 1) {
			char n = *std::next(text_index, 1);

			if (n == '$') {
				// $$ = $
				msg << n;
				++text_index;
			} else if (n >= '1' && n <= '9') {
				int i = (int)(n - '0');

				if (i + index < static_cast<int>(args.size())) {
					msg << args[i + index];
				}
				else {
					// $-ref out of range
					parse_okay = false;
					Output::Warning("{}: Vararg $-ref {} out of range", func_name, i);
					return {};
				}

				++text_index;
			} else {
				msg << chr;
			}
		} else {
			msg << chr;
		}
	}

	return msg.str();
}


static std::string ParseToken(std::string token, std::string_view function_name) {
	std::string::iterator text_index, end;
	text_index = token.begin();
	end = token.end();

	char chr = *text_index;

	bool first = true;

	bool number_encountered = false;

	std::stringstream var_part;
	std::stringstream number_part;

	for (;;) {
		// This loop checks if the token is to be substituted
		// If a token is (regex) [NT]?V+[0-9]+ it is resolved to a var or an actor
		// T is an EasyRPG extension: It will return a Maniac Patch String Var
		if (text_index != end) {
			chr = *text_index;
		}

		if (text_index == end) {
			// Variable reference
			std::string tmp = number_part.str();
			int number = atoi(tmp.c_str());
			tmp = var_part.str();
			if (tmp.empty()) {
				return token;
			}

			// Convert backwards (var_part is stored uppercase)
			for (std::string::reverse_iterator it = tmp.rbegin(); it != tmp.rend(); ++it) {
				if (*it == 'N') {
					if (!Main_Data::game_actors->ActorExists(number)) {
						Output::Warning("{}: Invalid actor id {} in {}", function_name, number, token);
						return {};
					}

					// N (Actor Name) is last
					return ToString(Main_Data::game_actors->GetActor(number)->GetName());
				} else if (*it == 'T' && Player::IsPatchManiac()) {
					// T (String Var) is last
					return ToString(Main_Data::game_strings->Get(number));
				} else {
					// Variable
					number = Main_Data::game_variables->Get(number);
				}
			}

			number_part.str("");
			number_part << number;
			return number_part.str();
		} else if (number_encountered || (chr >= '0' && chr <= '9')) {
			number_encountered = true;
			number_part << chr;
		} else if (chr == 'N' || chr == 'n') {
			// DynRPG tokens are case-insensitive ("v12" == "V12")
			if (!first) {
				break;
			}
			var_part << 'N';
		} else if (chr == 'V' || chr == 'v') {
			var_part << 'V';
		} else if ((chr == 'T' || chr == 't') && Player::IsPatchManiac()) {
			if (!first) {
				break;
			}
			var_part << 'T';
		} else {
			break;
		}

		++text_index;
		first = false;
	}

	// Normal token
	Utils::LowerCaseInPlace(token);
	return token;
}

void Game_DynRpg::InitPlugins() {
	if (plugins_loaded) {
		return;
	}

	if (Player::IsPatchDynRpg() || Player::HasEasyRpgExtensions()) {
		plugins.emplace_back(new DynRpg::EasyRpgPlugin(*this));
	}

	if (Player::IsPatchDynRpg()) {

		auto open_ini = []() {
			return FileFinder::Game() ? FileFinder::Game().OpenFile("DynRPG.ini") : Filesystem_Stream::InputStream();
		};
		if (auto is = open_ini()) {
			config = std::make_unique<lcf::INIReader>(is);
			if (config->ParseError()) {
				Output::Debug("DynRPG: Failed to parse DynRPG.ini");
			}
		}
		if (auto is = open_ini()) {
			std::string line;
			while (Utils::ReadLine(is, line)) {
				const auto start = line.find_first_not_of(" \t");
				const auto end = line.find(']');
				if (start != std::string::npos && line[start] == '[' && end != std::string::npos && end > start) {
					config_sections.push_back(Utils::LowerCase(line.substr(start + 1, end - start - 1)));
				}
			}
		}

		// The plugins that run. RPG_RT runs the DLLs of DynPlugins, the Player needs none:
		// the ones EasyRPG.ini declares ([DynRPG] Plugins=game_clock, ATBWaitOnly...), else
		// the ones the game has, by their DLL or their section in DynRPG.ini. The plugins only
		// working on their comments (DynText, DynParams, msg_commands) always run then.
		std::vector<std::string> declared;
		if (Player::game_config.dynrpg_plugins_declared) {
			for (auto name: Utils::Tokenize(Player::game_config.dynrpg_plugins.Get(), [](char32_t c) { return c == ',' || c == ' ' || c == '\t'; })) {
				name = Utils::LowerCase(name);
				if (EndsWith(name, ".dll")) {
					name.resize(name.size() - 4);
				}
				if (!name.empty()) {
					declared.push_back(name);
				}
			}
		}
		std::vector<std::string> running;
		auto add = [&](std::string_view dll, std::string_view section, auto&& create) {
			const bool runs = Player::game_config.dynrpg_plugins_declared
				? std::find(declared.begin(), declared.end(), Utils::LowerCase(dll)) != declared.end()
				: section.empty() || HasPluginDll(dll) || HasConfigSection(section);
			if (runs) {
				plugins.emplace_back(create());
				running.push_back(ToString(dll));
			}
		};
		add("DynTextPlugin", "", [this]() { return new DynRpg::TextPlugin(*this); });
		add("DynParams", "", [this]() { return new DynRpg::ParamsPlugin(*this); });
		add(DynRpg::MsgCommandsPlugin::dll_name, "", [this]() { return new DynRpg::MsgCommandsPlugin(*this); });
		add(DynRpg::FasterAtbPlugin::dll_name, DynRpg::FasterAtbPlugin::section, [this]() { return new DynRpg::FasterAtbPlugin(*this); });
		add(DynRpg::GameClockPlugin::dll_name, DynRpg::GameClockPlugin::section, [this]() { return new DynRpg::GameClockPlugin(*this); });
		add(DynRpg::AtbWaitOnlyPlugin::dll_name, DynRpg::AtbWaitOnlyPlugin::section, [this]() { return new DynRpg::AtbWaitOnlyPlugin(*this); });
		add(DynRpg::ExpCurvesPlugin::dll_name, DynRpg::ExpCurvesPlugin::section, [this]() { return new DynRpg::ExpCurvesPlugin(*this); });
		add(DynRpg::CortiCustomCritPlugin::dll_name, DynRpg::CortiCustomCritPlugin::section, [this]() { return new DynRpg::CortiCustomCritPlugin(*this); });
		add(DynRpg::PathfinderPlugin::dll_name, "", [this]() { return new DynRpg::PathfinderPlugin(*this); });
		add(DynRpg::DataAccessPlugin::dll_name, "", [this]() { return new DynRpg::DataAccessPlugin(*this); });

		for (const auto& name: declared) {
			if (std::none_of(running.begin(), running.end(), [&](const auto& dll) { return Utils::LowerCase(dll) == name; })) {
				Output::Warning("DynRPG: EasyRPG.ini declares the plugin {}, which the Player doesn't emulate", name);
			}
		}
		std::string names;
		for (const auto& name: running) {
			names += (names.empty() ? "" : ", ") + name;
		}
		Output::Debug("DynRPG: Plugins{}: {}", Player::game_config.dynrpg_plugins_declared ? " (EasyRPG.ini)" : "", names.empty() ? "none" : names);

		if (Main_Data::game_variables) {
			Main_Data::game_variables->SetWriteHook([](int id, int value) {
				return !Main_Data::game_dynrpg || Main_Data::game_dynrpg->OnSetVariable(id, value);
			});
		}
	}

	plugins_loaded = true;
}

bool Game_DynRpg::HasConfigSection(std::string_view section) const {
	const auto lower = Utils::LowerCase(section);
	return std::find(config_sections.begin(), config_sections.end(), lower) != config_sections.end();
}

bool Game_DynRpg::HasPluginDll(std::string_view name) {
	auto fs = FileFinder::Game();
	return fs && !fs.FindFile("DynPlugins", ToString(name) + ".dll").empty();
}

std::string_view Game_DynRpg::GetConfig(std::string_view section, std::string_view key) const {
	if (!config) {
		return {};
	}
	return config->GetString(section, key, "");
}

bool Game_DynRpg::HasConfig(std::string_view section, std::string_view key) const {
	return config && config->HasValue(section, key);
}

int Game_DynRpg::GetConfigInt(std::string_view section, std::string_view key) const {
	// the plugins read their settings with atoi
	return std::atoi(ToString(GetConfig(section, key)).c_str());
}

void Game_DynRpg::SetVariable(int id, int value) {
	Main_Data::game_variables->SetUnlimited(id, value);
}

bool Game_DynRpg::OnSetVariable(int id, int value) {
	for (auto& plugin : plugins) {
		if (!plugin->OnSetVariable(id, value)) {
			return false;
		}
	}
	return true;
}

void Game_DynRpg::OnFrame(Scene::SceneType scene) {
	if (!Player::IsPatchDynRpg() || !Main_Data::game_variables) {
		return;
	}
	InitPlugins();
	for (auto& plugin : plugins) {
		plugin->OnFrame(scene);
	}
}

void Game_DynRpg::OnNewGame() {
	if (!Player::IsPatchDynRpg()) {
		return;
	}
	InitPlugins();
	for (auto& plugin : plugins) {
		plugin->OnNewGame();
	}
}

void Game_DynRpg::OnDoBattlerAction(const Game_BattleAlgorithm::AlgorithmBase& action) {
	if (!Player::IsPatchDynRpg()) {
		return;
	}
	InitPlugins();
	for (auto& plugin : plugins) {
		plugin->OnDoBattlerAction(action);
	}
}

bool Game_DynRpg::HasStandardCriticalHits(const Game_Battler& source) {
	if (!Player::IsPatchDynRpg()) {
		return true;
	}
	InitPlugins();
	return std::all_of(plugins.begin(), plugins.end(), [&](auto& plugin) { return plugin->HasStandardCriticalHits(source); });
}

void Game_DynRpg::OnDrawScene(Scene::SceneType scene, Bitmap& dst) {
	for (auto& plugin : plugins) {
		plugin->OnDrawScene(scene, dst);
	}
}

void Game_DynRpg::OnSystemBackgroundDrawn(Scene::SceneType scene, Bitmap& dst) {
	if (plugins.empty()) {
		return;
	}
	Rect rect;
	switch (scene) {
		case Scene::Menu:
		case Scene::Item:
		case Scene::Skill:
		case Scene::Equip:
		case Scene::ActorTarget:
		case Scene::Status:
		case Scene::Order:
		case Scene::End:
		case Scene::Shop:
		case Scene::Name:
			rect = Rect(Player::menu_offset_x, Player::menu_offset_y, MENU_WIDTH, MENU_HEIGHT);
			break;
		case Scene::Save:
		case Scene::Load:
			// "In the save/load menu, there is only the small part at the top drawn"
			rect = Rect(Player::menu_offset_x, Player::menu_offset_y, MENU_WIDTH, 32);
			break;
		default:
			return;
	}
	for (auto& plugin : plugins) {
		plugin->OnSystemBackgroundDrawn(dst, rect);
	}
}

lcf::rpg::SaveEventExecFrame& Game_DynRpg::GetFrame(Game_Interpreter& interpreter) {
	return interpreter.GetFrame();
}

int Game_DynRpg::GetEventId(Game_Interpreter& interpreter) {
	const auto& frame = GetFrame(interpreter);
	if (frame.event_id > 0) {
		return frame.event_id;
	}
	using EventType = Game_Interpreter_Shared::EventType;
	if ((frame.maniac_event_info >> 4) == static_cast<int>(EventType::CommonEvent)) {
		return -frame.maniac_event_id;
	}
	return 0;
}

std::optional<int> Game_DynRpg::GetExpForLevel(int level, int base, int inflation, int correction) {
	if (!Player::IsPatchDynRpg()) {
		return {};
	}
	InitPlugins();
	for (auto& plugin : plugins) {
		if (auto exp = plugin->GetExpForLevel(level, base, inflation, correction)) {
			return exp;
		}
	}
	return {};
}

std::unique_ptr<Drawable> Game_DynRpg::CreateSceneOverlay(Scene::SceneType scene) {
	return std::make_unique<DynRpgSceneOverlay>(scene);
}

std::string DynRpg::ParseCommand(std::string command, std::vector<std::string>& args) {
	if (command.empty()) {
		// Not a DynRPG function (empty comment)
		return {};
	}

	std::string::iterator text_index, end;
	text_index = command.begin();
	end = command.end();

	char chr = *text_index;

	if (chr != '@') {
		// Not a DynRPG function, normal comment
		return {};
	}

	DynRpg_ParseMode mode = ParseMode_Function;
	std::string function_name;
	std::string tmp;
	std::stringstream token;

	++text_index;

	// Parameters can be of type Token, Number or String
	// Strings are in "", a "-literal is represented by ""
	// Number is a valid float number
	// Tokens are Strings without "" and with Whitespace stripped o_O

	// All arguments are passed as string to the DynRpg functions and are
	// converted to int or float on demand.

	for (;;) {
		if (text_index != end) {
			chr = *text_index;
		}

		if (text_index == end) {
			switch (mode) {
				case ParseMode_Function:
					// End of function token
					function_name = Utils::LowerCase(token.str());
					if (function_name.empty()) {
						// empty function name
						Output::Warning("Empty DynRPG function name");
						return {};
					}
					break;
				case ParseMode_WaitForComma:
					// no-op
					break;
				case ParseMode_WaitForArg:
					if (!args.empty()) {
						// Found , but no token -> empty arg
						args.emplace_back("");
					}
					break;
				case ParseMode_String:
					// Unterminated literal, handled like a terminated literal
					args.emplace_back(token.str());
					break;
				case ParseMode_Token:
					tmp = ParseToken(token.str(), function_name);
					args.emplace_back(tmp);
					break;
			}

			break;
		} else if (chr == ' ') {
			switch (mode) {
				case ParseMode_Function:
					// End of function token
					function_name = Utils::LowerCase(token.str());
					if (function_name.empty()) {
						// empty function name
						Output::Warning("Empty DynRPG function name");
						return {};
					}
					token.str("");

					mode = ParseMode_WaitForArg;
					break;
				case ParseMode_WaitForComma:
				case ParseMode_WaitForArg:
					// no-op
					break;
				case ParseMode_String:
					token << chr;
					break;
				case ParseMode_Token:
					// Skip whitespace
					break;
			}
		} else if (chr == ',') {
			switch (mode) {
				case ParseMode_Function:
					// End of function token
					function_name = Utils::LowerCase(token.str());
					if (function_name.empty()) {
						// empty function name
						Output::Warning("Empty DynRPG function name");
						return {};
					}
					token.str("");
					// Empty arg
					args.emplace_back("");
					mode = ParseMode_WaitForArg;
					break;
				case ParseMode_WaitForComma:
					mode = ParseMode_WaitForArg;
					break;
				case ParseMode_WaitForArg:
					// Empty arg
					args.emplace_back("");
					break;
				case ParseMode_String:
					token << chr;
					break;
				case ParseMode_Token:
					tmp = ParseToken(token.str(), function_name);
					args.emplace_back(tmp);
					// already on a comma
					mode = ParseMode_WaitForArg;
					token.str("");
					break;
			}
		} else if ((chr == '\r' || chr == '\n') && mode != ParseMode_Function) {
			// DynRPG skips the line breaks between comment lines, even in strings
		} else {
			// Anything else that isn't special purpose
			switch (mode) {
				case ParseMode_Function:
					token << chr;
					break;
				case ParseMode_WaitForComma:
					Output::Warning("{}: Expected \",\", got token", function_name);
					return {};
				case ParseMode_WaitForArg:
					if (chr == '"') {
						mode = ParseMode_String;
						// begin of string
					}
					else {
						mode = ParseMode_Token;
						token << chr;
					}
					break;
				case ParseMode_String:
					if (chr == '"') {
						// Test for "" -> append "
						// otherwise end of string
						if (std::distance(text_index, end) > 1 && *std::next(text_index, 1) == '"') {
							token << '"';
							++text_index;
						}
						else {
							// End of string
							args.emplace_back(token.str());

							mode = ParseMode_WaitForComma;
							token.str("");
						}
					}
					else {
						token << chr;
					}
					break;
				case ParseMode_Token:
					token << chr;
					break;
			}
		}

		++text_index;
	}

	return function_name;
}

bool Game_DynRpg::Invoke(std::string_view command, Game_Interpreter* interpreter) {
	InitPlugins();

	std::vector<std::string> args;
	std::string function_name = DynRpg::ParseCommand(ToString(command), args);

	if (function_name.empty()) {
		return true;
	}

	current_comment = ToString(command);
	bool result = Invoke(function_name, args, interpreter);
	current_comment.clear();
	return result;
}

bool Game_DynRpg::Invoke(std::string_view func, dyn_arg_list args, Game_Interpreter* interpreter) {
	InitPlugins();

	bool yield = false;

	for (auto& plugin: plugins) {
		if (plugin->Invoke(func, args, yield, interpreter)) {
			return !yield;
		}
	}

	Output::Warning("Unsupported DynRPG function: {}", func);
	return true;
}

std::string get_filename(int slot) {
	auto fs = FileFinder::Save();

	std::string filename = std::string("Save") + (slot <= 9 ? "0" : "") + std::to_string(slot) + ".dyn";

	std::string found = fs.FindFile(filename);

	if (found.empty()) {
		found = filename;
	}

	return found;
}

void Game_DynRpg::Load(int slot) {
	if (!Player::IsPatchDynRpg()) {
		return;
	}

	InitPlugins();
	LoadPluginData(slot);

	for (auto& plugin : plugins) {
		plugin->OnLoadGame();
	}
}

void Game_DynRpg::LoadPluginData(int slot) {
	std::string filename = get_filename(slot);

	auto in = FileFinder::Save().OpenInputStream(filename);

	if (!in) {
		// Normal for saves made before DynRPG state was stored (e.g. by RPG_RT)
		Output::Debug("No DynRPG save data: {}", filename);
		return;
	}

	std::vector<uint8_t> in_buffer;
	in_buffer.resize(8);

	in.read((char*)in_buffer.data(), 8);

	if (strncmp((char*)in_buffer.data(), "DYNSAVE1", 8) != 0) {
		Output::Warning("Corrupted DynRPG save: {}", filename);
		return;
	}

	while (!(in.eof() || in.fail())) {
		// Read header length followed by header (Plugin Identifier)

		uint32_t len;
		in.read((char *) &len, 4);
		Utils::SwapByteOrder(len);

		in_buffer.resize(len);
		in.read((char*)in_buffer.data(), len);

		// Find a plugin that feels responsible
		bool have_one = false;

		for (auto &plugin : plugins) {
			if (strncmp((char*)in_buffer.data(), plugin->GetIdentifier().data(), len) == 0) {
				// Chunk length
				in.read((char *) &len, 4);
				Utils::SwapByteOrder(len);

				if (len > 0) {
					// Read chunk
					in_buffer.resize(len);
					in.read((char*)in_buffer.data(), len);

					plugin->Load(in_buffer);
				}

				have_one = true;
				break;
			}
		}

		if (!have_one) {
			// Skip this chunk, no plugin found
			in.read((char *) &len, 4);
			Utils::SwapByteOrder(len);

			in.seekg(len, std::ios::cur);
		}
	}
}

void Game_DynRpg::Save(int slot) {
	if (!Player::IsPatchDynRpg()) {
		return;
	}

	InitPlugins();

	std::string filename = get_filename(slot);

	auto out = FileFinder::Save().OpenOutputStream(filename);

	if (!out) {
		Output::Warning("Couldn't write DynRPG save: {}", filename);
		return;
	}

	std::string header = "DYNSAVE1";

	out.write(header.c_str(), 8);

	for (auto &plugin : plugins) {
		uint32_t len = plugin->GetIdentifier().size();
		Utils::SwapByteOrder(len);

		out.write((char*)&len, 4);
		out.write(plugin->GetIdentifier().data(), len);

		std::vector<uint8_t> data = plugin->Save();
		len = data.size();
		Utils::SwapByteOrder(len);

		out.write((char*)&len, 4);
		out.write((char*)data.data(), data.size());
	}
}

void Game_DynRpg::Update() {
	for (auto& plugin : plugins) {
		plugin->Update();
	}
}

std::optional<lcf::rpg::EventCommand> Game_DynRpg::OnEventCommand(const Game_Interpreter& interpreter, lcf::rpg::SaveEventExecFrame& frame) {
	std::optional<lcf::rpg::EventCommand> original;
	for (auto& plugin : plugins) {
		auto rewritten = plugin->OnEventCommand(interpreter, frame);
		if (rewritten) {
			auto& com = frame.commands[frame.current_command];
			if (!original) {
				original = std::move(com);
			}
			com = std::move(*rewritten);
		}
	}
	return original;
}

void Game_DynRpg::OnFramesReset(const Game_Interpreter* interpreter, int first_frame) {
	for (auto& plugin : plugins) {
		plugin->OnFramesReset(interpreter, first_frame);
	}
}
