/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#include "mission/checkpointfile.h"

#include "cfile/cfile.h"
#include "mission/missioncampaign.h"
#include "mission/missionparse.h"
#include "mod_table/mod_table.h"
#include "parse/encrypt.h"
#include "pilotfile/FileHandler.h"
#include "pilotfile/JSONFileHandler.h"
#include "playerman/player.h"

#include <memory>

// Defined in freespace.cpp; declared here rather than pulling in the whole freespace header,
// which is the same thing scripting.cpp does.
extern char Game_current_mission_filename[];

namespace {

// One component of a checkpoint's identity.
//
// Lowercased, so a pilot whose callsign is typed with different capitalisation still finds their
// own checkpoints, and nothing else is touched.  These strings only ever feed a hash now, so they
// do not need to be safe as filenames -- and mangling them the way a filename would require makes
// genuinely different people collide: with punctuation folded to underscores, "Joe Bloggs",
// "Joe_Bloggs" and "Joe-Bloggs" all become the same pilot and share one checkpoint.
SCP_string identity_key(const SCP_string& in)
{
	SCP_string out;
	out.reserve(in.size());

	for (char ch : in) {
		out += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
	}

	return out;
}

// As above, for a mission or campaign filename, whose extension is not part of its identity.
SCP_string base_name(const char* filename)
{
	SCP_string out(filename != nullptr ? filename : "");

	auto dot = out.rfind('.');
	if (dot != SCP_string::npos) {
		out.erase(dot);
	}

	return identity_key(out);
}

// ------------------------------------------------------------------
// Small helpers for the repetitive name/value maps
// ------------------------------------------------------------------

void write_string_list(pilot::FileHandler* handler, const char* name, const SCP_vector<SCP_string>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& value : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("v", value.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_string_list(pilot::FileHandler* handler, const char* name, SCP_vector<SCP_string>& values)
{
	values.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		values.push_back(handler->readStringOr("v", ""));
	}
	handler->endArrayRead();
}

// Name/value maps go out as an array of {k, v} objects.  An array rather than a JSON object
// keyed by the field name because the handler's read side iterates arrays but cannot enumerate
// the keys of an arbitrary object.
template <typename T, typename ReadFn>
void read_named_map(pilot::FileHandler* handler, const char* name, SCP_map<SCP_string, T>& values, ReadFn read)
{
	values.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		auto key = handler->readStringOr("k", "");
		if (key.empty()) {
			continue;
		}
		values[key] = read(handler);
	}
	handler->endArrayRead();
}

void write_int_map(pilot::FileHandler* handler, const char* name, const SCP_map<SCP_string, int>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& entry : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("k", entry.first.c_str());
		handler->writeInt("v", entry.second);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_int_map(pilot::FileHandler* handler, const char* name, SCP_map<SCP_string, int>& values)
{
	read_named_map<int>(handler, name, values, [](pilot::FileHandler* h) { return h->readIntOr("v", 0); });
}

void write_float_map(pilot::FileHandler* handler, const char* name, const SCP_map<SCP_string, float>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& entry : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("k", entry.first.c_str());
		handler->writeFloat("v", entry.second);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_float_map(pilot::FileHandler* handler, const char* name, SCP_map<SCP_string, float>& values)
{
	read_named_map<float>(handler, name, values, [](pilot::FileHandler* h) { return h->readFloatOr("v", 0.0f); });
}

void write_vec_map(pilot::FileHandler* handler, const char* name, const SCP_map<SCP_string, vec3d>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& entry : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("k", entry.first.c_str());
		handler->writeFloat("x", entry.second.xyz.x);
		handler->writeFloat("y", entry.second.xyz.y);
		handler->writeFloat("z", entry.second.xyz.z);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_vec_map(pilot::FileHandler* handler, const char* name, SCP_map<SCP_string, vec3d>& values)
{
	read_named_map<vec3d>(handler, name, values, [](pilot::FileHandler* h) {
		vec3d out;
		out.xyz.x = h->readFloatOr("x", 0.0f);
		out.xyz.y = h->readFloatOr("y", 0.0f);
		out.xyz.z = h->readFloatOr("z", 0.0f);
		return out;
	});
}

void write_vector(pilot::FileHandler* handler, const char* prefix_x, const char* prefix_y, const char* prefix_z,
                  const vec3d& value)
{
	handler->writeFloat(prefix_x, value.xyz.x);
	handler->writeFloat(prefix_y, value.xyz.y);
	handler->writeFloat(prefix_z, value.xyz.z);
}

void read_vector(pilot::FileHandler* handler, const char* prefix_x, const char* prefix_y, const char* prefix_z,
                 vec3d& value)
{
	value.xyz.x = handler->readFloatOr(prefix_x, value.xyz.x);
	value.xyz.y = handler->readFloatOr(prefix_y, value.xyz.y);
	value.xyz.z = handler->readFloatOr(prefix_z, value.xyz.z);
}

// ------------------------------------------------------------------
// Weapon banks
// ------------------------------------------------------------------

void write_weapon_banks(pilot::FileHandler* handler, const char* name, const SCP_vector<checkpoint::weapon_bank>& banks)
{
	handler->startArrayWrite(name, banks.size());
	for (const auto& bank : banks) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("class", bank.weapon_class.c_str());
		handler->writeInt("ammo", bank.ammo);
		handler->writeInt("start_ammo", bank.start_ammo);
		handler->writeInt("capacity", bank.capacity);
		handler->writeInt("next_slot", bank.next_slot);
		handler->writeInt("next_fire_stamp", bank.next_fire_stamp);
		handler->writeInt("last_fire_stamp", bank.last_fire_stamp);
		handler->writeInt("rearm_time", bank.rearm_time);
		handler->writeInt("burst_counter", bank.burst_counter);
		handler->writeInt("burst_seed", bank.burst_seed);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_weapon_banks(pilot::FileHandler* handler, const char* name, SCP_vector<checkpoint::weapon_bank>& banks)
{
	banks.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::weapon_bank bank;
		bank.weapon_class = handler->readStringOr("class", "");
		bank.ammo = handler->readIntOr("ammo", 0);
		bank.start_ammo = handler->readIntOr("start_ammo", 0);
		bank.capacity = handler->readIntOr("capacity", 0);
		bank.next_slot = handler->readIntOr("next_slot", 0);
		bank.next_fire_stamp = handler->readIntOr("next_fire_stamp", 0);
		bank.last_fire_stamp = handler->readIntOr("last_fire_stamp", 0);
		bank.rearm_time = handler->readIntOr("rearm_time", 0);
		bank.burst_counter = handler->readIntOr("burst_counter", 0);
		bank.burst_seed = handler->readIntOr("burst_seed", 0);
		banks.push_back(std::move(bank));
	}
	handler->endArrayRead();
}

void write_weapon_state(pilot::FileHandler* handler, const checkpoint::weapon_state& weapons)
{
	write_weapon_banks(handler, "primary_banks", weapons.primary_banks);
	write_weapon_banks(handler, "secondary_banks", weapons.secondary_banks);
	handler->writeString("tertiary_class", weapons.tertiary_class.c_str());
	// "weapon_flags", not "flags".  The weapon state is written flat into whatever object owns it
	// -- a ship or a turret subsystem -- and both of those already have a "flags" of their own,
	// which this would otherwise overwrite.  It nearly always would, too, since weapon flags are
	// usually empty.
	write_string_list(handler, "weapon_flags", weapons.flags);
	write_int_map(handler, "scalars", weapons.scalars);
}

void read_weapon_state(pilot::FileHandler* handler, checkpoint::weapon_state& weapons)
{
	read_weapon_banks(handler, "primary_banks", weapons.primary_banks);
	read_weapon_banks(handler, "secondary_banks", weapons.secondary_banks);
	weapons.tertiary_class = handler->readStringOr("tertiary_class", "");
	read_string_list(handler, "weapon_flags", weapons.flags);
	read_int_map(handler, "scalars", weapons.scalars);
}

// ------------------------------------------------------------------
// Ship dispositions, written by name so the file does not depend on enum order
// ------------------------------------------------------------------

const char* disposition_name(checkpoint::ShipDisposition disposition)
{
	switch (disposition) {
	case checkpoint::ShipDisposition::Present:
		return "present";
	case checkpoint::ShipDisposition::NotYetHere:
		return "not_yet_here";
	case checkpoint::ShipDisposition::Destroyed:
		return "destroyed";
	case checkpoint::ShipDisposition::Departed:
		return "departed";
	case checkpoint::ShipDisposition::Vanished:
		return "vanished";
	}
	return "present";
}

checkpoint::ShipDisposition disposition_value(const SCP_string& name)
{
	if (name == "not_yet_here") {
		return checkpoint::ShipDisposition::NotYetHere;
	}
	if (name == "destroyed") {
		return checkpoint::ShipDisposition::Destroyed;
	}
	if (name == "departed") {
		return checkpoint::ShipDisposition::Departed;
	}
	if (name == "vanished") {
		return checkpoint::ShipDisposition::Vanished;
	}
	return checkpoint::ShipDisposition::Present;
}

// ------------------------------------------------------------------
// Sections
// ------------------------------------------------------------------

void write_info(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointInfo);

	handler->writeString("slot", data.slot.c_str());
	handler->writeString("mission_filename", data.mission_filename.c_str());
	handler->writeString("mission_modified", data.mission_modified.c_str());
	handler->writeUInt("mission_fingerprint", data.mission_fingerprint);
	handler->writeString("campaign", data.campaign.c_str());
	handler->writeString("pilot", data.pilot.c_str());
	handler->writeString("mod_title", data.mod_title.c_str());

	handler->endSectionWrite();
}

void read_info(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.slot = handler->readStringOr("slot", "");
	data.mission_filename = handler->readStringOr("mission_filename", "");
	data.mission_modified = handler->readStringOr("mission_modified", "");
	data.mission_fingerprint = handler->readUIntOr("mission_fingerprint", 0);
	data.campaign = handler->readStringOr("campaign", "");
	data.pilot = handler->readStringOr("pilot", "");
	data.mod_title = handler->readStringOr("mod_title", "");
}

void write_clock(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointClock);

	handler->writeInt("mission_time", static_cast<std::int32_t>(data.mission_time));
	// A fix is 16.16 and the microsecond count does not fit in 32 bits for a long mission, so
	// it goes out as two halves.
	handler->writeUInt("mission_time_us_hi", static_cast<std::uint32_t>(data.mission_time_microseconds >> 32));
	handler->writeUInt("mission_time_us_lo", static_cast<std::uint32_t>(data.mission_time_microseconds & 0xFFFFFFFFu));
	handler->writeInt("hud_timer_padding", data.hud_timer_padding);
	handler->writeInt("saved_timestamp_ms", data.saved_timestamp_ms);

	handler->endSectionWrite();
}

void read_clock(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.mission_time = static_cast<fix>(handler->readIntOr("mission_time", 0));

	std::uint64_t hi = handler->readUIntOr("mission_time_us_hi", 0);
	std::uint64_t lo = handler->readUIntOr("mission_time_us_lo", 0);
	data.mission_time_microseconds = (hi << 32) | lo;

	data.hud_timer_padding = handler->readIntOr("hud_timer_padding", 0);

	// Absent in checkpoints written before stamps were translated.  Zero makes the restore treat
	// every saved stamp as belonging to the very start of the clock, which is the same
	// already-elapsed behaviour those older files got anyway.
	data.saved_timestamp_ms = handler->readIntOr("saved_timestamp_ms", 0);
}

void write_subsystems(pilot::FileHandler* handler, const SCP_vector<checkpoint::subsystem_state>& subsystems)
{
	handler->startArrayWrite("subsystems", subsystems.size());
	for (const auto& subsys : subsystems) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", subsys.name.c_str());
		handler->writeInt("ordinal", subsys.ordinal);
		handler->writeString("sub_name", subsys.sub_name.c_str());
		handler->writeString("cargo_title", subsys.cargo_title.c_str());
		handler->writeString("cargo", subsys.cargo.c_str());
		handler->writeBool("cargo_no_deplete", subsys.cargo_no_deplete);
		handler->writeString("turret_target", subsys.turret_target.c_str());

		write_string_list(handler, "flags", subsys.flags);
		write_float_map(handler, "floats", subsys.floats);
		write_int_map(handler, "ints", subsys.ints);

		handler->writeBool("has_weapons", subsys.has_weapons);
		if (subsys.has_weapons) {
			write_weapon_state(handler, subsys.weapons);
		}

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_subsystems(pilot::FileHandler* handler, SCP_vector<checkpoint::subsystem_state>& subsystems)
{
	subsystems.clear();

	if (!handler->hasField("subsystems")) {
		return;
	}

	auto count = handler->startArrayRead("subsystems");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::subsystem_state subsys;

		subsys.name = handler->readStringOr("name", "");
		subsys.ordinal = handler->readIntOr("ordinal", 0);
		subsys.sub_name = handler->readStringOr("sub_name", "");
		subsys.cargo_title = handler->readStringOr("cargo_title", "");
		subsys.cargo = handler->readStringOr("cargo", "");
		subsys.cargo_no_deplete = handler->readBoolOr("cargo_no_deplete", false);
		subsys.turret_target = handler->readStringOr("turret_target", "");

		read_string_list(handler, "flags", subsys.flags);
		read_float_map(handler, "floats", subsys.floats);
		read_int_map(handler, "ints", subsys.ints);

		subsys.has_weapons = handler->readBoolOr("has_weapons", false);
		if (subsys.has_weapons) {
			read_weapon_state(handler, subsys.weapons);
		}

		subsystems.push_back(std::move(subsys));
	}
	handler->endArrayRead();
}

void write_parse_objects(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startArrayWrite("parse_objects", data.parse_objects.size());
	for (const auto& p_obj : data.parse_objects) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", p_obj.name.c_str());
		handler->writeString("class", p_obj.ship_class.c_str());
		handler->writeString("team", p_obj.team.c_str());

		handler->writeString("arrival_anchor", p_obj.arrival_anchor.c_str());
		handler->writeString("departure_anchor", p_obj.departure_anchor.c_str());
		handler->writeInt("arrival_location", p_obj.arrival_location);
		handler->writeInt("departure_location", p_obj.departure_location);
		handler->writeInt("arrival_path_mask", p_obj.arrival_path_mask);
		handler->writeInt("departure_path_mask", p_obj.departure_path_mask);

		handler->writeInt("initial_hull", p_obj.initial_hull);
		handler->writeInt("initial_shields", p_obj.initial_shields);
		handler->writeInt("arrival_distance", p_obj.arrival_distance);
		handler->writeInt("arrival_delay", p_obj.arrival_delay);
		handler->writeInt("departure_delay", p_obj.departure_delay);
		handler->writeInt("escort_priority", p_obj.escort_priority);
		handler->writeInt("respawn_priority", p_obj.respawn_priority);
		handler->writeInt("alt_type_index", p_obj.alt_type_index);
		handler->writeInt("callsign_index", p_obj.callsign_index);
		handler->writeString("cargo", p_obj.cargo.c_str());
		handler->writeBool("cargo_no_deplete", p_obj.cargo_no_deplete);

		write_string_list(handler, "flags", p_obj.flags);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_parse_objects(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.parse_objects.clear();

	if (!handler->hasField("parse_objects")) {
		return;
	}

	auto count = handler->startArrayRead("parse_objects");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::parse_object_state p_obj;

		p_obj.name = handler->readStringOr("name", "");
		p_obj.ship_class = handler->readStringOr("class", "");
		p_obj.team = handler->readStringOr("team", "");

		p_obj.arrival_anchor = handler->readStringOr("arrival_anchor", "");
		p_obj.departure_anchor = handler->readStringOr("departure_anchor", "");
		p_obj.arrival_location = handler->readIntOr("arrival_location", 0);
		p_obj.departure_location = handler->readIntOr("departure_location", 0);
		p_obj.arrival_path_mask = handler->readIntOr("arrival_path_mask", 0);
		p_obj.departure_path_mask = handler->readIntOr("departure_path_mask", 0);

		p_obj.initial_hull = handler->readIntOr("initial_hull", 100);
		p_obj.initial_shields = handler->readIntOr("initial_shields", 100);
		p_obj.arrival_distance = handler->readIntOr("arrival_distance", 0);
		p_obj.arrival_delay = handler->readIntOr("arrival_delay", 0);
		p_obj.departure_delay = handler->readIntOr("departure_delay", 0);
		p_obj.escort_priority = handler->readIntOr("escort_priority", 0);
		p_obj.respawn_priority = handler->readIntOr("respawn_priority", 0);
		p_obj.alt_type_index = handler->readIntOr("alt_type_index", -1);
		p_obj.callsign_index = handler->readIntOr("callsign_index", -1);
		p_obj.cargo = handler->readStringOr("cargo", "");
		p_obj.cargo_no_deplete = handler->readBoolOr("cargo_no_deplete", false);

		read_string_list(handler, "flags", p_obj.flags);

		if (!p_obj.name.empty()) {
			data.parse_objects.push_back(std::move(p_obj));
		}
	}
	handler->endArrayRead();
}

void write_hotkeys(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startArrayWrite("hotkeys", data.hotkeys.size());
	for (const auto& set : data.hotkeys) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeInt("set", set.set);
		write_string_list(handler, "ships", set.ship_names);

		handler->startArrayWrite("how_added", set.how_added.size());
		for (int how : set.how_added) {
			handler->startSectionWrite(Section::Unnamed);
			handler->writeInt("v", how);
			handler->endSectionWrite();
		}
		handler->endArrayWrite();

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_hotkeys(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.hotkeys.clear();

	if (!handler->hasField("hotkeys")) {
		return;
	}

	auto count = handler->startArrayRead("hotkeys");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::hotkey_state set;

		set.set = handler->readIntOr("set", -1);
		read_string_list(handler, "ships", set.ship_names);

		if (handler->hasField("how_added")) {
			auto how_count = handler->startArrayRead("how_added");
			for (size_t j = 0; j < how_count; j++, handler->nextArraySection()) {
				set.how_added.push_back(handler->readIntOr("v", 0));
			}
			handler->endArrayRead();
		}

		if (set.set >= 0 && !set.ship_names.empty()) {
			data.hotkeys.push_back(std::move(set));
		}
	}
	handler->endArrayRead();
}

void write_ships(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointShips);

	handler->startArrayWrite("ships", data.ships.size());
	for (const auto& ship_data : data.ships) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", ship_data.name.c_str());
		handler->writeString("disposition", disposition_name(ship_data.disposition));

		if (ship_data.disposition == checkpoint::ShipDisposition::Present) {
			handler->writeString("class", ship_data.ship_class.c_str());
			handler->writeString("team", ship_data.team.c_str());
			handler->writeString("display_name", ship_data.display_name.c_str());
			handler->writeString("wing", ship_data.wing_name.c_str());
			handler->writeString("cargo_title", ship_data.cargo_title.c_str());
			handler->writeString("countermeasure_class", ship_data.countermeasure_class.c_str());
			handler->writeString("persona", ship_data.persona.c_str());
			handler->writeString("cargo", ship_data.cargo.c_str());
			handler->writeBool("cargo_no_deplete", ship_data.cargo_no_deplete);

			write_vector(handler, "pos_x", "pos_y", "pos_z", ship_data.pos);
			write_vector(handler, "fvec_x", "fvec_y", "fvec_z", ship_data.orient.vec.fvec);
			write_vector(handler, "uvec_x", "uvec_y", "uvec_z", ship_data.orient.vec.uvec);
			write_vector(handler, "rvec_x", "rvec_y", "rvec_z", ship_data.orient.vec.rvec);

			handler->writeFloat("hull", ship_data.hull);
			handler->writeFloat("max_hull", ship_data.max_hull);

			handler->startArrayWrite("shields", ship_data.shield_quadrants.size());
			for (float quadrant : ship_data.shield_quadrants) {
				handler->startSectionWrite(Section::Unnamed);
				handler->writeFloat("v", quadrant);
				handler->endSectionWrite();
			}
			handler->endArrayWrite();

			write_string_list(handler, "flags", ship_data.flags);
			write_string_list(handler, "object_flags", ship_data.object_flags);
			write_float_map(handler, "floats", ship_data.floats);
			write_int_map(handler, "ints", ship_data.ints);
			write_float_map(handler, "physics_floats", ship_data.physics_floats);
			write_vec_map(handler, "physics_vecs", ship_data.physics_vecs);

			write_subsystems(handler, ship_data.subsystems);
			write_weapon_state(handler, ship_data.weapons);
		} else {
			handler->writeInt("exit_time", static_cast<std::int32_t>(ship_data.exit_time));
		}

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	// Ships that have not arrived ride along in the same section; they are the same kind of thing
	// seen from the other side.
	write_parse_objects(handler, data);
	write_hotkeys(handler, data);

	handler->endSectionWrite();
}

void read_ships(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.ships.clear();

	// Parse objects share this section, so an absent ships array must not skip them.
	if (!handler->hasField("ships")) {
		read_parse_objects(handler, data);
		read_hotkeys(handler, data);
		return;
	}

	auto count = handler->startArrayRead("ships");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::ship_state ship_data;

		ship_data.name = handler->readStringOr("name", "");
		ship_data.disposition = disposition_value(handler->readStringOr("disposition", "present"));

		if (ship_data.disposition == checkpoint::ShipDisposition::Present) {
			ship_data.ship_class = handler->readStringOr("class", "");
			ship_data.team = handler->readStringOr("team", "");
			ship_data.display_name = handler->readStringOr("display_name", "");
			ship_data.wing_name = handler->readStringOr("wing", "");
			ship_data.cargo_title = handler->readStringOr("cargo_title", "");
			ship_data.countermeasure_class = handler->readStringOr("countermeasure_class", "");
			ship_data.persona = handler->readStringOr("persona", "");
			ship_data.cargo = handler->readStringOr("cargo", "");
			ship_data.cargo_no_deplete = handler->readBoolOr("cargo_no_deplete", false);

			read_vector(handler, "pos_x", "pos_y", "pos_z", ship_data.pos);
			read_vector(handler, "fvec_x", "fvec_y", "fvec_z", ship_data.orient.vec.fvec);
			read_vector(handler, "uvec_x", "uvec_y", "uvec_z", ship_data.orient.vec.uvec);
			read_vector(handler, "rvec_x", "rvec_y", "rvec_z", ship_data.orient.vec.rvec);

			ship_data.hull = handler->readFloatOr("hull", 0.0f);
			ship_data.max_hull = handler->readFloatOr("max_hull", 0.0f);

			if (handler->hasField("shields")) {
				auto quadrants = handler->startArrayRead("shields");
				for (size_t q = 0; q < quadrants; q++, handler->nextArraySection()) {
					ship_data.shield_quadrants.push_back(handler->readFloatOr("v", 0.0f));
				}
				handler->endArrayRead();
			}

			read_string_list(handler, "flags", ship_data.flags);
			read_string_list(handler, "object_flags", ship_data.object_flags);
			read_float_map(handler, "floats", ship_data.floats);
			read_int_map(handler, "ints", ship_data.ints);
			read_float_map(handler, "physics_floats", ship_data.physics_floats);
			read_vec_map(handler, "physics_vecs", ship_data.physics_vecs);

			read_subsystems(handler, ship_data.subsystems);
			read_weapon_state(handler, ship_data.weapons);
		} else {
			ship_data.exit_time = static_cast<fix>(handler->readIntOr("exit_time", 0));
		}

		data.ships.push_back(std::move(ship_data));
	}
	handler->endArrayRead();

