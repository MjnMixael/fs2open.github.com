#pragma once

// Reference index for the Events editor graph view.
//
// Walks the dialog's WORKING-COPY sexp trees (the shared SexpTreeModel's
// tree_nodes) and records, for every argument that names an OFP data object
// (ship / wing / message / goal / event / waypoint / team / variable), a
// reference tuple. The graph view's relationship modes are all projections of
// this index. See memory `events-graph-view`.
//
// The index is derived and cheap; it is rebuilt from scratch whenever the
// working events change (edit / undo / redo / advanced-edit commit).

#include "globalincs/pstypes.h"

#include <unordered_map>

class SexpTreeModel;
struct mission_event;

namespace fso::fred {

enum class RefObjectKind {
	Unknown = 0,
	Ship,
	Wing,
	Prop,
	Message,
	Goal,
	Event,
	Waypoint,        // waypoint path
	JumpNode,
	CoordinatePoint,
	Team,
	Variable,
};

// Where a reference sits relative to its event's condition/action structure,
// determined by the nearest enclosing when/every-time trigger (arg 0 = the
// condition branch, later args = action branches).
enum class RefRole {
	Other = 0,
	Condition,
	Action,
};

struct EventObjectRef {
	SCP_string    name;       // object name as it appears in the sexp
	RefObjectKind kind = RefObjectKind::Unknown;
	int           eventIndex = -1; // index into the events list passed to rebuild()
	int           treeNode = -1;   // tree_nodes[] index of the referencing (leaf) node
	int           operatorNode = -1; // tree_nodes[] index of the operator holding the arg
	SCP_string    operatorText;      // display text of that operator (its name only)
	SCP_string    expression;        // full text of that operator's sub-expression
	SCP_vector<SCP_string> args;     // each argument of that operator, as display text
	RefRole       role = RefRole::Other;
};

class EventReferenceIndex {
  public:
	// Rebuild the whole index from the working events. `events[i].formula` is a
	// tree_nodes[] index (dialog working copy), `events[i].name` the event name.
	void rebuild(const SexpTreeModel& tree, const SCP_vector<mission_event>& events);
	void clear();

	// Every reference to the given object (kind + name), case-insensitive.
	SCP_vector<EventObjectRef> referencesTo(RefObjectKind kind, const SCP_string& name) const;

	// Distinct event indices referencing the object, in first-seen order.
	SCP_vector<int> eventsReferencing(RefObjectKind kind, const SCP_string& name) const;

	// Referencing sites for the object, deduped by operator node (one operator
	// that names the object twice counts once). Each carries its event + role.
	SCP_vector<EventObjectRef> referenceSites(RefObjectKind kind, const SCP_string& name) const;

	// Number of distinct events referencing the object.
	int eventReferenceCount(RefObjectKind kind, const SCP_string& name) const;

	const SCP_vector<EventObjectRef>& allReferences() const { return m_refs; }

	static const char* kindLabel(RefObjectKind kind);

  private:
	static SCP_string makeKey(RefObjectKind kind, const SCP_string& name);
	static RefObjectKind classify(int opf, const char* token);
	static RefRole classifyRole(const SexpTreeModel& tree, int operatorNode);
	static SCP_string nodeToText(const SexpTreeModel& tree, int node);

	void walkNode(const SexpTreeModel& tree, int node, int eventIndex);

	SCP_vector<EventObjectRef> m_refs;
	// key -> indices into m_refs
	std::unordered_map<SCP_string, SCP_vector<int>> m_forward;
};

} // namespace fso::fred
