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

		ship.no_parse_object = true;

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

		// Things in the air.  Both a weapon and a beam carry a flag list, and both live in the
		// same section, so this is the same collision hazard the ship section had.
		checkpoint::projectile_state shot;
		shot.weapon_class = "Harpoon";
		shot.pos = vm_vec_new(100.0f, 0.0f, -50.0f);
		shot.velocity = vm_vec_new(0.0f, 0.0f, 400.0f);
		shot.hull = 12.0f;
		shot.lifeleft = 3.5f;
		shot.creation_time = 65536;
		shot.group_id = 4;
		shot.team = "Hostile";
		shot.flags = {"locked_when_fired", "no_thruster"};
		shot.weapon_state = "homed_flight";
		shot.parent_ship = "Beta 1";
		shot.parent_turret = "turret01#0";
		shot.homing_ship = "Alpha 1";
		shot.homing_subsys = "engine01#1";
		shot.has_homing_pos = true;
		shot.homing_pos = vm_vec_new(1.0f, 2.0f, 3.0f);
		shot.lssm_stage = 2;
		shot.cmeasure_timer = 9000;
		data.projectiles.push_back(shot);

		checkpoint::beam_shot_state beam;
		beam.weapon_class = "LTerSlash";
		beam.shooter_ship = "Beta 1";
		beam.turret = "turret02#0";
		beam.target_ship = "Alpha 1";
		beam.target_subsys = "engine01#0";
		beam.team = "Hostile";
		beam.weapon_state = "firing";
		beam.flags = {"shrink", "force_firing"};
		beam.life_left = 1.75f;
		beam.framecount = 22;
		beam.shot_index = 1;
		beam.bank = 0;
		beam.warmup_stamp = -1;
		beam.warmdown_stamp = 4200;
		beam.dir_a = vm_vec_new(0.0f, 1.0f, 0.0f);
		beam.shot_count = 3;
		beam.shot_aim = {0.5f, 1.5f, 2.5f};
		data.beams.push_back(beam);

		// The world section: everything the mission load resets that a SEXP can have moved.
		auto& env = data.environment;
		env.present = true;
		env.skybox_model = "skybox.pof";
		env.skybox_texture = "skytex";
		env.skybox_flags_hi = 1;
		env.skybox_flags_lo = 0x80000000u;
		env.skybox_alpha = 0.75f;
		env.ambient_light = 0x00112233;
		env.fullneb = true;
		env.neb_range = 3000.0f;
		env.neb_pattern = "Nebula01";
		env.neb_fog_color_override = true;
		env.neb_fog_r = 10;
		env.neb_fog_g = 20;
		env.neb_fog_b = 30;
		env.subspace = true;
		env.background_index = 1;
		env.motion_debris_override = true;
		env.motion_debris_type = "Default";
		env.soundtrack = "3: Death's Door";
		env.music_battle_started = true;
		env.hud_draw = false;
		env.hud_disable_except_messages = true;
		env.hud_max_targeting_range = 5000;
		env.hud_display_warpout = 12345;
		env.no_traitor = true;
		env.traitor_override = "Custom Traitor";
		env.debriefing_persona = "Command";
		env.asteroids_enabled = true;
		env.squadron_wings = {"Alpha", "Beta", "", "Delta"};

		checkpoint::starfield_entry_state sun;
		sun.name = "SunGlow";
		sun.is_sun = true;
		sun.scale_x = 2.0f;
		sun.div_x = 3;
		sun.ang.p = 0.5f;
		env.starfield.push_back(sun);

		checkpoint::starfield_entry_state bitmap;
		bitmap.name = "Nebula02";
		bitmap.is_sun = false;
		bitmap.scale_y = 4.0f;
		bitmap.div_y = 5;
		bitmap.ang.h = 1.5f;
		env.starfield.push_back(bitmap);

		env.support_ship_class = "GTS Hygeia";
		env.support_arrival_location = "Near Ship";
		env.support_departure_location = "Hyperspace";
		env.support_arrival_anchor_ship = "Beta 1";
		env.support_departure_anchor_special = 1 << 30;
		env.support_max_ships = 5;
		env.support_max_concurrent = 1;
		env.support_tally = 2;
		env.support_available_for_species = 3;
		env.support_max_hull_repair = 80.0f;
		env.support_max_subsys_repair = 60.0f;
		env.support_disallow_rearm = true;
		env.rearm_pools.push_back({{"Subach HL-7", 12}, {"Harpoon", -1}});
		env.rearm_pools.push_back({{"Harpoon", 0}});

		checkpoint::navpoint_state nav;
		nav.name = "Rally Point";
		nav.flags = 0x0004;
		nav.target = "Waypoint path 1";
		nav.waypoint_num = 3;
		nav.normal_color[0] = 11;
		nav.visited_color[2] = 22;
		env.navpoints.push_back(nav);

		checkpoint::navpoint_state nav2;
		nav2.name = "Escort";
		nav2.flags = 0x0008;
		nav2.target = "Beta 1";
		env.navpoints.push_back(nav2);
		env.current_nav = 1;

		checkpoint::jump_node_state node;
		node.index = 0;
		node.name = "Alpha Node";
		node.display_name = "The Gate";
		node.model = "subspacenode.pof";
		node.hidden = true;
		node.colored = true;
		node.color[0] = 1;
		node.color[1] = 2;
		node.color[2] = 3;
		node.color[3] = 4;
		env.jump_nodes.push_back(node);

		auto& mission = data.mission;
		mission.present = true;
		mission.player_ints["warn_count"] = 3;
		mission.player_ints["allow_praise_timestamp"] = 45000;
		mission.training_ints["Training_context"] = 2;
		mission.training_ints["Training_failure"] = 1;
		mission.training_context_speed_timestamp = 7000;
		mission.used_personas = {"Alpha Wingman", "Command"};
		mission.mission_mood = 2;
		mission.no_builtin_msgs = true;
		mission.no_builtin_command = false;
		mission.reinforcements.push_back({"Delta", 2, true});
		mission.reinforcements.push_back({"Epsilon", 0, false});

		checkpoint::asteroid_state ast;
		ast.type_name = "Brown";
		ast.subtype = 2;
		ast.pos = vm_vec_new(10.0f, 20.0f, 30.0f);
		ast.vel = vm_vec_new(1.0f, 2.0f, 3.0f);
		ast.hull = 15.5f;
		ast.target_ship = "Beta 1";
		ast.final_death_time = 4321;
		data.asteroids.push_back(ast);

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