	read_parse_objects(handler, data);
	read_hotkeys(handler, data);
}

// AI orders.  Shared between the AI section and the wings section, since a wing carries the same
// array of them.
void write_ai_goals(pilot::FileHandler* handler, const char* name, const SCP_vector<checkpoint::ai_goal_state>& goals)
{
	handler->startArrayWrite(name, goals.size());
	for (const auto& goal : goals) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("mode", goal.mode.c_str());
		handler->writeString("type", goal.type.c_str());
		handler->writeString("target_name", goal.target_name.c_str());
		handler->writeString("docker_point", goal.docker_point.c_str());
		handler->writeString("dockee_point", goal.dockee_point.c_str());
		handler->writeInt("signature", goal.signature);
		handler->writeInt("submode", goal.submode);
		handler->writeInt("priority", goal.priority);
		handler->writeInt("time", static_cast<std::int32_t>(goal.time));
		handler->writeInt("wp_list_index", goal.wp_list_index);
		handler->writeInt("int_data", goal.int_data);
		handler->writeFloat("float_data", goal.float_data);
		write_string_list(handler, "flags", goal.flags);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_ai_goals(pilot::FileHandler* handler, const char* name, SCP_vector<checkpoint::ai_goal_state>& goals)
{
	goals.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::ai_goal_state goal;

		goal.mode = handler->readStringOr("mode", "none");
		goal.type = handler->readStringOr("type", "invalid");
		goal.target_name = handler->readStringOr("target_name", "");
		goal.docker_point = handler->readStringOr("docker_point", "");
		goal.dockee_point = handler->readStringOr("dockee_point", "");
		goal.signature = handler->readIntOr("signature", -1);
		goal.submode = handler->readIntOr("submode", -1);
		goal.priority = handler->readIntOr("priority", 0);
		goal.time = static_cast<fix>(handler->readIntOr("time", 0));
		goal.wp_list_index = handler->readIntOr("wp_list_index", -1);
		goal.int_data = handler->readIntOr("int_data", 0);
		goal.float_data = handler->readFloatOr("float_data", 0.0f);
		read_string_list(handler, "flags", goal.flags);

		goals.push_back(std::move(goal));
	}
	handler->endArrayRead();
}

