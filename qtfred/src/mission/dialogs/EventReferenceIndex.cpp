#include "EventReferenceIndex.h"

#include "missioneditor/sexp_annotation_model.h"
#include "missioneditor/sexp_tree_model.h"

#include <mission/missiongoals.h>
#include <parse/parselo.h>
#include <parse/sexp.h>
#include <ship/ship.h>
#include <iff_defs/iff_defs.h>
#include <prop/prop.h>
#include <object/waypoint.h>
#include <coordinate_points/coordinate_point.h>

#include <algorithm>
#include <cctype>

namespace fso::fred {

void EventReferenceIndex::clear()
{
	m_refs.clear();
	m_forward.clear();
}

void EventReferenceIndex::rebuild(const SexpTreeModel& tree, const SCP_vector<mission_event>& events)
{
	clear();

	for (int i = 0; i < static_cast<int>(events.size()); ++i) {
		const int root = events[i].formula; // tree_nodes[] index
		if (root < 0 || root >= static_cast<int>(tree.tree_nodes.size()))
			continue;
		if (tree.tree_nodes[root].type == SEXPT_UNUSED)
			continue;
		walkNode(tree, root, i);
	}
}

// Recurse the working subtree (child then next), recording object references.
void EventReferenceIndex::walkNode(const SexpTreeModel& tree, int node, int eventIndex)
{
	if (node < 0 || node >= static_cast<int>(tree.tree_nodes.size()))
		return;

	const sexp_tree_item& n = tree.tree_nodes[node];
	if (n.type == SEXPT_UNUSED)
		return;

	const int nodeType = SEXPT_TYPE(n.type);

	// Only data atoms can name objects; operator nodes are structure, numbers
	// are literals. Variables are handled specially below.
	if (nodeType != SEXPT_OPERATOR) {
		RefObjectKind kind = RefObjectKind::Unknown;
		SCP_string name;

		if (leafObject(tree, node, kind, name)) {
			EventObjectRef ref;
			ref.name = name;
			ref.kind = kind;
			ref.eventIndex = eventIndex;
			ref.treeNode = node;

			// The operator holding this argument (e.g. is-destroyed-delay) is the
			// node's parent; that's the card the graph shows, and its position
			// relative to the event's when-structure gives the cond/action role.
			const int op = n.parent;
			if (op >= 0 && op < static_cast<int>(tree.tree_nodes.size())) {
				ref.operatorNode = op;
				ref.operatorText = tree.tree_nodes[op].text;
				ref.expression = nodeToText(tree, op);
				ref.role = classifyRole(tree, op);
				for (int c = tree.tree_nodes[op].child; c != -1; c = tree.tree_nodes[c].next)
					ref.args.push_back(nodeToText(tree, c));
			}

			const int idx = static_cast<int>(m_refs.size());
			m_refs.push_back(std::move(ref));
			m_forward[makeKey(kind, m_refs[idx].name)].push_back(idx);
		}
	}

	// Recurse children then siblings.
	for (int c = n.child; c != -1; c = tree.tree_nodes[c].next)
		walkNode(tree, c, eventIndex);
}

// Map an OPF argument type (+ the token, for the ambiguous ship/wing families)
// to a concrete object kind. Only the kinds the graph cares about are surfaced;
// everything else (numbers, bools, strings, subsystems for now) is Unknown.
// Resolve an ambiguous name (ship/wing/team/prop/waypoint/coordinate) by
// lookup, ship first (names don't collide across the object namespaces).
// Mirrors the checks in check_sexp_syntax (sexp.cpp:2540-2594).
static RefObjectKind resolveAmbiguous(const char* token)
{
	if (ship_name_lookup(token, 1) >= 0)
		return RefObjectKind::Ship;
	if (wing_name_lookup(token, 1) >= 0)
		return RefObjectKind::Wing;
	if (prop_name_lookup(token) >= 0)
		return RefObjectKind::Prop;
	if (iff_lookup(token) >= 0)
		return RefObjectKind::Team;
	if (find_matching_waypoint_list(token) != nullptr)
		return RefObjectKind::Waypoint;
	if (find_coordinate_point_by_name(token) != nullptr)
		return RefObjectKind::CoordinatePoint;
	return RefObjectKind::Unknown;
}

RefObjectKind EventReferenceIndex::classify(int opf, const char* token)
{
	switch (opf) {
	case OPF_SHIP:
		return RefObjectKind::Ship;
	case OPF_WING:
		return RefObjectKind::Wing;
	case OPF_PROP:
		return RefObjectKind::Prop;
	case OPF_MESSAGE:
	case OPF_MESSAGE_OR_STRING:
		return RefObjectKind::Message;
	case OPF_GOAL_NAME:
		return RefObjectKind::Goal;
	case OPF_EVENT_NAME:
		return RefObjectKind::Event;
	case OPF_WAYPOINT_PATH:
		return RefObjectKind::Waypoint;
	case OPF_JUMP_NODE_NAME:
		return RefObjectKind::JumpNode;
	case OPF_COORDINATE_POINT:
		return RefObjectKind::CoordinatePoint;
	case OPF_IFF:
		return RefObjectKind::Team;

	// Ambiguous families — could name any of several object kinds.
	case OPF_SHIP_POINT:
	case OPF_POINT:
	case OPF_SHIP_WING:
	case OPF_SHIP_WING_WHOLETEAM:
	case OPF_SHIP_WING_SHIPONTEAM_POINT:
	case OPF_SHIP_WING_POINT:
	case OPF_SHIP_WING_POINT_OR_NONE:
	case OPF_SHIP_PROP:
	case OPF_SHIP_WING_PROP:
	case OPF_ORDER_RECIPIENT:
		return resolveAmbiguous(token);

	default:
		return RefObjectKind::Unknown;
	}
}

// Classify a leaf (non-operator) node as an object reference. Returns true only
// when the node names a first-class object we surface; false for literals.
bool EventReferenceIndex::leafObject(const SexpTreeModel& tree, int node, RefObjectKind& kind, SCP_string& name)
{
	const sexp_tree_item& n = tree.tree_nodes[node];
	if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR)
		return false;

