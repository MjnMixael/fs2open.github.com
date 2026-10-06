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

void write_iff_colors(pilot::FileHandler* handler, const char* name, const SCP_vector<checkpoint::iff_color_state>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& entry : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("observer", entry.observer.c_str());
		handler->writeString("observed", entry.observed.c_str());
		handler->writeInt("r", entry.r);
		handler->writeInt("g", entry.g);
		handler->writeInt("b", entry.b);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_iff_colors(pilot::FileHandler* handler, const char* name, SCP_vector<checkpoint::iff_color_state>& values)
{
	values.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::iff_color_state entry;
		entry.observer = handler->readStringOr("observer", "");
		entry.observed = handler->readStringOr("observed", "");
		entry.r = handler->readIntOr("r", 0);
		entry.g = handler->readIntOr("g", 0);
		entry.b = handler->readIntOr("b", 0);
		values.push_back(std::move(entry));
	}
	handler->endArrayRead();
}

void write_override_list(pilot::FileHandler* handler, const char* name, const SCP_vector<checkpoint::damage_type_override_state>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& value : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("subject", value.subject.c_str());
		handler->writeString("damage_type", value.damage_type.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_override_list(pilot::FileHandler* handler, const char* name, SCP_vector<checkpoint::damage_type_override_state>& values)
{
	values.clear();
	if (!handler->hasField(name)) {
		return;
	}
	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::damage_type_override_state value;
		value.subject = handler->readStringOr("subject", "");
		value.damage_type = handler->readStringOr("damage_type", "");
		if (!value.subject.empty()) {
			values.push_back(std::move(value));
		}
	}
	handler->endArrayRead();
}

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

void write_float_list(pilot::FileHandler* handler, const char* name, const SCP_vector<float>& values)
{
	handler->startArrayWrite(name, values.size());
	for (float value : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeFloat("v", value);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_float_list(pilot::FileHandler* handler, const char* name, SCP_vector<float>& values)
{
	values.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		values.push_back(handler->readFloatOr("v", 0.0f));
	}
	handler->endArrayRead();
}

void write_int_list(pilot::FileHandler* handler, const char* name, const SCP_vector<int>& values)
{
	handler->startArrayWrite(name, values.size());
	for (int value : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeInt("v", value);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_int_list(pilot::FileHandler* handler, const char* name, SCP_vector<int>& values)
{
	values.clear();

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		values.push_back(handler->readIntOr("v", 0));
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

void write_string_map(pilot::FileHandler* handler, const char* name, const SCP_map<SCP_string, SCP_string>& values)
{
	handler->startArrayWrite(name, values.size());
	for (const auto& entry : values) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("k", entry.first.c_str());
		handler->writeString("v", entry.second.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_string_map(pilot::FileHandler* handler, const char* name, SCP_map<SCP_string, SCP_string>& values)
{
	read_named_map<SCP_string>(handler, name, values, [](pilot::FileHandler* h) { return h->readStringOr("v", ""); });
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

void write_parse_subsystems(pilot::FileHandler* handler, const SCP_vector<checkpoint::parse_subsys_state>& subsystems)
{
	handler->startArrayWrite("parse_subsystems", subsystems.size());
	for (const auto& sub : subsystems) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", sub.name.c_str());
		handler->writeFloat("percent", sub.percent);
		handler->writeString("ai_class", sub.ai_class.c_str());
		handler->writeString("cargo", sub.cargo.c_str());
		handler->writeString("cargo_title", sub.cargo_title.c_str());
		write_string_list(handler, "primary_banks", sub.primary_banks);
		write_int_list(handler, "primary_ammo", sub.primary_ammo);
		write_string_list(handler, "secondary_banks", sub.secondary_banks);
		write_int_list(handler, "secondary_ammo", sub.secondary_ammo);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_parse_subsystems(pilot::FileHandler* handler, SCP_vector<checkpoint::parse_subsys_state>& subsystems)
{
	subsystems.clear();

	if (!handler->hasField("parse_subsystems")) {
		return;
	}

	auto count = handler->startArrayRead("parse_subsystems");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::parse_subsys_state sub;
		sub.name = handler->readStringOr("name", "");
		sub.percent = handler->readFloatOr("percent", 0.0f);
		sub.ai_class = handler->readStringOr("ai_class", "");
		sub.cargo = handler->readStringOr("cargo", "");
		sub.cargo_title = handler->readStringOr("cargo_title", "");
		read_string_list(handler, "primary_banks", sub.primary_banks);
		read_int_list(handler, "primary_ammo", sub.primary_ammo);
		read_string_list(handler, "secondary_banks", sub.secondary_banks);
		read_int_list(handler, "secondary_ammo", sub.secondary_ammo);
		subsystems.push_back(std::move(sub));
	}
	handler->endArrayRead();
}

// The world that is not made of ships: the sky, the nebula, the music, the HUD toggles, the
// support ship settings, the nav points and the jump nodes.
void write_world(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	const auto& env = data.environment;

	handler->startSectionWrite(Section::CheckpointWorld);

	handler->writeBool("present", env.present);

	handler->writeString("skybox_model", env.skybox_model.c_str());
	handler->writeString("skybox_texture", env.skybox_texture.c_str());
	handler->writeUInt("skybox_flags_hi", env.skybox_flags_hi);
	handler->writeUInt("skybox_flags_lo", env.skybox_flags_lo);
	handler->writeFloat("skybox_alpha", env.skybox_alpha);
	write_vector(handler, "sky_fvec_x", "sky_fvec_y", "sky_fvec_z", env.skybox_orient.vec.fvec);
	write_vector(handler, "sky_uvec_x", "sky_uvec_y", "sky_uvec_z", env.skybox_orient.vec.uvec);
	write_vector(handler, "sky_rvec_x", "sky_rvec_y", "sky_rvec_z", env.skybox_orient.vec.rvec);

	handler->writeInt("ambient_light", env.ambient_light);

	handler->writeBool("fullneb", env.fullneb);
	handler->writeFloat("neb_range", env.neb_range);
	handler->writeString("neb_pattern", env.neb_pattern.c_str());
	handler->writeBool("neb_fog_override", env.neb_fog_color_override);
	handler->writeInt("neb_fog_r", env.neb_fog_r);
	handler->writeInt("neb_fog_g", env.neb_fog_g);
	handler->writeInt("neb_fog_b", env.neb_fog_b);

	handler->writeBool("subspace", env.subspace);

	handler->writeInt("background_index", env.background_index);
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

	handler->writeBool("motion_debris_override", env.motion_debris_override);
	handler->writeString("motion_debris_type", env.motion_debris_type.c_str());

	handler->writeString("soundtrack", env.soundtrack.c_str());
	handler->writeBool("music_battle_started", env.music_battle_started);

	handler->writeBool("hud_draw", env.hud_draw);
	handler->writeBool("hud_only_messages", env.hud_disable_except_messages);
	handler->writeInt("hud_max_range", env.hud_max_targeting_range);
	handler->writeInt("hud_warpout", env.hud_display_warpout);

	handler->writeString("support_class", env.support_ship_class.c_str());
	handler->writeString("support_arrival", env.support_arrival_location.c_str());
	handler->writeString("support_departure", env.support_departure_location.c_str());
	handler->writeString("support_arrival_anchor", env.support_arrival_anchor.c_str());
	handler->writeString("support_departure_anchor", env.support_departure_anchor.c_str());
	handler->writeInt("support_max", env.support_max_ships);
	handler->writeInt("support_max_concurrent", env.support_max_concurrent);
	handler->writeInt("support_tally", env.support_tally);
	handler->writeInt("support_species", env.support_available_for_species);
	handler->writeFloat("support_hull_repair", env.support_max_hull_repair);
	handler->writeFloat("support_subsys_repair", env.support_max_subsys_repair);
	handler->writeBool("support_disallow_rearm", env.support_disallow_rearm);
	write_string_list(handler, "support_incoming_for", env.support_incoming_for);

	// One entry per team, each holding that team's weapon class -> rounds left map.
	handler->startArrayWrite("rearm_pools", env.rearm_pools.size());
	for (const auto& pool : env.rearm_pools) {
		handler->startSectionWrite(Section::Unnamed);
		write_int_map(handler, "pool", pool);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->writeBool("no_traitor", env.no_traitor);
	handler->writeString("traitor_override", env.traitor_override.c_str());
	handler->writeString("debriefing_persona", env.debriefing_persona.c_str());

	handler->writeBool("asteroids_enabled", env.asteroids_enabled);

	handler->writeBool("has_asteroid_field", env.has_asteroid_field);
	handler->writeInt("asteroid_field_num_initial", env.asteroid_field_num_initial);
	handler->writeBool("asteroid_field_active", env.asteroid_field_active);
	handler->writeBool("asteroid_field_is_debris", env.asteroid_field_is_debris);
	handler->writeBool("asteroid_field_enhanced_checks", env.asteroid_field_enhanced_checks);
	handler->writeBool("asteroid_field_has_inner_bound", env.asteroid_field_has_inner_bound);
	handler->writeFloat("asteroid_field_speed", env.asteroid_field_speed);
	handler->writeFloat("asteroid_field_bound_rad", env.asteroid_field_bound_rad);
	write_vector(handler, "asteroid_field_vel_x", "asteroid_field_vel_y", "asteroid_field_vel_z", env.asteroid_field_vel);
	write_vector(handler, "asteroid_field_min_x", "asteroid_field_min_y", "asteroid_field_min_z", env.asteroid_field_min);
	write_vector(handler, "asteroid_field_max_x", "asteroid_field_max_y", "asteroid_field_max_z", env.asteroid_field_max);
	write_vector(handler, "asteroid_field_inner_min_x", "asteroid_field_inner_min_y", "asteroid_field_inner_min_z",
	             env.asteroid_field_inner_min);
	write_vector(handler, "asteroid_field_inner_max_x", "asteroid_field_inner_max_y", "asteroid_field_inner_max_z",
	             env.asteroid_field_inner_max);
	write_string_list(handler, "asteroid_field_asteroid_types", env.asteroid_field_asteroid_types);
	write_string_list(handler, "asteroid_field_debris_types", env.asteroid_field_debris_types);
	write_string_list(handler, "asteroid_field_targets", env.asteroid_field_targets);

	handler->writeInt("supernova_stage", env.supernova_stage);
	handler->writeFloat("supernova_total", env.supernova_total);
	handler->writeFloat("supernova_left", env.supernova_left);
	handler->writeFloat("time_compression", env.time_compression);
	handler->writeBool("time_compression_locked", env.time_compression_locked);

	handler->writeInt("current_nav", env.current_nav);
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
		handler->writeBool("show_polys", node.show_polys);
		handler->writeBool("has_pos", node.has_pos);
		write_vector(handler, "pos_x", "pos_y", "pos_z", node.pos);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	write_string_list(handler, "squadron_wings", env.squadron_wings);

	handler->writeBool("effects_present", env.effects_present);
	write_vector(handler, "gravity_x", "gravity_y", "gravity_z", env.gravity);
	handler->writeString("storm", env.storm.c_str());

	handler->startArrayWrite("poofs", env.poofs.size());
	for (const auto& poof : env.poofs) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", poof.name.c_str());
		handler->writeBool("enabled", poof.enabled);
		handler->writeInt("fade_start", poof.fade_start);
		handler->writeInt("fade_duration", poof.fade_duration);
		handler->writeBool("fade_in", poof.fade_in);
		handler->writeFloat("fade_multiplier", poof.fade_multiplier);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->writeBool("has_volumetrics", env.has_volumetrics);
	handler->writeBool("volumetrics_enabled", env.volumetrics_enabled);
	handler->writeFloat("fog_near_distance", env.fog_near_distance);
	handler->writeFloat("fog_1000m_visibility", env.fog_1000m_visibility);
	handler->writeFloat("fog_skybox_clip_distance", env.fog_skybox_clip_distance);
	handler->writeFloat("fog_clip_distance", env.fog_clip_distance);

	handler->startArrayWrite("post_effects", env.post_effects.size());
	for (const auto& effect : env.post_effects) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", effect.name.c_str());
		handler->writeFloat("intensity", effect.intensity);
		write_vector(handler, "r", "g", "b", effect.rgb);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
	handler->writeBool("lightshafts_on", env.lightshafts_on);
	handler->writeFloat("lightshafts_intensity", env.lightshafts_intensity);

	handler->writeString("sound_env_preset", env.sound_env_preset.c_str());
	handler->writeFloat("sound_env_volume", env.sound_env_volume);
	handler->writeFloat("sound_env_damping", env.sound_env_damping);
	handler->writeFloat("sound_env_decay", env.sound_env_decay);

	handler->writeFloat("beam_friendly_damage_cap", env.beam_friendly_damage_cap);
	handler->writeFloat("weapon_friendly_damage_cap", env.weapon_friendly_damage_cap);
	handler->writeFloat("weapon_self_damage_cap", env.weapon_self_damage_cap);

	write_override_list(handler, "weapon_damage_types", env.weapon_damage_types);
	write_override_list(handler, "weapon_shockwave_damage_types", env.weapon_shockwave_damage_types);
	write_override_list(handler, "ship_shockwave_damage_types", env.ship_shockwave_damage_types);
	write_override_list(handler, "asteroid_damage_types", env.asteroid_damage_types);

	write_string_list(handler, "mission_music", env.mission_music);

	handler->startArrayWrite("coordinate_points", env.coordinate_points.size());
	for (const auto& point : env.coordinate_points) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", point.name.c_str());
		handler->writeString("group", point.group.c_str());
		write_vector(handler, "pos_x", "pos_y", "pos_z", point.pos);
		handler->writeInt("escort_priority", point.escort_priority);
		handler->writeInt("multi_team", point.multi_team);
		handler->writeBool("visible", point.visible);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->writeBool("shudder_perpetual", env.shudder_perpetual);
	handler->writeBool("shudder_everywhere", env.shudder_everywhere);
	handler->writeInt("shudder_time", env.shudder_time);
	handler->writeInt("shudder_total", env.shudder_total);
	handler->writeFloat("shudder_intensity", env.shudder_intensity);

	handler->writeBool("photo_mode_allowed", env.photo_mode_allowed);
	handler->writeBool("toggle_debriefing", env.toggle_debriefing);
	handler->writeBool("deactivate_autopilot", env.deactivate_autopilot);
	handler->writeBool("use_autopilot_cinematics", env.use_autopilot_cinematics);

	handler->startArrayWrite("waypoint_lists", data.waypoint_lists.size());
	for (const auto& list : data.waypoint_lists) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", list.name.c_str());
		handler->startArrayWrite("points", list.points.size());
		for (const auto& point : list.points) {
			handler->startSectionWrite(Section::Unnamed);
			write_vector(handler, "x", "y", "z", point);
			handler->endSectionWrite();
		}
		handler->endArrayWrite();
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("props", data.props.size());
	for (const auto& state : data.props) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", state.name.c_str());
		handler->writeString("disposition", disposition_name(state.disposition));
		handler->writeBool("no_parse_prop", state.no_parse_prop);
		if (state.disposition == checkpoint::ShipDisposition::Present) {
			handler->writeString("class", state.prop_class.c_str());
			write_vector(handler, "pos_x", "pos_y", "pos_z", state.pos);
			write_vector(handler, "fvec_x", "fvec_y", "fvec_z", state.orient.vec.fvec);
			write_vector(handler, "uvec_x", "uvec_y", "uvec_z", state.orient.vec.uvec);
			write_vector(handler, "rvec_x", "rvec_y", "rvec_z", state.orient.vec.rvec);
			write_vector(handler, "vel_x", "vel_y", "vel_z", state.vel);
			write_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", state.rotvel);
			handler->writeFloat("alpha_mult", state.alpha_mult);
			write_string_list(handler, "flags", state.flags);
			write_string_list(handler, "object_flags", state.object_flags);
			SCP_vector<int> glow_banks;
			for (bool on : state.glow_banks) {
				glow_banks.push_back(on ? 1 : 0);
			}
			write_int_list(handler, "glow_banks", glow_banks);
			write_string_list(handler, "texture_old", state.texture_old);
			write_string_list(handler, "texture_new", state.texture_new);
			handler->writeInt("collision_group_id", state.collision_group_id);
			handler->writeInt("despawn_delay", state.despawn_delay);
		}
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	// The asteroid field itself, which is the one part of the world made of objects.
	handler->startArrayWrite("asteroids", data.asteroids.size());
	for (const auto& ast : data.asteroids) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("type", ast.type_name.c_str());
		handler->writeInt("subtype", ast.subtype);
		write_vector(handler, "pos_x", "pos_y", "pos_z", ast.pos);
		write_vector(handler, "fvec_x", "fvec_y", "fvec_z", ast.orient.vec.fvec);
		write_vector(handler, "uvec_x", "uvec_y", "uvec_z", ast.orient.vec.uvec);
		write_vector(handler, "rvec_x", "rvec_y", "rvec_z", ast.orient.vec.rvec);
		write_vector(handler, "vel_x", "vel_y", "vel_z", ast.vel);
		write_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", ast.rotvel);
		handler->writeFloat("hull", ast.hull);
		handler->writeInt("flags", ast.flags);
		handler->writeString("target", ast.target_ship.c_str());
		handler->writeInt("check_for_wrap", ast.check_for_wrap);
		handler->writeInt("check_for_collide", ast.check_for_collide);
		handler->writeInt("final_death_time", ast.final_death_time);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_world(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	auto& env = data.environment;

	env.present = handler->readBoolOr("present", false);

	env.skybox_model = handler->readStringOr("skybox_model", "");
	env.skybox_texture = handler->readStringOr("skybox_texture", "");
	env.skybox_flags_hi = handler->readUIntOr("skybox_flags_hi", 0);
	env.skybox_flags_lo = handler->readUIntOr("skybox_flags_lo", 0);
	env.skybox_alpha = handler->readFloatOr("skybox_alpha", 1.0f);
	read_vector(handler, "sky_fvec_x", "sky_fvec_y", "sky_fvec_z", env.skybox_orient.vec.fvec);
	read_vector(handler, "sky_uvec_x", "sky_uvec_y", "sky_uvec_z", env.skybox_orient.vec.uvec);
	read_vector(handler, "sky_rvec_x", "sky_rvec_y", "sky_rvec_z", env.skybox_orient.vec.rvec);

	env.ambient_light = handler->readIntOr("ambient_light", 0);

	env.fullneb = handler->readBoolOr("fullneb", false);
	env.neb_range = handler->readFloatOr("neb_range", 0.0f);
	env.neb_pattern = handler->readStringOr("neb_pattern", "");
	env.neb_fog_color_override = handler->readBoolOr("neb_fog_override", false);
	env.neb_fog_r = handler->readIntOr("neb_fog_r", 0);
	env.neb_fog_g = handler->readIntOr("neb_fog_g", 0);
	env.neb_fog_b = handler->readIntOr("neb_fog_b", 0);

	env.subspace = handler->readBoolOr("subspace", false);

	env.background_index = handler->readIntOr("background_index", -1);
	env.starfield.clear();
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
			env.starfield.push_back(std::move(entry));
		}
		handler->endArrayRead();
	}

	env.motion_debris_override = handler->readBoolOr("motion_debris_override", false);
	env.motion_debris_type = handler->readStringOr("motion_debris_type", "");

	env.soundtrack = handler->readStringOr("soundtrack", "");
	env.music_battle_started = handler->readBoolOr("music_battle_started", false);

	env.hud_draw = handler->readBoolOr("hud_draw", true);
	env.hud_disable_except_messages = handler->readBoolOr("hud_only_messages", false);
	env.hud_max_targeting_range = handler->readIntOr("hud_max_range", 0);
	env.hud_display_warpout = handler->readIntOr("hud_warpout", 0);

	env.support_ship_class = handler->readStringOr("support_class", "");
	env.support_arrival_location = handler->readStringOr("support_arrival", "");
	env.support_departure_location = handler->readStringOr("support_departure", "");
	env.support_arrival_anchor = handler->readStringOr("support_arrival_anchor", "");
	env.support_departure_anchor = handler->readStringOr("support_departure_anchor", "");
	env.support_max_ships = handler->readIntOr("support_max", 0);
	env.support_max_concurrent = handler->readIntOr("support_max_concurrent", 0);
	env.support_tally = handler->readIntOr("support_tally", 0);
	env.support_available_for_species = handler->readIntOr("support_species", 0);
	env.support_max_hull_repair = handler->readFloatOr("support_hull_repair", 0.0f);
	env.support_max_subsys_repair = handler->readFloatOr("support_subsys_repair", 0.0f);
	env.support_disallow_rearm = handler->readBoolOr("support_disallow_rearm", false);
	read_string_list(handler, "support_incoming_for", env.support_incoming_for);

	env.rearm_pools.clear();
	if (handler->hasField("rearm_pools")) {
		auto count = handler->startArrayRead("rearm_pools");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			SCP_map<SCP_string, int> pool;
			read_int_map(handler, "pool", pool);
			env.rearm_pools.push_back(std::move(pool));
		}
		handler->endArrayRead();
	}

	env.no_traitor = handler->readBoolOr("no_traitor", false);
	env.traitor_override = handler->readStringOr("traitor_override", "");
	env.debriefing_persona = handler->readStringOr("debriefing_persona", "");

	env.asteroids_enabled = handler->readBoolOr("asteroids_enabled", true);

	env.has_asteroid_field = handler->readBoolOr("has_asteroid_field", false);
	env.asteroid_field_num_initial = handler->readIntOr("asteroid_field_num_initial", 0);
	env.asteroid_field_active = handler->readBoolOr("asteroid_field_active", true);
	env.asteroid_field_is_debris = handler->readBoolOr("asteroid_field_is_debris", false);
	env.asteroid_field_enhanced_checks = handler->readBoolOr("asteroid_field_enhanced_checks", false);
	env.asteroid_field_has_inner_bound = handler->readBoolOr("asteroid_field_has_inner_bound", false);
	env.asteroid_field_speed = handler->readFloatOr("asteroid_field_speed", 0.0f);
	env.asteroid_field_bound_rad = handler->readFloatOr("asteroid_field_bound_rad", 0.0f);
	read_vector(handler, "asteroid_field_vel_x", "asteroid_field_vel_y", "asteroid_field_vel_z", env.asteroid_field_vel);
	read_vector(handler, "asteroid_field_min_x", "asteroid_field_min_y", "asteroid_field_min_z", env.asteroid_field_min);
	read_vector(handler, "asteroid_field_max_x", "asteroid_field_max_y", "asteroid_field_max_z", env.asteroid_field_max);
	read_vector(handler, "asteroid_field_inner_min_x", "asteroid_field_inner_min_y", "asteroid_field_inner_min_z",
	            env.asteroid_field_inner_min);
	read_vector(handler, "asteroid_field_inner_max_x", "asteroid_field_inner_max_y", "asteroid_field_inner_max_z",
	            env.asteroid_field_inner_max);
	read_string_list(handler, "asteroid_field_asteroid_types", env.asteroid_field_asteroid_types);
	read_string_list(handler, "asteroid_field_debris_types", env.asteroid_field_debris_types);
	read_string_list(handler, "asteroid_field_targets", env.asteroid_field_targets);

	env.supernova_stage = handler->readIntOr("supernova_stage", 0);
	env.supernova_total = handler->readFloatOr("supernova_total", 0.0f);
	env.supernova_left = handler->readFloatOr("supernova_left", 0.0f);
	env.time_compression = handler->readFloatOr("time_compression", 1.0f);
	env.time_compression_locked = handler->readBoolOr("time_compression_locked", false);

	env.current_nav = handler->readIntOr("current_nav", -1);
	env.navpoints.clear();
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

	env.jump_nodes.clear();
	if (handler->hasField("jump_nodes")) {
		auto count = handler->startArrayRead("jump_nodes");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::jump_node_state node;
			node.index = handler->readIntOr("index", 0);
			node.name = handler->readStringOr("name", "");
			node.display_name = handler->readStringOr("display_name", "");
			node.model = handler->readStringOr("model", "");
			node.hidden = handler->readBoolOr("hidden", false);
			node.colored = handler->readBoolOr("colored", false);
			node.color[0] = handler->readIntOr("color_r", 0);
			node.color[1] = handler->readIntOr("color_g", 0);
			node.color[2] = handler->readIntOr("color_b", 0);
			node.color[3] = handler->readIntOr("color_a", 0);
			node.show_polys = handler->readBoolOr("show_polys", false);
			node.has_pos = handler->readBoolOr("has_pos", false);
			read_vector(handler, "pos_x", "pos_y", "pos_z", node.pos);
			env.jump_nodes.push_back(std::move(node));
		}
		handler->endArrayRead();
	}

	read_string_list(handler, "squadron_wings", env.squadron_wings);

	env.effects_present = handler->readBoolOr("effects_present", false);
	read_vector(handler, "gravity_x", "gravity_y", "gravity_z", env.gravity);
	env.storm = handler->readStringOr("storm", "");

	env.poofs.clear();
	if (handler->hasField("poofs")) {
		auto count = handler->startArrayRead("poofs");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::poof_state poof;
			poof.name = handler->readStringOr("name", "");
			poof.enabled = handler->readBoolOr("enabled", false);
			poof.fade_start = handler->readIntOr("fade_start", -1);
			poof.fade_duration = handler->readIntOr("fade_duration", -1);
			poof.fade_in = handler->readBoolOr("fade_in", true);
			poof.fade_multiplier = handler->readFloatOr("fade_multiplier", -1.0f);
			if (!poof.name.empty()) {
				env.poofs.push_back(std::move(poof));
			}
		}
		handler->endArrayRead();
	}

	env.has_volumetrics = handler->readBoolOr("has_volumetrics", false);
	env.volumetrics_enabled = handler->readBoolOr("volumetrics_enabled", true);
	env.fog_near_distance = handler->readFloatOr("fog_near_distance", 0.0f);
	env.fog_1000m_visibility = handler->readFloatOr("fog_1000m_visibility", 0.0f);
	env.fog_skybox_clip_distance = handler->readFloatOr("fog_skybox_clip_distance", 0.0f);
	env.fog_clip_distance = handler->readFloatOr("fog_clip_distance", 0.0f);

	env.post_effects.clear();
	if (handler->hasField("post_effects")) {
		auto count = handler->startArrayRead("post_effects");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::post_effect_state effect;
			effect.name = handler->readStringOr("name", "");
			effect.intensity = handler->readFloatOr("intensity", 0.0f);
			read_vector(handler, "r", "g", "b", effect.rgb);
			if (!effect.name.empty()) {
				env.post_effects.push_back(std::move(effect));
			}
		}
		handler->endArrayRead();
	}
	env.lightshafts_on = handler->readBoolOr("lightshafts_on", true);
	env.lightshafts_intensity = handler->readFloatOr("lightshafts_intensity", 0.0f);

	env.sound_env_preset = handler->readStringOr("sound_env_preset", "");
	env.sound_env_volume = handler->readFloatOr("sound_env_volume", 0.0f);
	env.sound_env_damping = handler->readFloatOr("sound_env_damping", 0.0f);
	env.sound_env_decay = handler->readFloatOr("sound_env_decay", 0.0f);

	env.beam_friendly_damage_cap = handler->readFloatOr("beam_friendly_damage_cap", 0.0f);
	env.weapon_friendly_damage_cap = handler->readFloatOr("weapon_friendly_damage_cap", 0.0f);
	env.weapon_self_damage_cap = handler->readFloatOr("weapon_self_damage_cap", 0.0f);

	read_override_list(handler, "weapon_damage_types", env.weapon_damage_types);
	read_override_list(handler, "weapon_shockwave_damage_types", env.weapon_shockwave_damage_types);
	read_override_list(handler, "ship_shockwave_damage_types", env.ship_shockwave_damage_types);
	read_override_list(handler, "asteroid_damage_types", env.asteroid_damage_types);

	read_string_list(handler, "mission_music", env.mission_music);

	env.coordinate_points.clear();
	if (handler->hasField("coordinate_points")) {
		auto count = handler->startArrayRead("coordinate_points");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::coordinate_point_state point;
			point.name = handler->readStringOr("name", "");
			point.group = handler->readStringOr("group", "");
			read_vector(handler, "pos_x", "pos_y", "pos_z", point.pos);
			point.escort_priority = handler->readIntOr("escort_priority", 0);
			point.multi_team = handler->readIntOr("multi_team", -1);
			point.visible = handler->readBoolOr("visible", false);
			if (!point.name.empty()) {
				env.coordinate_points.push_back(std::move(point));
			}
		}
		handler->endArrayRead();
	}

	env.shudder_perpetual = handler->readBoolOr("shudder_perpetual", false);
	env.shudder_everywhere = handler->readBoolOr("shudder_everywhere", false);
	env.shudder_time = handler->readIntOr("shudder_time", -1);
	env.shudder_total = handler->readIntOr("shudder_total", 0);
	env.shudder_intensity = handler->readFloatOr("shudder_intensity", 0.0f);

	env.photo_mode_allowed = handler->readBoolOr("photo_mode_allowed", false);
	env.toggle_debriefing = handler->readBoolOr("toggle_debriefing", false);
	env.deactivate_autopilot = handler->readBoolOr("deactivate_autopilot", false);
	env.use_autopilot_cinematics = handler->readBoolOr("use_autopilot_cinematics", false);

	data.waypoint_lists.clear();
	if (handler->hasField("waypoint_lists")) {
		auto count = handler->startArrayRead("waypoint_lists");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::waypoint_list_state list;
			list.name = handler->readStringOr("name", "");
			if (handler->hasField("points")) {
				auto num_points = handler->startArrayRead("points");
				for (size_t j = 0; j < num_points; j++, handler->nextArraySection()) {
					vec3d point = vmd_zero_vector;
					read_vector(handler, "x", "y", "z", point);
					list.points.push_back(point);
				}
				handler->endArrayRead();
			}
			if (!list.name.empty()) {
				data.waypoint_lists.push_back(std::move(list));
			}
		}
		handler->endArrayRead();
	}

	data.props.clear();
	if (handler->hasField("props")) {
		auto count = handler->startArrayRead("props");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::prop_state state;
			state.name = handler->readStringOr("name", "");
			state.disposition = disposition_value(handler->readStringOr("disposition", "present"));
			state.no_parse_prop = handler->readBoolOr("no_parse_prop", false);
			if (state.disposition == checkpoint::ShipDisposition::Present) {
				state.prop_class = handler->readStringOr("class", "");
				read_vector(handler, "pos_x", "pos_y", "pos_z", state.pos);
				read_vector(handler, "fvec_x", "fvec_y", "fvec_z", state.orient.vec.fvec);
				read_vector(handler, "uvec_x", "uvec_y", "uvec_z", state.orient.vec.uvec);
				read_vector(handler, "rvec_x", "rvec_y", "rvec_z", state.orient.vec.rvec);
				read_vector(handler, "vel_x", "vel_y", "vel_z", state.vel);
				read_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", state.rotvel);
				state.alpha_mult = handler->readFloatOr("alpha_mult", 1.0f);
				read_string_list(handler, "flags", state.flags);
				read_string_list(handler, "object_flags", state.object_flags);
				SCP_vector<int> glow_banks;
				read_int_list(handler, "glow_banks", glow_banks);
				for (int on : glow_banks) {
					state.glow_banks.push_back(on != 0);
				}
				read_string_list(handler, "texture_old", state.texture_old);
				read_string_list(handler, "texture_new", state.texture_new);
				state.collision_group_id = handler->readIntOr("collision_group_id", 0);
				state.despawn_delay = handler->readIntOr("despawn_delay", 0);
			}
			if (!state.name.empty()) {
				data.props.push_back(std::move(state));
			}
		}
		handler->endArrayRead();
	}

	data.asteroids.clear();
	if (handler->hasField("asteroids")) {
		auto count = handler->startArrayRead("asteroids");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::asteroid_state ast;
			ast.type_name = handler->readStringOr("type", "");
			ast.subtype = handler->readIntOr("subtype", 0);
			read_vector(handler, "pos_x", "pos_y", "pos_z", ast.pos);
			read_vector(handler, "fvec_x", "fvec_y", "fvec_z", ast.orient.vec.fvec);
			read_vector(handler, "uvec_x", "uvec_y", "uvec_z", ast.orient.vec.uvec);
			read_vector(handler, "rvec_x", "rvec_y", "rvec_z", ast.orient.vec.rvec);
			read_vector(handler, "vel_x", "vel_y", "vel_z", ast.vel);
			read_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", ast.rotvel);
			ast.hull = handler->readFloatOr("hull", 0.0f);
			ast.flags = handler->readIntOr("flags", 0);
			ast.target_ship = handler->readStringOr("target", "");
			ast.check_for_wrap = handler->readIntOr("check_for_wrap", 0);
			ast.check_for_collide = handler->readIntOr("check_for_collide", 0);
			ast.final_death_time = handler->readIntOr("final_death_time", 0);
			data.asteroids.push_back(std::move(ast));
		}
		handler->endArrayRead();
	}
}

