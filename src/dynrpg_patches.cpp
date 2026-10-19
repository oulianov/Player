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
#include "dynrpg_patches.h"
#include "filefinder.h"
#include "output.h"
#include "game_config_game.h"
#include "player.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace {
	// DynRPG's hook in RPG_RT: the loader refuses to patch it
	constexpr uint32_t protected_address = 0x45CE00;
	// the memory of RPG_RT.exe quickpatches may write
	constexpr uint32_t memory_start = 0x400000;
	constexpr uint32_t memory_end = 0x4CF000;

	DynRpg::PatchedCode patched_code;

	/** OffsetToVA of the loader: the sections of RPG_RT 1.08 (header, CODE, DATA) */
	std::optional<uint32_t> OffsetToAddress(uint32_t offset) {
		if (offset <= 0x3FF) {
			return offset + 0x400000;
		}
		if (offset <= 0xC9200) {
			return offset + 0x400C00;
		}
		if (offset <= 0xCD400) {
			return offset + 0x400E00;
		}
		return {};
	}

	/** ValUInt of FreeBASIC: digits until the first other character, 0 without any */
	uint32_t ParseUInt(std::string_view s, int base) {
		size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
			++i;
		}
		bool negative = false;
		if (base == 10 && i < s.size() && (s[i] == '-' || s[i] == '+')) {
			negative = s[i] == '-';
			++i;
		}
		uint32_t value = 0;
		for (; i < s.size(); ++i) {
			const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
			int digit;
			if (c >= '0' && c <= '9') {
				digit = c - '0';
			} else if (base == 16 && c >= 'a' && c <= 'f') {
				digit = c - 'a' + 10;
			} else {
				break;
			}
			value = value * base + digit;
		}
		return negative ? 0u - value : value;
	}

	std::string_view Trim(std::string_view s) {
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
			s.remove_prefix(1);
		}
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.remove_suffix(1);
		}
		return s;
	}

	/** The operands of a call to TLcfWindow::DrawText in TLcfStatusViewWindow::Refresh (0x49F8A4) */
	struct DrawTextCode {
		uint32_t x; // mov edx, x
		uint32_t y; // mov ecx, y
		uint32_t color; // push color (0: not an immediate)
		uint32_t shown; // push shown
		uint32_t call; // call [edi+0x38]
		int default_x;
		int default_y;
		int default_color;
	};

	// in the order of DynRpg::Patches::StatusText
	constexpr DrawTextCode status_texts[] = {
		{ 0x49F91D, 0x49F918, 0x49F914, 0x49F916, 0x49F925, 92, 8, 0 }, // row: front
		{ 0x49F945, 0x49F940, 0x49F93C, 0x49F93E, 0x49F94D, 92, 8, 0 }, // row: back
		{ 0x49F96B, 0x49F966, 0x49F962, 0x49F964, 0x49F973, 8, 56, 1 },
		{ 0x49F98E, 0x49F989, 0x49F985, 0x49F987, 0x49F996, 44, 72, 0 },
		{ 0x49F9B4, 0x49F9AF, 0x49F9AB, 0x49F9AD, 0x49F9BC, 8, 88, 1 },
		{ 0x49F9ED, 0x49F9E8, 0x49F9E4, 0x49F9E6, 0x49F9F5, 44, 104, 0 },
		{ 0x49FA13, 0x49FA0E, 0x49FA0A, 0x49FA0C, 0x49FA1B, 8, 120, 1 },
		{ 0x49FA36, 0x49FA31, 0x49FA2D, 0x49FA2F, 0x49FA3E, 44, 136, 0 },
		{ 0x49FA5C, 0x49FA57, 0x49FA53, 0x49FA55, 0x49FA64, 8, 152, 1 },
		{ 0x49FA85, 0x49FA80, 0, 0x49FA7E, 0x49FA8D, 44, 168, 0 }, // its color is the condition's
		{ 0x49FAA8, 0x49FAA3, 0x49FA9F, 0x49FAA1, 0x49FAB0, 8, 184, 1 },
		{ 0x49FAE2, 0x49FADD, 0x49FAD9, 0x49FADB, 0x49FAEA, 74, 184, 0 },
	};
	static_assert(std::size(status_texts) == static_cast<size_t>(DynRpg::Patches::StatusText::Level) + 1);
}

bool DynRpg::PatchedCode::Write(uint32_t address, uint8_t value) {
	bytes[address] = value;
	return true;
}