void write_wings(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointWings);

	handler->startArrayWrite("wings", data.wings.size());
	for (const auto& wing_data : data.wings) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", wing_data.name.c_str());
		handler->writeInt("time_gone", static_cast<std::int32_t>(wing_data.time_gone));
		handler->writeInt("wave_delay_timestamp", wing_data.wave_delay_timestamp);
		handler->writeString("display_name", wing_data.display_name.c_str());
		handler->writeString("arrival_anchor", wing_data.arrival_anchor.c_str());
		handler->writeString("departure_anchor", wing_data.departure_anchor.c_str());
		handler->writeInt("arrival_location", wing_data.arrival_location);
		handler->writeInt("departure_location", wing_data.departure_location);
		handler->writeInt("arrival_path_mask", wing_data.arrival_path_mask);
		handler->writeInt("departure_path_mask", wing_data.departure_path_mask);
		write_int_map(handler, "ints", wing_data.ints);
		write_string_list(handler, "ships", wing_data.ship_names);
		write_string_list(handler, "flags", wing_data.flags);
		write_ai_goals(handler, "goals", wing_data.goals);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	// SEXP variables ride along in this section rather than getting one of their own; they are
	// small and always wanted together with the rest of the mission's logical state.
	handler->startArrayWrite("variables", data.variables.size());
	for (const auto& var : data.variables) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", var.name.c_str());
		handler->writeBool("is_number", var.is_number);
		handler->writeString("value", var.value.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_wings(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.wings.clear();
	data.variables.clear();

	if (handler->hasField("wings")) {
		auto count = handler->startArrayRead("wings");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::wing_state wing_data;

			wing_data.name = handler->readStringOr("name", "");
			wing_data.time_gone = static_cast<fix>(handler->readIntOr("time_gone", 0));
			wing_data.wave_delay_timestamp = handler->readIntOr("wave_delay_timestamp", 0);
			wing_data.display_name = handler->readStringOr("display_name", "");
			wing_data.arrival_anchor = handler->readStringOr("arrival_anchor", "");
			wing_data.departure_anchor = handler->readStringOr("departure_anchor", "");
			wing_data.arrival_location = handler->readIntOr("arrival_location", 0);
			wing_data.departure_location = handler->readIntOr("departure_location", 0);
			wing_data.arrival_path_mask = handler->readIntOr("arrival_path_mask", 0);
			wing_data.departure_path_mask = handler->readIntOr("departure_path_mask", 0);
			read_int_map(handler, "ints", wing_data.ints);
			read_string_list(handler, "ships", wing_data.ship_names);
			read_string_list(handler, "flags", wing_data.flags);
			read_ai_goals(handler, "goals", wing_data.goals);

			data.wings.push_back(std::move(wing_data));
		}
		handler->endArrayRead();
	}

	if (handler->hasField("variables")) {
		auto count = handler->startArrayRead("variables");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::variable_state var;

			var.name = handler->readStringOr("name", "");
			var.is_number = handler->readBoolOr("is_number", false);
			var.value = handler->readStringOr("value", "");

			data.variables.push_back(std::move(var));
		}
		handler->endArrayRead();
	}
}