void write_animations(pilot::FileHandler* handler, const SCP_vector<checkpoint::animation_state>& animations)
{
	handler->startArrayWrite("animations", animations.size());
	for (const auto& anim : animations) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeUInt("id", anim.id);
		handler->writeInt("state", anim.state);
		handler->writeInt("direction", anim.direction);
		handler->writeFloat("time", anim.time);
		handler->writeFloat("duration", anim.duration);
		handler->writeFloat("speed", anim.speed);
		write_string_list(handler, "instance_flags", anim.instance_flags);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_animations(pilot::FileHandler* handler, SCP_vector<checkpoint::animation_state>& animations)
{
	animations.clear();

	if (!handler->hasField("animations")) {
		return;
	}

	auto count = handler->startArrayRead("animations");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::animation_state anim;
		anim.id = handler->readUIntOr("id", 0);
		anim.state = handler->readIntOr("state", 0);
		anim.direction = handler->readIntOr("direction", 0);
		anim.time = handler->readFloatOr("time", 0.0f);
		anim.duration = handler->readFloatOr("duration", 0.0f);
		anim.speed = handler->readFloatOr("speed", 1.0f);
		read_string_list(handler, "instance_flags", anim.instance_flags);

		animations.push_back(std::move(anim));
	}
	handler->endArrayRead();
}

