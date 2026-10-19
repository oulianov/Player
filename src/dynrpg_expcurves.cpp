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
#include "dynrpg_expcurves.h"

namespace {
	constexpr int64_t scale = 1000000;

	// the DLL's arithmetic: 32 bit products by 10^6 and 64 bit sums and products that wrap
	int32_t Scale32(int value) {
		return static_cast<int32_t>(static_cast<uint32_t>(value) * static_cast<uint32_t>(scale));
	}

	int64_t Add64(int64_t x, int64_t y) {
		return static_cast<int64_t>(static_cast<uint64_t>(x) + static_cast<uint64_t>(y));
	}

	int64_t Mul64(int64_t x, int64_t y) {
		return static_cast<int64_t>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y));
	}
}

DynRpg::ExpCurvesPlugin::ExpCurvesPlugin(Game_DynRpg& instance) : DynRpgPlugin(dll_name, instance) {
	a = instance.GetConfigInt(section, "CoefficientA");
	b = instance.GetConfigInt(section, "CoefficientB");
	c = instance.GetConfigInt(section, "CoefficientC");
	d = instance.GetConfigInt(section, "CoefficientD");
	e = instance.GetConfigInt(section, "CoefficientE");
}

std::optional<int> DynRpg::ExpCurvesPlugin::GetExpForLevel(int level, int base, int inflation, int correction) {
	return Curve(level, base, inflation, correction, a, b, c, d, e);
}

int DynRpg::ExpCurvesPlugin::Curve(int level, int base, int inflation, int correction,
		int64_t a, int64_t b, int64_t c, int64_t d, int64_t e) {
	if (level <= 1) {
		return 0;
	}

	const int64_t base6 = Scale32(base);
	const int64_t correction6 = Scale32(correction);
	// value of the level, its inflation and the growth of the inflation
	int64_t value = base6;
	int64_t infl = Add64(static_cast<int64_t>(Scale32(inflation)) * a / scale, b);
	const int64_t growth = Add64(d, c * Scale32(level) / scale);
	int64_t sum = 0;
	for (int i = 0; i < level - 1; ++i) {
		sum = Add64(Add64(sum, correction6), value);
		value = Mul64(value, infl) / scale;
		infl = Add64(Mul64(Add64(infl, -e), growth) / scale, e);
	}

	int32_t exp = static_cast<int32_t>(sum / scale);
	if (exp > 9999999) {
		exp = 10000000;
	}
	return exp;
}