void write_events(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointEvents);

	handler->writeInt("goal_timestamp", data.goal_timestamp);

	handler->startArrayWrite("events", data.events.size());
	for (const auto& event : data.events) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", event.name.c_str());
		handler->writeInt("result", event.result);
		handler->writeInt("previous_result", event.previous_result);
		handler->writeInt("repeat_count", event.repeat_count);
		handler->writeInt("trigger_count", event.trigger_count);
		handler->writeInt("count", event.count);
		handler->writeInt("mission_log_flags", event.mission_log_flags);
		handler->writeInt("timestamp", event.timestamp);
		handler->writeInt("satisfied_time", event.satisfied_time);
		handler->writeInt("born_on_date", event.born_on_date);

		write_string_list(handler, "flags", event.flags);
		write_string_list(handler, "log_buffer", event.log_buffer);
		write_string_list(handler, "log_variable_buffer", event.log_variable_buffer);
		write_string_list(handler, "log_container_buffer", event.log_container_buffer);
		write_string_list(handler, "log_argument_buffer", event.log_argument_buffer);
		write_string_list(handler, "backup_log_buffer", event.backup_log_buffer);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_events(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.goal_timestamp = handler->readIntOr("goal_timestamp", 0);

	data.events.clear();

	if (!handler->hasField("events")) {
		return;
	}

	auto count = handler->startArrayRead("events");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::event_state event;

		event.name = handler->readStringOr("name", "");
		event.result = handler->readIntOr("result", 0);
		event.previous_result = handler->readIntOr("previous_result", 0);
		event.repeat_count = handler->readIntOr("repeat_count", 0);
		event.trigger_count = handler->readIntOr("trigger_count", 0);
		event.count = handler->readIntOr("count", 0);
		event.mission_log_flags = handler->readIntOr("mission_log_flags", 0);
		event.timestamp = handler->readIntOr("timestamp", -1);
		event.satisfied_time = handler->readIntOr("satisfied_time", -1);
		event.born_on_date = handler->readIntOr("born_on_date", -1);

		read_string_list(handler, "flags", event.flags);
		read_string_list(handler, "log_buffer", event.log_buffer);
		read_string_list(handler, "log_variable_buffer", event.log_variable_buffer);
		read_string_list(handler, "log_container_buffer", event.log_container_buffer);
		read_string_list(handler, "log_argument_buffer", event.log_argument_buffer);
		read_string_list(handler, "backup_log_buffer", event.backup_log_buffer);

		if (!event.name.empty()) {
			data.events.push_back(std::move(event));
		}
	}
	handler->endArrayRead();
}

void write_goals(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointGoals);

	handler->startArrayWrite("goals", data.goals.size());
	for (const auto& goal : data.goals) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", goal.name.c_str());
		handler->writeInt("satisfied", goal.satisfied);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_goals(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.goals.clear();

	if (!handler->hasField("goals")) {
		return;
	}

	auto count = handler->startArrayRead("goals");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::goal_state goal;

		goal.name = handler->readStringOr("name", "");
		goal.satisfied = handler->readIntOr("satisfied", 0);

		if (!goal.name.empty()) {
			data.goals.push_back(std::move(goal));
		}
	}
	handler->endArrayRead();
}

void write_log(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointLog);

	handler->startArrayWrite("entries", data.log_entries.size());
	for (const auto& entry : data.log_entries) {
		handler->startSectionWrite(Section::Unnamed);

		// LogType's values are written out explicitly in the enum, so they cannot shift when
		// somebody adds a new one and the raw number is safe to store.
		handler->writeInt("type", entry.type);
		handler->writeInt("flags", entry.flags);
		handler->writeInt("timestamp", static_cast<std::int32_t>(entry.timestamp));
		handler->writeInt("timer_padding", entry.timer_padding);
		handler->writeInt("index", entry.index);
		handler->writeString("primary_team", entry.primary_team.c_str());
		handler->writeString("secondary_team", entry.secondary_team.c_str());
		handler->writeString("pname", entry.pname.c_str());
		handler->writeString("sname", entry.sname.c_str());
		handler->writeString("pname_display", entry.pname_display.c_str());
		handler->writeString("sname_display", entry.sname_display.c_str());

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_log(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.log_entries.clear();

	if (!handler->hasField("entries")) {
		return;
	}

	auto count = handler->startArrayRead("entries");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::log_entry_state entry;

		entry.type = handler->readIntOr("type", 0);
		entry.flags = handler->readIntOr("flags", 0);
		entry.timestamp = static_cast<fix>(handler->readIntOr("timestamp", 0));
		entry.timer_padding = handler->readIntOr("timer_padding", 0);
		entry.index = handler->readIntOr("index", 0);
		entry.primary_team = handler->readStringOr("primary_team", "");
		entry.secondary_team = handler->readStringOr("secondary_team", "");
		entry.pname = handler->readStringOr("pname", "");
		entry.sname = handler->readStringOr("sname", "");
		entry.pname_display = handler->readStringOr("pname_display", "");
		entry.sname_display = handler->readStringOr("sname_display", "");

		if (entry.type != 0) {
			data.log_entries.push_back(std::move(entry));
		}
	}
	handler->endArrayRead();
}

void write_sexp(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointSexp);

	handler->startArrayWrite("nodes", data.sexp_nodes.size());
	for (const auto& node : data.sexp_nodes) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeInt("i", node.index);
		handler->writeInt("v", node.value);
		handler->writeInt("f", node.flags);
		if (!node.text.empty()) {
			handler->writeString("t", node.text.c_str());
		}
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("containers", data.containers.size());
	for (const auto& container : data.containers) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", container.name.c_str());
		write_string_list(handler, "list_data", container.list_data);
		write_string_list(handler, "map_keys", container.map_keys);
		write_string_list(handler, "map_values", container.map_values);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_sexp(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.sexp_nodes.clear();
	data.containers.clear();

	if (handler->hasField("nodes")) {
		auto count = handler->startArrayRead("nodes");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::sexp_node_state node;
			node.index = handler->readIntOr("i", -1);
			node.value = handler->readIntOr("v", 0);
			node.flags = handler->readIntOr("f", 0);
			node.text = handler->readStringOr("t", "");

			if (node.index >= 0) {
				data.sexp_nodes.push_back(node);
			}
		}
		handler->endArrayRead();
	}

	if (handler->hasField("containers")) {
		auto count = handler->startArrayRead("containers");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::container_state container;
			container.name = handler->readStringOr("name", "");
			read_string_list(handler, "list_data", container.list_data);
			read_string_list(handler, "map_keys", container.map_keys);
			read_string_list(handler, "map_values", container.map_values);

			if (!container.name.empty()) {
				data.containers.push_back(std::move(container));
			}
		}
		handler->endArrayRead();
	}
}

void write_ai(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointAI);

	handler->startArrayWrite("ai", data.ai.size());
	for (const auto& state : data.ai) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("ship", state.ship.c_str());
		handler->writeString("ai_class", state.ai_class.c_str());

		handler->writeString("target", state.target.c_str());
		handler->writeString("previous_target", state.previous_target.c_str());
		handler->writeString("goal_ship", state.goal_ship.c_str());
		handler->writeString("guard_ship", state.guard_ship.c_str());
		handler->writeString("ignore", state.ignore.c_str());
		handler->writeString("support_ship", state.support_ship.c_str());
		handler->writeString("hitter", state.hitter.c_str());
		handler->writeString("attacker", state.attacker.c_str());
		handler->writeString("artillery_target", state.artillery_target.c_str());

		handler->writeString("targeted_subsys_ship", state.targeted_subsys_ship.c_str());
		handler->writeString("targeted_subsys", state.targeted_subsys.c_str());
		handler->writeInt("targeted_subsys_ordinal", state.targeted_subsys_ordinal);
		handler->writeString("last_subsys_target_ship", state.last_subsys_target_ship.c_str());
		handler->writeString("last_subsys_target", state.last_subsys_target.c_str());
		handler->writeInt("last_subsys_target_ordinal", state.last_subsys_target_ordinal);

		write_string_list(handler, "flags", state.flags);
		write_string_list(handler, "override_flags", state.override_flags);
		write_string_list(handler, "ignore_new", state.ignore_new);
		write_int_map(handler, "ints", state.ints);
		write_float_map(handler, "floats", state.floats);
		write_vec_map(handler, "vecs", state.vecs);
		write_float_map(handler, "override_floats", state.override_floats);
		write_ai_goals(handler, "goals", state.goals);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_ai(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.ai.clear();

	if (!handler->hasField("ai")) {
		return;
	}

	auto count = handler->startArrayRead("ai");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::ai_state state;

		state.ship = handler->readStringOr("ship", "");
		state.ai_class = handler->readStringOr("ai_class", "");

		state.target = handler->readStringOr("target", "");
		state.previous_target = handler->readStringOr("previous_target", "");
		state.goal_ship = handler->readStringOr("goal_ship", "");
		state.guard_ship = handler->readStringOr("guard_ship", "");
		state.ignore = handler->readStringOr("ignore", "");
		state.support_ship = handler->readStringOr("support_ship", "");
		state.hitter = handler->readStringOr("hitter", "");
		state.attacker = handler->readStringOr("attacker", "");
		state.artillery_target = handler->readStringOr("artillery_target", "");

		state.targeted_subsys_ship = handler->readStringOr("targeted_subsys_ship", "");
		state.targeted_subsys = handler->readStringOr("targeted_subsys", "");
		state.targeted_subsys_ordinal = handler->readIntOr("targeted_subsys_ordinal", 0);
		state.last_subsys_target_ship = handler->readStringOr("last_subsys_target_ship", "");
		state.last_subsys_target = handler->readStringOr("last_subsys_target", "");
		state.last_subsys_target_ordinal = handler->readIntOr("last_subsys_target_ordinal", 0);

		read_string_list(handler, "flags", state.flags);
		read_string_list(handler, "override_flags", state.override_flags);
		read_string_list(handler, "ignore_new", state.ignore_new);
		read_int_map(handler, "ints", state.ints);
		read_float_map(handler, "floats", state.floats);
		read_vec_map(handler, "vecs", state.vecs);
		read_float_map(handler, "override_floats", state.override_floats);
		read_ai_goals(handler, "goals", state.goals);

		if (!state.ship.empty()) {
			data.ai.push_back(std::move(state));
		}
	}
	handler->endArrayRead();
}

