#pragma once
#include <qevent.h>
#include "dialogs/AbstractDialogModel.h"
#include "EditorViewport.h"

void generate_weaponry_usage_list_team(int team, int *arr);

void generate_weaponry_usage_list_wing(int wing_num, int *arr);

void time_to_mission_info_string(const std::tm* src, char* dest, size_t dest_max_len);

bool rejectOrCloseHandler(QDialog* dialog,
	fso::fred::dialogs::AbstractDialogModel* model,
	fso::fred::EditorViewport* viewport);