#include <lcf/data.h>
#include "doctest.h"
#include "dynrpg_expcurves.h"
#include "dynrpg_msgcommands.h"
#include "dynrpg_patches.h"
#include <sstream>
#include "game_dynrpg.h"
#include "game_variables.h"
#include "test_mock_actor.h"

TEST_SUITE_BEGIN("DynRPG");

TEST_CASE("Basic parsing") {
	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand(R"(@FunC abc , "Hello World", 42, 1.5, 1.5a, "Str"",ing")", args) == "func");
	CHECK(args.size() == 6);
	CHECK(args[0] == "abc");
	CHECK(args[1] == "Hello World");
	CHECK(args[2] == "42");
	CHECK(args[3] == "1.5");
	CHECK(args[4] == "1.5a"); // DynRPG returns 1.5, not critical, we extract float later
	CHECK(args[5] == "Str\",ing");
}

TEST_CASE("Unterminated literal") {
	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC abc , \"Hello ", args) == "func");
	CHECK(args.size() == 2);
	CHECK(args[0] == "abc");
	CHECK(args[1] == "Hello ");
}

TEST_CASE("Comma") {
	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC , abc,,,xyz,,", args) == "func");
	CHECK(args.size() == 7);
	CHECK(args[0] == "");
	CHECK(args[1] == "abc");
	CHECK(args[2] == "");
	CHECK(args[3] == "");
	CHECK(args[4] == "xyz");
	CHECK(args[5] == "");
	CHECK(args[6] == "");
}

TEST_CASE("Tokens") {
	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC ab cd,Bla  ,   Bla Blub, Ab\"C \"d e", args) == "func");
	CHECK(args.size() == 4);
	CHECK(args[0] == "abcd");
	CHECK(args[1] == "bla");
	CHECK(args[2] == "blablub");
	CHECK(args[3] == "ab\"c\"de");
}

TEST_CASE("Line breaks") {
	// Comment lines are joined with line breaks, which DynRPG skips (even in strings)
	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC 4\r\n2, \"Hello \r\nWorld\",\r\nab\r\nc", args) == "func");
	CHECK(args.size() == 3);
	CHECK(args[0] == "42");
	CHECK(args[1] == "Hello World");
	CHECK(args[2] == "abc");
}

TEST_CASE("Variable names") {
	const MockActor m;

	std::vector<int32_t> vars = {100, 4, 2, 1};
	Main_Data::game_variables->SetData(vars);
	Main_Data::game_variables->SetWarning(0);

	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC V2,VV3,VVVV3", args) == "func");
	CHECK(args.size() == 3);
	CHECK(args[0] == "4");
	CHECK(args[1] == "4");
	CHECK(args[2] == "100");
}

TEST_CASE("Variable names with junk") {
	const MockActor m;

	std::vector<int32_t> vars = {0, 4, 2, 1};
	Main_Data::game_variables->SetData(vars);
	Main_Data::game_variables->SetWarning(0);

	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC V2junk,VV3 junk,VVVjunk3", args) == "func");
	CHECK(args.size() == 3);
	CHECK(args[0] == "4");
	CHECK(args[1] == "4");
	CHECK(args[2] == "vvvjunk3");
}

TEST_CASE("Variable names with junk") {
	const MockActor m;

	std::vector<int32_t> vars = {0, 4, 2};
	Main_Data::game_variables->SetData(vars);
	Main_Data::game_variables->SetWarning(0);

	MakeDBActor(2);
	MakeDBActor(4);
	Main_Data::game_actors->GetActor(2)->SetName("Actor 2");
	Main_Data::game_actors->GetActor(4)->SetName("Actor 4");

	std::vector<std::string> args;
	CHECK(DynRpg::ParseCommand("@FunC N2junk,NV3 junk,NVV3", args) == "func");
	CHECK(args.size() == 3);
	CHECK(args[0] == "Actor 2");
	CHECK(args[1] == "Actor 2");
	CHECK(args[2] == "Actor 4");
}