void write_docks(pilot::FileHandler* handler, const SCP_vector<checkpoint::dock_link_state>& docks)
{
	handler->startArrayWrite("docks", docks.size());
	for (const auto& link : docks) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("other", link.other_ship.c_str());
		handler->writeString("my_point", link.my_point.c_str());
		handler->writeString("their_point", link.their_point.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_docks(pilot::FileHandler* handler, SCP_vector<checkpoint::dock_link_state>& docks)
{
	docks.clear();

	if (!handler->hasField("docks")) {
		return;
	}

	auto count = handler->startArrayRead("docks");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::dock_link_state link;
		link.other_ship = handler->readStringOr("other", "");
		link.my_point = handler->readStringOr("my_point", "");
		link.their_point = handler->readStringOr("their_point", "");
		docks.push_back(std::move(link));
	}
	handler->endArrayRead();
}

// AI orders, shared by the per-ship list and the per-wing one.  slots is the originating index of
// each entry, which the ship list needs because active_goal indexes that array; a wing's goals are
// a plain list and pass null.
void write_goal_list(pilot::FileHandler* handler,
	const char* name,
	const SCP_vector<checkpoint::ai_goal_state>& goals,
	const SCP_vector<int>* slots)
{
	handler->startArrayWrite(name, goals.size());
	for (size_t i = 0; i < goals.size(); i++) {
		const auto& goal = goals[i];
		handler->startSectionWrite(Section::Unnamed);

		int slot = (slots != nullptr && i < slots->size()) ? (*slots)[i] : static_cast<int>(i);
		handler->writeInt("slot", slot);
		handler->writeString("mode", goal.mode.c_str());
		handler->writeString("type", goal.type.c_str());
		write_string_list(handler, "flags", goal.flags);
		handler->writeInt("signature", goal.signature);
		handler->writeInt("submode", goal.submode);
		handler->writeInt("priority", goal.priority);
		handler->writeInt("time", static_cast<std::int32_t>(goal.time));
		handler->writeString("target_name", goal.target_name.c_str());
		handler->writeString("waypoint_list", goal.waypoint_list.c_str());
		handler->writeString("docker_point", goal.docker_point.c_str());
		handler->writeString("dockee_point", goal.dockee_point.c_str());
		handler->writeString("submode_ship_class", goal.submode_ship_class.c_str());
		handler->writeInt("int_data", goal.int_data);
		handler->writeFloat("float_data", goal.float_data);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_goal_list(pilot::FileHandler* handler,
	const char* name,
	SCP_vector<checkpoint::ai_goal_state>& goals,
	SCP_vector<int>* slots)
{
	goals.clear();
	if (slots != nullptr) {
		slots->clear();
	}

	if (!handler->hasField(name)) {
		return;
	}

	auto count = handler->startArrayRead(name);
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::ai_goal_state goal;

		int slot = handler->readIntOr("slot", static_cast<int>(i));
		goal.mode = handler->readStringOr("mode", "");
		goal.type = handler->readStringOr("type", "invalid");
		read_string_list(handler, "flags", goal.flags);
		goal.signature = handler->readIntOr("signature", 0);
		goal.submode = handler->readIntOr("submode", 0);
		goal.priority = handler->readIntOr("priority", 0);
		goal.time = static_cast<fix>(handler->readIntOr("time", 0));
		goal.target_name = handler->readStringOr("target_name", "");
		goal.waypoint_list = handler->readStringOr("waypoint_list", "");
		goal.docker_point = handler->readStringOr("docker_point", "");
		goal.dockee_point = handler->readStringOr("dockee_point", "");
		goal.submode_ship_class = handler->readStringOr("submode_ship_class", "");
		goal.int_data = handler->readIntOr("int_data", 0);
		goal.float_data = handler->readFloatOr("float_data", 0.0f);

		goals.push_back(std::move(goal));
		if (slots != nullptr) {
			slots->push_back(slot);
		}
	}
	handler->endArrayRead();
}

void write_ai(pilot::FileHandler* handler, const checkpoint::ai_state& ai)
{
	handler->writeBool("ai_present", ai.present);
	if (!ai.present) {
		return;
	}

	write_string_list(handler, "ai_flags", ai.flags);
	write_string_list(handler, "ai_override_flags", ai.override_flags);
	write_int_map(handler, "ai_ints", ai.ints);
	write_float_map(handler, "ai_floats", ai.floats);
	write_int_map(handler, "ai_mission_times", ai.mission_times);
	write_int_map(handler, "ai_stamps", ai.stamps);
	write_vec_map(handler, "ai_vecs", ai.vecs);
	write_float_map(handler, "ai_override_floats", ai.override_floats);

	handler->writeString("ai_class", ai.ai_class.c_str());
	handler->writeString("ai_target_ship", ai.target_ship.c_str());
	handler->writeString("ai_previous_target_ship", ai.previous_target_ship.c_str());
	handler->writeString("ai_target_subsystem", ai.target_subsystem.c_str());
	handler->writeString("ai_target_subsystem_ship", ai.target_subsystem_ship.c_str());
	handler->writeString("ai_goal_ship", ai.goal_ship.c_str());
	handler->writeString("ai_guard_ship", ai.guard_ship.c_str());
	handler->writeString("ai_guard_wing", ai.guard_wing.c_str());
	handler->writeString("ai_ignore_ship", ai.ignore_ship.c_str());
	handler->writeString("ai_ignore_wing", ai.ignore_wing.c_str());
	write_string_list(handler, "ai_ignore_new", ai.ignore_new);
	handler->writeString("ai_support_ship", ai.support_ship.c_str());
	handler->writeString("ai_hitter_ship", ai.hitter_ship.c_str());
	handler->writeString("ai_attacker_ship", ai.attacker_ship.c_str());
	handler->writeString("ai_artillery_ship", ai.artillery_ship.c_str());
	handler->writeString("ai_waypoint_list", ai.waypoint_list.c_str());
	handler->writeString("ai_last_subsys_ship", ai.last_subsys_target_ship.c_str());
	handler->writeString("ai_last_subsys", ai.last_subsys_target.c_str());

	write_goal_list(handler, "ai_goals", ai.goals, &ai.goal_slots);
}

void read_ai(pilot::FileHandler* handler, checkpoint::ai_state& ai)
{
	ai = checkpoint::ai_state();

	ai.present = handler->readBoolOr("ai_present", false);
	if (!ai.present) {
		return;
	}

	read_string_list(handler, "ai_flags", ai.flags);
	read_string_list(handler, "ai_override_flags", ai.override_flags);
	read_int_map(handler, "ai_ints", ai.ints);
	read_float_map(handler, "ai_floats", ai.floats);
	read_int_map(handler, "ai_mission_times", ai.mission_times);
	read_int_map(handler, "ai_stamps", ai.stamps);
	read_vec_map(handler, "ai_vecs", ai.vecs);
	read_float_map(handler, "ai_override_floats", ai.override_floats);

	ai.ai_class = handler->readStringOr("ai_class", "");
	ai.target_ship = handler->readStringOr("ai_target_ship", "");
	ai.previous_target_ship = handler->readStringOr("ai_previous_target_ship", "");
	ai.target_subsystem = handler->readStringOr("ai_target_subsystem", "");
	ai.target_subsystem_ship = handler->readStringOr("ai_target_subsystem_ship", "");
	ai.goal_ship = handler->readStringOr("ai_goal_ship", "");
	ai.guard_ship = handler->readStringOr("ai_guard_ship", "");
	ai.guard_wing = handler->readStringOr("ai_guard_wing", "");
	ai.ignore_ship = handler->readStringOr("ai_ignore_ship", "");
	ai.ignore_wing = handler->readStringOr("ai_ignore_wing", "");
	read_string_list(handler, "ai_ignore_new", ai.ignore_new);
	ai.support_ship = handler->readStringOr("ai_support_ship", "");
	ai.hitter_ship = handler->readStringOr("ai_hitter_ship", "");
	ai.attacker_ship = handler->readStringOr("ai_attacker_ship", "");
	ai.artillery_ship = handler->readStringOr("ai_artillery_ship", "");
	ai.waypoint_list = handler->readStringOr("ai_waypoint_list", "");
	ai.last_subsys_target_ship = handler->readStringOr("ai_last_subsys_ship", "");
	ai.last_subsys_target = handler->readStringOr("ai_last_subsys", "");

	read_goal_list(handler, "ai_goals", ai.goals, &ai.goal_slots);
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

		handler->writeString("armor_type", subsys.armor_type.c_str());
		write_int_list(handler, "targeting_order", subsys.targeting_order);
		write_string_list(handler, "target_priorities", subsys.target_priorities);
		handler->writeString("forced_target_subsys", subsys.forced_target_subsys.c_str());
		handler->writeBool("scripting_target_override", subsys.scripting_target_override);
		handler->writeBool("has_rotation", subsys.has_rotation);
		if (subsys.has_rotation) {
			handler->writeFloat("cur_angle", subsys.cur_angle);
			handler->writeFloat("cur_offset", subsys.cur_offset);
			handler->writeFloat("current_turn_rate", subsys.current_turn_rate);
			handler->writeFloat("desired_turn_rate", subsys.desired_turn_rate);
			handler->writeFloat("turn_accel", subsys.turn_accel);
			handler->writeFloat("current_shift_rate", subsys.current_shift_rate);
			handler->writeFloat("desired_shift_rate", subsys.desired_shift_rate);
			write_vector(handler, "orient_fvec_x", "orient_fvec_y", "orient_fvec_z", subsys.canonical_orient.vec.fvec);
			write_vector(handler, "orient_uvec_x", "orient_uvec_y", "orient_uvec_z", subsys.canonical_orient.vec.uvec);
			write_vector(handler, "orient_rvec_x", "orient_rvec_y", "orient_rvec_z", subsys.canonical_orient.vec.rvec);
			write_vector(handler, "offset_x", "offset_y", "offset_z", subsys.canonical_offset);
		}
		handler->writeBool("has_gun_orient", subsys.has_gun_orient);
		if (subsys.has_gun_orient) {
			write_vector(handler, "gun_fvec_x", "gun_fvec_y", "gun_fvec_z", subsys.gun_canonical_orient.vec.fvec);
			write_vector(handler, "gun_uvec_x", "gun_uvec_y", "gun_uvec_z", subsys.gun_canonical_orient.vec.uvec);
			write_vector(handler, "gun_rvec_x", "gun_rvec_y", "gun_rvec_z", subsys.gun_canonical_orient.vec.rvec);
		}

		write_string_list(handler, "flags", subsys.flags);
		write_float_map(handler, "floats", subsys.floats);
		write_int_map(handler, "ints", subsys.ints);

		handler->writeBool("has_weapons", subsys.has_weapons);
		if (subsys.has_weapons) {
			write_weapon_state(handler, subsys.weapons);
			handler->writeString("ai_class", subsys.ai_class.c_str());
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

		subsys.armor_type = handler->readStringOr("armor_type", "");
		read_int_list(handler, "targeting_order", subsys.targeting_order);
		read_string_list(handler, "target_priorities", subsys.target_priorities);
		subsys.forced_target_subsys = handler->readStringOr("forced_target_subsys", "");
		subsys.scripting_target_override = handler->readBoolOr("scripting_target_override", false);
		subsys.has_rotation = handler->readBoolOr("has_rotation", false);
		if (subsys.has_rotation) {
			subsys.cur_angle = handler->readFloatOr("cur_angle", 0.0f);
			subsys.cur_offset = handler->readFloatOr("cur_offset", 0.0f);
			subsys.current_turn_rate = handler->readFloatOr("current_turn_rate", 0.0f);
			subsys.desired_turn_rate = handler->readFloatOr("desired_turn_rate", 0.0f);
			subsys.turn_accel = handler->readFloatOr("turn_accel", 0.0f);
			subsys.current_shift_rate = handler->readFloatOr("current_shift_rate", 0.0f);
			subsys.desired_shift_rate = handler->readFloatOr("desired_shift_rate", 0.0f);
			read_vector(handler, "orient_fvec_x", "orient_fvec_y", "orient_fvec_z", subsys.canonical_orient.vec.fvec);
			read_vector(handler, "orient_uvec_x", "orient_uvec_y", "orient_uvec_z", subsys.canonical_orient.vec.uvec);
			read_vector(handler, "orient_rvec_x", "orient_rvec_y", "orient_rvec_z", subsys.canonical_orient.vec.rvec);
			read_vector(handler, "offset_x", "offset_y", "offset_z", subsys.canonical_offset);
		}
		subsys.has_gun_orient = handler->readBoolOr("has_gun_orient", false);
		if (subsys.has_gun_orient) {
			read_vector(handler, "gun_fvec_x", "gun_fvec_y", "gun_fvec_z", subsys.gun_canonical_orient.vec.fvec);
			read_vector(handler, "gun_uvec_x", "gun_uvec_y", "gun_uvec_z", subsys.gun_canonical_orient.vec.uvec);
			read_vector(handler, "gun_rvec_x", "gun_rvec_y", "gun_rvec_z", subsys.gun_canonical_orient.vec.rvec);
		}

		read_string_list(handler, "flags", subsys.flags);
		read_float_map(handler, "floats", subsys.floats);
		read_int_map(handler, "ints", subsys.ints);

		subsys.has_weapons = handler->readBoolOr("has_weapons", false);
		if (subsys.has_weapons) {
			read_weapon_state(handler, subsys.weapons);
			subsys.ai_class = handler->readStringOr("ai_class", "");
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
		handler->writeString("alt_name", p_obj.alt_name.c_str());
		handler->writeString("callsign", p_obj.callsign.c_str());
		handler->writeString("cargo", p_obj.cargo.c_str());
		handler->writeBool("cargo_no_deplete", p_obj.cargo_no_deplete);
		handler->writeInt("collision_group_id", p_obj.collision_group_id);
		handler->writeString("team_color", p_obj.team_color.c_str());
		write_string_list(handler, "texture_old", p_obj.texture_old);
		write_string_list(handler, "texture_new", p_obj.texture_new);
		write_iff_colors(handler, "iff_colors", p_obj.iff_colors);

		write_string_list(handler, "flags", p_obj.flags);
		write_parse_subsystems(handler, p_obj.subsystems);

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
		p_obj.alt_name = handler->readStringOr("alt_name", "");
		p_obj.callsign = handler->readStringOr("callsign", "");
		p_obj.cargo = handler->readStringOr("cargo", "");
		p_obj.cargo_no_deplete = handler->readBoolOr("cargo_no_deplete", false);
		p_obj.collision_group_id = handler->readIntOr("collision_group_id", 0);
		p_obj.team_color = handler->readStringOr("team_color", "");
		read_string_list(handler, "texture_old", p_obj.texture_old);
		read_string_list(handler, "texture_new", p_obj.texture_new);
		read_iff_colors(handler, "iff_colors", p_obj.iff_colors);

		read_string_list(handler, "flags", p_obj.flags);
		read_parse_subsystems(handler, p_obj.subsystems);

		if (!p_obj.name.empty()) {
			data.parse_objects.push_back(std::move(p_obj));
		}
	}
	handler->endArrayRead();
}

void write_hotkeys(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->writeInt("current_hotkey_set", data.current_hotkey_set);
	write_string_list(handler, "escort_points", data.escort_points);

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
	data.current_hotkey_set = handler->readIntOr("current_hotkey_set", -1);
	read_string_list(handler, "escort_points", data.escort_points);

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
		if (!ship_data.parse_name.empty()) {
			handler->writeString("parse_name", ship_data.parse_name.c_str());
		}

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
			handler->writeString("alt_name", ship_data.alt_name.c_str());
			handler->writeString("callsign", ship_data.callsign.c_str());

			handler->writeInt("departure_location", ship_data.departure_location);
			handler->writeString("departure_anchor", ship_data.departure_anchor.c_str());
			handler->writeString("armor_type", ship_data.armor_type.c_str());
			handler->writeString("shield_armor_type", ship_data.shield_armor_type.c_str());
			handler->writeString("collision_damage_type", ship_data.collision_damage_type.c_str());
			handler->writeString("debris_damage_type", ship_data.debris_damage_type.c_str());
			handler->writeBool("use_special_explosion", ship_data.use_special_explosion);
			handler->writeBool("use_shockwave", ship_data.use_shockwave);
			handler->writeBool("orders_present", ship_data.orders_present);
			write_string_list(handler, "orders_accepted", ship_data.orders_accepted);
			write_string_list(handler, "orders_allowed_against", ship_data.orders_allowed_against);
			handler->startArrayWrite("guard_ranges", ship_data.guard_ranges.size());
			for (const auto& guard : ship_data.guard_ranges) {
				handler->startSectionWrite(Section::Unnamed);
				handler->writeString("ship", guard.ship.c_str());
				handler->writeFloat("range", guard.range);
				handler->endSectionWrite();
			}
			handler->endArrayWrite();
			handler->writeString("special_warpout_ship", ship_data.special_warpout_ship.c_str());
			{
				SCP_vector<int> glow_banks;
				for (bool on : ship_data.glow_banks) {
					glow_banks.push_back(on ? 1 : 0);
				}
				write_int_list(handler, "glow_banks", glow_banks);
			}
			write_string_list(handler, "texture_old", ship_data.texture_old);
			write_string_list(handler, "texture_new", ship_data.texture_new);
			handler->writeInt("collision_group_id", ship_data.collision_group_id);
			handler->writeString("team_color", ship_data.team_color.c_str());
			handler->writeString("secondary_team_color", ship_data.secondary_team_color.c_str());
			write_iff_colors(handler, "iff_colors", ship_data.iff_colors);
			handler->writeFloat("sim_hull", ship_data.sim_hull);
			handler->startArrayWrite("damage_credits", ship_data.damage_credits.size());
			for (const auto& credit : ship_data.damage_credits) {
				handler->startSectionWrite(Section::Unnamed);
				handler->writeString("ship", credit.ship.c_str());
				handler->writeFloat("damage", credit.damage);
				handler->endSectionWrite();
			}
			handler->endArrayWrite();
			handler->writeBool("no_parse_object", ship_data.no_parse_object);

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
			write_ai(handler, ship_data.ai);
			write_docks(handler, ship_data.docks);
			write_animations(handler, ship_data.animations);
		} else {
			handler->writeInt("exit_time", static_cast<std::int32_t>(ship_data.exit_time));
			// The exited record: what the ship had become when it left.
			handler->writeString("class", ship_data.ship_class.c_str());
			handler->writeString("team", ship_data.team.c_str());
			handler->writeString("display_name", ship_data.display_name.c_str());
			handler->writeString("cargo", ship_data.cargo.c_str());
			handler->writeBool("cargo_no_deplete", ship_data.cargo_no_deplete);
			write_string_list(handler, "exit_flags", ship_data.exit_flags);
			handler->writeInt("time_cargo_revealed", static_cast<std::int32_t>(ship_data.time_cargo_revealed));
			handler->writeInt("exit_hull_strength", ship_data.exit_hull_strength);
			handler->writeBool("no_parse_object", ship_data.no_parse_object);
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
		ship_data.parse_name = handler->readStringOr("parse_name", "");

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
			ship_data.alt_name = handler->readStringOr("alt_name", "");
			ship_data.callsign = handler->readStringOr("callsign", "");

			ship_data.departure_location = handler->readIntOr("departure_location", 0);
			ship_data.departure_anchor = handler->readStringOr("departure_anchor", "");
			ship_data.armor_type = handler->readStringOr("armor_type", "");
			ship_data.shield_armor_type = handler->readStringOr("shield_armor_type", "");
			ship_data.collision_damage_type = handler->readStringOr("collision_damage_type", "");
			ship_data.debris_damage_type = handler->readStringOr("debris_damage_type", "");
			ship_data.use_special_explosion = handler->readBoolOr("use_special_explosion", false);
			ship_data.use_shockwave = handler->readBoolOr("use_shockwave", false);
			ship_data.orders_present = handler->readBoolOr("orders_present", false);
			read_string_list(handler, "orders_accepted", ship_data.orders_accepted);
			read_string_list(handler, "orders_allowed_against", ship_data.orders_allowed_against);
			ship_data.guard_ranges.clear();
			if (handler->hasField("guard_ranges")) {
				auto count = handler->startArrayRead("guard_ranges");
				for (size_t j = 0; j < count; j++, handler->nextArraySection()) {
					checkpoint::guard_range_state guard;
					guard.ship = handler->readStringOr("ship", "");
					guard.range = handler->readFloatOr("range", -1.0f);
					ship_data.guard_ranges.push_back(std::move(guard));
				}
				handler->endArrayRead();
			}
			ship_data.special_warpout_ship = handler->readStringOr("special_warpout_ship", "");
			{
				SCP_vector<int> glow_banks;
				read_int_list(handler, "glow_banks", glow_banks);
				for (int on : glow_banks) {
					ship_data.glow_banks.push_back(on != 0);
				}
			}
			read_string_list(handler, "texture_old", ship_data.texture_old);
			read_string_list(handler, "texture_new", ship_data.texture_new);
			ship_data.collision_group_id = handler->readIntOr("collision_group_id", 0);
			ship_data.team_color = handler->readStringOr("team_color", "");
			ship_data.secondary_team_color = handler->readStringOr("secondary_team_color", "");
			read_iff_colors(handler, "iff_colors", ship_data.iff_colors);
			ship_data.sim_hull = handler->readFloatOr("sim_hull", 0.0f);
			ship_data.damage_credits.clear();
			if (handler->hasField("damage_credits")) {
				auto count = handler->startArrayRead("damage_credits");
				for (size_t j = 0; j < count; j++, handler->nextArraySection()) {
					checkpoint::damage_credit_state credit;
					credit.ship = handler->readStringOr("ship", "");
					credit.damage = handler->readFloatOr("damage", 0.0f);
					ship_data.damage_credits.push_back(std::move(credit));
				}
				handler->endArrayRead();
			}
			ship_data.no_parse_object = handler->readBoolOr("no_parse_object", false);

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
			read_ai(handler, ship_data.ai);
			read_docks(handler, ship_data.docks);
			read_animations(handler, ship_data.animations);
		} else {
			ship_data.exit_time = static_cast<fix>(handler->readIntOr("exit_time", 0));
			ship_data.ship_class = handler->readStringOr("class", "");
			ship_data.team = handler->readStringOr("team", "");
			ship_data.display_name = handler->readStringOr("display_name", "");
			ship_data.cargo = handler->readStringOr("cargo", "");
			ship_data.cargo_no_deplete = handler->readBoolOr("cargo_no_deplete", false);
			read_string_list(handler, "exit_flags", ship_data.exit_flags);
			ship_data.time_cargo_revealed = static_cast<fix>(handler->readIntOr("time_cargo_revealed", 0));
			ship_data.exit_hull_strength = handler->readIntOr("exit_hull_strength", 0);
			ship_data.no_parse_object = handler->readBoolOr("no_parse_object", false);
		}

		data.ships.push_back(std::move(ship_data));
	}
	handler->endArrayRead();

	read_parse_objects(handler, data);
	read_hotkeys(handler, data);
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
		write_int_map(handler, "ints", wing_data.ints);
		write_string_list(handler, "flags", wing_data.flags);
		write_string_list(handler, "ships", wing_data.ship_names);
		handler->writeString("special_ship", wing_data.special_ship.c_str());
		handler->writeString("formation", wing_data.formation.c_str());
		handler->writeFloat("formation_scale", wing_data.formation_scale);

		handler->writeString("display_name", wing_data.display_name.c_str());
		handler->writeString("arrival_anchor", wing_data.arrival_anchor.c_str());
		handler->writeString("departure_anchor", wing_data.departure_anchor.c_str());
		handler->writeInt("arrival_location", wing_data.arrival_location);
		handler->writeInt("departure_location", wing_data.departure_location);
		handler->writeInt("arrival_path_mask", wing_data.arrival_path_mask);
		handler->writeInt("departure_path_mask", wing_data.departure_path_mask);

		write_goal_list(handler, "goals", wing_data.goals, nullptr);

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
		handler->writeInt("type", var.type);
		if (var.array_index >= 0) {
			handler->writeInt("array_index", var.array_index);
		}
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
			read_int_map(handler, "ints", wing_data.ints);
			read_string_list(handler, "flags", wing_data.flags);
			read_string_list(handler, "ships", wing_data.ship_names);
			wing_data.special_ship = handler->readStringOr("special_ship", "");
			wing_data.formation = handler->readStringOr("formation", "");
			wing_data.formation_scale = handler->readFloatOr("formation_scale", 1.0f);

			wing_data.display_name = handler->readStringOr("display_name", "");
			wing_data.arrival_anchor = handler->readStringOr("arrival_anchor", "");
			wing_data.departure_anchor = handler->readStringOr("departure_anchor", "");
			wing_data.arrival_location = handler->readIntOr("arrival_location", 0);
			wing_data.departure_location = handler->readIntOr("departure_location", 0);
			wing_data.arrival_path_mask = handler->readIntOr("arrival_path_mask", 0);
			wing_data.departure_path_mask = handler->readIntOr("departure_path_mask", 0);

			read_goal_list(handler, "goals", wing_data.goals, nullptr);

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
			var.type = handler->readIntOr("type", 0);
			var.array_index = handler->readIntOr("array_index", -1);

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
		handler->writeBool("invalid", goal.invalid);
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
		goal.invalid = handler->readBoolOr("invalid", false);

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
		handler->writeString("index_name", entry.index_name.c_str());
		handler->writeString("index_class", entry.index_class.c_str());
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
		entry.index_name = handler->readStringOr("index_name", "");
		entry.index_class = handler->readStringOr("index_class", "");
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
		handler->writeString("owner", node.owner_kind.c_str());
		handler->writeString("name", node.owner_name.c_str());
		handler->writeInt("n", node.owner_occurrence);
		handler->writeInt("i", node.ordinal);
		handler->writeInt("v", node.value);
		handler->writeInt("f", node.flags);
		if (!node.text.empty()) {
			handler->writeString("t", node.text.c_str());
		}
		if (node.has_duration) {
			handler->writeInt("d", static_cast<std::int32_t>(node.duration_start));
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
			node.owner_kind = handler->readStringOr("owner", "");
			node.owner_name = handler->readStringOr("name", "");
			node.owner_occurrence = handler->readIntOr("n", 0);
			node.ordinal = handler->readIntOr("i", -1);
			node.value = handler->readIntOr("v", 0);
			node.flags = handler->readIntOr("f", 0);
			node.text = handler->readStringOr("t", "");
			if (handler->hasField("d")) {
				node.has_duration = true;
				node.duration_start = static_cast<fix>(handler->readIntOr("d", 0));
			}

			if (!node.owner_kind.empty() && node.ordinal >= 0) {
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

void write_script_data(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointScriptData);
	write_string_map(handler, "data", data.script_data);
	handler->endSectionWrite();
}

void read_script_data(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	read_string_map(handler, "data", data.script_data);
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
		handler->writeString("parent_alt_name", piece.parent_alt_name.c_str());
		handler->writeBool("do_not_expire", piece.do_not_expire);
		handler->writeBool("is_hull", piece.is_hull);
		handler->writeString("model", piece.model.c_str());
		handler->writeInt("time_started", static_cast<std::int32_t>(piece.time_started));

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
		piece.parent_alt_name = handler->readStringOr("parent_alt_name", "");
		piece.do_not_expire = handler->readBoolOr("do_not_expire", false);
		piece.is_hull = handler->readBoolOr("is_hull", true);
		piece.model = handler->readStringOr("model", "");
		piece.time_started = static_cast<fix>(handler->readIntOr("time_started", 0));

		if (!piece.submodel.empty() && (!piece.ship_class.empty() || !piece.model.empty())) {
			data.debris.push_back(std::move(piece));
		}
	}
	handler->endArrayRead();
}

void write_beam_list(pilot::FileHandler* handler, const SCP_vector<checkpoint::beam_shot_state>& beams)
{
	handler->startArrayWrite("beams", beams.size());
	for (const auto& b : beams) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("class", b.weapon_class.c_str());
		handler->writeString("shooter", b.shooter_ship.c_str());
		handler->writeString("turret", b.turret.c_str());
		handler->writeString("target", b.target_ship.c_str());
		handler->writeString("target_subsys", b.target_subsys.c_str());
		handler->writeString("team", b.team.c_str());
		handler->writeString("state", b.weapon_state.c_str());
		write_string_list(handler, "flags", b.flags);

		write_vector(handler, "tpos1_x", "tpos1_y", "tpos1_z", b.target_pos1);
		write_vector(handler, "tpos2_x", "tpos2_y", "tpos2_z", b.target_pos2);
		write_vector(handler, "start_x", "start_y", "start_z", b.last_start);
		write_vector(handler, "shot_x", "shot_y", "shot_z", b.last_shot);
		write_vector(handler, "local_x", "local_y", "local_z", b.local_fire_position);

		handler->writeFloat("life_left", b.life_left);
		handler->writeFloat("width_factor", b.current_width_factor);
		handler->writeFloat("u_offset", b.u_offset_local);
		handler->writeFloat("glow_frame", b.beam_glow_frame);
		handler->writeInt("framecount", b.framecount);
		handler->writeInt("shot_index", b.shot_index);
		handler->writeInt("bank", b.bank);
		handler->writeInt("firingpoint", b.firingpoint);
		handler->writeInt("warmup", b.warmup_stamp);
		handler->writeInt("warmdown", b.warmdown_stamp);

		write_vector(handler, "dir_a_x", "dir_a_y", "dir_a_z", b.dir_a);
		write_vector(handler, "dir_b_x", "dir_b_y", "dir_b_z", b.dir_b);
		write_vector(handler, "rot_x", "rot_y", "rot_z", b.rot_axis);
		handler->writeInt("shot_count", b.shot_count);
		write_float_list(handler, "shot_aim", b.shot_aim);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();
}

void read_beam_list(pilot::FileHandler* handler, SCP_vector<checkpoint::beam_shot_state>& beams)
{
	beams.clear();

	if (!handler->hasField("beams")) {
		return;
	}

	auto count = handler->startArrayRead("beams");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::beam_shot_state b;

		b.weapon_class = handler->readStringOr("class", "");
		b.shooter_ship = handler->readStringOr("shooter", "");
		b.turret = handler->readStringOr("turret", "");
		b.target_ship = handler->readStringOr("target", "");
		b.target_subsys = handler->readStringOr("target_subsys", "");
		b.team = handler->readStringOr("team", "");
		b.weapon_state = handler->readStringOr("state", "");
		read_string_list(handler, "flags", b.flags);

		read_vector(handler, "tpos1_x", "tpos1_y", "tpos1_z", b.target_pos1);
		read_vector(handler, "tpos2_x", "tpos2_y", "tpos2_z", b.target_pos2);
		read_vector(handler, "start_x", "start_y", "start_z", b.last_start);
		read_vector(handler, "shot_x", "shot_y", "shot_z", b.last_shot);
		read_vector(handler, "local_x", "local_y", "local_z", b.local_fire_position);

		b.life_left = handler->readFloatOr("life_left", 0.0f);
		b.current_width_factor = handler->readFloatOr("width_factor", 1.0f);
		b.u_offset_local = handler->readFloatOr("u_offset", 0.0f);
		b.beam_glow_frame = handler->readFloatOr("glow_frame", 0.0f);
		b.framecount = handler->readIntOr("framecount", 0);
		b.shot_index = handler->readIntOr("shot_index", 0);
		b.bank = handler->readIntOr("bank", -1);
		b.firingpoint = handler->readIntOr("firingpoint", -1);
		b.warmup_stamp = handler->readIntOr("warmup", -1);
		b.warmdown_stamp = handler->readIntOr("warmdown", -1);

		read_vector(handler, "dir_a_x", "dir_a_y", "dir_a_z", b.dir_a);
		read_vector(handler, "dir_b_x", "dir_b_y", "dir_b_z", b.dir_b);
		read_vector(handler, "rot_x", "rot_y", "rot_z", b.rot_axis);
		b.shot_count = handler->readIntOr("shot_count", 0);
		read_float_list(handler, "shot_aim", b.shot_aim);

		if (!b.weapon_class.empty()) {
			beams.push_back(std::move(b));
		}
	}
	handler->endArrayRead();
}

// Everything that is currently in the air: individual weapons, and the beams being fired at
// them.  Two arrays under distinct names in one section.
// Mission state that belongs to no ship: the built-in message budget, the personas already spoken
// for, the mission mood, the training context and the reinforcement allowances.
void write_mission_extras(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	const auto& state = data.mission;

	handler->startSectionWrite(Section::CheckpointMission);

	handler->writeBool("present", state.present);

	write_int_map(handler, "player_ints", state.player_ints);
	write_int_map(handler, "training_ints", state.training_ints);
	handler->writeInt("training_speed_stamp", state.training_context_speed_timestamp);

	write_string_list(handler, "used_personas", state.used_personas);
	handler->writeInt("mission_mood", state.mission_mood);
	handler->writeBool("no_builtin_msgs", state.no_builtin_msgs);
	handler->writeBool("no_builtin_command", state.no_builtin_command);

	handler->writeBool("player_use_ai", state.player_use_ai);
	handler->writeBool("perspective_locked", state.perspective_locked);
	handler->writeBool("slew_locked", state.slew_locked);
	handler->writeInt("viewer_mode", state.viewer_mode);

	handler->startArrayWrite("preferred_primaries", state.preferred_primaries.size());
	for (const auto& entry : state.preferred_primaries) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("subject", entry.subject.c_str());
		handler->writeString("target", entry.target.c_str());
		handler->writeString("weapon", entry.weapon.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
	handler->startArrayWrite("huge_fire", state.huge_fire.size());
	for (const auto& entry : state.huge_fire) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("team", entry.team.c_str());
		handler->writeString("weapon", entry.weapon.c_str());
		handler->writeString("ship", entry.ship.c_str());
		handler->writeInt("max_fire_count", entry.max_fire_count);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->writeFloat("player_throttle", state.player_throttle);
	handler->writeBool("auto_targeting", state.auto_targeting);
	handler->writeBool("auto_match_speed", state.auto_match_speed);
	handler->writeBool("match_target", state.match_target);
	handler->writeString("death_message", state.death_message.c_str());
	handler->writeInt("friendly_hits", state.friendly_hits);
	handler->writeFloat("friendly_damage", state.friendly_damage);
	handler->writeInt("friendly_last_hit_time", static_cast<std::int32_t>(state.friendly_last_hit_time));
	handler->writeInt("last_warning_message_time", static_cast<std::int32_t>(state.last_warning_message_time));

	handler->writeBool("promoted", state.promoted);
	handler->writeBool("no_check_all_alone_msg", state.no_check_all_alone_msg);
	write_string_list(handler, "granted_ships", state.granted_ships);
	write_string_list(handler, "granted_weapons", state.granted_weapons);
	write_string_list(handler, "tech_ships", state.tech_ships);
	write_string_list(handler, "tech_weapons", state.tech_weapons);
	write_string_list(handler, "tech_intel", state.tech_intel);

	handler->startArrayWrite("message_queue", state.message_queue.size());
	for (const auto& entry : state.message_queue) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("message", entry.message.c_str());
		handler->writeString("special_message", entry.special_message.c_str());
		handler->writeString("who_from", entry.who_from.c_str());
		handler->writeInt("source", entry.source);
		handler->writeInt("builtin_type", entry.builtin_type);
		handler->writeInt("flags", entry.flags);
		handler->writeInt("group", entry.group);
		handler->writeInt("priority", entry.priority);
		handler->writeInt("time_added", static_cast<std::int32_t>(entry.time_added));
		handler->writeInt("window_timestamp", entry.window_timestamp);
		handler->writeInt("min_delay_stamp", entry.min_delay_stamp);
		handler->writeString("event_to_cancel", entry.event_to_cancel.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("squad_history", state.squad_history.size());
	for (const auto& entry : state.squad_history) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeBool("to_all_fighters", entry.to_all_fighters);
		handler->writeString("order_to", entry.order_to.c_str());
		handler->writeString("order", entry.order.c_str());
		handler->writeString("target", entry.target.c_str());
		handler->writeString("order_from", entry.order_from.c_str());
		handler->writeString("special_subsys", entry.special_subsys.c_str());
		handler->writeInt("order_time", static_cast<std::int32_t>(entry.order_time));
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("added_messages", state.added_messages.size());
	for (const auto& entry : state.added_messages) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", entry.name.c_str());
		handler->writeString("text", entry.text.c_str());
		handler->writeString("persona", entry.persona.c_str());
		handler->writeInt("multi_team", entry.multi_team);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("reinforcements", state.reinforcements.size());
	for (const auto& reinforcement : state.reinforcements) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", reinforcement.name.c_str());
		handler->writeInt("num_uses", reinforcement.num_uses);
		handler->writeBool("available", reinforcement.available);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->writeBool("hud_present", state.hud_present);
	handler->startArrayWrite("hud_gauges", state.hud_gauges.size());
	for (const auto& gauge : state.hud_gauges) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("name", gauge.name.c_str());
		handler->writeBool("custom", gauge.custom);
		handler->writeBool("active", gauge.active);
		handler->writeBool("sexp_override", gauge.sexp_override);
		handler->writeBool("sexp_color", gauge.sexp_color);
		handler->writeInt("color_r", gauge.color[0]);
		handler->writeInt("color_g", gauge.color[1]);
		handler->writeInt("color_b", gauge.color[2]);
		handler->writeInt("color_a", gauge.color[3]);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();
	handler->writeBool("hud_high_contrast", state.hud_high_contrast);
	handler->writeBool("disable_cockpits", state.disable_cockpits);
	handler->writeBool("disable_cockpit_sway", state.disable_cockpit_sway);
	handler->writeBool("sensor_static_forced", state.sensor_static_forced);

	handler->startArrayWrite("ignored_keys", state.ignored_keys.size());
	for (const auto& key : state.ignored_keys) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("action", key.action.c_str());
		handler->writeInt("count", key.count);
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("scrollback", state.scrollback.size());
	for (const auto& line : state.scrollback) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeInt("time", static_cast<std::int32_t>(line.time));
		handler->writeInt("source", line.source);
		handler->writeString("text", line.text.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->startArrayWrite("file_sounds", state.file_sounds.size());
	for (const auto& sound : state.file_sounds) {
		handler->startSectionWrite(Section::Unnamed);
		handler->writeString("filename", sound.filename.c_str());
		handler->writeInt("type", sound.type);
		handler->writeBool("loop", sound.loop);
		handler->writeBool("paused", sound.paused);
		handler->writeString("variable", sound.variable.c_str());
		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	handler->endSectionWrite();
}

void read_mission_extras(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	auto& state = data.mission;

	state.present = handler->readBoolOr("present", false);

	read_int_map(handler, "player_ints", state.player_ints);
	read_int_map(handler, "training_ints", state.training_ints);
	state.training_context_speed_timestamp = handler->readIntOr("training_speed_stamp", 0);

	read_string_list(handler, "used_personas", state.used_personas);
	state.mission_mood = handler->readIntOr("mission_mood", 0);
	state.no_builtin_msgs = handler->readBoolOr("no_builtin_msgs", false);
	state.no_builtin_command = handler->readBoolOr("no_builtin_command", false);

	state.player_use_ai = handler->readBoolOr("player_use_ai", false);
	state.perspective_locked = handler->readBoolOr("perspective_locked", false);
	state.slew_locked = handler->readBoolOr("slew_locked", false);
	state.viewer_mode = handler->readIntOr("viewer_mode", 0);

	state.preferred_primaries.clear();
	if (handler->hasField("preferred_primaries")) {
		auto count = handler->startArrayRead("preferred_primaries");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::preferred_primary_state entry;
			entry.subject = handler->readStringOr("subject", "");
			entry.target = handler->readStringOr("target", "");
			entry.weapon = handler->readStringOr("weapon", "");
			state.preferred_primaries.push_back(std::move(entry));
		}
		handler->endArrayRead();
	}
	state.huge_fire.clear();
	if (handler->hasField("huge_fire")) {
		auto count = handler->startArrayRead("huge_fire");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::huge_fire_state entry;
			entry.team = handler->readStringOr("team", "");
			entry.weapon = handler->readStringOr("weapon", "");
			entry.ship = handler->readStringOr("ship", "");
			entry.max_fire_count = handler->readIntOr("max_fire_count", 0);
			state.huge_fire.push_back(std::move(entry));
		}
		handler->endArrayRead();
	}

	state.player_throttle = handler->readFloatOr("player_throttle", 0.0f);
	state.auto_targeting = handler->readBoolOr("auto_targeting", false);
	state.auto_match_speed = handler->readBoolOr("auto_match_speed", false);
	state.match_target = handler->readBoolOr("match_target", false);
	state.death_message = handler->readStringOr("death_message", "");
	state.friendly_hits = handler->readIntOr("friendly_hits", 0);
	state.friendly_damage = handler->readFloatOr("friendly_damage", 0.0f);
	state.friendly_last_hit_time = static_cast<fix>(handler->readIntOr("friendly_last_hit_time", 0));
	state.last_warning_message_time = static_cast<fix>(handler->readIntOr("last_warning_message_time", 0));

	state.promoted = handler->readBoolOr("promoted", false);
	state.no_check_all_alone_msg = handler->readBoolOr("no_check_all_alone_msg", false);
	read_string_list(handler, "granted_ships", state.granted_ships);
	read_string_list(handler, "granted_weapons", state.granted_weapons);
	read_string_list(handler, "tech_ships", state.tech_ships);
	read_string_list(handler, "tech_weapons", state.tech_weapons);
	read_string_list(handler, "tech_intel", state.tech_intel);

	state.message_queue.clear();
	if (handler->hasField("message_queue")) {
		auto count = handler->startArrayRead("message_queue");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::message_queue_state entry;
			entry.message = handler->readStringOr("message", "");
			entry.special_message = handler->readStringOr("special_message", "");
			entry.who_from = handler->readStringOr("who_from", "");
			entry.source = handler->readIntOr("source", 0);
			entry.builtin_type = handler->readIntOr("builtin_type", -1);
			entry.flags = handler->readIntOr("flags", 0);
			entry.group = handler->readIntOr("group", 0);
			entry.priority = handler->readIntOr("priority", 0);
			entry.time_added = static_cast<fix>(handler->readIntOr("time_added", 0));
			entry.window_timestamp = handler->readIntOr("window_timestamp", 0);
			entry.min_delay_stamp = handler->readIntOr("min_delay_stamp", 0);
			entry.event_to_cancel = handler->readStringOr("event_to_cancel", "");
			if (!entry.message.empty()) {
				state.message_queue.push_back(std::move(entry));
			}
		}
		handler->endArrayRead();
	}

	state.squad_history.clear();
	if (handler->hasField("squad_history")) {
		auto count = handler->startArrayRead("squad_history");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::squadmsg_history_state entry;
			entry.to_all_fighters = handler->readBoolOr("to_all_fighters", false);
			entry.order_to = handler->readStringOr("order_to", "");
			entry.order = handler->readStringOr("order", "");
			entry.target = handler->readStringOr("target", "");
			entry.order_from = handler->readStringOr("order_from", "");
			entry.special_subsys = handler->readStringOr("special_subsys", "");
			entry.order_time = static_cast<fix>(handler->readIntOr("order_time", 0));
			if (!entry.order.empty()) {
				state.squad_history.push_back(std::move(entry));
			}
		}
		handler->endArrayRead();
	}

	state.added_messages.clear();
	if (handler->hasField("added_messages")) {
		auto count = handler->startArrayRead("added_messages");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::message_state entry;
			entry.name = handler->readStringOr("name", "");
			entry.text = handler->readStringOr("text", "");
			entry.persona = handler->readStringOr("persona", "");
			entry.multi_team = handler->readIntOr("multi_team", -1);
			if (!entry.name.empty()) {
				state.added_messages.push_back(std::move(entry));
			}
		}
		handler->endArrayRead();
	}

	state.reinforcements.clear();
	if (handler->hasField("reinforcements")) {
		auto count = handler->startArrayRead("reinforcements");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::reinforcement_state reinforcement;
			reinforcement.name = handler->readStringOr("name", "");
			reinforcement.num_uses = handler->readIntOr("num_uses", 0);
			reinforcement.available = handler->readBoolOr("available", false);
			state.reinforcements.push_back(std::move(reinforcement));
		}
		handler->endArrayRead();
	}

	state.hud_present = handler->readBoolOr("hud_present", false);
	state.hud_gauges.clear();
	if (handler->hasField("hud_gauges")) {
		auto count = handler->startArrayRead("hud_gauges");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::hud_gauge_state gauge;
			gauge.name = handler->readStringOr("name", "");
			gauge.custom = handler->readBoolOr("custom", false);
			gauge.active = handler->readBoolOr("active", true);
			gauge.sexp_override = handler->readBoolOr("sexp_override", false);
			gauge.sexp_color = handler->readBoolOr("sexp_color", false);
			gauge.color[0] = handler->readIntOr("color_r", 0);
			gauge.color[1] = handler->readIntOr("color_g", 0);
			gauge.color[2] = handler->readIntOr("color_b", 0);
			gauge.color[3] = handler->readIntOr("color_a", 0);
			if (!gauge.name.empty()) {
				state.hud_gauges.push_back(std::move(gauge));
			}
		}
		handler->endArrayRead();
	}
	state.hud_high_contrast = handler->readBoolOr("hud_high_contrast", false);
	state.disable_cockpits = handler->readBoolOr("disable_cockpits", false);
	state.disable_cockpit_sway = handler->readBoolOr("disable_cockpit_sway", false);
	state.sensor_static_forced = handler->readBoolOr("sensor_static_forced", false);

	state.ignored_keys.clear();
	if (handler->hasField("ignored_keys")) {
		auto count = handler->startArrayRead("ignored_keys");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::ignored_key_state key;
			key.action = handler->readStringOr("action", "");
			key.count = handler->readIntOr("count", 0);
			if (!key.action.empty()) {
				state.ignored_keys.push_back(std::move(key));
			}
		}
		handler->endArrayRead();
	}

	state.scrollback.clear();
	if (handler->hasField("scrollback")) {
		auto count = handler->startArrayRead("scrollback");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::scrollback_line_state line;
			line.time = static_cast<fix>(handler->readIntOr("time", 0));
			line.source = handler->readIntOr("source", 0);
			line.text = handler->readStringOr("text", "");
			if (!line.text.empty()) {
				state.scrollback.push_back(std::move(line));
			}
		}
		handler->endArrayRead();
	}

	state.file_sounds.clear();
	if (handler->hasField("file_sounds")) {
		auto count = handler->startArrayRead("file_sounds");
		for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
			checkpoint::file_sound_state sound;
			sound.filename = handler->readStringOr("filename", "");
			sound.type = handler->readIntOr("type", -1);
			sound.loop = handler->readBoolOr("loop", false);
			sound.paused = handler->readBoolOr("paused", false);
			sound.variable = handler->readStringOr("variable", "");
			if (!sound.filename.empty()) {
				state.file_sounds.push_back(std::move(sound));
			}
		}
		handler->endArrayRead();
	}
}