// A weapon and a beam share a section and both carry a "flags" array, so this is the same shape
// of hazard as the ship section -- one key reused between them would drop the other's list.
TEST_F(CheckpointRoundTripTest, ProjectilesAndBeamsSurvive)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));

	ASSERT_EQ(read.projectiles.size(), 1u);
	const auto& shot = read.projectiles[0];

	EXPECT_EQ(shot.weapon_class, SCP_string("Harpoon"));
	EXPECT_FLOAT_EQ(shot.pos.xyz.z, -50.0f);
	EXPECT_FLOAT_EQ(shot.velocity.xyz.z, 400.0f);
	EXPECT_FLOAT_EQ(shot.lifeleft, 3.5f);
	EXPECT_EQ(shot.creation_time, 65536);
	EXPECT_EQ(shot.group_id, 4);
	EXPECT_EQ(shot.flags, SCP_vector<SCP_string>({"locked_when_fired", "no_thruster"}));
	EXPECT_EQ(shot.weapon_state, SCP_string("homed_flight"));
	EXPECT_EQ(shot.parent_ship, SCP_string("Beta 1"));
	EXPECT_EQ(shot.parent_turret, SCP_string("turret01#0"));
	EXPECT_EQ(shot.homing_ship, SCP_string("Alpha 1"));
	EXPECT_EQ(shot.homing_subsys, SCP_string("engine01#1"));
	EXPECT_TRUE(shot.has_homing_pos);
	EXPECT_FLOAT_EQ(shot.homing_pos.xyz.y, 2.0f);
	EXPECT_EQ(shot.lssm_stage, 2);
	EXPECT_EQ(shot.cmeasure_timer, 9000);

	ASSERT_EQ(read.beams.size(), 1u);
	const auto& beam = read.beams[0];

	EXPECT_EQ(beam.weapon_class, SCP_string("LTerSlash"));
	EXPECT_EQ(beam.shooter_ship, SCP_string("Beta 1"));
	EXPECT_EQ(beam.turret, SCP_string("turret02#0"));
	EXPECT_EQ(beam.target_ship, SCP_string("Alpha 1"));
	EXPECT_EQ(beam.target_subsys, SCP_string("engine01#0"));
	EXPECT_EQ(beam.flags, SCP_vector<SCP_string>({"shrink", "force_firing"}));
	EXPECT_EQ(beam.weapon_state, SCP_string("firing"));
	EXPECT_FLOAT_EQ(beam.life_left, 1.75f);
	EXPECT_EQ(beam.framecount, 22);
	EXPECT_EQ(beam.shot_index, 1);
	EXPECT_EQ(beam.warmup_stamp, -1);
	EXPECT_EQ(beam.warmdown_stamp, 4200);
	EXPECT_FLOAT_EQ(beam.dir_a.xyz.y, 1.0f);

	// The aim vectors decide exactly how the beam sweeps, so the whole list has to come back.
	EXPECT_EQ(beam.shot_count, 3);
	ASSERT_EQ(beam.shot_aim.size(), 3u);
	EXPECT_FLOAT_EQ(beam.shot_aim[2], 2.5f);
}