TEST_CASE("Arg parse") {
	const MockActor m; // disable log

	// 2.5x not tested here because the result differs between different C++ libraries
	// See: https://bugs.llvm.org/show_bug.cgi?id=17782
	std::vector<std::string> args = {"A", "1y", "2.5 x"};
	bool okay = false;

	std::string s, s2;
	int i, i2;
	float f;

	std::tie(s, i, f) = DynRpg::ParseArgs<std::string, int, float>("func", args, &okay);
	CHECK(okay);
	CHECK(s == "A");
	CHECK(i == 1);
	CHECK(f == 2.5);

	std::tie(i, i2, f) = DynRpg::ParseArgs<int, int, float>("func", args, &okay);
	CHECK(!okay);
	CHECK(i == 0);
	CHECK(i2 == 0);
	CHECK(f == 0.0);

	std::tie(f, i,s) = DynRpg::ParseArgs<float, int, std::string>("func", args, &okay);
	CHECK(!okay);
	CHECK(f == 0.0f);
	CHECK(i == 0);
	CHECK(s == "");

	std::tie(s, s2, i) = DynRpg::ParseArgs<std::string, std::string, int>("func", args, &okay);
	CHECK(okay);
	CHECK(s == "A");
	CHECK(s2 == "1y");
	CHECK(i == 2);

	std::tie(std::ignore, std::ignore, s) = DynRpg::ParseArgs<std::string, std::string, std::string>("func", args, &okay);
	CHECK(okay);
	CHECK(s == "2.5 x");

	// DynRPG reports string here but for convenience empty strings are 0
	args = {"", ""};
	std::tie(f, i) = DynRpg::ParseArgs<float, int>("func", args, &okay);
	CHECK(okay);
	CHECK(f == 0.0f);
	CHECK(i == 0);
}

TEST_CASE("easyrpg dynrpg invoke") {
	Game_DynRpg dyn;
	const MockActor m;

	Player::game_config.patch_dynrpg.Set(true);

	std::vector<int32_t> vars = {-1};
	Main_Data::game_variables->SetData(vars);
	Main_Data::game_variables->SetWarning(0);

	dyn.Invoke("@easyrpg_add 1, 2, 4");
	CHECK(Main_Data::game_variables->Get(1) == 6);

	// Invalid, stays 6
	dyn.Invoke("@easyrpg_add 1, 2, a");
	CHECK(Main_Data::game_variables->Get(1) == 6);

	// Not enough args, stays 6
	dyn.Invoke("@easyrpg_add 1");
	CHECK(Main_Data::game_variables->Get(1) == 6);

	dyn.Invoke("@easyrpg_add 1, 2");
	CHECK(Main_Data::game_variables->Get(1) == 2);

	dyn.Invoke("@call easyrpg_add, 1, 4.3, 7.7");
	CHECK(Main_Data::game_variables->Get(1) == 11);

	// Extra args ignored
	dyn.Invoke("@call easyrpg_add, 1, 4, 8, -14.0");
	CHECK(Main_Data::game_variables->Get(1) == -2);

	// Invalid func, stays -2
	dyn.Invoke("@call easyrpg_xxx, 1, 4, 7");
	CHECK(Main_Data::game_variables->Get(1) == -2);

	// does not crash
	dyn.Invoke("@unknownfunc 1, 2, 3");
}

TEST_CASE("Incompatible changes") {
	const MockActor m; // disable log

	std::vector<std::string> args;

	// Empty function name (DynRPG accepts this, but makes no sense)
	CHECK(DynRpg::ParseCommand("@ aaa", args).empty());

	// Text after literal (DynRPG accepts this, extra data is removed)
	CHECK(DynRpg::ParseCommand(R"(@FunC "Arg"junk)", args).empty());

	// DynRpg interprets as float and reads V3
	Main_Data::game_variables = std::make_unique<Game_Variables>(-99999, 99999);
	std::vector<int32_t> vars = {0, 4, 5};
	Main_Data::game_variables->SetData(vars);
	args.clear();
	CHECK(DynRpg::ParseCommand("@FunC V2.5", args) == "func");
	CHECK(args.size() == 1);
	CHECK(args[0] == "4");
}

