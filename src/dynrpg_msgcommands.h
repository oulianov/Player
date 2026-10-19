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

#ifndef EP_DYNRPG_MSGCOMMANDS_H
#define EP_DYNRPG_MSGCOMMANDS_H

#include <string>
#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * msg_commands.dll: "@process_cmd" right before a Show Message replaces
	 * codes in its lines (once, when the comment runs, in the running
	 * script's copy of the commands):
	 *
	 *   \V[n] \v[n]  variable n         \M[n]  enemy name        \m[n]  enemy's drop item id
	 *   \I[n]  item name                 \i[n]  item description
	 *   \K[n]  skill name                \k[n]  skill description
	 *   \O[n] \o[n]  state name          \A[n] \a[n]  attribute name
	 *   \T[n] \t[n]  terrain name        \D[n] \d[n]  actor's title
	 *   \B[n] \b[n]  battle command      \R[n] \r[n]  term (1-87)
	 *
	 * One \V or \m can be nested in another code: \I[\V[395]]. Like the DLL,
	 * the lines processed are the 4 after the comment's first line, \M \A \T
	 * \D \B reject the last entry of their list, and a wrong id writes the
	 * rest of the line from the code letter, minus its last character.
	 * Other codes are left to the message.
	 */
	class MsgCommandsPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "msg_commands";

		explicit MsgCommandsPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {}

		bool Invoke(std::string_view func, dyn_arg_list args, bool& do_yield, Game_Interpreter* interpreter) override;

		/** @return a message line with its codes replaced (processLine of the DLL) */
		std::string ProcessLine(const std::string& in);

	private:
		std::string Convert(const std::string& s);

		// globals of the DLL: a nested \V or \m swallows the "]" of the code around it
		bool flag_v = false;
		bool flag_m = false;
	};
}

#endif