	if (n.type & SEXPT_VARIABLE) {
		// A variable reference regardless of the slot it fills; the object is the
		// variable itself. Text is stored as "varname(value)".
		char var_name[TOKEN_LENGTH];
		get_variable_name_from_sexp_tree_node_text(n.text, var_name);
		kind = RefObjectKind::Variable;
		name = var_name;
		return !name.empty();
	}

	if (n.type & (SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA)) {
		// A SEXP container reference (name or data access); the node text is the
		// container's name.
		kind = RefObjectKind::Container;
		name = n.text;
		return !name.empty();
	}

	if (SEXPT_TYPE(n.type) == SEXPT_STRING) {
		const int opf = tree.query_node_argument_type(node);
		kind = classify(opf, n.text);
		name = n.text;
		return kind != RefObjectKind::Unknown && !name.empty();
	}

	return false;
}

// Serialize a tree_nodes subtree back to readable sexp text, e.g.
// "( is-destroyed-delay 0 Gamma Wing )". Operators wrap their children in
// parentheses; data atoms render as their text.
SCP_string EventReferenceIndex::nodeToText(const SexpTreeModel& tree, int node)
{
	const auto& nodes = tree.tree_nodes;
	if (node < 0 || node >= static_cast<int>(nodes.size()))
		return "";

	if (SEXPT_TYPE(nodes[node].type) == SEXPT_OPERATOR) {
		SCP_string out = "( ";
		out += nodes[node].text;
		for (int c = nodes[node].child; c != -1; c = nodes[c].next) {
			out += ' ';
			out += nodeToText(tree, c);
		}
		out += " )";
		return out;
	}
	return nodes[node].text;
}

