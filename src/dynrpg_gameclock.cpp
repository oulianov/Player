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
#include "dynrpg_gameclock.h"
#include "async_handler.h"
#include "bitmap.h"
#include "cache.h"
#include "filefinder.h"
#include "font.h"
#include "game_clock.h"
#include "game_variables.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "rect.h"
#include "text.h"

#include <algorithm>
#include <chrono>

namespace {
	/** GetTickCount() / 1000 of RPG_RT: seconds of a clock that doesn't depend on the frames */
	/** RPG::variables[id], 0 for a key missing from DynRPG.ini */
	int GetVar(int id) {
		return id > 0 ? Main_Data::game_variables->Get(id) : 0;
	}

	uint32_t ClockSeconds() {
		auto secs = std::chrono::duration_cast<std::chrono::seconds>(Game_Clock::now().time_since_epoch());
		return static_cast<uint32_t>(secs.count());
	}

	/**
	 * Loads an image given as in DynRPG.ini ("Picture\file.png").
	 *
	 * @param path the path
	 * @param transparent use the mask color (RPG::Image::useMaskColor)
	 * @param done set once there is nothing more to wait for: loaded or missing
	 * @return the image, none while it downloads (web) or when it's missing
	 */
	BitmapRef LoadIniImage(std::string_view path, bool transparent, bool& done) {
		std::string file = ToString(path);
		std::replace(file.begin(), file.end(), '\\', '/');
		auto slash = file.find('/');
		if (slash == std::string::npos) {
			if (!file.empty()) {
				Output::Warning("game_clock: Image {} is not in a folder of the game", path);
			}
			done = true;
			return {};
		}
		std::string folder = file.substr(0, slash);
		std::string name = file.substr(slash + 1);
		auto dot = name.rfind('.');
		if (dot != std::string::npos) {
			name.resize(dot);
		}

		auto* request = AsyncHandler::RequestFile(folder, name);
		request->SetGraphicFile(true);
		request->Start();
		if (!request->IsReady()) {
			return {};
		}
		done = true;

		if (FileFinder::FindImage(folder, name).empty()) {
			// RPG::Image::loadFromFile(file, false): no error, an empty image
			Output::Warning("game_clock: Image {} not found", path);
			return {};
		}
		auto lower = Utils::LowerCase(folder);
		if (lower == "picture") {
			return Cache::Picture(name, transparent);
		} else if (lower == "panorama") {
			return Cache::Panorama(name);
		} else if (lower == "backdrop") {
			return Cache::Backdrop(name);
		} else if (lower == "title") {
			return Cache::Title(name);
		} else if (lower == "frame") {
			return Cache::Frame(name, transparent);
		} else if (lower == "system") {
			return Cache::System(name);
		}
		Output::Warning("game_clock: Images in {} are not supported", folder);
		return {};
	}
}

DynRpg::GameClockPlugin::GameClockPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {
	var_session_start = instance.GetConfigInt(dll_name, "SessionStart");
	var_session_current = instance.GetConfigInt(dll_name, "SessionCurrent");
	var_session_secs = instance.GetConfigInt(dll_name, "SessionSecs");
	var_playtime_secs = instance.GetConfigInt(dll_name, "PlaytimeSecs");
	var_playtime_hh = instance.GetConfigInt(dll_name, "PlaytimeHH");
	var_playtime_mm = instance.GetConfigInt(dll_name, "PlaytimeMM");
	var_playtime_ss = instance.GetConfigInt(dll_name, "PlaytimeSS");
	var_restart_trigger = instance.GetConfigInt(dll_name, "RestartTrigger");
	var_foreground = instance.GetConfigInt(dll_name, "ForegroundVar");
	var_background = instance.GetConfigInt(dll_name, "BackgroundVar");
}

void DynRpg::GameClockPlugin::StartSession() {
	instance.SetVariable(var_session_start, static_cast<int>(ClockSeconds()));
}

void DynRpg::GameClockPlugin::OnNewGame() {
	StartSession();
	playtime_secs = 0;
}

void DynRpg::GameClockPlugin::OnLoadGame() {
	StartSession();
	playtime_secs = static_cast<uint32_t>(GetVar(var_playtime_secs));
}

