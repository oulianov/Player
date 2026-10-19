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
#include <iomanip>
#include <sstream>
#include "window_actorinfo.h"
#include "game_actors.h"
#include "game_party.h"
#include "bitmap.h"
#include "font.h"
#include "feature.h"
#include "dynrpg_patches.h"
#include "game_runtime_patches.h"

Window_ActorInfo::Window_ActorInfo(int ix, int iy, int iwidth, int iheight, const Game_Actor& actor) :
	Window_Base(ix, iy, iwidth, iheight),
	actor(actor) {

	SetContents(Bitmap::Create(width - 16, height - 16));

	Refresh();
}

void Window_ActorInfo::Refresh() {
	contents->Clear();

	DrawInfo();
}

void Window_ActorInfo::DrawInfo() {
	using DynRpg::Patches::StatusText;

	// draws a text where RPG_RT does, with the changes of DynRPG patches
	auto draw = [this](StatusText which, int x, int y, int color, std::string_view text, Text::Alignment align = Text::AlignLeft) {
		const auto patched = DynRpg::Patches::GetStatusText(which);
		if (patched.shown) {
			contents->TextDraw(x + patched.dx, y + patched.dy, color < 0 ? patched.color : color, text, align);
		}
	};
	// the color RPG_RT draws a text with (patches may change it)
	constexpr int patched_color = -1;

	if (Feature::HasRow()) {
		// Draw Row formation.
		if (actor.GetBattleRow() == Game_Actor::RowType::RowType_back) {
			draw(StatusText::RowBack, contents->GetWidth(), 2, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_back, "Back"), Text::AlignRight);
		} else if (!RuntimePatches::NoRow::IsActive()) {
			draw(StatusText::RowFront, contents->GetWidth(), 2, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_front, "Front"), Text::AlignRight);
		}
	}

	// Draw Face
	DrawActorFace(actor, 0, 0);

	// Draw Name
	draw(StatusText::NameLabel, 0, 50, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_name, "Name"));
	draw(StatusText::Name, 36, 66, patched_color, actor.GetName());

	// Draw Profession
	draw(StatusText::ClassLabel, 0, 82, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_class, "Class"));
	draw(StatusText::Class, 36, 98, patched_color, actor.GetClassName());

	// Draw Rank
	draw(StatusText::TitleLabel, 0, 114, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_title, "Title"));
	draw(StatusText::Title, 36, 130, patched_color, actor.GetTitle());

	// Draw Status
	draw(StatusText::ConditionLabel, 0, 146, patched_color, lcf::rpg::Terms::TermOrDefault(lcf::Data::terms.easyrpg_status_scene_condition, "State"));
	// Unit has Normal state if no state is set
	const lcf::rpg::State* state = actor.GetSignificantState();
	draw(StatusText::Condition, 36, 162, state ? state->color : Font::ColorDefault, state ? ToString(state->name) : ToString(lcf::Data::terms.normal_status));

	//Draw Level
	draw(StatusText::LevelLabel, 0, 178, patched_color, lcf::Data::terms.level);
	draw(StatusText::Level, 78, 178, patched_color, std::to_string(actor.GetLevel()), Text::AlignRight);
}