void write_projectiles(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointProjectiles);

	handler->startArrayWrite("projectiles", data.projectiles.size());
	for (const auto& shot : data.projectiles) {
		handler->startSectionWrite(Section::Unnamed);

		handler->writeString("class", shot.weapon_class.c_str());
		handler->writeString("team", shot.team.c_str());
		handler->writeString("species", shot.species.c_str());
		handler->writeString("state", shot.weapon_state.c_str());
		write_string_list(handler, "flags", shot.flags);

		write_vector(handler, "pos_x", "pos_y", "pos_z", shot.pos);
		write_vector(handler, "fvec_x", "fvec_y", "fvec_z", shot.orient.vec.fvec);
		write_vector(handler, "uvec_x", "uvec_y", "uvec_z", shot.orient.vec.uvec);
		write_vector(handler, "rvec_x", "rvec_y", "rvec_z", shot.orient.vec.rvec);
		write_vector(handler, "vel_x", "vel_y", "vel_z", shot.velocity);
		write_vector(handler, "dvel_x", "dvel_y", "dvel_z", shot.desired_velocity);
		write_vector(handler, "start_x", "start_y", "start_z", shot.start_pos);

		handler->writeFloat("hull", shot.hull);
		handler->writeFloat("lifeleft", shot.lifeleft);
		handler->writeInt("creation_time", static_cast<int>(shot.creation_time));
		handler->writeInt("group_id", shot.group_id);

		handler->writeString("parent", shot.parent_ship.c_str());
		handler->writeString("parent_turret", shot.parent_turret.c_str());

		handler->writeString("homing_ship", shot.homing_ship.c_str());
		handler->writeString("homing_subsys", shot.homing_subsys.c_str());
		handler->writeBool("has_homing_pos", shot.has_homing_pos);
		write_vector(handler, "homing_x", "homing_y", "homing_z", shot.homing_pos);

		handler->writeFloat("det_range", shot.det_range);
		handler->writeFloat("max_vel", shot.weapon_max_vel);
		handler->writeFloat("launch_speed", shot.launch_speed);
		handler->writeFloat("alpha", shot.alpha_current);
		handler->writeBool("alpha_backward", shot.alpha_backward);

		handler->writeInt("lssm_stage", shot.lssm_stage);
		handler->writeInt("lssm_warpout", static_cast<int>(shot.lssm_warpout_time));
		handler->writeInt("lssm_warpin", static_cast<int>(shot.lssm_warpin_time));
		write_vector(handler, "lssm_x", "lssm_y", "lssm_z", shot.lssm_target_pos);

		handler->writeInt("cmeasure_timer", shot.cmeasure_timer);

		write_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", shot.rotational_velocity);
		handler->writeInt("mine_chase_expires", shot.mine_chase_expires);
		handler->writeInt("mine_chase_cooldown_expires", shot.mine_chase_cooldown_expires);
		write_int_list(handler, "last_spawn_times", shot.last_spawn_times);
		handler->writeInt("big_attack_point_stamp", shot.big_attack_point_stamp);
		write_vector(handler, "big_attack_x", "big_attack_y", "big_attack_z", shot.big_attack_point);
		handler->writeInt("collision_group_id", shot.collision_group_id);

		handler->endSectionWrite();
	}
	handler->endArrayWrite();

	write_beam_list(handler, data.beams);

	handler->endSectionWrite();
}

