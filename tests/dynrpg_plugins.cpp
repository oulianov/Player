#include <lcf/data.h>
#include <lcf/rpg/saveeventexecframe.h>
#include "doctest.h"
#include "bitmap.h"
#include "drawable_list.h"
#include "drawable_mgr.h"
#include "dynrpg_patches.h"
#include "filefinder.h"
#include "game_battle.h"
#include "game_dynrpg.h"
#include "game_interpreter.h"
#include "game_runtime_patches.h"
#include "game_switches.h"
#include "game_system.h"
#include "game_variables.h"
#include "options.h"
#include "pixel_format.h"
#include "scene.h"
#include "scene_battle_rpg2k3.h"
#include "test_mock_actor.h"
#include "window_actorinfo.h"

// The DynRPG plugins and patches as a game uses them: the test game of tests/assets/dynrpg
// has a DynRPG.ini and DynPatches, but no DynPlugins (the Player doesn't need the DLLs).

namespace {
using Cmd = lcf::rpg::EventCommand::Code;

lcf::rpg::EventCommand MakeCommand(Cmd code, std::vector<int32_t> params = {}, std::string text = "") {
	lcf::rpg::EventCommand com;
	com.code = static_cast<int32_t>(code);
	com.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
	com.string = lcf::DBString(text);
	return com;
}

class TestInterpreter : public Game_Interpreter {
public:
	using Game_Interpreter::GetFrame;

	void Push(std::vector<lcf::rpg::EventCommand> list) {
		list.push_back(MakeCommand(Cmd::END));
		Game_Interpreter::Push<InterpreterExecutionType::Call, InterpreterEventType::CommonEvent>(std::move(list), 0);
	}

	/** Executes the commands of the current frame up to (excluding) index */
	void RunTo(int index) {
		while (GetFrame().current_command < index) {
			ExecuteCommand();
			++GetFrame().current_command;
		}
	}
};

/**
 * The test game with DynRPG, as the Player loads it (its patches, then its plugins),
 * for the duration of a test. plugins: the [DynRPG] Plugins of EasyRPG.ini, if any.
 */
class DynRpgGame {
public:
	explicit DynRpgGame(std::optional<std::string> plugins = {}) {
		FileFinder::SetGameFilesystem(FileFinder::Root().Subtree(EP_TEST_PATH "/dynrpg"));
		Player::game_config.patch_dynrpg.Set(true);
		Player::game_config.dynrpg_plugins.Set(plugins.value_or(""));
		Player::game_config.dynrpg_plugins_declared = plugins.has_value();
		DynRpg::Patches::Load();
		Game_DynRpg::SetBattleSpeed(100);
		Main_Data::game_dynrpg = std::make_unique<Game_DynRpg>();
		Main_Data::game_dynrpg->LoadPlugins(); // as Player::ResetGameObjects
	}

	~DynRpgGame() {
		Main_Data::game_dynrpg.reset();
		Game_DynRpg::SetBattleSpeed(100);
		Player::game_config.patch_dynrpg.Set(false);
		Player::game_config.dynrpg_plugins_declared = false;
		DynRpg::Patches::Load(); // forgets the patches
		FileFinder::SetGameFilesystem(previous);
	}

	Game_DynRpg& dyn() { return *Main_Data::game_dynrpg; }

private:
	FilesystemView previous = FileFinder::Game();
};

/** @return the pixels drawn (not transparent) in a rectangle of a bitmap */
int CountDrawn(const Bitmap& bitmap, Rect rect) {
	int count = 0;
	for (int y = rect.y; y < rect.y + rect.height; ++y) {
		for (int x = rect.x; x < rect.x + rect.width; ++x) {
			count += bitmap.GetColorAt(x, y).alpha > 0;
		}
	}
	return count;
}

/** The command window of RPG Maker 2003 battles */
struct BattleCommands : Scene_Battle_Rpg2k3 {
	using Scene_Battle_Rpg2k3::GetBattleCommandNames;
};

Game_Actor* PartyActor(int id) {
	Main_Data::game_party->AddActor(id);
	auto* actor = Main_Data::game_actors->GetActor(id);
	Setup(actor, 100, 10, 10, 10, 10, 50);
	return actor;
}
}

TEST_SUITE_BEGIN("DynRPG plugins");

