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

#ifndef EP_DYNRPG_PATCHES_H
#define EP_DYNRPG_PATCHES_H

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string_view>
#include <vector>
#include "filesystem_stream.h"

namespace DynRpg {
	/**
	 * The code of RPG_RT 1.08 as the DynRPG loader patches it in memory at
	 * start-up: the IPS files of DynPatches, in the order of their names, then
	 * the [QuickPatches] of DynRPG.ini. Only the bytes the patches write are
	 * known: the Player recognizes the patches it emulates from them.
	 */
	class PatchedCode {
	public:
		/**
		 * Applies an IPS file like the loader: its file offsets are those of
		 * RPG_RT.exe, a write outside of the exe's memory or to DynRPG's hook
		 * stops the file (what it wrote before stays).
		 *
		 * @return false when it isn't an IPS file or stopped
		 */
		bool ApplyIps(Filesystem_Stream::InputStream& is);

		/**
		 * Applies a quickpatch: "Address,Values,Address,Values...". Values are
		 * hex bytes (EB71), %n for a byte or #n for 4 bytes, in decimal.
		 *
		 * @return false when it is malformed (what it wrote before stays)
		 */
		bool ApplyQuickPatch(std::string_view patch);

		/** @return the byte at a virtual address, none when no patch wrote it */
		std::optional<uint8_t> GetU8(uint32_t address) const;

		/** @return the 4 bytes at a virtual address, none unless patches wrote them all */
		std::optional<int32_t> GetI32(uint32_t address) const;

		/** @return whether the patches wrote these bytes at address (-1: any byte) */
		bool Matches(uint32_t address, std::initializer_list<int> bytes) const;

		bool IsEmpty() const { return bytes.empty(); }

	private:
		bool Write(uint32_t address, uint8_t value);

		std::map<uint32_t, uint8_t> bytes;
	};

	/** @return the [QuickPatches] of a DynRPG.ini, in file order, as GetPrivateProfileString reads them */
	std::vector<std::pair<std::string, std::string>> ReadQuickPatches(Filesystem_Stream::InputStream& is);

	namespace Patches {
		/**
		 * With DynRPG, reads the patches of the game (the .ips files of DynPatches and the
		 * [QuickPatches]) and turns on the ones the Player emulates, unless
		 * EasyRPG.ini or the command line configured them:
		 * Direct Menu (DMP), KillSwitch, norow and FixBattlerFlash.
		 */
		void Load();

		/** @return the code as the patches of the game changed it (empty without DynRPG) */
		const PatchedCode& GetCode();

		/**
		 * A text of a window of RPG_RT, drawn by TLcfWindow::DrawText(x, y,
		 * text, color, shown), as the patches changed its operands.
		 */
		struct WindowText {
			int dx = 0; //!< how far the patches moved it
			int dy = 0;
			int color = 0;
			bool shown = true; //!< false: not drawn (or its call removed)
		};

		/** The texts of the actor window of the status scene (TLcfStatusViewWindow) */
		enum class StatusText {
			RowFront,
			RowBack,
			NameLabel,
			Name,
			ClassLabel,
			Class,
			TitleLabel,
			Title,
			ConditionLabel,
			Condition,
			LevelLabel,
			Level,
		};

		/** @return how RPG_RT draws a text of the status scene, as the patches changed it */
		WindowText GetStatusText(StatusText text, const PatchedCode& code = GetCode());
	}
}

#endif