void write_mission_state(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	const auto& state = data.mission;

	handler->startSectionWrite(Section::CheckpointMissionState);

	handler->writeInt("mission_mood", state.mission_mood);
	handler->writeBool("no_builtin_msgs", state.no_builtin_msgs);
	handler->writeBool("no_builtin_command", state.no_builtin_command);
	handler->writeInt("training_context_speed_timestamp", state.training_context_speed_timestamp);

	write_int_map(handler, "player", state.player_ints);
	write_int_map(handler, "training", state.training_ints);
	write_string_list(handler, "used_personas", state.used_personas);

	handler->startArrayWrite("reinforcements", state.reinforcements.size());
	for (const auto& reinforcement : state.reinforcements) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", reinforcement.name.c_str());
		handler->writeInt("num_uses", reinforcement.num_uses);
		handler->writeBool("available", reinforcement.available);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_mission_state(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	auto& state = data.mission;
	state = checkpoint::mission_state();

	// Same rule as the environment: reaching the section is what says the file describes this,
	// since zero built-in messages used is a real value rather than an absent one.
	state.present = true;

	state.mission_mood = handler->readIntOr("mission_mood", 0);
	state.no_builtin_msgs = handler->readBoolOr("no_builtin_msgs", false);
	state.no_builtin_command = handler->readBoolOr("no_builtin_command", false);
	state.training_context_speed_timestamp = handler->readIntOr("training_context_speed_timestamp", 0);

	read_int_map(handler, "player", state.player_ints);
	read_int_map(handler, "training", state.training_ints);
	read_string_list(handler, "used_personas", state.used_personas);

	if (handler->hasField("reinforcements")) {
		auto count = handler->startArrayRead("reinforcements");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::reinforcement_state reinforcement;
			reinforcement.name = handler->readStringOr("name", "");
			reinforcement.num_uses = handler->readIntOr("num_uses", 0);
			reinforcement.available = handler->readBoolOr("available", false);

			if (!reinforcement.name.empty()) {
				state.reinforcements.push_back(std::move(reinforcement));
			}
		}
		handler->endArrayRead();
	}
}