TEST_CASE("faster_atb_advanced: the ATB gauges fill at the speed of its variable") {
	const MockActor m;
	DynRpgGame game;
	auto* actor = PartyActor(1);
	auto& vars = *Main_Data::game_variables;

	auto atb_increment = [&]() {
		actor->SetAtbGauge(0);
		Game_Battle::UpdateAtbGauges();
		return actor->GetAtbGauge();
	};

	// [faster_atb_advanced] BattleSpeedVariable=10, DefaultSpeed=100
	vars.Set(10, 1); // 1 to 16: 100 % to 1600 %
	REQUIRE(Game_DynRpg::GetBattleSpeed() == 100);
	const int normal = atb_increment();
	REQUIRE(normal > 0);

	vars.Set(10, 2);
	CHECK(Game_DynRpg::GetBattleSpeed() == 200);
	CHECK(atb_increment() == normal * 2);

	vars.Set(10, -2); // -4 to -1: 0 % to 75 %
	CHECK(Game_DynRpg::GetBattleSpeed() == 50);
	CHECK(atb_increment() == normal * 50 / 100);

	vars.Set(10, -4); // the battle stops
	CHECK(atb_increment() == 0);

	vars.Set(10, 3);
	vars.Set(10, -5); // below -4: unchanged
	CHECK(Game_DynRpg::GetBattleSpeed() == 300);

	vars.Set(10, 0); // 0 or above 16: DefaultSpeed
	CHECK(Game_DynRpg::GetBattleSpeed() == 100);
	vars.Set(10, 4);
	vars.Set(10, 17);
	CHECK(Game_DynRpg::GetBattleSpeed() == 100);

	// another variable
	vars.Set(11, 4);
	CHECK(Game_DynRpg::GetBattleSpeed() == 100);
}

TEST_CASE("game_clock: the play time in variables, the clock in the main menu") {
	const MockActor m;
	Bitmap::SetFormat(format_R8G8B8A8_a().format());
	DynRpgGame game;
	auto& vars = *Main_Data::game_variables;

	game.dyn().OnNewGame();
	game.dyn().OnFrame(Scene::Map);
	// SessionStart=21 SessionCurrent=22 SessionSecs=23 PlaytimeSecs=24 PlaytimeHH/MM/SS=25-27
	CHECK(vars.Get(22) >= vars.Get(21));
	CHECK(vars.Get(23) == vars.Get(22) - vars.Get(21));
	CHECK(vars.Get(24) == vars.Get(23)); // a new game: the session is the play time

	// RestartTrigger=28: writing it sets the play time
	vars.Set(28, 3725);
	game.dyn().OnFrame(Scene::Map);
	const int playtime = vars.Get(24);
	CHECK(playtime >= 3725);
	CHECK(playtime <= 3726);
	CHECK(vars.Get(25) == playtime / 3600);
	CHECK(vars.Get(26) == playtime % 3600 / 60);
	CHECK(vars.Get(27) == playtime % 60);

	// drawn over the main menu only: TimeTextDisplay at 8,168, the time at 184
	auto screen = Bitmap::Create(SCREEN_TARGET_WIDTH, SCREEN_TARGET_HEIGHT);
	const Rect clock(Player::menu_offset_x, Player::menu_offset_y + 168, 88, 32);
	game.dyn().OnDrawScene(Scene::Map, *screen);
	CHECK(CountDrawn(*screen, clock) == 0);
	game.dyn().OnFrame(Scene::Menu);
	game.dyn().OnDrawScene(Scene::Menu, *screen);
	CHECK(CountDrawn(*screen, clock) > 0);
}

TEST_CASE("ATBWaitOnly: battles in Wait mode") {
	const MockActor m;
	DynRpgGame game;

	Main_Data::game_system->SetAtbMode(lcf::rpg::SaveSystem::AtbMode_atb_active);
	game.dyn().OnNewGame();
	CHECK(Main_Data::game_system->GetAtbMode() == lcf::rpg::SaveSystem::AtbMode_atb_wait);
}

TEST_CASE("ExpBug-in: the EXP curve of [Exp_Curves]") {
	const MockActor m;
	auto& db_actor = lcf::Data::actors[0];
	db_actor.exp_base = 145;
	db_actor.exp_inflation = 1;
	db_actor.exp_correction = 45;

	// the Player's (RPG_RT's) curve
	Main_Data::game_actors = std::make_unique<Game_Actors>();
	CHECK(Main_Data::game_actors->GetActor(1)->GetBaseExp(2) == 191);

	DynRpgGame game;
	// the actors make their EXP lists with the plugin
	Main_Data::game_actors = std::make_unique<Game_Actors>();
	const auto* actor = Main_Data::game_actors->GetActor(1);
	const std::vector<int> exp = { 0, 190, 464, 847, 1360, 2017, 2832, 3810, 4953, 6262, 7733, 9362 };
	for (int level = 1; level <= 12; ++level) {
		CHECK(actor->GetBaseExp(level) == exp[level - 1]);
	}
	CHECK(actor->GetBaseExp(50) == 225669);
	CHECK(actor->GetBaseExp(99) == 10000000);
}