TEST_CASE("ExpBug-in curve") {
	// [Exp_Curves] of Fighting Robots Quest
	auto curve = [](int level, int base, int inflation, int correction) {
		return DynRpg::ExpCurvesPlugin::Curve(level, base, inflation, correction, 85000, 1500000, 1300, 800000, 1000000);
	};

	CHECK(curve(1, 145, 1, 45) == 0);
	const std::vector<int> exp = { 190, 464, 847, 1360, 2017, 2832, 3810, 4953, 6262, 7733, 9362 };
	for (int level = 2; level <= 12; ++level) {
		CHECK(curve(level, 145, 1, 45) == exp[level - 2]);
	}
	CHECK(curve(20, 145, 1, 45) == 27704);
	CHECK(curve(50, 145, 1, 45) == 225669);
	CHECK(curve(99, 145, 1, 45) == 10000000);

	// the DLL's 32 bit products overflow, RPG_RT gets these values too
	const std::vector<int> overflow = { 300, 8400, 186075, 3373680, -4866245, -7214082, -1062048, -9416665, -2077842, -3967390, -327786 };
	for (int level = 2; level <= 12; ++level) {
		CHECK(curve(level, 300, 300, 0) == overflow[level - 2]);
	}
}

TEST_CASE("msg_commands codes") {
	const MockActor m;

	std::vector<int32_t> vars(400);
	vars[0] = 5;
	vars[1] = 3;
	vars[2] = 4;
	vars[3] = 112;
	vars[394] = 17;
	Main_Data::game_variables->SetData(vars);
	Main_Data::game_variables->SetWarning(0);
	lcf::Data::items[4].name = "Bolt";
	lcf::Data::items[16].name = "Potion";
	lcf::Data::items[111].name = "Gear";
	lcf::Data::enemies[0].name = "Slime";
	lcf::Data::enemies[0].drop_id = 17;
	lcf::Data::terms.victory = "Victory";

	Game_DynRpg dynrpg;
	DynRpg::MsgCommandsPlugin plugin(dynrpg);
	auto process = [&](const std::string& line) {
		return plugin.ProcessLine(line);
	};

	CHECK(process(R"(           You received \I[\V[395]]!)") == "           You received Potion!");
	CHECK(process(R"(you get \I[\V[4]]!)") == "you get Gear!");
	CHECK(process(R"(\I[5]\V[1].)") == "Bolt5.");
	CHECK(process(R"(\I[\m[1]] dropped)") == "Potion dropped");
	// codes of RPG_RT stay
	CHECK(process(R"(\C[\V[1]] and \N[\V[2]])") == R"(\C[5] and \N[3])");
	// two nested codes leave a "]"
	CHECK(process(R"(\I[\V[\V[3]]]!)") == "Gear]!");
	// a wrong id gives the rest of the line minus its last character, the last enemy is rejected
	CHECK(process(R"(\M[0] x)") == "M[0]  x");
	CHECK(process(R"(\M[1] and \M[20]!)") == "Slime and M[20]!");
	CHECK(process(R"(\R[3] \R[36].)") == "Victory R[36].");
}

namespace {
	Filesystem_Stream::InputStream MemoryStream(const std::string& data, std::string name) {
		return Filesystem_Stream::InputStream(new std::stringbuf(data), std::move(name));
	}
}

TEST_CASE("DynRPG IPS patches") {
	const MockActor m; // disable log

	// KillSwitch's "mov edx, 999" (file offset 0xA37EE: CODE), 5 nops of norow (RLE), then EOF
	std::string ips = "PATCH";
	ips += std::string("\x0A\x37\xEE\x00\x0C", 5) + std::string("\x89\xC7\xBA\xE7\x03\x00\x00\xE8\x2A\x6F\xFE\xFF", 12);
	ips += std::string("\x09\xED\x12\x00\x00\x00\x05\x90", 8);
	ips += "EOF";
	DynRpg::PatchedCode code;
	auto is = MemoryStream(ips, "test.ips");
	CHECK(code.ApplyIps(is));
	CHECK(code.GetI32(0x4A43F1) == 999);
	CHECK(code.Matches(0x4A43EE, { 0x89, 0xC7, 0xBA, -1, -1, -1, -1, 0xE8, 0x2A, 0x6F, 0xFE, 0xFF }));
	CHECK(!code.Matches(0x4A43EE, { 0x89, 0xC8 }));
	CHECK(code.GetU8(0x49F912) == 0x90);
	CHECK(code.GetU8(0x49F916) == 0x90);
	CHECK(!code.GetU8(0x49F917));
	CHECK(!code.GetI32(0x4A43F9)); // only 3 of its bytes are patched

	// the header (offset < 0x400) and the DATA section map too, past it the patch stops
	std::string limits = "PATCH";
	limits += std::string("\x00\x00\x3C\x00\x01" "\x41", 6); // 0x40003C
	limits += std::string("\x0C\x93\x00\x00\x01" "\x42", 6); // DATA: 0x4CA100
	limits += std::string("\x0C\xD4\x01\x00\x02" "\x43\x44", 7); // outside of the exe
	limits += "EOF";
	auto header = MemoryStream(limits, "limits.ips");
	CHECK(!code.ApplyIps(header));
	CHECK(code.GetU8(0x40003C) == 0x41);
	CHECK(code.GetU8(0x4CA100) == 0x42);

	auto not_ips = MemoryStream("PAT", "short.ips");
	CHECK(!code.ApplyIps(not_ips));
}

