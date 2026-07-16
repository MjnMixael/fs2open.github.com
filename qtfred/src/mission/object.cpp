#include "object.h"
#include "globalincs/linklist.h"
#include "object/object.h"
#include "object/waypoint.h"
#include "object/objectdock.h"
#include "ship/ship.h"
#include "jumpnode/jumpnode.h"
#include "prop/prop.h"

void object_moved(object *objp)
{
	if (objp->type == OBJ_WAYPOINT)
	{
		waypoint *wpt = find_waypoint_with_instance(objp->instance);
		Assert(wpt != NULL);
		wpt->set_pos(&objp->pos);
	}

	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) // do we have a ship?
	{
		// reset the already-handled flag (inefficient, but it's FRED, so who cares)
		for (int i = 0; i < MAX_OBJECTS; i++)
			Objects[i].flags.set(Object::Object_Flags::Docked_already_handled);

		// move all docked objects docked to me
		dock_move_docked_objects(objp);
	}
}

bool query_valid_object(int index)
{
	bool obj_found = false;
	object *ptr;

	if (index < 0 || index >= MAX_OBJECTS || Objects[index].type == OBJ_NONE)
		return false;

	ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		Assert(ptr->type != OBJ_NONE);
		if (OBJ_INDEX(ptr) == index)
			obj_found = true;

		ptr = GET_NEXT(ptr);
	}

	Assert(obj_found);  // just to make sure it's in the list like it should be.
	return true;
}



const char* object_name(int obj) {
	static char text[80];

	if (!query_valid_object(obj))
		return "*none*";

	switch (Objects[obj].type) {
	case OBJ_SHIP:
	case OBJ_START:
		return Ships[Objects[obj].instance].ship_name;

	case OBJ_WAYPOINT:
		waypoint_stuff_name(text, Objects[obj].instance);
		return text;

	case OBJ_JUMP_NODE: {
		const CJumpNode* jnp = jumpnode_get_by_objnum(obj);
		if (jnp != nullptr)
			return jnp->GetName();
		break;
	}

	case OBJ_PROP: {
		int idx = Objects[obj].instance;
		if (idx >= 0 && idx < (int)Props.size() && Props[idx].has_value())
			return Props[idx]->prop_name;
		break;
	}
	}

	return "*unknown*";
}
int get_ship_from_obj(int obj) {
	Assertion(query_valid_object(obj), "Invalid object index detected!");

	return get_ship_from_obj(&Objects[obj]);
}
int get_ship_from_obj(object* objp) {
	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START))
		return objp->instance;

	Int3();
	return 0;
}

SCP_string fred_object_name_collision(const char* name, int except_objnum, const waypoint_list* except_wp_list)
{
	if (name == nullptr || *name == '\0')
		return "";

	// ships, starts, and props all share the object list
	for (object* ptr = GET_FIRST(&obj_used_list); ptr != END_OF_LIST(&obj_used_list); ptr = GET_NEXT(ptr)) {
		if (OBJ_INDEX(ptr) == except_objnum)
			continue;

		if ((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) {
			if (!stricmp(name, Ships[ptr->instance].ship_name))
				return "a ship";
		} else if (ptr->type == OBJ_PROP) {
			int idx = ptr->instance;
			if (idx >= 0 && idx < static_cast<int>(Props.size()) && Props[idx].has_value() &&
				!stricmp(name, Props[idx]->prop_name))
				return "a prop";
		}
	}

	// wings
	for (auto& w : Wings) {
		if (w.wave_count && !stricmp(name, w.name))
			return "a wing";
	}

	// AI target-priority groups
	for (auto& tp : Ai_tp_list) {
		if (!stricmp(name, tp.name))
			return "a target priority group";
	}

	// waypoint paths
	for (auto& wp_list : Waypoint_lists) {
		if (&wp_list != except_wp_list && !stricmp(name, wp_list.get_name()))
			return "a waypoint path";
	}

	// jump nodes
	auto* jnp = jumpnode_get_by_name(name);
	if (jnp != nullptr && jnp->GetSCPObjectNumber() != except_objnum)
		return "a jump node";

	return "";
}