TEST_CASE("msg_commands: @process_cmd replaces the codes of the next message") {
	const MockActor m;
	lcf::Data::items[16].name = "Potion";
	Main_Data::game_variables->Set(1, 17);
	Main_Data::game_variables->Set(2, 3);
	DynRpgGame game;

	TestInterpreter interp;
	interp.Push({
		MakeCommand(Cmd::Comment, {}, "@process_cmd"),
		MakeCommand(Cmd::ShowMessage, {}, "You got \\I[\\V[1]]!"),
		MakeCommand(Cmd::ShowMessage_2, {}, "\\V[2] left, \\C[2]so long"),
		MakeCommand(Cmd::ShowMessage, {}, "Not this one: \\V[1]"),
	});
	interp.RunTo(1);

	const auto& list = interp.GetFrame().commands;
	CHECK(ToString(list[1].string) == "You got Potion!");
	CHECK(ToString(list[2].string) == "3 left, \\C[2]so long"); // \C is RPG_RT's
	CHECK(ToString(list[3].string) == "Not this one: \\V[1]");
}

TEST_CASE("EasyRPG.ini [DynRPG] Plugins: only these run") {
	const MockActor m;
	auto& vars = *Main_Data::game_variables;

	SUBCASE("not declared: found by their DynRPG.ini section") {
		DynRpgGame game;
		vars.Set(10, 2);
		CHECK(Game_DynRpg::GetBattleSpeed() == 200);
	}
	SUBCASE("declared: the others don't run, even with their section") {
		DynRpgGame game(std::string("ATBWaitOnly.dll , game_clock"));
		vars.Set(10, 2);
		CHECK(Game_DynRpg::GetBattleSpeed() == 100);

		Main_Data::game_system->SetAtbMode(lcf::rpg::SaveSystem::AtbMode_atb_active);
		game.dyn().OnNewGame();
		CHECK(Main_Data::game_system->GetAtbMode() == lcf::rpg::SaveSystem::AtbMode_atb_wait);

		// msg_commands isn't declared
		TestInterpreter interp;
		interp.Push({
			MakeCommand(Cmd::Comment, {}, "@process_cmd"),
			MakeCommand(Cmd::ShowMessage, {}, "\\V[2]"),
		});
		interp.RunTo(1);
		CHECK(ToString(interp.GetFrame().commands[1].string) == "\\V[2]");
	}
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("DynRPG patches");

TEST_CASE("The patches of DynPatches and the quickpatches of DynRPG.ini") {
	const MockActor m;
	auto& config = Player::game_config;

	{
		DynRpgGame game;
		CHECK(config.patch_direct_menu.Get() == 3326); // DMP.ips: 11, then the quickpatch DMPVar
		CHECK(config.patch_kill_switch.Get() == 999);
		CHECK(config.patch_no_row.Get() == 1);
		CHECK(config.patch_fix_battler_flash.Get() == 1);
	}

	// EasyRPG.ini decides: a patch it set keeps its value
	config.patch_kill_switch.Set(5);
	config.patch_no_row.Set(0);
	{
		DynRpgGame game;
		CHECK(config.patch_kill_switch.Get() == 5);
	}

	// without DynRPG, none is read
	config.patch_no_row.Set(0);
	config.patch_fix_battler_flash.Set(0);
	DynRpg::Patches::Load();
	CHECK(DynRpg::Patches::GetCode().IsEmpty());
	CHECK(config.patch_no_row.Get() == 0);
}

TEST_CASE("KillSwitch: no Game Over on the map while its switch is OFF") {
	const MockActor m;
	Scene::instance = std::make_shared<Scene>();
	auto* actor = PartyActor(1);
	DynRpgGame game; // KillSwitch: switch 999

	// Change HP: the party loses 9999 HP, may die
	TestInterpreter interp;
	interp.Push({
		MakeCommand(Cmd::ChangeHP, { 0, 0, 1, 0, 9999, 1 }),
		MakeCommand(Cmd::ChangeHP, { 0, 0, 1, 0, 9999, 1 }),
	});
	interp.RunTo(1);
	CHECK(actor->IsDead());
	CHECK(!Scene::instance->HasRequestedScene()); // the game handles the death of the party
	CHECK(!RuntimePatches::KillSwitch::IsMapGameOver());

	// the map checks the party every frame: the Game Over once the switch is ON
	Main_Data::game_switches->Set(999, true);
	CHECK(RuntimePatches::KillSwitch::IsMapGameOver());

	// and the commands that kill lead to it again
	interp.RunTo(2);
	auto next = Scene::instance->TakeRequestedScene();
	REQUIRE(next);
	CHECK(next->type == Scene::Gameover);

	// without the patch, at once
	Main_Data::game_switches->Set(999, false);
	Player::game_config.patch_kill_switch.Set(0);
	interp.Push({ MakeCommand(Cmd::ChangeHP, { 0, 0, 1, 0, 9999, 1 }) });
	interp.RunTo(1);
	next = Scene::instance->TakeRequestedScene();
	REQUIRE(next);
	CHECK(next->type == Scene::Gameover);
	CHECK(!RuntimePatches::KillSwitch::IsMapGameOver()); // the Player's own check is after the commands

	Scene::instance.reset();
}

TEST_CASE("norow: no Row command in battles") {
	const MockActor m;
	lcf::Data::battlecommands.commands[0].name = "Attack";
	lcf::Data::battlecommands.commands[1].name = "Skill";
	lcf::Data::terms.row = "Row";
	lcf::Data::actors[0].battle_commands = { 1, 2 };
	auto* actor = PartyActor(1);

	using Names = std::vector<std::string>;
	CHECK(BattleCommands::GetBattleCommandNames(actor) == Names{ "Attack", "Skill", "Row" });

	DynRpgGame game; // norow
	CHECK(BattleCommands::GetBattleCommandNames(actor) == Names{ "Attack", "Skill" });
}

TEST_CASE("FixBattlerFlash: actors on auto battle don't flash") {
	const MockActor m;
	auto* automatic = PartyActor(1);
	auto* controlled = PartyActor(2);
	automatic->SetAutoBattle(true);

	auto flash = [&](bool party_auto_battle) {
		for (auto* actor: { automatic, controlled }) {
			actor->Flash(31, 31, 31, 31, 20);
		}
		RuntimePatches::FixBattlerFlash::OnUpdateBattlers(party_auto_battle);
		automatic->UpdateBattle();
		controlled->UpdateBattle();
	};

	flash(false);
	CHECK(automatic->GetFlashColor().alpha > 0);
	CHECK(controlled->GetFlashColor().alpha > 0);

	DynRpgGame game; // FixBattlerFlash
	flash(false);
	CHECK(automatic->GetFlashColor().alpha == 0);
	CHECK(controlled->GetFlashColor().alpha > 0);

	// the Auto command of the party
	flash(true);
	CHECK(automatic->GetFlashColor().alpha == 0);
	CHECK(controlled->GetFlashColor().alpha == 0);
}

TEST_CASE("Quickpatches of the status scene: no Class and Title labels, the title higher") {
	const MockActor m;
	Bitmap::SetFormat(format_R8G8B8A8_a().format());
	DrawableList drawables;
	DrawableMgr::SetLocalList(&drawables);
	lcf::Data::actors[0].title = "Champion";
	lcf::Data::terms.easyrpg_status_scene_class = "Class";
	lcf::Data::terms.easyrpg_status_scene_title = "Title";
	auto* actor = PartyActor(1);

	// in the window's contents: the labels at x 0, the values at x 36
	const Rect class_label(0, 82, 30, 12);
	const Rect title_label(0, 114, 30, 12);
	const Rect title_moved(36, 82, 48, 12);
	const Rect title(36, 130, 48, 12);
	const Rect front(60, 2, 48, 12);
	auto draw = [&]() {
		Window_ActorInfo window(0, 0, 124, 208, *actor);
		return window.GetContents();
	};

	{
		auto contents = draw();
		CHECK(CountDrawn(*contents, class_label) > 0);
		CHECK(CountDrawn(*contents, title_label) > 0);
		CHECK(CountDrawn(*contents, title) > 0);
		CHECK(CountDrawn(*contents, title_moved) == 0);
		CHECK(CountDrawn(*contents, front) > 0);
	}
	{
		DynRpgGame game; // RemoveClassTitle, StatusTitle(y)=88 and norow
		auto contents = draw();
		CHECK(CountDrawn(*contents, class_label) == 0);
		CHECK(CountDrawn(*contents, title_label) == 0);
		CHECK(CountDrawn(*contents, title) == 0);
		CHECK(CountDrawn(*contents, title_moved) > 0);
		CHECK(CountDrawn(*contents, front) == 0);
	}

	DrawableMgr::SetLocalList(nullptr);
}

TEST_SUITE_END();