void read_projectiles(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	read_beam_list(handler, data.beams);

	data.projectiles.clear();

	if (!handler->hasField("projectiles")) {
		return;
	}

	auto count = handler->startArrayRead("projectiles");
	for (size_t i = 0; i < count; i++, handler->nextArraySection()) {
		checkpoint::projectile_state shot;

		shot.weapon_class = handler->readStringOr("class", "");
		shot.team = handler->readStringOr("team", "");
		shot.species = handler->readStringOr("species", "");
		shot.weapon_state = handler->readStringOr("state", "");
		read_string_list(handler, "flags", shot.flags);

		read_vector(handler, "pos_x", "pos_y", "pos_z", shot.pos);
		read_vector(handler, "fvec_x", "fvec_y", "fvec_z", shot.orient.vec.fvec);
		read_vector(handler, "uvec_x", "uvec_y", "uvec_z", shot.orient.vec.uvec);
		read_vector(handler, "rvec_x", "rvec_y", "rvec_z", shot.orient.vec.rvec);
		read_vector(handler, "vel_x", "vel_y", "vel_z", shot.velocity);
		read_vector(handler, "dvel_x", "dvel_y", "dvel_z", shot.desired_velocity);
		read_vector(handler, "start_x", "start_y", "start_z", shot.start_pos);

		shot.hull = handler->readFloatOr("hull", 0.0f);
		shot.lifeleft = handler->readFloatOr("lifeleft", 0.0f);
		shot.creation_time = handler->readIntOr("creation_time", 0);
		shot.group_id = handler->readIntOr("group_id", -1);

		shot.parent_ship = handler->readStringOr("parent", "");
		shot.parent_turret = handler->readStringOr("parent_turret", "");

		shot.homing_ship = handler->readStringOr("homing_ship", "");
		shot.homing_subsys = handler->readStringOr("homing_subsys", "");
		shot.has_homing_pos = handler->readBoolOr("has_homing_pos", false);
		read_vector(handler, "homing_x", "homing_y", "homing_z", shot.homing_pos);

		shot.det_range = handler->readFloatOr("det_range", 0.0f);
		shot.weapon_max_vel = handler->readFloatOr("max_vel", 0.0f);
		shot.launch_speed = handler->readFloatOr("launch_speed", 0.0f);
		shot.alpha_current = handler->readFloatOr("alpha", 1.0f);
		shot.alpha_backward = handler->readBoolOr("alpha_backward", false);

		shot.lssm_stage = handler->readIntOr("lssm_stage", -1);
		shot.lssm_warpout_time = handler->readIntOr("lssm_warpout", 0);
		shot.lssm_warpin_time = handler->readIntOr("lssm_warpin", 0);
		read_vector(handler, "lssm_x", "lssm_y", "lssm_z", shot.lssm_target_pos);

		shot.cmeasure_timer = handler->readIntOr("cmeasure_timer", 0);

		read_vector(handler, "rotvel_x", "rotvel_y", "rotvel_z", shot.rotational_velocity);
		shot.mine_chase_expires = handler->readIntOr("mine_chase_expires", -1);
		shot.mine_chase_cooldown_expires = handler->readIntOr("mine_chase_cooldown_expires", -1);
		read_int_list(handler, "last_spawn_times", shot.last_spawn_times);
		shot.big_attack_point_stamp = handler->readIntOr("big_attack_point_stamp", 0);
		read_vector(handler, "big_attack_x", "big_attack_y", "big_attack_z", shot.big_attack_point);
		shot.collision_group_id = handler->readIntOr("collision_group_id", 0);

		if (!shot.weapon_class.empty()) {
			data.projectiles.push_back(std::move(shot));
		}
	}
	handler->endArrayRead();
}

