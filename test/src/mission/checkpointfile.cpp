/*
 * Tests for the file-format guarantees the mission checkpoint system relies on.
 *
 * A checkpoint has to stay readable across engine updates, and the way it achieves that is
 * entirely down to three properties of the self-describing file handler:
 *
 *   - a field the reader asks for but the file does not contain yields a default, so a field
 *     added to the engine after the checkpoint was written costs nothing;
 *   - a field the file contains but the reader never asks for is ignored, so a field removed
 *     from the engine costs nothing either;
 *   - an unrecognised section is skipped rather than aborting the read.
 *
 * Plus nested arrays, which the checkpoint format needs for ships -> subsystems -> banks and
 * which the JSON handler did not originally support.
 *
 * If any of these regress, checkpoints silently stop surviving engine updates, which is
 * exactly the failure the format exists to prevent -- hence testing them directly rather than
 * through a whole mission.
 *
 * Also here: the timestamp translation rules, which are the other thing a restore gets silently
 * and invisibly wrong if they regress.
 */

#include "cfile/cfile.h"
#include "mission/checkpointfile.h"
#include "mission/missioncheckpoint.h"
#include "pilotfile/JSONFileHandler.h"
#include "util/FSTestFixture.h"

#include <gtest/gtest.h>

#include <memory>

class CheckpointFileTest : public test::FSTestFixture {
  public:
	CheckpointFileTest() : test::FSTestFixture(INIT_CFILE) {}

  protected:
	static const char* TestFileName() { return "checkpoint_format_test.json"; }

	static CFILE* openForWrite()
	{
		return cfopen(TestFileName(), "wb", CF_TYPE_PLAYERS, false,
		              CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	}

	static CFILE* openForRead()
	{
		return cfopen(TestFileName(), "rb", CF_TYPE_PLAYERS, false,
		              CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	}

	void TearDown() override
	{
		cf_delete(TestFileName(), CF_TYPE_PLAYERS, CF_LOCATION_ROOT_USER | CF_LOCATION_TYPE_ROOT);

		FSTestFixture::TearDown();
	}
};

// A field the writer never wrote must read back as the caller's default rather than failing.
// This is what lets a newer engine read an older checkpoint.
TEST_F(CheckpointFileTest, MissingFieldYieldsDefault)
{
	{
		auto fp = openForWrite();
		ASSERT_NE(fp, nullptr);

		std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));
		handler->writeInt("present_int", 42);
		handler->writeFloat("present_float", 1.5f);
		handler->flush();
	}

	auto fp = openForRead();
	ASSERT_NE(fp, nullptr);

	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, true));

	EXPECT_TRUE(handler->hasField("present_int"));
	EXPECT_FALSE(handler->hasField("absent_int"));

	EXPECT_EQ(handler->readIntOr("present_int", 7), 42);
	EXPECT_EQ(handler->readIntOr("absent_int", 7), 7);

	EXPECT_FLOAT_EQ(handler->readFloatOr("present_float", 9.0f), 1.5f);
	EXPECT_FLOAT_EQ(handler->readFloatOr("absent_float", 9.0f), 9.0f);

	EXPECT_EQ(handler->readStringOr("absent_string", "fallback"), SCP_string("fallback"));
	EXPECT_TRUE(handler->readBoolOr("absent_bool", true));
}

// A field the file carries but this build no longer reads must simply be ignored.  This is
// what lets an older engine -- or a build that dropped a field -- read a newer checkpoint.
TEST_F(CheckpointFileTest, UnreadFieldIsIgnored)
{
	{
		auto fp = openForWrite();
		ASSERT_NE(fp, nullptr);

		std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));
		handler->writeInt("wanted", 1);
		handler->writeInt("retired_field", 999);
		handler->writeString("another_retired_field", "junk");
		handler->flush();
	}

	auto fp = openForRead();
	ASSERT_NE(fp, nullptr);

	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, true));

	// Reading only the field we still care about must work, and must not be thrown off by the
	// two it does not know about.
	EXPECT_EQ(handler->readIntOr("wanted", 0), 1);
}

