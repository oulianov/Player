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

#ifndef EP_DYNRPG_EXPCURVES_H
#define EP_DYNRPG_EXPCURVES_H

#include <cstdint>
#include "game_dynrpg.h"

namespace DynRpg {
	/**
	 * ExpBug-in.dll: replaces the EXP curve of RPG_RT (GetExpForLevel) by a
	 * configurable form of the RPG Maker 2000 curve, in fixed point (x 10^6):
	 *
	 *   [Exp_Curves]
	 *   CoefficientA=85000     inflation per point of the EXP inflation parameter
	 *   CoefficientB=1500000   base inflation
	 *   CoefficientC=1300      growth of the inflation per level
	 *   CoefficientD=800000    base growth of the inflation
	 *   CoefficientE=1000000   growth correction
	 *
	 * 2000's curve is A=10000, B=1500000, C=2000, D=800000, E=1000000, but
	 * the steps are truncated to 10^-6 and the arithmetic wraps like the DLL's:
	 * large parameters give garbage values, which RPG_RT uses as they are.
	 */
	class ExpCurvesPlugin : public DynRpgPlugin {
	public:
		static constexpr const char* dll_name = "ExpBug-in";

		explicit ExpCurvesPlugin(Game_DynRpg& instance);

		bool Invoke(std::string_view, dyn_arg_list, bool&, Game_Interpreter*) override { return false; }
		std::optional<int> GetExpForLevel(int level, int base, int inflation, int correction) override;

		/**
		 * The curve of ExpBug-in.dll with its coefficients.
		 *
		 * @return total EXP needed to reach level (0 for level 1)
		 */
		static int Curve(int level, int base, int inflation, int correction,
			int64_t a, int64_t b, int64_t c, int64_t d, int64_t e);

	private:
		int64_t a = 0, b = 0, c = 0, d = 0, e = 0;
	};
}

#endif