void write_environment(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	const auto& env = data.environment;

	handler->startSectionWrite(Section::CheckpointEnvironment);

	handler->writeString("skybox_model", env.skybox_model.c_str());
	handler->writeString("skybox_texture", env.skybox_texture.c_str());
	handler->writeUInt("skybox_flags_hi", env.skybox_flags_hi);
	handler->writeUInt("skybox_flags_lo", env.skybox_flags_lo);
	handler->writeFloat("skybox_alpha", env.skybox_alpha);
	write_vector(handler, "skybox_fvec_x", "skybox_fvec_y", "skybox_fvec_z", env.skybox_orient.vec.fvec);
	write_vector(handler, "skybox_uvec_x", "skybox_uvec_y", "skybox_uvec_z", env.skybox_orient.vec.uvec);
	write_vector(handler, "skybox_rvec_x", "skybox_rvec_y", "skybox_rvec_z", env.skybox_orient.vec.rvec);

	handler->writeInt("ambient_light", env.ambient_light);

	handler->writeBool("fullneb", env.fullneb);
	handler->writeFloat("neb_range", env.neb_range);
	handler->writeString("neb_pattern", env.neb_pattern.c_str());
	handler->writeBool("neb_fog_color_override", env.neb_fog_color_override);
	handler->writeInt("neb_fog_r", env.neb_fog_r);
	handler->writeInt("neb_fog_g", env.neb_fog_g);
	handler->writeInt("neb_fog_b", env.neb_fog_b);

	handler->writeBool("subspace", env.subspace);

	handler->writeInt("background_index", env.background_index);
	handler->writeBool("motion_debris_override", env.motion_debris_override);
	handler->writeString("motion_debris_type", env.motion_debris_type.c_str());
	handler->writeString("soundtrack", env.soundtrack.c_str());

	handler->writeBool("hud_draw", env.hud_draw);
	handler->writeBool("hud_disable_except_messages", env.hud_disable_except_messages);
	handler->writeInt("hud_max_targeting_range", env.hud_max_targeting_range);
	handler->writeInt("hud_display_warpout", env.hud_display_warpout);
	handler->writeInt("hud_timer_padding", env.hud_timer_padding);

	handler->writeString("support_ship_class", env.support_ship_class.c_str());
	handler->writeString("support_arrival_anchor", env.support_arrival_anchor.c_str());
	handler->writeString("support_departure_anchor", env.support_departure_anchor.c_str());
	handler->writeInt("support_arrival_location", env.support_arrival_location);
	handler->writeInt("support_departure_location", env.support_departure_location);
	handler->writeInt("support_max_ships", env.support_max_ships);
	handler->writeInt("support_max_concurrent", env.support_max_concurrent);
	handler->writeInt("support_tally", env.support_tally);
	handler->writeInt("support_available_for_species", env.support_available_for_species);
	handler->writeFloat("support_max_hull_repair", env.support_max_hull_repair);
	handler->writeFloat("support_max_subsys_repair", env.support_max_subsys_repair);
	handler->writeBool("support_disallow_rearm", env.support_disallow_rearm);

	handler->writeBool("no_traitor", env.no_traitor);
	handler->writeString("traitor_override", env.traitor_override.c_str());
	handler->writeString("debriefing_persona", env.debriefing_persona.c_str());
	handler->writeBool("asteroids_enabled", env.asteroids_enabled);
	handler->writeInt("current_nav", env.current_nav);

	handler->startArrayWrite("rearm_pool", env.rearm_pool.size());
	for (const auto& entry : env.rearm_pool) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeInt("team", entry.team);
		handler->writeString("weapon_class", entry.weapon_class.c_str());
		handler->writeInt("count", entry.count);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("navpoints", env.navpoints.size());
	for (const auto& nav : env.navpoints) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", nav.name.c_str());
		handler->writeInt("flags", nav.flags);
		handler->writeString("target", nav.target.c_str());
		handler->writeInt("waypoint_num", nav.waypoint_num);
		handler->writeInt("normal_r", nav.normal_color[0]);
		handler->writeInt("normal_g", nav.normal_color[1]);
		handler->writeInt("normal_b", nav.normal_color[2]);
		handler->writeInt("visited_r", nav.visited_color[0]);
		handler->writeInt("visited_g", nav.visited_color[1]);
		handler->writeInt("visited_b", nav.visited_color[2]);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("jump_nodes", env.jump_nodes.size());
	for (const auto& node : env.jump_nodes) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeInt("index", node.index);
		handler->writeString("name", node.name.c_str());
		handler->writeString("display_name", node.display_name.c_str());
		handler->writeString("model", node.model.c_str());
		handler->writeBool("hidden", node.hidden);
		handler->writeBool("colored", node.colored);
		handler->writeInt("color_r", node.color[0]);
		handler->writeInt("color_g", node.color[1]);
		handler->writeInt("color_b", node.color[2]);
		handler->writeInt("color_a", node.color[3]);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("starfield", env.starfield.size());
	for (const auto& entry : env.starfield) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("name", entry.name.c_str());
		handler->writeBool("is_sun", entry.is_sun);
		handler->writeFloat("scale_x", entry.scale_x);
		handler->writeFloat("scale_y", entry.scale_y);
		handler->writeInt("div_x", entry.div_x);
		handler->writeInt("div_y", entry.div_y);
		handler->writeFloat("ang_p", entry.ang.p);
		handler->writeFloat("ang_b", entry.ang.b);
		handler->writeFloat("ang_h", entry.ang.h);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_environment(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	auto& env = data.environment;
	env = checkpoint::environment_state();

	// Reaching this section at all is what says the file describes the sky; nothing here has a
	// meaningful "absent" value, so the flag rather than a per-field default is what stops an
	// older checkpoint from blanking it.
	env.present = true;

	env.skybox_model = handler->readStringOr("skybox_model", "");
	env.skybox_texture = handler->readStringOr("skybox_texture", "");
	env.skybox_flags_hi = handler->readUIntOr("skybox_flags_hi", 0);
	env.skybox_flags_lo = handler->readUIntOr("skybox_flags_lo", 0);
	env.skybox_alpha = handler->readFloatOr("skybox_alpha", 1.0f);
	read_vector(handler, "skybox_fvec_x", "skybox_fvec_y", "skybox_fvec_z", env.skybox_orient.vec.fvec);
	read_vector(handler, "skybox_uvec_x", "skybox_uvec_y", "skybox_uvec_z", env.skybox_orient.vec.uvec);
	read_vector(handler, "skybox_rvec_x", "skybox_rvec_y", "skybox_rvec_z", env.skybox_orient.vec.rvec);

	env.ambient_light = handler->readIntOr("ambient_light", 0);

	env.fullneb = handler->readBoolOr("fullneb", false);
	env.neb_range = handler->readFloatOr("neb_range", 0.0f);
	env.neb_pattern = handler->readStringOr("neb_pattern", "");
	env.neb_fog_color_override = handler->readBoolOr("neb_fog_color_override", false);
	env.neb_fog_r = handler->readIntOr("neb_fog_r", 0);
	env.neb_fog_g = handler->readIntOr("neb_fog_g", 0);
	env.neb_fog_b = handler->readIntOr("neb_fog_b", 0);

	env.subspace = handler->readBoolOr("subspace", false);

	env.background_index = handler->readIntOr("background_index", -1);
	env.motion_debris_override = handler->readBoolOr("motion_debris_override", false);
	env.motion_debris_type = handler->readStringOr("motion_debris_type", "");
	env.soundtrack = handler->readStringOr("soundtrack", "");

	env.hud_draw = handler->readBoolOr("hud_draw", true);
	env.hud_disable_except_messages = handler->readBoolOr("hud_disable_except_messages", false);
	env.hud_max_targeting_range = handler->readIntOr("hud_max_targeting_range", 0);
	env.hud_display_warpout = handler->readIntOr("hud_display_warpout", 0);
	env.hud_timer_padding = handler->readIntOr("hud_timer_padding", 0);

	env.support_ship_class = handler->readStringOr("support_ship_class", "");
	env.support_arrival_anchor = handler->readStringOr("support_arrival_anchor", "");
	env.support_departure_anchor = handler->readStringOr("support_departure_anchor", "");
	env.support_arrival_location = handler->readIntOr("support_arrival_location", 0);
	env.support_departure_location = handler->readIntOr("support_departure_location", 0);
	env.support_max_ships = handler->readIntOr("support_max_ships", 0);
	env.support_max_concurrent = handler->readIntOr("support_max_concurrent", 0);
	env.support_tally = handler->readIntOr("support_tally", 0);
	env.support_available_for_species = handler->readIntOr("support_available_for_species", 0);
	env.support_max_hull_repair = handler->readFloatOr("support_max_hull_repair", 0.0f);
	env.support_max_subsys_repair = handler->readFloatOr("support_max_subsys_repair", 0.0f);
	env.support_disallow_rearm = handler->readBoolOr("support_disallow_rearm", false);

	env.no_traitor = handler->readBoolOr("no_traitor", false);
	env.traitor_override = handler->readStringOr("traitor_override", "");
	env.debriefing_persona = handler->readStringOr("debriefing_persona", "");
	env.asteroids_enabled = handler->readBoolOr("asteroids_enabled", true);
	env.current_nav = handler->readIntOr("current_nav", -1);

	if (handler->hasField("rearm_pool")) {
		auto count = handler->startArrayRead("rearm_pool");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::rearm_pool_entry entry;
			entry.team = handler->readIntOr("team", 0);
			entry.weapon_class = handler->readStringOr("weapon_class", "");
			entry.count = handler->readIntOr("count", 0);

			if (!entry.weapon_class.empty()) {
				env.rearm_pool.push_back(std::move(entry));
			}
		}
		handler->endArrayRead();
	}

	if (handler->hasField("navpoints")) {
		auto count = handler->startArrayRead("navpoints");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::navpoint_state nav;
			nav.name = handler->readStringOr("name", "");
			nav.flags = handler->readIntOr("flags", 0);
			nav.target = handler->readStringOr("target", "");
			nav.waypoint_num = handler->readIntOr("waypoint_num", -1);
			nav.normal_color[0] = handler->readIntOr("normal_r", 0);
			nav.normal_color[1] = handler->readIntOr("normal_g", 0);
			nav.normal_color[2] = handler->readIntOr("normal_b", 0);
			nav.visited_color[0] = handler->readIntOr("visited_r", 0);
			nav.visited_color[1] = handler->readIntOr("visited_g", 0);
			nav.visited_color[2] = handler->readIntOr("visited_b", 0);

			env.navpoints.push_back(std::move(nav));
		}
		handler->endArrayRead();
	}

	if (handler->hasField("jump_nodes")) {
		auto count = handler->startArrayRead("jump_nodes");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::jump_node_state node;
			node.index = handler->readIntOr("index", -1);
			node.name = handler->readStringOr("name", "");
			node.display_name = handler->readStringOr("display_name", "");
			node.model = handler->readStringOr("model", "");
			node.hidden = handler->readBoolOr("hidden", false);
			node.colored = handler->readBoolOr("colored", false);
			node.color[0] = handler->readIntOr("color_r", 0);
			node.color[1] = handler->readIntOr("color_g", 0);
			node.color[2] = handler->readIntOr("color_b", 0);
			node.color[3] = handler->readIntOr("color_a", 0);

			if (node.index >= 0 && !node.name.empty()) {
				env.jump_nodes.push_back(std::move(node));
			}
		}
		handler->endArrayRead();
	}

	if (handler->hasField("starfield")) {
		auto count = handler->startArrayRead("starfield");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::starfield_entry_state entry;

			entry.name = handler->readStringOr("name", "");
			entry.is_sun = handler->readBoolOr("is_sun", false);
			entry.scale_x = handler->readFloatOr("scale_x", 1.0f);
			entry.scale_y = handler->readFloatOr("scale_y", 1.0f);
			entry.div_x = handler->readIntOr("div_x", 1);
			entry.div_y = handler->readIntOr("div_y", 1);
			entry.ang.p = handler->readFloatOr("ang_p", 0.0f);
			entry.ang.b = handler->readFloatOr("ang_b", 0.0f);
			entry.ang.h = handler->readFloatOr("ang_h", 0.0f);

			if (!entry.name.empty()) {
				env.starfield.push_back(std::move(entry));
			}
		}
		handler->endArrayRead();
	}
}

void write_animations(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointAnimations);

	handler->startArrayWrite("ships", data.animations.size());
	for (const auto& state : data.animations) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("ship", state.ship.c_str());

		handler->startArrayWrite("animations", state.animations.size());
		for (const auto& anim : state.animations) {
			handler->startSectionWrite(Section::Unnamed);

			handler->writeUInt("id", anim.id);
			handler->writeInt("state", anim.state);
			handler->writeInt("direction", anim.direction);
			handler->writeFloat("time", anim.time);
			handler->writeFloat("speed", anim.speed);
			write_string_list(handler, "instance_flags", anim.instance_flags);

			handler->endSectionWrite();
		}
		handler->endArrayWrite();

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_animations(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.animations.clear();

	if (!handler->hasField("ships")) {
		return;
	}

	auto ship_count = handler->startArrayRead("ships");
	for (size_t i = 0; i < ship_count; i++, handler->nextArraySection()) {
		checkpoint::ship_animation_state state;

		state.ship = handler->readStringOr("ship", "");

		if (handler->hasField("animations")) {
			auto count = handler->startArrayRead("animations");
			for (size_t j = 0; j < count; j++, handler->nextArraySection()) {
				checkpoint::animation_state anim;

				anim.id = handler->readUIntOr("id", 0);
				anim.state = handler->readIntOr("state", 0);
				anim.direction = handler->readIntOr("direction", 0);
				anim.time = handler->readFloatOr("time", 0.0f);
				anim.speed = handler->readFloatOr("speed", 1.0f);
				read_string_list(handler, "instance_flags", anim.instance_flags);

				if (anim.id != 0) {
					state.animations.push_back(std::move(anim));
				}
			}
			handler->endArrayRead();
		}

		if (!state.ship.empty() && !state.animations.empty()) {
			data.animations.push_back(std::move(state));
		}
	}
	handler->endArrayRead();
}

void write_docking(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointDocking);

	handler->startArrayWrite("pairs", data.dock_pairs.size());
	for (const auto& pair : data.dock_pairs) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("docker", pair.docker.c_str());
		handler->writeString("dockee", pair.dockee.c_str());
		handler->writeString("docker_point", pair.docker_point.c_str());
		handler->writeString("dockee_point", pair.dockee_point.c_str());

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_docking(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.dock_pairs.clear();

	if (!handler->hasField("pairs")) {
		return;
	}

	auto count = handler->startArrayRead("pairs");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::dock_pair pair;

		pair.docker = handler->readStringOr("docker", "");
		pair.dockee = handler->readStringOr("dockee", "");
		pair.docker_point = handler->readStringOr("docker_point", "");
		pair.dockee_point = handler->readStringOr("dockee_point", "");

		// A link missing any of its four names cannot be rebuilt, and guessing a bay would put
		// one ship inside another.
		if (!pair.docker.empty() && !pair.dockee.empty() && !pair.docker_point.empty() &&
			!pair.dockee_point.empty()) {
			data.dock_pairs.push_back(std::move(pair));
		}
	}
	handler->endArrayRead();
}