// readFloat has to accept a whole number written without a decimal point, because a
// hand-edited checkpoint will contain "0" where the engine wrote "0.0".
TEST_F(CheckpointFileTest, IntegerReadsAsFloat)
{
	{
		auto fp = openForWrite();
		ASSERT_NE(fp, nullptr);

		std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));
		handler->writeInt("written_as_int", 3);
		handler->flush();
	}

	auto fp = openForRead();
	ASSERT_NE(fp, nullptr);

	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, true));

	EXPECT_FLOAT_EQ(handler->readFloatOr("written_as_int", 0.0f), 3.0f);
}

// The checkpoint format nests arrays (ships contain subsystems, which contain weapon banks),
// which the JSON handler originally refused to do.
TEST_F(CheckpointFileTest, NestedArraysRoundTrip)
{
	const int outer_count = 3;
	const int inner_counts[outer_count] = {2, 0, 4};

	{
		auto fp = openForWrite();
		ASSERT_NE(fp, nullptr);

		std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));

		handler->startArrayWrite("outer", outer_count);
		for (int i = 0; i < outer_count; i++) {
			handler->startSectionWrite(Section::Unnamed);
			handler->writeInt("index", i);

			handler->startArrayWrite("inner", inner_counts[i]);
			for (int j = 0; j < inner_counts[i]; j++) {
				handler->startSectionWrite(Section::Unnamed);
				handler->writeInt("value", i * 100 + j);
				handler->endSectionWrite();
			}
			handler->endArrayWrite();

			// Written after the nested array to prove the outer element is still the current
			// object once the inner array has been closed.
			handler->writeInt("trailing", i * 10);

			handler->endSectionWrite();
		}
		handler->endArrayWrite();

		handler->flush();
	}

	auto fp = openForRead();
	ASSERT_NE(fp, nullptr);

	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, true));

	auto outer = handler->startArrayRead("outer");
	ASSERT_EQ(outer, static_cast<size_t>(outer_count));

	for (size_t i = 0; i < outer; i++, handler->nextArraySection()) {
		EXPECT_EQ(handler->readIntOr("index", -1), static_cast<int>(i));

		auto inner = handler->startArrayRead("inner");
		EXPECT_EQ(inner, static_cast<size_t>(inner_counts[i]));

		for (size_t j = 0; j < inner; j++, handler->nextArraySection()) {
			EXPECT_EQ(handler->readIntOr("value", -1), static_cast<int>(i * 100 + j));
		}
		handler->endArrayRead();

		// The outer array must have resumed at the right element.
		EXPECT_EQ(handler->readIntOr("trailing", -1), static_cast<int>(i * 10));
	}
	handler->endArrayRead();
}

// A section written by a newer engine must be skipped rather than aborting the read, and the
// sections either side of it must still come through.
TEST_F(CheckpointFileTest, UnknownSectionIsSkipped)
{
	{
		auto fp = openForWrite();
		ASSERT_NE(fp, nullptr);

		std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));

		handler->beginWritingSections();

		handler->startSectionWrite(Section::CheckpointInfo);
		handler->writeString("slot", "alpha");
		handler->endSectionWrite();

		// Stands in for a section this build does not know about.
		handler->startSectionWrite(Section::Techroom);
		handler->writeInt("unknown_payload", 5);
		handler->endSectionWrite();

		handler->startSectionWrite(Section::CheckpointClock);
		handler->writeInt("mission_time", 1234);
		handler->endSectionWrite();

		handler->endWritingSections();
		handler->flush();
	}

	auto fp = openForRead();
	ASSERT_NE(fp, nullptr);

	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, true));

	SCP_string slot;
	int mission_time = 0;
	int skipped = 0;

	handler->beginSectionRead();
	while (handler->hasMoreSections()) {
		auto section = handler->nextSection();

		switch (section) {
		case Section::CheckpointInfo:
			slot = handler->readStringOr("slot", "");
			break;

		case Section::CheckpointClock:
			mission_time = handler->readIntOr("mission_time", 0);
			break;

		default:
			// This is the branch that keeps a newer file readable.
			++skipped;
			break;
		}
	}
	handler->endSectionRead();

	EXPECT_EQ(slot, SCP_string("alpha"));
	EXPECT_EQ(mission_time, 1234);
	EXPECT_EQ(skipped, 1);
}

