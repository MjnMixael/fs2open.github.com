#pragma once

#include <globalincs/pstypes.h>

class object;
class waypoint_list;

void    object_moved(object *ptr);
bool     query_valid_object(int index);

const char *object_name(int obj);

int get_ship_from_obj(int obj);

int get_ship_from_obj(object *objp);

// Checks whether an object name is already taken anywhere in the shared object-name namespace
// (ships/starts, props, wings, AI target-priority groups, waypoint paths, jump nodes). Returns a
// human-readable description of the conflicting owner (e.g. "a ship", "a prop"), or an empty string
// if the name is free.
//   except_objnum   - object number being renamed (ship, prop, or jump node), so it doesn't collide
//                     with itself; pass -1 for a brand new name.
//   except_wp_list  - waypoint path being renamed, excluded from the waypoint-path check; nullptr otherwise.
SCP_string fred_object_name_collision(const char *name, int except_objnum = -1, const waypoint_list *except_wp_list = nullptr);