// Walk up from the operator to the nearest enclosing when/every-time trigger,
// and report which branch it sits in: argument 0 is the condition, later
// arguments are actions. The "nearest" trigger means a nested-when's condition
// is reported as a condition even though it lives under an outer action branch.
RefRole EventReferenceIndex::classifyRole(const SexpTreeModel& tree, int operatorNode)
{
	const auto& nodes = tree.tree_nodes;
	int child = operatorNode;
	int parent = (child >= 0 && child < static_cast<int>(nodes.size())) ? nodes[child].parent : -1;

	while (parent >= 0) {
		const int op = get_operator_const(nodes[parent].text);
		if (op == OP_WHEN || op == OP_WHEN_ARGUMENT || op == OP_EVERY_TIME || op == OP_EVERY_TIME_ARGUMENT) {
			// Argument index of `child` among parent's children.
			int arg = 0;
			for (int c = nodes[parent].child; c != -1; c = nodes[c].next, ++arg) {
				if (c == child)
					return (arg == 0) ? RefRole::Condition : RefRole::Action;
			}
			return RefRole::Other; // shouldn't happen
		}
		child = parent;
		parent = nodes[child].parent;
	}
	return RefRole::Other;
}

SCP_string EventReferenceIndex::makeKey(RefObjectKind kind, const SCP_string& name)
{
	SCP_string key;
	key.reserve(name.size() + 2);
	key.push_back(static_cast<char>('0' + static_cast<int>(kind)));
	key.push_back('\1');
	for (char ch : name)
		key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	return key;
}

SCP_vector<EventObjectRef> EventReferenceIndex::referencesTo(RefObjectKind kind, const SCP_string& name) const
{
	SCP_vector<EventObjectRef> result;
	auto it = m_forward.find(makeKey(kind, name));
	if (it == m_forward.end())
		return result;
	result.reserve(it->second.size());
	for (int idx : it->second)
		result.push_back(m_refs[idx]);
	return result;
}

SCP_vector<int> EventReferenceIndex::eventsReferencing(RefObjectKind kind, const SCP_string& name) const
{
	SCP_vector<int> events;
	auto it = m_forward.find(makeKey(kind, name));
	if (it == m_forward.end())
		return events;
	for (int idx : it->second) {
		const int ev = m_refs[idx].eventIndex;
		if (std::find(events.begin(), events.end(), ev) == events.end())
			events.push_back(ev);
	}
	return events;
}

int EventReferenceIndex::eventReferenceCount(RefObjectKind kind, const SCP_string& name) const
{
	return static_cast<int>(eventsReferencing(kind, name).size());
}

SCP_vector<EventObjectRef> EventReferenceIndex::referenceSites(RefObjectKind kind, const SCP_string& name) const
{
	SCP_vector<EventObjectRef> sites;
	auto it = m_forward.find(makeKey(kind, name));
	if (it == m_forward.end())
		return sites;

	// One operator that names the object more than once is a single site.
	SCP_vector<int> seenOps;
	for (int idx : it->second) {
		const EventObjectRef& r = m_refs[idx];
		if (r.operatorNode >= 0) {
			if (std::find(seenOps.begin(), seenOps.end(), r.operatorNode) != seenOps.end())
				continue;
			seenOps.push_back(r.operatorNode);
		}
		sites.push_back(r);
	}
	return sites;
}

