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

// Whole-mission operator dataflow graph, for the Basic graph view. Every sexp
// operator is a node; literal (non-object) leaf args are inlined into the card;
// object-naming leaf args become edges to shared object nodes. The position
// fields are filled by the dialog from saved annotations (the builder itself is
// stateless); they seed the view's layout when set.
struct BasicOpNode {
	int        treeNode = -1;   // tree_nodes[] index; also the annotation key for its position
	int        eventIndex = -1;
	bool       isCond = false;  // condition vs action subtree (card color)
	SCP_string opText;          // operator name
	SCP_string expression;      // full sub-expression text
	SCP_vector<SCP_string> inlineArgs; // literal args shown inside the card
	SCP_vector<int> childOps;   // indices into BasicGraph::ops
	SCP_vector<int> objectRefs; // indices into BasicGraph::objects

	int   posKey = -1;          // annotation key to persist this node's position under
	bool  hasPos = false;
	float posX = 0.0f;
	float posY = 0.0f;
};

struct BasicObjNode {
	RefObjectKind kind = RefObjectKind::Unknown;
	SCP_string    name;
	int           repTreeNode = -1;    // representative reference leaf (the position key)
	SCP_vector<int> refTreeNodes;      // all reference leaves (position read fallback)

	int   posKey = -1;
	bool  hasPos = false;
	float posX = 0.0f;
	float posY = 0.0f;
};

// One node per event (the trigger card, like the event nodes in the radial /
// swimlanes modes). Its root operator hangs off it to the right.
struct BasicEventNode {
	int   eventIndex = -1;
	int   rootOp = -1;   // index into BasicGraph::ops, or -1 if the event has no operator root
	int   posKey = -1;   // rootKey(formula) annotation key for this event node's position

	bool  hasPos = false;
	float posX = 0.0f;
	float posY = 0.0f;
};

struct BasicGraph {
	SCP_vector<BasicOpNode>    ops;
	SCP_vector<BasicObjNode>   objects;
	SCP_vector<BasicEventNode> events; // one per event, in event order
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

	// Build the whole-mission operator dataflow graph (Basic view). Stateless
	// with respect to the index; walks the same working tree as rebuild().
	BasicGraph buildBasicGraph(const SexpTreeModel& tree, const SCP_vector<mission_event>& events) const;

	static const char* kindLabel(RefObjectKind kind);

  private:
	static SCP_string makeKey(RefObjectKind kind, const SCP_string& name);
	static RefObjectKind classify(int opf, const char* token);
	static RefRole classifyRole(const SexpTreeModel& tree, int operatorNode);
	static SCP_string nodeToText(const SexpTreeModel& tree, int node);
	// Classify a leaf node as an object reference; returns false for operators,
	// literals, and anything not naming a first-class object.
	static bool leafObject(const SexpTreeModel& tree, int node, RefObjectKind& kind, SCP_string& name);

	void walkNode(const SexpTreeModel& tree, int node, int eventIndex);
	// Recursively add an operator subtree to a BasicGraph; returns its ops[] index.
	int buildOpSubtree(const SexpTreeModel& tree, int node, int eventIndex, BasicGraph& g,
		std::unordered_map<int, int>& nodeToOp, std::unordered_map<SCP_string, int>& objKey) const;

	SCP_vector<EventObjectRef> m_refs;
	// key -> indices into m_refs
	std::unordered_map<SCP_string, SCP_vector<int>> m_forward;
};

} // namespace fso::fred