// The world section is one object holding half a dozen arrays and several dozen scalars, so it
// is the same shape of hazard as the ship section: a key reused between two of those arrays
// would silently drop one of them.
TEST_F(CheckpointRoundTripTest, WorldStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));

	const auto& env = read.environment;

	// present is what tells a restore the section was written at all; without it an absent
	// section would blank the sky rather than leave it alone.
	EXPECT_TRUE(env.present);

	EXPECT_EQ(env.skybox_model, SCP_string("skybox.pof"));
	EXPECT_EQ(env.skybox_texture, SCP_string("skytex"));
	// Above 32 bits, so this also pins the two-halves encoding.
	EXPECT_EQ(env.skybox_flags_hi, 1u);
	EXPECT_EQ(env.skybox_flags_lo, 0x80000000u);
	EXPECT_FLOAT_EQ(env.skybox_alpha, 0.75f);
	EXPECT_EQ(env.ambient_light, 0x00112233);

	EXPECT_TRUE(env.fullneb);
	EXPECT_FLOAT_EQ(env.neb_range, 3000.0f);
	EXPECT_EQ(env.neb_pattern, SCP_string("Nebula01"));
	EXPECT_TRUE(env.neb_fog_color_override);
	EXPECT_EQ(env.neb_fog_g, 20);
	EXPECT_TRUE(env.subspace);

	EXPECT_EQ(env.background_index, 1);
	ASSERT_EQ(env.starfield.size(), 2u);
	EXPECT_EQ(env.starfield[0].name, SCP_string("SunGlow"));
	EXPECT_TRUE(env.starfield[0].is_sun);
	EXPECT_FLOAT_EQ(env.starfield[0].scale_x, 2.0f);
	EXPECT_EQ(env.starfield[0].div_x, 3);
	EXPECT_FLOAT_EQ(env.starfield[0].ang.p, 0.5f);
	EXPECT_FALSE(env.starfield[1].is_sun);
	EXPECT_FLOAT_EQ(env.starfield[1].ang.h, 1.5f);

	EXPECT_TRUE(env.motion_debris_override);
	EXPECT_EQ(env.motion_debris_type, SCP_string("Default"));
	EXPECT_EQ(env.soundtrack, SCP_string("3: Death's Door"));
	EXPECT_TRUE(env.music_battle_started);

	EXPECT_FALSE(env.hud_draw);
	EXPECT_TRUE(env.hud_disable_except_messages);
	EXPECT_EQ(env.hud_max_targeting_range, 5000);
	EXPECT_EQ(env.hud_display_warpout, 12345);

	EXPECT_TRUE(env.no_traitor);
	EXPECT_EQ(env.traitor_override, SCP_string("Custom Traitor"));
	EXPECT_EQ(env.debriefing_persona, SCP_string("Command"));
	EXPECT_TRUE(env.asteroids_enabled);

	// Navs bind to either a waypoint path or a ship, and both go out under the same key.
	ASSERT_EQ(env.navpoints.size(), 2u);
	EXPECT_EQ(env.navpoints[0].name, SCP_string("Rally Point"));
	EXPECT_EQ(env.navpoints[0].target, SCP_string("Waypoint path 1"));
	EXPECT_EQ(env.navpoints[0].waypoint_num, 3);
	EXPECT_EQ(env.navpoints[0].normal_color[0], 11);
	EXPECT_EQ(env.navpoints[0].visited_color[2], 22);
	EXPECT_EQ(env.navpoints[1].target, SCP_string("Beta 1"));
	EXPECT_EQ(env.current_nav, 1);

	ASSERT_EQ(env.jump_nodes.size(), 1u);
	EXPECT_EQ(env.jump_nodes[0].name, SCP_string("Alpha Node"));
	EXPECT_EQ(env.jump_nodes[0].display_name, SCP_string("The Gate"));
	EXPECT_EQ(env.jump_nodes[0].model, SCP_string("subspacenode.pof"));
	EXPECT_TRUE(env.jump_nodes[0].hidden);
	EXPECT_TRUE(env.jump_nodes[0].colored);
	EXPECT_EQ(env.jump_nodes[0].color[3], 4);

	// Empty slots have to come back as empty rather than being dropped, since the position in
	// the list is what says which squadron slot a wing occupies.
	EXPECT_EQ(env.squadron_wings, SCP_vector<SCP_string>({"Alpha", "Beta", "", "Delta"}));

	ASSERT_EQ(read.asteroids.size(), 1u);
	EXPECT_EQ(read.asteroids[0].type_name, SCP_string("Brown"));
	EXPECT_FLOAT_EQ(read.asteroids[0].pos.xyz.y, 20.0f);
	EXPECT_FLOAT_EQ(read.asteroids[0].hull, 15.5f);
	EXPECT_EQ(read.asteroids[0].final_death_time, 4321);
}