TEST_CASE("DynRPG quickpatches") {
	const MockActor m; // disable log

	// Fighting Robots Quest
	auto ini = MemoryStream(
		"[QuickPatches]\r\n"
		"KillSwitch=4A43F1,E7030000\r\n"
		"DMPVar=446EDE,#3326\r\n"
		"\r\n"
		"RemoveClassTitle=49F9AD,00,49FA0C,00\r\n"
		"StatusClass(y)=49F9E8,#104\r\n"
		"StatusTitle(y)=49FA31,#88\r\n"
		"StatusClassColor=49F9E4,%0\r\n"
		"StatusTitleColor=49FA2D,%0\r\n"
		";Disabled=49F9AD,01\r\n"
		"killswitch=4A43F1,00000000\r\n"
		"[faster_atb_advanced]\r\n"
		"BattleSpeedVariable=3328\r\n", "DynRPG.ini");
	const auto patches = DynRpg::ReadQuickPatches(ini);
	REQUIRE(patches.size() == 7);
	CHECK(patches[0].first == "KillSwitch");
	CHECK(patches[0].second == "4A43F1,E7030000");
	CHECK(patches[6].first == "StatusTitleColor");

	DynRpg::PatchedCode code;
	for (const auto& patch: patches) {
		CHECK(code.ApplyQuickPatch(patch.second));
	}
	CHECK(code.GetI32(0x4A43F1) == 999);
	CHECK(code.GetI32(0x446EDE) == 3326);
	CHECK(code.GetU8(0x49F9AD) == 0);
	CHECK(code.GetI32(0x49FA31) == 88);

	using DynRpg::Patches::StatusText;
	auto title = DynRpg::Patches::GetStatusText(StatusText::Title, code);
	CHECK(title.dx == 0);
	CHECK(title.dy == 88 - 136);
	CHECK(title.color == 0);
	CHECK(title.shown);
	CHECK(!DynRpg::Patches::GetStatusText(StatusText::ClassLabel, code).shown);
	CHECK(!DynRpg::Patches::GetStatusText(StatusText::TitleLabel, code).shown);
	CHECK(DynRpg::Patches::GetStatusText(StatusText::NameLabel, code).shown);
	CHECK(DynRpg::Patches::GetStatusText(StatusText::Class, code).dy == 0);
	CHECK(DynRpg::Patches::GetStatusText(StatusText::LevelLabel, code).color == 1);

	// several bytes, spaces, negative values
	CHECK(code.ApplyQuickPatch("49F925,90 90 90,401000,%-1"));
	CHECK(!DynRpg::Patches::GetStatusText(StatusText::RowFront, code).shown);
	CHECK(code.GetU8(0x401000) == 0xFF);
	CHECK(code.ApplyQuickPatch("401000,#-2"));
	CHECK(code.GetI32(0x401000) == -2);

	// malformed or outside of limits
	CHECK(!code.ApplyQuickPatch("49F9AD,0"));
	CHECK(!code.ApplyQuickPatch("zz,00"));
	CHECK(!code.ApplyQuickPatch("3FFFFF,00"));
	CHECK(!code.ApplyQuickPatch("4CEFFD,#1"));
	CHECK(!code.ApplyQuickPatch("45CE00,90"));
}

TEST_SUITE_END();
