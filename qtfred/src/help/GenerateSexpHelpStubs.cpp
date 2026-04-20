#include "ship/ship.h"

int Fred_running = 1;
int Show_cpu     = 0;

void game_start_subspace_ambient_sound() {}
void game_stop_subspace_ambient_sound() {}

char Fred_callsigns[MAX_SHIPS][NAME_LENGTH + 1] = {};
char Fred_alt_names[MAX_SHIPS][NAME_LENGTH + 1] = {};