void write_debris(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointDebris);

	handler->startArrayWrite("debris", data.debris.size());
	for (const auto& piece : data.debris) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("class", piece.ship_class.c_str());
		handler->writeString("submodel", piece.submodel.c_str());
		handler->writeString("team", piece.team.c_str());
		handler->writeString("species", piece.species.c_str());
		handler->writeString("damage_type", piece.damage_type.c_str());

		write_vector(handler, "pos_x", "pos_y", "pos_z", piece.pos);
		write_vector(handler, "fvec_x", "fvec_y", "fvec_z", piece.orient.vec.fvec);
		write_vector(handler, "uvec_x", "uvec_y", "uvec_z", piece.orient.vec.uvec);
		write_vector(handler, "rvec_x", "rvec_y", "rvec_z", piece.orient.vec.rvec);
		write_vector(handler, "vel_x", "vel_y", "vel_z", piece.velocity);
		write_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", piece.rotational_velocity);

		handler->writeFloat("hull", piece.hull_strength);
		handler->writeFloat("max_hull", piece.max_hull);
		handler->writeFloat("lifeleft", piece.lifeleft);
		handler->writeFloat("damage_mult", piece.damage_mult);
		handler->writeInt("parent_alt_name", piece.parent_alt_name);
		handler->writeBool("do_not_expire", piece.do_not_expire);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_debris(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	data.debris.clear();

	if (!handler->hasField("debris")) {
		return;
	}

	auto count = handler->startArrayRead("debris");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::debris_state piece;

		piece.ship_class = handler->readStringOr("class", "");
		piece.submodel = handler->readStringOr("submodel", "");
		piece.team = handler->readStringOr("team", "");
		piece.species = handler->readStringOr("species", "");
		piece.damage_type = handler->readStringOr("damage_type", "");

		read_vector(handler, "pos_x", "pos_y", "pos_z", piece.pos);
		read_vector(handler, "fvec_x", "fvec_y", "fvec_z", piece.orient.vec.fvec);
		read_vector(handler, "uvec_x", "uvec_y", "uvec_z", piece.orient.vec.uvec);
		read_vector(handler, "rvec_x", "rvec_y", "rvec_z", piece.orient.vec.rvec);
		read_vector(handler, "vel_x", "vel_y", "vel_z", piece.velocity);
		read_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", piece.rotational_velocity);

		piece.hull_strength = handler->readFloatOr("hull", 0.0f);
		piece.max_hull = handler->readFloatOr("max_hull", 0.0f);
		piece.lifeleft = handler->readFloatOr("lifeleft", -1.0f);
		piece.damage_mult = handler->readFloatOr("damage_mult", 1.0f);
		piece.parent_alt_name = handler->readIntOr("parent_alt_name", -1);
		piece.do_not_expire = handler->readBoolOr("do_not_expire", false);

		if (!piece.ship_class.empty() && !piece.submodel.empty()) {
			data.debris.push_back(std::move(piece));
		}
	}
	handler->endArrayRead();
}

void write_scoring(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointScoring);

	write_int_map(handler, "ints", data.scoring.ints);
	write_int_map(handler, "class_kills", data.scoring.class_kills);

	handler->endSectionWrite();
}

void read_scoring(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	read_int_map(handler, "ints", data.scoring.ints);
	read_int_map(handler, "class_kills", data.scoring.class_kills);
}

// Read nothing but the Info section, for enumeration.  Stops as soon as it has what it came for
// rather than parsing every ship in the file.
bool checkpoint_peek_info(const SCP_string& filename, checkpoint::checkpoint_data& data)
{
	auto fp = cfopen(filename.c_str(), "rb", CF_TYPE_CHECKPOINTS, false,
	                 CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	if (fp == nullptr) {
		return false;
	}

	std::unique_ptr<pilot::FileHandler> handler;
	try {
		handler.reset(new pilot::JSONFileHandler(fp, true));
	} catch (const std::exception&) {
		// Not our file, or not valid JSON.  Enumeration walks whatever is in the directory, so
		// this is a perfectly ordinary thing to run into.
		return false;
	}

	if (handler->readUIntOr("signature", 0) != checkpoint::CHECKPOINT_FILE_ID) {
		return false;
	}

	bool found = false;

	handler->beginSectionRead();
	while (handler->hasMoreSections()) {
		if (handler->nextSection() == Section::CheckpointInfo) {
			read_info(handler.get(), data);
			found = true;
			break;
		}
	}
	handler->endSectionRead();

	return found;
}

// ------------------------------------------------------------------
// Identity and file naming
// ------------------------------------------------------------------
//
// A checkpoint is identified by pilot, campaign, mission and slot.  Spelling all four out in the
// filename is the obvious thing to do and is what this originally did, but it does not fit:
// MAX_FILENAME_LEN is 32 including the extension, and a real campaign blows straight past that
// ("mjnmixael", "between_the_ashes_2", "bta2_m3_04", "midway_point" is 55 characters before any
// separators).  Truncating any component would make distinct checkpoints collide.
//
// So the name is a hash and carries no meaning.  The identity it was built from is written into
// the file's Info section in full, which is what enumeration reads.

SCP_string checkpoint_identity(const SCP_string& mission_name, const SCP_string& slot)
{
	SCP_string identity;

	sprintf(identity,
	        "%s|%s|%s|%s",
	        identity_key(Player != nullptr ? Player->callsign : "").c_str(),
	        base_name(Campaign.filename).c_str(),
	        base_name(mission_name.empty() ? Game_current_mission_filename : mission_name.c_str()).c_str(),
	        identity_key(slot).c_str());

	return identity;
}

SCP_string checkpoint_filename_for(const SCP_string& mission_name, const SCP_string& slot)
{
	SCP_string filename;
	sprintf(filename, "chk_%08x.chk", hash_fnv1a(checkpoint_identity(mission_name, slot)));
	return filename;
}

struct found_checkpoint {
	SCP_string filename;
	SCP_string slot;
};

// Every checkpoint on disk belonging to this pilot, this campaign and the given mission.  Since
// the filenames are opaque, this has to open each one and read its Info section; there are only
// ever a handful, and they are small.
SCP_vector<found_checkpoint> checkpoint_find_files(const SCP_string& mission_name)
{
	SCP_vector<found_checkpoint> found;
	SCP_vector<SCP_string> files;

	cf_get_file_list(files, CF_TYPE_CHECKPOINTS, "*.chk", CF_SORT_NAME, nullptr,
	                 CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);

	SCP_string wanted_pilot = identity_key(Player != nullptr ? Player->callsign : "");
	SCP_string wanted_campaign = base_name(Campaign.filename);
	SCP_string wanted_mission =
		base_name(mission_name.empty() ? Game_current_mission_filename : mission_name.c_str());

	for (const auto& file : files) {
		// cfile strips the extension when building the list.
		SCP_string filename = file + ".chk";

		checkpoint::checkpoint_data info;
		if (!checkpoint_peek_info(filename, info)) {
			continue;
		}

		if (!lcase_equal(identity_key(info.pilot), wanted_pilot) ||
		    !lcase_equal(base_name(info.campaign.c_str()), wanted_campaign) ||
		    !lcase_equal(base_name(info.mission_filename.c_str()), wanted_mission)) {
			continue;
		}

		found.push_back({filename, info.slot});
	}

	return found;
}

} // namespace

