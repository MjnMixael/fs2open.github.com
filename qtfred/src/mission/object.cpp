#include "object.h"
#include "globalincs/linklist.h"
#include "object/object.h"
#include "object/waypoint.h"
#include "object/objectdock.h"
#include "ship/ship.h"
#include "missioneditor/common.h"

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

const char* object_name(int obj) {
	static char text[80];
	waypoint_list *wp_list;
	int waypoint_num;

	if (!query_valid_object(obj))
		return "*none*";

	switch (Objects[obj].type) {
	case OBJ_SHIP:
	case OBJ_START:
		return Ships[Objects[obj].instance].ship_name;

	case OBJ_WAYPOINT:
		wp_list = find_waypoint_list_with_instance(Objects[obj].instance, &waypoint_num);
		Assert(wp_list != NULL);
		sprintf(text, "%s:%d", wp_list->get_name(), waypoint_num + 1);
		return text;

	case OBJ_POINT:
		return "Briefing icon";
	}

	return "*unknown*";
}