void write_scoring(pilot::FileHandler* handler, const checkpoint::checkpoint_data& data)
{
	handler->startSectionWrite(Section::CheckpointScoring);

	write_int_map(handler, "ints", data.scoring.ints);
	write_int_map(handler, "class_kills", data.scoring.class_kills);
	handler->writeString("medal_earned", data.scoring.medal_earned.c_str());

	handler->endSectionWrite();
}

void read_scoring(pilot::FileHandler* handler, checkpoint::checkpoint_data& data)
{
	read_int_map(handler, "ints", data.scoring.ints);
	read_int_map(handler, "class_kills", data.scoring.class_kills);
	data.scoring.medal_earned = handler->readStringOr("medal_earned", "");
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
		auto json_handler = new pilot::JSONFileHandler(fp, true);
		json_handler->setThrowOnReadError(true);
		json_handler->setDecodeStrings(true);
		handler.reset(json_handler);
	} catch (const std::exception&) {
		// Not our file, or not valid JSON.  Enumeration walks whatever is in the directory, so
		// this is a perfectly ordinary thing to run into.  The handler's constructor threw before
		// there was a handler to own the file, so it is still ours to close.
		cfclose(fp);
		return false;
	}

	bool found = false;

	// A damaged file is skipped like any other stranger in the directory; see checkpoint_read().
	try {
		if (handler->readUIntOr("signature", 0) != checkpoint::CHECKPOINT_FILE_ID) {
			return false;
		}

		handler->beginSectionRead();
		while (handler->hasMoreSections()) {
			if (handler->nextSection() == Section::CheckpointInfo) {
				read_info(handler.get(), data);
				found = true;
				break;
			}
		}
		handler->endSectionRead();
	} catch (const std::exception& e) {
		mprintf(("CHECKPOINT => Skipping damaged '%s': %s\n", filename.c_str(), e.what()));
		return false;
	}

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

	// The same locations the write and the single delete name: checkpoint_find_files() looks in
	// the game root as well as the user root, so a file it found there has to be deletable there.
	for (const auto& found : checkpoint_find_files(mission_name)) {
		if (cf_delete(found.filename.c_str(),
			CF_TYPE_CHECKPOINTS,
			CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT)) {
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

	// No explicit cf_create_directory() here: cfopen() creates the directory itself, with the
	// location flags it is given.  Called on its own with the default flags it would create an
	// empty checkpoints directory inside the active mod's folder instead.
	auto fp = cfopen(filename.c_str(), "wb", CF_TYPE_CHECKPOINTS, false,
	                 CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
	if (fp == nullptr) {
		mprintf(("CHECKPOINT => Unable to open '%s' for writing!\n", filename.c_str()));
		return false;
	}

	// The handler takes ownership of the file and closes it in its destructor.  The concrete type
	// is kept to hand because only it can say whether the final write went through.
	auto json_handler = new pilot::JSONFileHandler(fp, false);
	std::unique_ptr<pilot::FileHandler> handler(json_handler);

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
	write_world(handler.get(), data);
	write_projectiles(handler.get(), data);
	write_mission_extras(handler.get(), data);
	write_script_data(handler.get(), data);

	handler->endWritingSections();

	handler->flush();

	// Opening for writing truncated whatever was there, so a dump that failed part-way (disk
	// full, most likely) has left a file that is not a checkpoint.  Better none at all than one
	// that will be rejected as corrupt on every future read.
	if (json_handler->writeFailed()) {
		handler.reset();   // closes the file first
		cf_delete(filename.c_str(),
			CF_TYPE_CHECKPOINTS,
			CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
		mprintf(("CHECKPOINT => Failed to write '%s'; the partial file has been removed.\n", filename.c_str()));
		return false;
	}

	mprintf(("CHECKPOINT => Wrote '%s' (%d ships, %d wings, %d variables, %d events, %d goals, %d log entries, "
	         "%d debris, %d pending arrivals)\n",
	         filename.c_str(),
	         static_cast<int>(data.ships.size()),
	         static_cast<int>(data.wings.size()),
	         static_cast<int>(data.variables.size()),
	         static_cast<int>(data.events.size()),
	         static_cast<int>(data.goals.size()),
	         static_cast<int>(data.log_entries.size()),
	         static_cast<int>(data.debris.size()),
	         static_cast<int>(data.parse_objects.size())));

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
		auto json_handler = new pilot::JSONFileHandler(fp, true);
		json_handler->setThrowOnReadError(true);
		json_handler->setDecodeStrings(true);
		handler.reset(json_handler);
	} catch (const std::exception& e) {
		mprintf(("CHECKPOINT => Failed to parse '%s': %s\n", filename.c_str(), e.what()));
		// The constructor threw before a handler existed to own the file.  Left open, a checkpoint
		// truncated by a crash would cost one CFILE block on every mission entry.
		cfclose(fp);
		return false;
	}

	// A file that parses as JSON but has the wrong type where a value belongs (hand-edited, or
	// written by something else) throws out of the handler rather than stopping the game; the
	// resume prompt reads the default slot on every mission entry, so a hard error here would
	// make the mission unplayable until the file was deleted by hand.
	auto read_sections = [&]() -> bool {
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

			case Section::CheckpointWorld:
				read_world(handler.get(), data);
				break;

			case Section::CheckpointProjectiles:
				read_projectiles(handler.get(), data);
				break;

			case Section::CheckpointMission:
				read_mission_extras(handler.get(), data);
				break;

			case Section::CheckpointScriptData:
				read_script_data(handler.get(), data);
				break;

			default:
				// A section this build does not know about -- most likely written by a newer
				// engine.  Skipping it is the whole point of the sectioned layout.
				mprintf(("CHECKPOINT => Skipping unknown section 0x%04x.\n", static_cast<int>(section_id)));
				break;
			}
		}
		handler->endSectionRead();
		return true;
	};

	try {
		if (!read_sections()) {
			return false;
		}
	} catch (const std::exception& e) {
		mprintf(("CHECKPOINT => '%s' is damaged: %s\n", filename.c_str(), e.what()));
		data = checkpoint_data();
		return false;
	}

	data.slot = slot;
	data.loaded = true;

	mprintf(("CHECKPOINT => Read '%s' (%d ships, %d wings, %d variables)\n",
	         filename.c_str(),
	         static_cast<int>(data.ships.size()),
	         static_cast<int>(data.wings.size()),
	         static_cast<int>(data.variables.size())));

	return true;
}

bool checkpoint_same_mission(const char* a, const char* b)
{
	return lcase_equal(base_name(a), base_name(b));
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

	if (!checkpoint_same_mission(data.mission_filename.c_str(), Game_current_mission_filename)) {
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

	// The same locations checkpoint_write() opens for writing.  Scoping the delete to the user
	// root alone would leave a checkpoint that had landed in the game root on disk, and
	// checkpoint_exists() would go on finding it after delete-checkpoint said it was gone.
	cf_delete(filename.c_str(),
		CF_TYPE_CHECKPOINTS,
		CF_LOCATION_ROOT_USER | CF_LOCATION_ROOT_GAME | CF_LOCATION_TYPE_ROOT);
}

} // namespace checkpoint