namespace checkpoint {

SCP_string checkpoint_filename(const SCP_string& slot)
{
	return checkpoint_filename_for(SCP_string(), slot);
}

// A fingerprint that identifies the exact contents of a mission file, and keeps identifying them
// after the game has been closed and reopened.
//
// Current_file_checksum cannot be used for this, despite looking like exactly the right thing.
// It is netmisc_calc_checksum() over MISSION_CHECKSUM_SIZE bytes from the start of the mission
// struct (missionparse.cpp:7226), and that size was fixed when mission::name and mission::author
// were char arrays.  They are SCP_strings now, so what is actually being checksummed is two
// std::string objects -- heap pointers and small-string buffers.  Re-parsing the same mission
// inside one run usually lands on the same addresses, so it looks stable; a new process gives
// completely different numbers.  That is why a checkpoint used to reload fine in the session that
// wrote it and then be reported as missing after a restart.
//
// Checksum the mission file itself instead.  Cached, because this is asked on every entry to a
// mission and on every checkpoint-exists.
// Cache for the above.  Keyed on the filename, so it has to be thrown away whenever a mission is
// loaded: the same mission can be edited and reloaded inside one run of the game, and a cached
// fingerprint would go on insisting the file is what it used to be.
static SCP_string Fingerprint_cached_for;
static uint Fingerprint_cached_value = 0;

void checkpoint_invalidate_fingerprint()
{
	Fingerprint_cached_for.clear();
	Fingerprint_cached_value = 0;
}

uint checkpoint_mission_fingerprint(const SCP_string& mission_name)
{
	SCP_string filename(mission_name.empty() ? Game_current_mission_filename : mission_name.c_str());
	if (filename.empty()) {
		return 0;
	}

	// mission_load() strips the extension before storing the name (missionload.cpp:112) and warns
	// if it is given one, so what we have here is a bare mission name.  cfopen needs the real
	// filename.
	filename = cf_add_ext(filename.c_str(), FS_MISSION_FILE_EXT);

	if (filename == Fingerprint_cached_for) {
		return Fingerprint_cached_value;
	}

	uint checksum = 0;
	if (!cf_chksum_long(filename.c_str(), &checksum, -1, CF_TYPE_MISSIONS)) {
		mprintf(("CHECKPOINT => Could not fingerprint mission '%s'.\n", filename.c_str()));
		return 0;
	}

	Fingerprint_cached_for = filename;
	Fingerprint_cached_value = checksum;

	return checksum;
}

SCP_vector<SCP_string> checkpoint_list_slots(const SCP_string& mission_name)
{
	SCP_vector<SCP_string> slots;

	for (const auto& found : checkpoint_find_files(mission_name)) {
		slots.push_back(found.slot);
	}

	return slots;
}

int checkpoint_delete_all(const SCP_string& mission_name)
{
	int deleted = 0;

	for (const auto& found : checkpoint_find_files(mission_name)) {
		if (cf_delete(found.filename.c_str(), CF_TYPE_CHECKPOINTS, CF_LOCATION_ROOT_USER | CF_LOCATION_TYPE_ROOT)) {
			++deleted;
		}
	}

	mprintf(("CHECKPOINT => Deleted %d checkpoint(s) for '%s'.\n",
	         deleted,
	         mission_name.empty() ? Game_current_mission_filename : mission_name.c_str()));

	return deleted;
}

bool checkpoint_write(const checkpoint_data& data)
{
	auto filename = checkpoint_filename(data.slot);

	cf_create_directory(CF_TYPE_CHECKPOINTS);

	auto fp = cfopen(filename.c_str(), "wb", CF_TYPE_CHECKPOINTS, false,
	                 CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	if (fp == nullptr) {
		mprintf(("CHECKPOINT => Unable to open '%s' for writing!\n", filename.c_str()));
		return false;
	}

	// The handler takes ownership of the file and closes it in its destructor.
	std::unique_ptr<pilot::FileHandler> handler(new pilot::JSONFileHandler(fp, false));

	handler->writeUInt("signature", CHECKPOINT_FILE_ID);
	handler->writeUInt("version", CHECKPOINT_VERSION);

	handler->beginWritingSections();

	write_info(handler.get(), data);
	write_clock(handler.get(), data);
	write_ships(handler.get(), data);
	write_wings(handler.get(), data);
	write_scoring(handler.get(), data);
	write_events(handler.get(), data);
	write_goals(handler.get(), data);
	write_log(handler.get(), data);
	write_sexp(handler.get(), data);
	write_debris(handler.get(), data);
	write_docking(handler.get(), data);
	write_ai(handler.get(), data);
	write_animations(handler.get(), data);
	write_environment(handler.get(), data);
	write_mission_state(handler.get(), data);

	handler->endWritingSections();

	handler->flush();

	mprintf(("CHECKPOINT => Wrote '%s' (%d ships, %d wings, %d variables, %d events, %d goals, %d log entries, "
	         "%d debris, %d pending arrivals, %d docked pairs, %d AI states, %d animated ships)\n",
	         filename.c_str(),
	         static_cast<int>(data.ships.size()),
	         static_cast<int>(data.wings.size()),
	         static_cast<int>(data.variables.size()),
	         static_cast<int>(data.events.size()),
	         static_cast<int>(data.goals.size()),
	         static_cast<int>(data.log_entries.size()),
	         static_cast<int>(data.debris.size()),
	         static_cast<int>(data.parse_objects.size()),
	         static_cast<int>(data.dock_pairs.size()),
	         static_cast<int>(data.ai.size()),
	         static_cast<int>(data.animations.size())));

	return true;
}

bool checkpoint_read(const SCP_string& slot, checkpoint_data& data)
{
	data = checkpoint_data();

	auto filename = checkpoint_filename(slot);

	auto fp = cfopen(filename.c_str(), "rb", CF_TYPE_CHECKPOINTS, false,
	                 CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	if (fp == nullptr) {
		mprintf(("CHECKPOINT => No checkpoint '%s'.\n", filename.c_str()));
		return false;
	}

	std::unique_ptr<pilot::FileHandler> handler;
	try {
		handler.reset(new pilot::JSONFileHandler(fp, true));
	} catch (const std::exception& e) {
		mprintf(("CHECKPOINT => Failed to parse '%s': %s\n", filename.c_str(), e.what()));
		return false;
	}

	if (handler->readUIntOr("signature", 0) != CHECKPOINT_FILE_ID) {
		mprintf(("CHECKPOINT => '%s' is not a checkpoint file!\n", filename.c_str()));
		return false;
	}

	data.version = static_cast<int>(handler->readUIntOr("version", 0));
	if (data.version > static_cast<int>(CHECKPOINT_VERSION)) {
		// Newer files may be structured in ways this build cannot interpret.  Individual
		// unknown fields and sections are fine, but a structural bump is not.
		mprintf(("CHECKPOINT => '%s' was written by a newer version (%d > %d); ignoring it.\n",
		         filename.c_str(),
		         data.version,
		         CHECKPOINT_VERSION));
		return false;
	}

	handler->beginSectionRead();
	while (handler->hasMoreSections()) {
		auto section_id = handler->nextSection();

		switch (section_id) {
		case Section::CheckpointInfo:
			read_info(handler.get(), data);
			break;

		case Section::CheckpointClock:
			read_clock(handler.get(), data);
			break;

		case Section::CheckpointShips:
			read_ships(handler.get(), data);
			break;

		case Section::CheckpointWings:
			read_wings(handler.get(), data);
			break;

		case Section::CheckpointScoring:
			read_scoring(handler.get(), data);
			break;

		case Section::CheckpointEvents:
			read_events(handler.get(), data);
			break;

		case Section::CheckpointGoals:
			read_goals(handler.get(), data);
			break;

		case Section::CheckpointLog:
			read_log(handler.get(), data);
			break;

		case Section::CheckpointSexp:
			read_sexp(handler.get(), data);
			break;

		case Section::CheckpointDebris:
			read_debris(handler.get(), data);
			break;

		case Section::CheckpointDocking:
			read_docking(handler.get(), data);
			break;

		case Section::CheckpointAI:
			read_ai(handler.get(), data);
			break;

		case Section::CheckpointAnimations:
			read_animations(handler.get(), data);
			break;

		case Section::CheckpointEnvironment:
			read_environment(handler.get(), data);
			break;

		case Section::CheckpointMissionState:
			read_mission_state(handler.get(), data);
			break;

		default:
			// A section this build does not know about -- most likely written by a newer
			// engine.  Skipping it is the whole point of the sectioned layout.
			mprintf(("CHECKPOINT => Skipping unknown section 0x%04x.\n", static_cast<int>(section_id)));
			break;
		}
	}
	handler->endSectionRead();

	data.slot = slot;
	data.loaded = true;

	mprintf(("CHECKPOINT => Read '%s' (%d ships, %d wings, %d variables)\n",
	         filename.c_str(),
	         static_cast<int>(data.ships.size()),
	         static_cast<int>(data.wings.size()),
	         static_cast<int>(data.variables.size())));

	return true;
}

bool checkpoint_matches_current_mission(const checkpoint_data& data)
{
	// The filename is a hash of pilot, campaign, mission and slot, so opening the right file is
	// normally proof enough of all four.  Check the two the file records anyway: a hash is not a
	// guarantee, and being handed another pilot's saved game is a bad way to find that out.
	if (!lcase_equal(identity_key(data.pilot), identity_key(Player != nullptr ? Player->callsign : ""))) {
		mprintf(("CHECKPOINT => '%s' belongs to pilot '%s', not '%s'.\n",
		         data.slot.c_str(),
		         data.pilot.c_str(),
		         Player != nullptr ? Player->callsign : ""));
		return false;
	}

	if (!lcase_equal(base_name(data.campaign.c_str()), base_name(Campaign.filename))) {
		mprintf(("CHECKPOINT => '%s' belongs to campaign '%s', not '%s'.\n",
		         data.slot.c_str(),
		         data.campaign.c_str(),
		         Campaign.filename));
		return false;
	}

	if (stricmp(data.mission_filename.c_str(), Game_current_mission_filename) != 0) {
		return false;
	}

	// The fingerprint is the real test -- it changes whenever the mission file does, and an
	// edited mission invalidates the SEXP node indices the checkpoint depends on.
	//
	// A zero fingerprint means the checkpoint was written by a build that could not read the
	// mission file, so there is nothing to compare against and the checkpoint is accepted.  Say so
	// rather than silently waving it through: this failing quietly is exactly how the check came
	// to be skipped for every checkpoint without anyone noticing.
	if (data.mission_fingerprint == 0) {
		mprintf(("CHECKPOINT => '%s' carries no mission fingerprint, so it cannot be checked against "
		         "the current mission.  Accepting it.\n",
		         data.slot.c_str()));
		return true;
	}

	if (data.mission_fingerprint != checkpoint_mission_fingerprint(SCP_string())) {
		return false;
	}

	return true;
}

void checkpoint_delete_file(const SCP_string& slot)
{
	auto filename = checkpoint_filename(slot);

	cf_delete(filename.c_str(), CF_TYPE_CHECKPOINTS, CF_LOCATION_ROOT_USER | CF_LOCATION_TYPE_ROOT);
}

} // namespace checkpoint