bool DynRpg::PatchedCode::ApplyIps(Filesystem_Stream::InputStream& is) {
	const std::vector<uint8_t> data = Utils::ReadStream(is);
	size_t pos = 0;
	auto read = [&](size_t n) {
		uint32_t value = 0;
		for (size_t i = 0; i < n; ++i) {
			// the loader reads 0 past the end of the file
			value = (value << 8) | (pos < data.size() ? data[pos] : 0);
			++pos;
		}
		return value;
	};

	if (data.size() < 5 || std::string_view(reinterpret_cast<const char*>(data.data()), 5) != "PATCH") {
		Output::Warning("DynRPG: {}: invalid file header, not an IPS file?", is.GetName());
		return false;
	}
	pos = 5;

	while (pos < data.size()) {
		const uint32_t offset = read(3);
		if (offset == 0x454F46) { // EOF
			break;
		}
		uint32_t length = read(2);
		std::vector<uint8_t> record;
		if (length == 0) {
			length = read(2);
			record.assign(length, static_cast<uint8_t>(read(1)));
		} else {
			for (uint32_t i = 0; i < length; ++i) {
				record.push_back(static_cast<uint8_t>(read(1)));
			}
		}

		for (uint32_t i = 0; i < length; ++i) {
			const auto address = OffsetToAddress(offset + i);
			if (address == protected_address) {
				Output::Warning("DynRPG: {}: writes to the protected address {:X}", is.GetName(), protected_address);
				return false;
			}
			if (!address) {
				Output::Warning("DynRPG: {}: offset {:X} is outside of limits", is.GetName(), offset + i);
				return false;
			}
			Write(*address, record[i]);
		}
	}
	return true;
}

bool DynRpg::PatchedCode::ApplyQuickPatch(std::string_view patch) {
	size_t p = 0;
	auto at = [&](size_t i) { return i < patch.size() ? patch[i] : '\0'; };

	while (at(p)) {
		const size_t addr_start = p;
		while (at(p) && at(p) != ',') {
			++p;
		}
		uint32_t address = ParseUInt(patch.substr(addr_start, p - addr_start), 16);
		if (at(p) == ',') {
			++p;
		}
		if (address == 0) {
			return false;
		}

		if (at(p) == '%' || at(p) == '#') {
			const bool int32 = at(p) == '#';
			++p;
			std::string value_str;
			for (; at(p) && at(p) != ','; ++p) {
				if (at(p) != ' ') {
					value_str += at(p);
				}
			}
			const uint32_t value = ParseUInt(value_str, 10);
			if (address == protected_address || address < memory_start || address >= memory_end - 3) {
				return false;
			}
			for (int i = 0; i < (int32 ? 4 : 1); ++i) {
				Write(address + i, static_cast<uint8_t>(value >> (8 * i)));
			}
		} else {
			while (at(p) && at(p) != ',') {
				if (at(p) == ' ') {
					++p;
					continue;
				}
				const char byte_str[] = { at(p), at(p + 1) };
				const uint32_t value = ParseUInt(std::string_view(byte_str, byte_str[1] ? 2 : 1), 16);
				// the loader's checks of the 2 characters
				if (byte_str[1] == '\0' || byte_str[1] == ','
						|| (value == 0 && !(byte_str[0] == '0' && byte_str[1] == '0'))
						|| (byte_str[0] != '0' && value < 10)) {
					return false;
				}
				if (address == protected_address || address < memory_start || address >= memory_end) {
					return false;
				}
				Write(address, static_cast<uint8_t>(value));
				p += 2;
				++address;
			}
		}
		if (at(p) == ',') {
			++p;
		}
	}
	return true;
}

std::optional<uint8_t> DynRpg::PatchedCode::GetU8(uint32_t address) const {
	auto it = bytes.find(address);
	if (it == bytes.end()) {
		return {};
	}
	return it->second;
}

std::optional<int32_t> DynRpg::PatchedCode::GetI32(uint32_t address) const {
	uint32_t value = 0;
	for (int i = 3; i >= 0; --i) {
		auto byte = GetU8(address + i);
		if (!byte) {
			return {};
		}
		value = (value << 8) | *byte;
	}
	return static_cast<int32_t>(value);
}

bool DynRpg::PatchedCode::Matches(uint32_t address, std::initializer_list<int> pattern) const {
	for (int expected: pattern) {
		auto byte = GetU8(address++);
		if (!byte || (expected >= 0 && *byte != expected)) {
			return false;
		}
	}
	return true;
}

