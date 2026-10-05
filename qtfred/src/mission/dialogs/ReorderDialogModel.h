#pragma once
#include "mission/dialogs/AbstractDialogModel.h"

#include <utility>

namespace fso::fred::dialogs {

// Model for the Reorder dialog.  A direct-edit model: each move is applied to the
// mission immediately, reordering how the object type is written to the mission file 
// and shown in Scene Browser.
class ReorderDialogModel : public AbstractDialogModel {
	Q_OBJECT

public:
	enum class Type {
		Ships,
		Wings,
		Props,
		WaypointLists,
		JumpNodes,
		CoordinatePoints,
	};

	// How a group move places the selected items.  Up and Down move each item one position and
	// stop at the end of the list, so a block of selected items slides as a unit and gaps between
	// selected items are kept; Top and Bottom gather the items at that end in their current order.
	enum class MoveKind {
		Top,
		Up,
		Down,
		Bottom,
	};

	// A single move from one display position to another; a group move is a sequence of these.
	using Step = std::pair<int, int>;

	ReorderDialogModel(QObject* parent, EditorViewport* viewport);

	bool apply() override;
	void reject() override;

	// Display names for the given type, in current storage (mission-file) order.
	static SCP_vector<SCP_string> getItemNames(Type type);

	// Move the items at the given display rows as a group, applying the reorder to the mission
	// immediately.  Returns the items' new rows; the single moves it made are appended to steps,
	// for the undo command.
	SCP_vector<int> moveItems(Type type, const SCP_vector<int>& rows, MoveKind kind, SCP_vector<Step>& steps);

	// Replay a group move's steps (forward), or undo them (reverse order, each move inverted).
	static void applySteps(EditorViewport* viewport, Type type, const SCP_vector<Step>& steps, bool reverse);

	// Ships tab: the rows of every ship in the same wing as the ship at row (empty if it has none).
	static SCP_vector<int> getSameWingShipRows(int row);

	// The reorder itself, without the modified/changed bookkeeping.  Shared with
	// ReorderCommand so undo/redo can replay a move without standing up a model.
	// rotate_*_slots() preserves the set of occupied slots, so applyMove(to, from)
	// is an exact inverse of applyMove(from, to).
	static void applyMove(EditorViewport* viewport, Type type, int from_pos, int to_pos);

private: // NOLINT(readability-redundant-access-specifiers)
	// The occupied storage indices for the given type, in display order.  For
	// ships/wings/props/jump nodes these are the live Ships[]/Wings[]/Props[]/
	// Jump_nodes[] slots; for waypoint lists and coordinate points, whose storage
	// has no empty entries, they are simply 0..N-1.
	static SCP_vector<int> getSlots(Type type);
};

} // namespace fso::fred::dialogs