// Recurse an operator subtree into the BasicGraph. `node` is assumed to be an
// operator. Operator children become their own nodes (linked by childOps);
// object-naming leaves become shared object nodes (linked by objectRefs);
// everything else (numbers, plain strings) is inlined into the card.
int EventReferenceIndex::buildOpSubtree(const SexpTreeModel& tree, int node, int eventIndex, BasicGraph& g,
	std::unordered_map<int, int>& nodeToOp, std::unordered_map<SCP_string, int>& objKey) const
{
	const int opIndex = static_cast<int>(g.ops.size());
	g.ops.emplace_back();
	nodeToOp[node] = opIndex;

	// Fill the fixed fields now; the vector fields are accumulated into locals
	// first because recursion can reallocate g.ops out from under a reference.
	{
		BasicOpNode& op = g.ops[opIndex];
		op.treeNode = node;
		op.posKey = node;
		op.eventIndex = eventIndex;
		op.isCond = (classifyRole(tree, node) == RefRole::Condition);
		op.opText = tree.tree_nodes[node].text;
		op.expression = nodeToText(tree, node);
	}

	SCP_vector<SCP_string> inlineArgs;
	SCP_vector<int> childOps;
	SCP_vector<BasicObjRef> objectRefs;

	for (int c = tree.tree_nodes[node].child; c != -1; c = tree.tree_nodes[c].next) {
		if (SEXPT_TYPE(tree.tree_nodes[c].type) == SEXPT_OPERATOR) {
			const int childIdx = buildOpSubtree(tree, c, eventIndex, g, nodeToOp, objKey);
			if (childIdx >= 0)
				childOps.push_back(childIdx);
			continue;
		}

		RefObjectKind kind = RefObjectKind::Unknown;
		SCP_string name;
		if (leafObject(tree, c, kind, name)) {
			const SCP_string key = makeKey(kind, name);
			auto it = objKey.find(key);
			int objIdx;
			if (it == objKey.end()) {
				objIdx = static_cast<int>(g.objects.size());
				g.objects.emplace_back();
				g.objects[objIdx].kind = kind;
				g.objects[objIdx].name = name;
				g.objects[objIdx].repTreeNode = c;
				g.objects[objIdx].posKey = c;
				objKey.emplace(key, objIdx);
			} else {
				objIdx = it->second;
			}
			g.objects[objIdx].refTreeNodes.push_back(c);
			// Keep every reference (not deduped): combined rendering dedups by
			// objectIndex, duplicate rendering needs one node per leaf.
			BasicObjRef ref;
			ref.objectIndex = objIdx;
			ref.leafTreeNode = c;
			objectRefs.push_back(ref);
		} else {
			inlineArgs.push_back(nodeToText(tree, c));
		}
	}

	BasicOpNode& op = g.ops[opIndex];
	op.inlineArgs = std::move(inlineArgs);
	op.childOps = std::move(childOps);
	op.objectRefs = std::move(objectRefs);
	return opIndex;
}

BasicGraph EventReferenceIndex::buildBasicGraph(const SexpTreeModel& tree, const SCP_vector<mission_event>& events) const
{
	BasicGraph g;
	std::unordered_map<int, int> nodeToOp;
	std::unordered_map<SCP_string, int> objKey;

	g.events.reserve(events.size());
	for (int i = 0; i < static_cast<int>(events.size()); ++i) {
		BasicEventNode en;
		en.eventIndex = i;
		const int root = events[i].formula; // tree_nodes[] index
		if (root >= 0)
			en.posKey = SexpAnnotationModel::rootKey(root);
		if (root >= 0 && root < static_cast<int>(tree.tree_nodes.size()) &&
		    tree.tree_nodes[root].type != SEXPT_UNUSED &&
		    SEXPT_TYPE(tree.tree_nodes[root].type) == SEXPT_OPERATOR) {
			en.rootOp = buildOpSubtree(tree, root, i, g, nodeToOp, objKey);
		}
		g.events.push_back(en);
	}
	return g;
}

const char* EventReferenceIndex::kindLabel(RefObjectKind kind)
{
	switch (kind) {
	case RefObjectKind::Ship:            return "ship";
	case RefObjectKind::Wing:            return "wing";
	case RefObjectKind::Prop:            return "prop";
	case RefObjectKind::Message:         return "message";
	case RefObjectKind::Goal:            return "goal";
	case RefObjectKind::Event:           return "event";
	case RefObjectKind::Waypoint:        return "waypoint path";
	case RefObjectKind::JumpNode:        return "jump node";
	case RefObjectKind::CoordinatePoint: return "coordinate point";
	case RefObjectKind::Team:            return "team";
	case RefObjectKind::Variable:        return "variable";
	case RefObjectKind::Container:       return "container";
	default:                             return "?";
	}
}

} // namespace fso::fred