std::vector<std::pair<std::string, std::string>> DynRpg::ReadQuickPatches(Filesystem_Stream::InputStream& is) {
	std::vector<std::pair<std::string, std::string>> patches;
	bool in_section = false;
	bool section_seen = false;
	std::string line;
	while (Utils::ReadLine(is, line)) {
		const auto text = Trim(line);
		if (text.empty() || text.front() == ';') {
			continue;
		}
		if (text.front() == '[') {
			const auto end = text.find(']');
			const auto name = Trim(text.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1));
			// GetPrivateProfileString reads the first section of that name
			in_section = !section_seen && Utils::StrICmp(name, "QuickPatches") == 0;
			section_seen |= in_section;
			continue;
		}
		const auto eq = text.find('=');
		if (!in_section || eq == std::string_view::npos) {
			continue;
		}
		const auto key = Trim(text.substr(0, eq));
		auto value = Trim(text.substr(eq + 1));
		if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
			value = value.substr(1, value.size() - 2);
		}
		// the first of a repeated key is the one read
		const bool repeated = std::any_of(patches.begin(), patches.end(), [&](const auto& patch) {
			return Utils::StrICmp(patch.first, key) == 0;
		});
		if (!repeated) {
			patches.emplace_back(ToString(key), ToString(value));
		}
	}
	return patches;
}

void DynRpg::Patches::Load() {
	patched_code = {};
	if (!Player::IsPatchDynRpg()) {
		return;
	}
	auto fs = FileFinder::Game();
	if (!fs) {
		return;
	}

	// the IPS files of DynPatches, in the order of their names
	std::vector<std::string> names;
	if (auto* entries = fs.ListDirectory("DynPatches")) {
		for (const auto& entry: *entries) {
			if (entry.second.type == DirectoryTree::FileType::Regular && EndsWith(Utils::LowerCase(entry.second.name), ".ips")) {
				names.push_back(entry.second.name);
			}
		}
	}
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		return Utils::StrICmp(a, b) < 0;
	});
	for (const auto& name: names) {
		auto is = fs.OpenFile("DynPatches", name);
		if (is && patched_code.ApplyIps(is)) {
			Output::Debug("DynRPG: Applied patch {}", name);
		}
	}

	// then the quickpatches of DynRPG.ini
	if (auto is = fs.OpenFile("DynRPG.ini")) {
		for (const auto& patch: ReadQuickPatches(is)) {
			if (!patched_code.ApplyQuickPatch(patch.second)) {
				Output::Warning("DynRPG: Quickpatch {}: malformed or outside of limits", patch.first);
			}
		}
	}

	if (patched_code.IsEmpty()) {
		return;
	}

	// The patches the Player emulates, recognized by the code they write.
	// EasyRPG.ini and the command line still decide (or patch support is off).
	auto& config = Player::game_config;
	auto enable = [](ConfigParam<int>& param, int value) {
		if (param.Get() == 0 && param.Set(value)) {
			Output::Debug("DynRPG: Emulating patch {} ({})", param.GetName(), value);
		}
	};

	// Direct Menu Patch: "mov edx, var" opens the scene of V[var], for the actor of V[var + 1]
	if (patched_code.Matches(0x446EDD, { 0xBA, -1, -1, -1, -1, 0xA1, 0x7C, 0xDC, 0x4C, 0x00, 0x8B, 0x00, 0xC3 })) {
		enable(config.patch_direct_menu, *patched_code.GetI32(0x446EDE));
	}
	// KillSwitch: the Game Over of the map's check for a dead party (0x4A4398) only when "mov edx, switch" is ON
	if (patched_code.Matches(0x4A43EE, { 0x89, 0xC7, 0xBA, -1, -1, -1, -1, 0xE8, 0x2A, 0x6F, 0xFE, 0xFF })) {
		enable(config.patch_kill_switch, *patched_code.GetI32(0x4A43F1));
	}
	// norow: the actor's command window counts no Row command, and draws none
	if (patched_code.Matches(0x495107, { 0x90 }) && patched_code.Matches(0x495F94, { 0x8F })) {
		enable(config.patch_no_row, 1);
	}
	// FixBattlerFlash: the actor's update calls, instead of the flash update, code that stops
	// the flash of the actors on auto battle (actor+0x348) before it
	if (patched_code.Matches(0x4BBD8F, { 0x6D, 0xA7, 0xF6, 0xFF })
			&& patched_code.Matches(0x426500, { 0x80, 0xB8, 0x48, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x84 })) {
		enable(config.patch_fix_battler_flash, 1);
	}
}

const DynRpg::PatchedCode& DynRpg::Patches::GetCode() {
	return patched_code;
}

DynRpg::Patches::WindowText DynRpg::Patches::GetStatusText(StatusText text, const PatchedCode& patched) {
	const auto& code = status_texts[static_cast<int>(text)];
	WindowText out;
	out.dx = patched.GetI32(code.x).value_or(code.default_x) - code.default_x;
	out.dy = patched.GetI32(code.y).value_or(code.default_y) - code.default_y;
	out.color = code.color ? static_cast<int8_t>(patched.GetU8(code.color).value_or(code.default_color)) : code.default_color;
	out.shown = patched.GetU8(code.shown).value_or(1) != 0
		// a patch removed the call (norow writes nop)
		&& patched.GetU8(code.call).value_or(0xFF) == 0xFF;
	return out;
}