bool DynRpg::GameClockPlugin::OnSetVariable(int id, int value) {
	if (id == var_restart_trigger) {
		StartSession();
		playtime_secs = static_cast<uint32_t>(value);
	}
	if (id == var_foreground) {
		foreground.reset();
		foreground_tried = false;
		clock.reset();
	}
	if (id == var_background) {
		background.reset();
		background_tried = false;
	}
	return true;
}

std::string DynRpg::GameClockPlugin::ImageKey(const char* key, int var_id) const {
	// ForegroundImage1, ForegroundImage2... when the variable is set, else ForegroundImage
	int n = GetVar(var_id);
	return n > 0 ? std::string(key) + std::to_string(n) : std::string(key);
}

void DynRpg::GameClockPlugin::OnFrame(Scene::SceneType scene) {
	const uint32_t now = ClockSeconds();
	instance.SetVariable(var_session_current, static_cast<int>(now));
	const uint32_t session_secs = now - static_cast<uint32_t>(GetVar(var_session_start));
	instance.SetVariable(var_session_secs, static_cast<int>(session_secs));

	uint32_t secs = playtime_secs + session_secs;
	instance.SetVariable(var_playtime_secs, static_cast<int>(secs));
	instance.SetVariable(var_playtime_hh, static_cast<uint16_t>(secs / 3600));
	secs %= 3600;
	instance.SetVariable(var_playtime_mm, static_cast<uint8_t>(secs / 60));
	instance.SetVariable(var_playtime_ss, static_cast<uint8_t>(secs % 60));

	// "0" of the menu screens is the main menu: Scene_Menu here
	if (scene != Scene::Menu) {
		return;
	}
	if (frame_timer != 0) {
		++frame_timer;
	}
	if (frame_timer > 11 || frame_timer == 0) {
		frame_timer = 1;
		DrawClock();
	}
}

void DynRpg::GameClockPlugin::DrawClock() {
	if (!foreground_tried) {
		foreground = LoadIniImage(instance.GetConfig(dll_name, ImageKey("ForegroundImage", var_foreground)), true, foreground_tried);
	}

	if (!clock) {
		clock = Bitmap::Create(88, 208, true);
	}
	clock->Clear();

	const int hh = GetVar(var_playtime_hh);
	const int mm = GetVar(var_playtime_mm);
	const int ss = GetVar(var_playtime_ss);
	int offset = 14;
	if (hh < 10) {
		offset = 38;
	} else if (hh < 100) {
		offset = 32;
	} else if (hh < 1000) {
		offset = 26;
	} else if (hh < 10000) {
		offset = 20;
	}
	// the ":" are drawn apart, in another color
	std::string time = std::to_string(hh) + " " + (mm < 10 ? "0" : "") + std::to_string(mm)
		+ " " + (ss < 10 ? "0" : "") + std::to_string(ss);

	auto font = Font::Default();
	auto system = Cache::SystemOrBlack();
	Text::Draw(*clock, offset, 184, *font, *system, 0, time);
	Text::Draw(*clock, 8, 168, *font, *system, 1, instance.GetConfig(dll_name, "TimeTextDisplay"));
	Text::Draw(*clock, 26, 184, *font, *system, 1, "   :  :  ");
}

void DynRpg::GameClockPlugin::OnDrawScene(Scene::SceneType scene, Bitmap& dst) {
	if (scene != Scene::Menu) {
		return;
	}
	if (foreground) {
		dst.Blit(Player::menu_offset_x, Player::menu_offset_y, *foreground, foreground->GetRect(), Opacity::Opaque());
	}
	if (clock) {
		dst.Blit(Player::menu_offset_x, Player::menu_offset_y, *clock, clock->GetRect(), Opacity::Opaque());
	}
}

void DynRpg::GameClockPlugin::OnSystemBackgroundDrawn(Bitmap& dst, const Rect& rect) {
	if (!background_tried) {
		background = LoadIniImage(instance.GetConfig(dll_name, ImageKey("BackgroundImage", var_background)), false, background_tried);
	}
	if (!background) {
		return;
	}
	// the part of the image under the area drawn, in the coordinates of RPG_RT's 320x240 screen
	Rect src(rect.x - Player::menu_offset_x, rect.y - Player::menu_offset_y, rect.width, rect.height);
	dst.Blit(rect.x, rect.y, *background, src, Opacity::Opaque());
}