// Saved timestamps have to be shifted into the clock of the run restoring them, because the
// engine's timestamp space runs from game launch rather than from mission start.  The shift
// itself is trivial arithmetic; what needs pinning down is which values must NOT move.
TEST(CheckpointStampTest, RealStampsShiftByTheDelta)
{
	// A stamp five seconds in the future stays five seconds in the future.
	EXPECT_EQ(mission_checkpoint_translate_stamp(105000, 400000), 505000);

	// And one in the past stays the same distance in the past.
	EXPECT_EQ(mission_checkpoint_translate_stamp(95000, 400000), 495000);

	// Restoring in a fresh session moves the clock backwards, which is equally valid.
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -400000), 100000);

	EXPECT_EQ(mission_checkpoint_translate_stamp(12345, 0), 12345);
}

// -1 (invalid), 0 (never) and 1 (immediate) are not points in time.  Shifting them would turn
// "this will never happen" into a real deadline -- and for the dual-purpose arrival/departure
// delays, where a non-positive value means "so many seconds, timer not armed yet", it would
// corrupt the delay itself.
TEST(CheckpointStampTest, SentinelsArePreserved)
{
	for (int delta : {400000, -400000, 0}) {
		EXPECT_EQ(mission_checkpoint_translate_stamp(-1, delta), -1);
		EXPECT_EQ(mission_checkpoint_translate_stamp(0, delta), 0);
		EXPECT_EQ(mission_checkpoint_translate_stamp(1, delta), 1);

		// Negative values are also the "not armed yet" encoding for arrival/departure delays.
		EXPECT_EQ(mission_checkpoint_translate_stamp(-30, delta), -30);
	}
}

// A large negative delta could otherwise push an already-elapsed stamp down onto a sentinel,
// silently turning "long since past" into "never" -- so it clamps to the smallest real value.
TEST(CheckpointStampTest, ElapsedStampsCannotBecomeSentinels)
{
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -500000), 2);  // would land on 0
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -500001), 2);  // would land on -1
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -600000), 2);  // far below

	// Just above the clamp, the arithmetic stands on its own.
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -499998), 2);
	EXPECT_EQ(mission_checkpoint_translate_stamp(500000, -499997), 3);
}

// ------------------------------------------------------------------
// Whole-checkpoint round trip
// ------------------------------------------------------------------
//
// The tests above cover the handler's guarantees in isolation.  These cover the layer above:
// that a populated checkpoint_data survives being written and read back.
//
// This is the shape of test that would have caught the bug where write_weapon_state() wrote its
// flag list under the key "flags", which the ship and each turret subsystem also used -- JSON
// object keys being unique, the weapon list won and every ship and turret flag list in the file
// came back empty.  Nothing in the format tests could see that, because each piece was correct
// on its own; only writing a ship WITH flags and reading it back shows it.

class CheckpointRoundTripTest : public test::FSTestFixture {
  public:
	CheckpointRoundTripTest() : test::FSTestFixture(INIT_CFILE) {}

  protected:
	static const char* Slot() { return "roundtrip"; }

	void TearDown() override
	{
		checkpoint::checkpoint_delete_file(Slot());

		FSTestFixture::TearDown();
	}