// Support ships are the one kind of ship the mission file will not recreate, so the restore has
// to know which ships those were and what the mission's support settings had become.
TEST_F(CheckpointRoundTripTest, SupportStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));

	ASSERT_EQ(read.ships.size(), 1u);
	EXPECT_TRUE(read.ships[0].no_parse_object);

	const auto& env = read.environment;

	EXPECT_EQ(env.support_tally, 2);
	EXPECT_EQ(env.support_ship_class, SCP_string("GTS Hygeia"));
	EXPECT_EQ(env.support_max_ships, 5);
	EXPECT_EQ(env.support_max_concurrent, 1);
	EXPECT_EQ(env.support_arrival_location, SCP_string("Near Ship"));
	EXPECT_EQ(env.support_departure_location, SCP_string("Hyperspace"));
	EXPECT_EQ(env.support_available_for_species, 3);
	EXPECT_FLOAT_EQ(env.support_max_hull_repair, 80.0f);
	EXPECT_FLOAT_EQ(env.support_max_subsys_repair, 60.0f);
	EXPECT_TRUE(env.support_disallow_rearm);

	// A ship anchor travels by name, a special anchor by value, and the two must not be
	// confused for one another.
	EXPECT_EQ(env.support_arrival_anchor_ship, SCP_string("Beta 1"));
	EXPECT_EQ(env.support_arrival_anchor_special, -1);
	EXPECT_TRUE(env.support_departure_anchor_ship.empty());
	EXPECT_EQ(env.support_departure_anchor_special, 1 << 30);

	// Per-team pools, including the 0 and -1 that mean "not rearmable" and "unlimited".
	ASSERT_EQ(env.rearm_pools.size(), 2u);
	EXPECT_EQ(env.rearm_pools[0].at("Subach HL-7"), 12);
	EXPECT_EQ(env.rearm_pools[0].at("Harpoon"), -1);
	EXPECT_EQ(env.rearm_pools[1].at("Harpoon"), 0);
	EXPECT_EQ(env.rearm_pools[1].count("Subach HL-7"), 0u);
}

// The built-in message budget, the training context and the reinforcement allowances all move as
// the mission runs, and all of them are reset by the reload.
TEST_F(CheckpointRoundTripTest, MissionStateSurvives)
{
	ASSERT_TRUE(checkpoint::checkpoint_write(makePopulated()));

	checkpoint::checkpoint_data read;
	ASSERT_TRUE(checkpoint::checkpoint_read(Slot(), read));

	const auto& mission = read.mission;

	EXPECT_TRUE(mission.present);

	EXPECT_EQ(mission.player_ints.at("warn_count"), 3);
	EXPECT_EQ(mission.player_ints.at("allow_praise_timestamp"), 45000);
	EXPECT_EQ(mission.training_ints.at("Training_context"), 2);
	EXPECT_EQ(mission.training_ints.at("Training_failure"), 1);
	EXPECT_EQ(mission.training_context_speed_timestamp, 7000);

	EXPECT_EQ(mission.used_personas, SCP_vector<SCP_string>({"Alpha Wingman", "Command"}));
	EXPECT_EQ(mission.mission_mood, 2);
	EXPECT_TRUE(mission.no_builtin_msgs);
	EXPECT_FALSE(mission.no_builtin_command);

	// A reinforcement with no uses left and one that is not yet available are both real states,
	// so neither may be dropped for looking like a default.
	ASSERT_EQ(mission.reinforcements.size(), 2u);
	EXPECT_EQ(mission.reinforcements[0].name, SCP_string("Delta"));
	EXPECT_EQ(mission.reinforcements[0].num_uses, 2);
	EXPECT_TRUE(mission.reinforcements[0].available);
	EXPECT_EQ(mission.reinforcements[1].name, SCP_string("Epsilon"));
	EXPECT_EQ(mission.reinforcements[1].num_uses, 0);
	EXPECT_FALSE(mission.reinforcements[1].available);
}