	// A checkpoint with something in every container that has ever been written flat into a
	// shared JSON object, which is where key collisions hide.
	static checkpoint::checkpoint_data makePopulated()
	{
		checkpoint::checkpoint_data data;

		data.version = static_cast<int>(checkpoint::CHECKPOINT_VERSION);
		data.slot = Slot();
		data.mission_filename = "roundtrip.fs2";
		data.mission_fingerprint = 0xABCDEF01;
		data.campaign = "roundtrip_campaign";
		data.pilot = "Test Pilot";
		data.mission_time = 1234;
		data.saved_timestamp_ms = 56789;

		checkpoint::ship_state ship;
		ship.name = "Alpha 1";
		ship.disposition = checkpoint::ShipDisposition::Present;
		ship.ship_class = "GTF Ulysses";
		ship.team = "Friendly";
		ship.hull = 42.5f;
		ship.flags = {"cargo_revealed", "escort", "no_ets"};
		ship.object_flags = {"invulnerable", "protected"};
		ship.ints["escort_priority"] = 7;
		ship.floats["afterburner_fuel"] = 12.5f;

		// A turret subsystem, which owns a weapon state written into the same object it is.
		checkpoint::subsystem_state turret;
		turret.name = "turret01";
		turret.ordinal = 0;
		turret.flags = {"has_fired", "untargetable"};
		turret.floats["current_hits"] = 99.0f;
		turret.has_weapons = true;
		turret.weapons.flags = {"beam_free"};
		turret.weapons.scalars["current_primary_bank"] = 1;
		ship.subsystems.push_back(turret);

		ship.weapons.flags = {"turret_lock"};
		ship.weapons.scalars["current_secondary_bank"] = 2;

		checkpoint::weapon_bank bank;
		bank.weapon_class = "Subach HL-7";
		bank.ammo = 30;
		bank.capacity = 60;
		ship.weapons.primary_banks.push_back(bank);

		ship.ai.present = true;
		ship.ai.flags = {"kamikaze", "no_dynamic"};
		ship.ai.target_ship = "Beta 1";
		ship.ai.ints["mode"] = 3;

		checkpoint::ai_goal_state goal;
		goal.mode = "Attack ship";
		goal.type = "event_ship";
		goal.flags = {"on_hold"};
		goal.target_name = "Beta 1";
		goal.priority = 88;
		ship.ai.goals.push_back(goal);
		ship.ai.goal_slots.push_back(2);

		checkpoint::dock_link_state dock;
		dock.other_ship = "Beta 1";
		dock.my_point = "dockpoint01";
		dock.their_point = "dockpoint02";
		ship.docks.push_back(dock);

		checkpoint::animation_state anim;
		anim.id = 0xDEADBEEF;
		anim.time = 1.25f;
		anim.speed = 2.0f;
		anim.instance_flags = 0x1'0000'0001ULL; // deliberately above 32 bits
		ship.animations.push_back(anim);

		data.ships.push_back(std::move(ship));

		// The world section: ambient state that belongs to the mission rather than to a ship.
		checkpoint::asteroid_state ast;
		ast.type_name = "Brown";
		ast.subtype = 2;
		ast.pos = vm_vec_new(10.0f, 20.0f, 30.0f);
		ast.vel = vm_vec_new(1.0f, 2.0f, 3.0f);
		ast.hull = 15.5f;
		ast.target_ship = "Beta 1";
		ast.final_death_time = 4321;
		data.asteroids.push_back(ast);
		data.asteroids_enabled = true;

		checkpoint::nav_state nav;
		nav.name = "Rally Point";
		nav.flags = 0x0004;
		nav.waypoint_list = "Waypoint path 1";
		nav.waypoint_num = 3;
		data.navs.push_back(nav);

		checkpoint::nav_state nav2;
		nav2.name = "Escort";
		nav2.flags = 0x0008;
		nav2.target_ship = "Beta 1";
		data.navs.push_back(nav2);

		data.current_nav = "Escort";
		data.autopilot_engaged = true;
		data.soundtrack = "3: Death's Door";
		data.music_battle_started = true;

		return data;
	}
};

// The headline case: a ship, its turret and its weapons all carry flag lists, and all three have
// to survive together.
TEST_F(CheckpointRoundTripTest, FlagListsDoNotCollide)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));
	ASSERT_EQ(read.ships.size(), 1u);

	const auto& ship = read.ships[0];

	EXPECT_EQ(ship.flags, SCP_vector<SCP_string>({"cargo_revealed", "escort", "no_ets"}));
	EXPECT_EQ(ship.object_flags, SCP_vector<SCP_string>({"invulnerable", "protected"}));
	EXPECT_EQ(ship.weapons.flags, SCP_vector<SCP_string>({"turret_lock"}));

	ASSERT_EQ(ship.subsystems.size(), 1u);
	EXPECT_EQ(ship.subsystems[0].flags, SCP_vector<SCP_string>({"has_fired", "untargetable"}));
	EXPECT_EQ(ship.subsystems[0].weapons.flags, SCP_vector<SCP_string>({"beam_free"}));
}

TEST_F(CheckpointRoundTripTest, ShipStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));
	ASSERT_EQ(read.ships.size(), 1u);

	const auto& ship = read.ships[0];

	EXPECT_EQ(ship.name, SCP_string("Alpha 1"));
	EXPECT_EQ(ship.ship_class, SCP_string("GTF Ulysses"));
	EXPECT_EQ(ship.team, SCP_string("Friendly"));
	EXPECT_FLOAT_EQ(ship.hull, 42.5f);
	EXPECT_EQ(ship.ints.at("escort_priority"), 7);
	EXPECT_FLOAT_EQ(ship.floats.at("afterburner_fuel"), 12.5f);

	ASSERT_EQ(ship.weapons.primary_banks.size(), 1u);
	EXPECT_EQ(ship.weapons.primary_banks[0].weapon_class, SCP_string("Subach HL-7"));
	EXPECT_EQ(ship.weapons.primary_banks[0].ammo, 30);
}

TEST_F(CheckpointRoundTripTest, AiStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));
	ASSERT_EQ(read.ships.size(), 1u);

	const auto& ai = read.ships[0].ai;

	EXPECT_TRUE(ai.present);
	EXPECT_EQ(ai.flags, SCP_vector<SCP_string>({"kamikaze", "no_dynamic"}));
	EXPECT_EQ(ai.target_ship, SCP_string("Beta 1"));
	EXPECT_EQ(ai.ints.at("mode"), 3);

	ASSERT_EQ(ai.goals.size(), 1u);
	EXPECT_EQ(ai.goals[0].mode, SCP_string("Attack ship"));
	EXPECT_EQ(ai.goals[0].target_name, SCP_string("Beta 1"));
	EXPECT_EQ(ai.goals[0].priority, 88);
	EXPECT_EQ(ai.goals[0].flags, SCP_vector<SCP_string>({"on_hold"}));

	// The originating goal slot has to survive, because active_goal indexes that array.
	ASSERT_EQ(ai.goal_slots.size(), 1u);
	EXPECT_EQ(ai.goal_slots[0], 2);
}

TEST_F(CheckpointRoundTripTest, DocksAndAnimationsSurvive)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));
	ASSERT_EQ(read.ships.size(), 1u);

	const auto& ship = read.ships[0];

	ASSERT_EQ(ship.docks.size(), 1u);
	EXPECT_EQ(ship.docks[0].other_ship, SCP_string("Beta 1"));
	EXPECT_EQ(ship.docks[0].my_point, SCP_string("dockpoint01"));
	EXPECT_EQ(ship.docks[0].their_point, SCP_string("dockpoint02"));

	ASSERT_EQ(ship.animations.size(), 1u);
	EXPECT_EQ(ship.animations[0].id, 0xDEADBEEFu);
	EXPECT_FLOAT_EQ(ship.animations[0].time, 1.25f);
	EXPECT_FLOAT_EQ(ship.animations[0].speed, 2.0f);
	// Above 32 bits, so this also pins the two-halves encoding.
	EXPECT_EQ(ship.animations[0].instance_flags, 0x1'0000'0001ULL);
}

// The world section holds two arrays plus a handful of scalars in one object, so it is the same
// shape of hazard as the ship section: a key reused between the asteroid list and the nav list
// would silently drop one of them.
TEST_F(CheckpointRoundTripTest, WorldStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));

	ASSERT_EQ(read.asteroids.size(), 1u);
	EXPECT_EQ(read.asteroids[0].type_name, SCP_string("Brown"));
	EXPECT_EQ(read.asteroids[0].subtype, 2);
	EXPECT_FLOAT_EQ(read.asteroids[0].pos.xyz.y, 20.0f);
	EXPECT_FLOAT_EQ(read.asteroids[0].vel.xyz.z, 3.0f);
	EXPECT_FLOAT_EQ(read.asteroids[0].hull, 15.5f);
	EXPECT_EQ(read.asteroids[0].target_ship, SCP_string("Beta 1"));
	EXPECT_EQ(read.asteroids[0].final_death_time, 4321);
	EXPECT_TRUE(read.asteroids_enabled);

	// Navs bind to either a waypoint path or a ship, and each is stored under its own key.
	ASSERT_EQ(read.navs.size(), 2u);
	EXPECT_EQ(read.navs[0].name, SCP_string("Rally Point"));
	EXPECT_EQ(read.navs[0].waypoint_list, SCP_string("Waypoint path 1"));
	EXPECT_EQ(read.navs[0].waypoint_num, 3);
	EXPECT_TRUE(read.navs[0].target_ship.empty());
	EXPECT_EQ(read.navs[1].name, SCP_string("Escort"));
	EXPECT_EQ(read.navs[1].target_ship, SCP_string("Beta 1"));
	EXPECT_TRUE(read.navs[1].waypoint_list.empty());

	EXPECT_EQ(read.current_nav, SCP_string("Escort"));
	EXPECT_TRUE(read.autopilot_engaged);
	EXPECT_EQ(read.soundtrack, SCP_string("3: Death's Door"));
	EXPECT_TRUE(read.music_battle_started);
}
