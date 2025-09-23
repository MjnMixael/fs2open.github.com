/*
 * sexp_tree_core.cpp
 * Shared, UI-agnostic SEXP tree model for FRED and QtFRED.
 */

#include "sexp_tree_core.h"
#include "sexp_opf_core.h"

#include "fireball/fireballs.h"
#include "mission/missiongoals.h"
#include "mission/missionmessage.h"
#include "mission/missionparse.h"
#include "weapon/emp.h"

static constexpr int kNodeIncrement = 100; // mirrors TREE_NODE_INCREMENT

// ISexpEnvironment defaults

SCP_vector<SCP_string> ISexpEnvironment::getMessageNames() const
{
	SCP_vector<SCP_string> out;
	for (int i = Num_builtin_messages; i < Num_messages; ++i) {
		out.emplace_back(Messages[i].name);
	}
	return out;
}

SCP_vector<SCP_string> ISexpEnvironment::getMissionNames() const
{
	SCP_vector<SCP_string> out;
	out.emplace_back(Mission_filename);
	return out;
}

bool ISexpEnvironment::isCampaignContext() const
{
	return false;
}

/*SCP_vector<SCP_string> ISexpEnvironment::getMessages() { return {}; }
SCP_vector<SCP_string> ISexpEnvironment::getPersonaNames() { return {}; }
SCP_vector<SCP_string> ISexpEnvironment::getMissionNames() { return {}; }
int ISexpEnvironment::getRootReturnType() const { return OPR_NULL; }
int ISexpEnvironment::getDynamicEnumPosition(const SCP_string&) { return -1; }
SCP_vector<SCP_string> ISexpEnvironment::getDynamicEnumList(int) { return {}; }
bool ISexpEnvironment::isLuaOperator(int) const { return false; }
int ISexpEnvironment::getDynamicParameterIndex(const SCP_string&, int) { return -1; }
SCP_string ISexpEnvironment::getChildEnumSuffix(const SCP_string&, int) { return {}; }*/

// SexpTreeModel
SexpTreeModel::SexpTreeModel() : _actions(std::make_unique<SexpActionsHandler>(this)) {}
SexpTreeModel::SexpTreeModel(ISexpEnvironment* env) : _env(env), _actions(std::make_unique<SexpActionsHandler>(this)) {}

SexpTreeModel::~SexpTreeModel() = default;

void SexpTreeModel::setEnvironment(ISexpEnvironment* env) { _env = env; }
ISexpEnvironment* SexpTreeModel::environment() const { return _env; }

void SexpTreeModel::clear() { _nodes.clear(); }

int SexpTreeModel::allocateNode(int parent, int after_sibling) {
	// Find a free slot. if none, grow by kNodeIncrement.
	int idx = -1;
	for (int i = 0; i < static_cast<int>(_nodes.size()); ++i) {
		if (_nodes[i].type == SEXPT_UNUSED) {
			idx = i;
			break;
		}
	}
	if (idx == -1) {
		const int oldSize = static_cast<int>(_nodes.size());
		_nodes.resize(oldSize + kNodeIncrement);
		mprintf(("Bumping dynamic tree node limit from %d to %d...\n", oldSize, static_cast<int>(_nodes.size())));
		idx = oldSize;
	}

	// Initialize the node
	SexpNode& n = _nodes[idx];
	n.type = SEXPT_UNINIT;
	n.parent = -1;
	n.child = -1;
	n.next = -1;
	n.flags = 0;
	n.text.clear();

	// Link into the parent/sibling chain.
	if (parent >= 0) {
		n.parent = parent;
		int& first = _nodes[parent].child;

		if (first < 0) {
			first = idx; // parent had no children
		} else if (after_sibling >= 0) {
			// Walk the chain to find after_sibling
			int i = first;
			while (i >= 0 && i != after_sibling && _nodes[i].next >= 0) {
				i = _nodes[i].next;
			}
			if (i == after_sibling) { // found, so splice after it
				n.next = _nodes[i].next;
				_nodes[i].next = idx;
			} else { // not found, so append at tail
				int tail = first;
				while (_nodes[tail].next >= 0)
					tail = _nodes[tail].next;
				_nodes[tail].next = idx;
			}
		} else {
			// No after given, so append at tail
			int tail = first;
			while (_nodes[tail].next >= 0)
				tail = _nodes[tail].next;
			_nodes[tail].next = idx;
		}
	}

	return idx;
}

// Private helper that mirrors legacy free_node2: frees `node`,
// its entire subtree, and its next-chain. Assumes external links
// (parent/prev->next) have already been severed.
void SexpTreeModel::freeNodeChain(SCP_vector<SexpNode>& nodes, int node)
{
	if (node < 0)
		return;

	Assertion(nodes[node].type != SEXPT_UNUSED, "freeNodeChain on UNUSED node %d", node);

	const int child = nodes[node].child;
	const int next = nodes[node].next;

	// Mark this node as unused.
	nodes[node].type = SEXPT_UNUSED;

	if (child >= 0)
		freeNodeChain(nodes, child);
	if (next >= 0)
		freeNodeChain(nodes, next);
}

void SexpTreeModel::freeNode(int node_index, bool cascade)
{
	if (!SCP_vector_inbounds(_nodes, node_index))
		return;

	const int parent = _nodes[node_index].parent;
	// Legacy asserts parent exists (root deletion is handled elsewhere)
	Assertion(parent >= 0, "freeNode called on node %d with no parent", node_index);

	// Detach node_index from its parent's child list
	int& first = _nodes[parent].child;
	if (first == node_index) {
		first = _nodes[node_index].next; // skip over the node being deleted
	} else {
		int i = first;
		while (i >= 0 && _nodes[i].next != -1) {
			if (_nodes[i].next == node_index) {
				_nodes[i].next = _nodes[node_index].next; // splice out
				break;
			}
			i = _nodes[i].next;
		}
	}

	if (!cascade) {
		// if not cascading ensure we don't free later siblings
		_nodes[node_index].next = -1;
	}

	// Now free this node and its chilren
	freeNodeChain(_nodes, node_index);
}

const SexpNode& SexpTreeModel::node(int index) const
{ 
	return _nodes.at(index);
}
SexpNode& SexpTreeModel::node(int index)
{ 
	return _nodes.at(index);
}
int SexpTreeModel::size() const
{ 
	return static_cast<int>(_nodes.size());
}
int SexpTreeModel::liveCount() const
{
	int c = 0;
	for (const auto& n : _nodes) {
		if (n.type != SEXPT_UNUSED) {
			++c;
		}
	}
	return c;
}

void SexpTreeModel::setNode(int index, int type, const char* text)
{
	Assertion(SCP_vector_inbounds(_nodes, index), "setNode: index %d out of range.", index);
	Assertion(type != SEXPT_UNUSED, "setNode: cannot set type to SEXPT_UNUSED.");
	Assertion(_nodes[index].type != SEXPT_UNUSED, "setNode: node %d is UNUSED; allocateNode before setting.", index);
	Assertion(text != nullptr, "setNode: text must not be null.");

	size_t max_length = 0;
	if (type & SEXPT_VARIABLE) {
		max_length = 2 * TOKEN_LENGTH + 2;
	} else if (type & (SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA)) {
		max_length = sexp_container::NAME_MAX_LENGTH + 1;
	} else {
		max_length = TOKEN_LENGTH;
	}
	Assertion(std::strlen(text) < max_length, "setNode: text '%s' exceeds max length %zu for type 0x%x.", text, max_length, type);

	auto& n = _nodes[index];
	n.type = type;
	n.text = text;
	n.flags = computeDefaultFlagsFor(n);
}

void SexpTreeModel::detachFromParent(int index)
{
	Assertion(SCP_vector_inbounds(_nodes, index), "detachFromParent: index %d out of range.", index);

	const int parent = _nodes[index].parent;
	if (parent < 0)
		return; // already a root

	int& first = _nodes[parent].child;
	if (first == index) {
		first = _nodes[index].next; // skip over this node
	} else {
		int i = first;
		while (i >= 0 && _nodes[i].next != index) {
			i = _nodes[i].next;
			Assertion(i != -1, "detachFromParent: node %d not found under parent %d.", index, parent);
		}
		if (i >= 0) {
			_nodes[i].next = _nodes[index].next;
		}
	}

	_nodes[index].parent = -1;
	_nodes[index].next = -1;
}

void SexpTreeModel::appendAsChild(int parent_index, int index)
{
	Assertion(SCP_vector_inbounds(_nodes, index), "appendAsChild: index %d out of range.", index);
	Assertion(SCP_vector_inbounds(_nodes, parent_index), "appendAsChild: parent %d out of range.", parent_index);

	// Node must not currently be linked under a parent
	Assertion(_nodes[index].parent < 0, "appendAsChild: node %d already has a parent %d.", index, _nodes[index].parent);
	Assertion(_nodes[index].next < 0, "appendAsChild: node %d already has a next sibling %d.", index, _nodes[index].next);

	_nodes[index].parent = parent_index;
	int& first = _nodes[parent_index].child;
	if (first < 0) {
		first = index;
	} else {
		int tail = first;
		while (_nodes[tail].next >= 0)
			tail = _nodes[tail].next;
		_nodes[tail].next = index;
	}
}

void SexpTreeModel::moveBranch(int source_index, int new_parent_index)
{
	Assertion(SCP_vector_inbounds(_nodes, source_index), "moveBranch: source %d out of range.", source_index);

	// Nothing to do if caller passes -1
	if (source_index == -1)
		return;

	// Splice out of old parent, if there is one
	const int old_parent = _nodes[source_index].parent;
	if (old_parent >= 0) {
		int& first = _nodes[old_parent].child;
		if (first == source_index) {
			first = _nodes[source_index].next;
		} else {
			int i = first;
			while (i >= 0 && _nodes[i].next != source_index) {
				i = _nodes[i].next;
				Assertion(i != -1, "moveBranch: source %d not found under old parent %d.", source_index, old_parent);
			}
			if (i >= 0) {
				_nodes[i].next = _nodes[source_index].next;
			}
		}
	}

	// Reset link; we'll reattach under the new parent
	_nodes[source_index].parent = -1;
	_nodes[source_index].next = -1;

	if (new_parent_index >= 0) {
		appendAsChild(new_parent_index, source_index);
	}
}

// Find Operators[] index from an operator value
int SexpTreeModel::find_operator_index_by_value(int value)
{
	for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
		if (Operators[i].value == value)
			return i;
	}
	return -1;
}

int SexpTreeModel::nodeFlags(int index) const
{
	Assertion(SCP_vector_inbounds(_nodes, index), "nodeFlags: index %d out of range.", index);
	return _nodes[index].flags;
}

void SexpTreeModel::setNodeFlags(int index, int flags)
{
	Assertion(SCP_vector_inbounds(_nodes, index), "setNodeFlags: index %d out of range.", index);
	_nodes[index].flags = flags;
}

// Heuristic defaults
int SexpTreeModel::computeDefaultFlagsFor(const SexpNode& n) const
{
	const int t = SEXPT_TYPE(n.type);
	int flags = NOT_EDITABLE;

	if (t == SEXPT_OPERATOR) {
		// Operators are chosen from menus, not free-typed
		flags |= OPERAND;
		// Generally not EDITABLE as plain text; UI replaces via operator picker.
		return flags;
	}

	// Data/atoms: numbers and strings are free-typed by default
	if (t == SEXPT_NUMBER || t == SEXPT_STRING) {
		flags |= EDITABLE;
	}

	// Variables: we show combined "name(...)" representation
	if ((n.type & SEXPT_VARIABLE) != 0) {
		flags |= COMBINED;
		flags |= EDITABLE; // you can edit via the special handler
	}

	// Containers: names/data are string-like; allow editing unless a dialog locks them down.
	if ((n.type & (SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA)) != 0) {
		flags |= EDITABLE;
	}

	return flags;
}

void SexpTreeModel::applyDefaultFlags(int index)
{
	Assertion(SCP_vector_inbounds(_nodes, index), "applyDefaultFlags: index %d out of range.", index);
	_nodes[index].flags = computeDefaultFlagsFor(_nodes[index]);
}

// Map OPF_* to coarse bucket
ArgBucket SexpTreeModel::opf_to_bucket(int opf) const
{
	switch (opf) {
	case OPF_NUMBER:
	case OPF_POSITIVE:
		return ArgBucket::NUM;
	case OPF_BOOL:
		return ArgBucket::BOOL;
	default:
		return ArgBucket::STR; // ships/wings/messages/etc. are string atoms in tree
	}
}

// Map a concrete node chain to bucket
ArgBucket SexpTreeModel::node_to_bucket(const SexpNode& n) const
{
	const int t = SEXPT_TYPE(n.type);
	if (t == SEXPT_NUMBER)
		return ArgBucket::NUM;
	if (t == SEXPT_OPERATOR) {
		// Operator’s return type decides its bucket
		const int op_const = get_operator_const(n.text.c_str());
		const int ret = query_operator_return_type(op_const);
		if (ret == OPR_NUMBER)
			return ArgBucket::NUM;
		if (ret == OPR_BOOL)
			return ArgBucket::BOOL;
		return ArgBucket::STR;
	}
	// Strings, variables, containers are treated as string like for argument matching
	return ArgBucket::STR;
}

bool SexpTreeModel::argsCompatibleWithOperator(int op_node, int new_op_index) const
{
	Assertion(SCP_vector_inbounds(_nodes, op_node), "argsCompatibleWithOperator: node %d out of range.", op_node);
	Assertion(SCP_vector_inbounds(Operators, new_op_index), "argsCompatibleWithOperator: op_index %d out of range.", new_op_index);

	const int op_const = Operators[new_op_index].value;

	int i = _nodes[op_node].child;
	int arg_pos = 0;
	while (i >= 0) {
		const int expected_opf = query_operator_argument_type(op_const, arg_pos);
		// If the operator doesn't define a type for this arg_pos (beyond max), treat as incompatible
		if (expected_opf < 0)
			return false;

		const ArgBucket want = opf_to_bucket(expected_opf);
		const ArgBucket got = node_to_bucket(_nodes[i]);
		if (want != got)
			return false;

		i = _nodes[i].next;
		++arg_pos;
	}
	return true;
}

int SexpTreeModel::createDefaultArgForOpf(int type, int parent, int op_index, int i, int context_index)
{
	auto make_num = [&](int v) {
		char buf[TOKEN_LENGTH];
		std::snprintf(buf, sizeof(buf), "%d", v);
		const int n = allocateNode(parent);
		setNode(n, (SEXPT_NUMBER | SEXPT_VALID), buf);
		return n;
	};
	auto make_num_str = [&](const char* v) {
		const int n = allocateNode(parent);
		setNode(n, (SEXPT_NUMBER | SEXPT_VALID), v);
		return n;
	};
	auto make_str = [&](const char* v) {
		const int n = allocateNode(parent);
		setNode(n, (SEXPT_STRING | SEXPT_VALID), v);
		return n;
	};
	auto make_op = [&](int op_value) {
		// convert operator value -> Operators[] index
		int idx = -1;
		for (int k = 0; k < static_cast<int>(Operators.size()); ++k)
			if (Operators[k].value == op_value) {
				idx = k;
				break;
			}
		Assertion(idx >= 0, "createDefaultArgForOpf: operator value %d not found.", op_value);
		const int n = allocateNode(parent);
		setNode(n, (SEXPT_OPERATOR | SEXPT_VALID), Operators[idx].text.c_str());
		return n;
	};

	const int op_const = Operators[op_index].value;

	switch (type) {
	case OPF_NULL:
		return make_op(OP_NOP);

	case OPF_BOOL:
		return make_op(OP_TRUE);

	case OPF_ANYTHING:
		if (op_const == OP_INVALIDATE_ARGUMENT || op_const == OP_VALIDATE_ARGUMENT)
			return make_str(SEXP_ARGUMENT_STRING); // legacy prefers argument placeholder here
		return make_str("<any data>");

	case OPF_DATA_OR_STR_CONTAINER:
		return make_str("<any data or string container>");

	case OPF_NUMBER:
	case OPF_POSITIVE:
	case OPF_AMBIGUOUS: {
		// AI goal last required arg is always priority 89
		if (query_operator_return_type(op_index) == OPR_AI_GOAL && i == (Operators[op_index].min - 1))
			return make_num(89);

		// dock timing / time docked defaults at arg 2 as 1
		if ((op_const == OP_HAS_DOCKED_DELAY || op_const == OP_HAS_UNDOCKED_DELAY || op_const == OP_TIME_DOCKED ||
				op_const == OP_TIME_UNDOCKED) &&
			i == 2)
			return make_num(1);

		if (op_const == OP_SHIP_TYPE_DESTROYED || op_const == OP_GOOD_SECONDARY_TIME)
			return make_num(100);

		if (op_const == OP_SET_SUPPORT_SHIP)
			return make_num(-1);

		if ((op_const == OP_SHIP_TAG && i == 1) || (op_const == OP_TRIGGER_SUBMODEL_ANIMATION && i == 3))
			return make_num(1);

		if (op_const == OP_EXPLOSION_EFFECT) {
			int temp;
			switch (i) {
			case 3:
				temp = 10;
				break;
			case 4:
				temp = 10;
				break;
			case 5:
				temp = 100;
				break;
			case 6:
				temp = 10;
				break;
			case 7:
				temp = 100;
				break;
			case 11:
				temp = static_cast<int>(EMP_DEFAULT_INTENSITY);
				break;
			case 12:
				temp = static_cast<int>(EMP_DEFAULT_TIME);
				break;
			default:
				temp = 0;
				break;
			}
			return make_num(temp);
		}

		if (op_const == OP_WARP_EFFECT) {
			int temp;
			switch (i) {
			case 6:
				temp = 100;
				break;
			case 7:
				temp = 10;
				break;
			default:
				temp = 0;
				break;
			}
			return make_num(temp);
		}

		if (op_const == OP_CHANGE_BACKGROUND)
			return make_num(1);

		if (op_const == OP_ADD_BACKGROUND_BITMAP || op_const == OP_ADD_BACKGROUND_BITMAP_NEW) {
			int temp = 0;
			switch (i) {
			case 4:
			case 5:
				temp = 100;
				break;
			case 6:
			case 7:
				temp = 1;
				break;
			default:
				break;
			}
			return make_num(temp);
		}

		if (op_const == OP_ADD_SUN_BITMAP || op_const == OP_ADD_SUN_BITMAP_NEW) {
			int temp = (i == 4) ? 100 : 0;
			return make_num(temp);
		}

		if (op_const == OP_MISSION_SET_NEBULA) {
			return (i == 0) ? make_num(1) : make_num(3000);
		}

		if (op_const == OP_MODIFY_VARIABLE) {
			// uses current node context to determine numeric vs string
			if (getModifyVariableType(context_index) == OPF_NUMBER)
				return make_num(0);
			return make_str("<any data>");
		}

		if (op_const == OP_MODIFY_VARIABLE_XSTR) {
			return (i == 1) ? make_str("<any data>") : make_num(-1);
		}

		if (op_const == OP_SET_VARIABLE_BY_INDEX) {
			return (i == 0) ? make_num(0) : make_str("<any data>");
		}

		if (op_const == OP_JETTISON_CARGO_NEW)
			return make_num(25);

		if (op_const == OP_TECH_ADD_INTEL_XSTR || op_const == OP_TECH_REMOVE_INTEL_XSTR)
			return make_num(-1);

		// default number
		return make_num(0);
	}

	// Hybrids that used to be numbers:
	case OPF_GAME_SND: {
		gamesnd_id sound_index;
		if (op_const == OP_EXPLOSION_EFFECT) {
			sound_index = GameSounds::SHIP_EXPLODE_1;
		} else if (op_const == OP_WARP_EFFECT) {
			sound_index = (i == 8) ? GameSounds::CAPITAL_WARP_IN : GameSounds::CAPITAL_WARP_OUT;
		}
		if (sound_index.isValid()) {
			game_snd* snd = gamesnd_get_game_sound(sound_index);
			if (can_construe_as_integer(snd->name.c_str()))
				return make_num_str(snd->name.c_str());
			else
				return make_str(snd->name.c_str());
		}
		// fall through to listing/default if no hardcoded default
		break;
	}

	case OPF_FIREBALL: {
		int fireball_index = -1;
		if (op_const == OP_EXPLOSION_EFFECT) {
			fireball_index = FIREBALL_MEDIUM_EXPLOSION;
		} else if (op_const == OP_WARP_EFFECT) {
			fireball_index = FIREBALL_WARP;
		}
		if (fireball_index >= 0) {
			char* unique_id = Fireball_info[fireball_index].unique_id;
			if (std::strlen(unique_id) > 0)
				return make_str(unique_id);
			char num_str[NAME_LENGTH];
			std::snprintf(num_str, sizeof(num_str), "%d", fireball_index);
			return make_num_str(num_str);
		}
		// fall through to listing/default
		break;
	}

	case OPF_PRIORITY:
		return make_str("Normal");
	}

	// Try OPF listing default (skip the Argument placeholder)
	{
		SexpOpfListBuilder builder(_nodes, _env);
		SexpListItemPtr list = builder.buildListing(type, parent, i);

		if (list && list->text == SEXP_ARGUMENT_STRING) {
			SexpListItemPtr rest_of_list(list->next);
			list->next = nullptr;
			list = std::move(rest_of_list);
		}
		if (list) {
			const int n = allocateNode(parent);
			if (list->type & SEXPT_OPERATOR) {
				setNode(n, (SEXPT_OPERATOR | SEXPT_VALID), list->text.c_str());
			} else if (list->type & SEXPT_NUMBER) {
				setNode(n, (SEXPT_NUMBER | SEXPT_VALID), list->text.c_str());
			} else {
				setNode(n, (SEXPT_STRING | SEXPT_VALID), list->text.c_str());
			}
			list->destroy();
			return n;
		}
	}

	// Final catch-all placeholders
	const char* str = nullptr;
	switch (type) {
	case OPF_SHIP:
	case OPF_SHIP_NOT_PLAYER:
	case OPF_SHIP_POINT:
	case OPF_SHIP_WING:
	case OPF_SHIP_WING_WHOLETEAM:
	case OPF_SHIP_WING_SHIPONTEAM_POINT:
	case OPF_SHIP_WING_POINT:
		str = "<name of ship here>";
		break;

	case OPF_ORDER_RECIPIENT:
		str = "<all fighters>";
		break;

	case OPF_SHIP_OR_NONE:
	case OPF_SUBSYSTEM_OR_NONE:
	case OPF_SHIP_WING_POINT_OR_NONE:
		str = SEXP_NONE_STRING;
		break;

	case OPF_WING:
		str = "<name of wing here>";
		break;
	case OPF_DOCKER_POINT:
		str = "<docker point>";
		break;
	case OPF_DOCKEE_POINT:
		str = "<dockee point>";
		break;

	case OPF_SUBSYSTEM:
	case OPF_AWACS_SUBSYSTEM:
	case OPF_ROTATING_SUBSYSTEM:
	case OPF_TRANSLATING_SUBSYSTEM:
	case OPF_SUBSYS_OR_GENERIC:
		str = "<name of subsystem>";
		break;

	case OPF_SUBSYSTEM_TYPE:
		str = Subsystem_types[SUBSYSTEM_NONE];
		break;

	case OPF_POINT:
		str = "<waypoint>";
		break;
	case OPF_MESSAGE:
		str = "<Message>";
		break;
	case OPF_WHO_FROM:
		str = "<any wingman>";
		break;
	case OPF_WAYPOINT_PATH:
		str = "<waypoint path>";
		break;
	case OPF_MISSION_NAME:
		str = "<mission name>";
		break;
	case OPF_GOAL_NAME:
		str = "<goal name>";
		break;
	case OPF_SHIP_TYPE:
		str = "<ship type here>";
		break;
	case OPF_EVENT_NAME:
		str = "<event name>";
		break;
	case OPF_HUGE_WEAPON:
		str = "<huge weapon type>";
		break;
	case OPF_JUMP_NODE_NAME:
		str = "<Jump node name>";
		break;
	case OPF_NAV_POINT:
		str = "<Nav 1>";
		break;
	case OPF_ANYTHING:
		str = "<any data>";
		break;
	case OPF_DATA_OR_STR_CONTAINER:
		str = "<any data or string container>";
		break;
	case OPF_PERSONA:
		str = "<persona name>";
		break;
	case OPF_FONT:
		str = font::FontManager::getFont(0)->getName().c_str();
		break;
	case OPF_AUDIO_VOLUME_OPTION:
		str = "Music";
		break;
	case OPF_POST_EFFECT:
		str = "<Effect Name>";
		break;
	case OPF_CUSTOM_HUD_GAUGE:
		str = "<Custom hud gauge>";
		break;
	case OPF_ANY_HUD_GAUGE:
		str = "<Custom or builtin hud gauge>";
		break;
	case OPF_ANIMATION_NAME:
		str = "<Animation trigger name>";
		break;
	case OPF_CONTAINER_VALUE:
		str = "<container value>";
		break;
	case OPF_MESSAGE_TYPE:
		str = Builtin_messages[0].name;
		break;
	default:
		str = "<new default required!>";
		break;
	}
	return make_str(str);
}

int SexpTreeModel::makeOperatorNode(int op_index, int parent, int after_sibling)
{
	Assertion(SCP_vector_inbounds(Operators, op_index), "makeOperatorNode: op_index %d out of range.", op_index);

	const int node = allocateNode(parent, after_sibling);
	setNode(node, (SEXPT_OPERATOR | SEXPT_VALID), Operators[op_index].text.c_str());
	return node;
}

void SexpTreeModel::ensureOperatorArity(int op_node, int op_index)
{
	Assertion(SCP_vector_inbounds(_nodes, op_node), "ensureOperatorArity: node %d out of range.", op_node);
	Assertion((_nodes[op_node].type & SEXPT_OPERATOR) != 0, "ensureOperatorArity: node %d is not an operator.", op_node);
	Assertion(SCP_vector_inbounds(Operators, op_index), "ensureOperatorArity: op_index %d out of range.", op_index);

	// Compute current argument count
	int first = _nodes[op_node].child;
	int argc = 0;
	for (int i = first; i >= 0; i = _nodes[i].next)
		++argc;

	// Required/allowed counts from operator table
	const int min_args = Operators[op_index].min;
	const int max_args = Operators[op_index].max; // -1 means unbounded in FRED

	// Append defaults until we reach min
	int arg_tail = first;
	if (arg_tail >= 0) {
		while (_nodes[arg_tail].next >= 0)
			arg_tail = _nodes[arg_tail].next;
	}

	for (int arg_i = argc; arg_i < min_args; ++arg_i) {
		// Determine arg type at position arg_i
		const int op_const = Operators[op_index].value;
		const int opf = query_operator_argument_type(op_const, arg_i);
		const int added = createDefaultArgForOpf(opf, op_node, op_index, arg_i, op_node);

		if (first < 0) {
			first = added;
			_nodes[op_node].child = added;
			arg_tail = added;
		} else {
			_nodes[arg_tail].next = added;
			arg_tail = added;
		}
	}

	// Trim extras if max bounded and we exceed it
	if (max_args >= 0 && argc > max_args) {
		// Walk to the (max_args-1)th node to keep; cut after it
		int keep_tail = _nodes[op_node].child;
		for (int k = 1; k < max_args; ++k) {
			keep_tail = _nodes[keep_tail].next;
		}
		// Free everything after keep_tail
		const int to_free = _nodes[keep_tail].next;
		_nodes[keep_tail].next = -1;
		if (to_free >= 0) {
			// free the whole sibling chain and their subtrees
			freeNode(to_free, /*cascade=*/true);
		}
	}
}

void SexpTreeModel::replaceOperator(int node_index, int new_op_index)
{
	Assertion(SCP_vector_inbounds(_nodes, node_index), "replaceOperator: node %d out of range.", node_index);
	Assertion(SCP_vector_inbounds(Operators, new_op_index), "replaceOperator: new_op_index %d out of range.", new_op_index);

	// Must be an operator already (this mirrors typical replace flow in FRED)
	Assertion((_nodes[node_index].type & SEXPT_OPERATOR) != 0, "replaceOperator: node %d is not an operator.", node_index);

	const bool compatible = argsCompatibleWithOperator(node_index, new_op_index);

	// Update the node in place (type stays operator, text changes)
	setNode(node_index, (SEXPT_OPERATOR | SEXPT_VALID), Operators[new_op_index].text.c_str());

	if (!compatible) {
		// Nuke existing child list
		const int to_free = _nodes[node_index].child;
		_nodes[node_index].child = -1;
		if (to_free >= 0) {
			freeNode(to_free, /*cascade=*/true); // free child subtree + siblings
		}
	}

	// Enforce min/max, append default args, trim extra args
	ensureOperatorArity(node_index, new_op_index);
}

// Recursive worker that translates a chain of Sexp_nodes into the model's node structure.
// Returns the model index of the last node created in the sibling chain.
int SexpTreeModel::loadBranchRecursive(int sexp_idx, int model_parent_idx)
{
	int last_created_model_idx = -1;

	while (sexp_idx != -1) {
		const auto& sexp_node = Sexp_nodes[sexp_idx];
		Assertion(sexp_node.type != SEXP_NOT_USED, "Encountered an unused SEXP node during load.");

		if (sexp_node.subtype == SEXP_ATOM_LIST) {
			// A list in a SEXP is just a parenthesis. We process its contents
			// as direct children of the current parent, effectively flattening it.
			loadBranchRecursive(sexp_node.first, model_parent_idx);

		} else if (sexp_node.subtype == SEXP_ATOM_OPERATOR) {
			// Operators create a new level of hierarchy.
			const int model_node_idx = allocateNode(model_parent_idx);
			last_created_model_idx = model_node_idx;

			SexpNode& n = node(model_node_idx);
			n.text = sexp_node.text;
			n.type = (SEXPT_OPERATOR | SEXPT_VALID);

			// The rest of the current SEXP list are arguments to this operator.
			loadBranchRecursive(sexp_node.rest, model_node_idx);

			// Because the rest of the list was consumed as arguments, we must return now.
			return model_node_idx;

		} else {
			// This handles all atomic types: NUMBER, STRING, CONTAINER_NAME, CONTAINER_DATA.
			const int model_node_idx = allocateNode(model_parent_idx);
			last_created_model_idx = model_node_idx;
			SexpNode& n = node(model_node_idx);

			// Base type and text
			if (sexp_node.type & SEXP_FLAG_VARIABLE) {
				getCombinedVariableName(n.text, sexp_node.text);
				n.type = SEXPT_VARIABLE;
			} else {
				n.text = sexp_node.text;
				n.type = 0;
			}

			// Add specific type flags
			if (sexp_node.subtype == SEXP_ATOM_NUMBER) {
				n.type |= SEXPT_NUMBER;
			} else if (sexp_node.subtype == SEXP_ATOM_STRING) {
				n.type |= SEXPT_STRING;
			} else if (sexp_node.subtype == SEXP_ATOM_CONTAINER_NAME) {
				n.type |= (SEXPT_CONTAINER_NAME | SEXPT_STRING);
			} else if (sexp_node.subtype == SEXP_ATOM_CONTAINER_DATA) {
				n.type |= (SEXPT_CONTAINER_DATA | SEXPT_STRING);
			}

			// All loaded nodes are initially valid
			n.type |= SEXPT_VALID;

			// Special check for modifiers which must happen after parent is assigned in allocateNode
			if (model_parent_idx >= 0 && (node(model_parent_idx).type & SEXPT_CONTAINER_DATA)) {
				n.type |= SEXPT_MODIFIER;
			}

			// If it's container data, it can have children
			if (sexp_node.subtype == SEXP_ATOM_CONTAINER_DATA) {
				loadBranchRecursive(sexp_node.first, model_node_idx);
			}
		}

		// Move to the next SEXP node in the sibling list
		sexp_idx = sexp_node.rest;
	}

	return last_created_model_idx;
}

int SexpTreeModel::loadTreeFromSexp(int sexp_root)
{
	clear();

	// No SEXP provided, create a default tree.
	if (sexp_root < 0) {
		const int root_idx = allocateNode();
		node(root_idx).type = (SEXPT_OPERATOR | SEXPT_VALID);
		node(root_idx).text = "true";
		return root_idx;
	}

	// Legacy quirk where a root can be a number, needs to be converted to true/false. Thanks Allender...
	if (Sexp_nodes[sexp_root].subtype == SEXP_ATOM_NUMBER) {
		const int root_idx = allocateNode();
		node(root_idx).type = (SEXPT_OPERATOR | SEXPT_VALID);
		if (atoi(Sexp_nodes[sexp_root].text)) {
			node(root_idx).text = "true";
		} else {
			node(root_idx).text = "false";
		}
		return root_idx;
	}

	// Standard SEXP, starting with an operator.
	// assumption: first token is an operator.  I require this because it would cause problems
	// with child/parent relations otherwise, and it should be this way anyway, since the
	// return type of the whole sexp is boolean, and only operators can satisfy this.
	Assertion(Sexp_nodes[sexp_root].subtype == SEXP_ATOM_OPERATOR, "SEXP root must be an operator.");

	return loadBranchRecursive(sexp_root, -1);
}

// Helper to create the "varName(value)" string for variable nodes.
void SexpTreeModel::getCombinedVariableName(SCP_string& out, const char* sexp_var_name)
{
	const int sexp_var_index = get_index_sexp_variable_name(sexp_var_name);

	if (sexp_var_index >= 0) {
		out = Sexp_variables[sexp_var_index].variable_name;
		out += "(";
		out += Sexp_variables[sexp_var_index].text;
		out += ")";
	} else {
		out = sexp_var_name;
		out += "(undefined)";
	}
}

// Extract variable name from a node's text "varName(...)"
void SexpTreeModel::varNameFromTreeText(SCP_string& out, const SCP_string& text)
{
	const auto p = text.find('(');
	if (p == SCP_string::npos) {
		out = text;
	} else {
		out = text.substr(0, p);
	}
}

int SexpTreeModel::getModifyVariableType(int parent_index) const
{
	Assertion(SCP_vector_inbounds(_nodes, parent_index), "getModifyVariableType: parent index %d out of range.", parent_index);

	const auto& parent = _nodes[parent_index];
	const int op_const = get_operator_const(parent.text.c_str());

	Assertion(parent.child >= 0,
		"getModifyVariableType: operator '%s' at %d has no first child (variable spec).",
		parent.text.c_str(),
		parent_index);

	const char* node_text = _nodes[parent.child].text.c_str();

	int sexp_var_index = -1;

	if (op_const == OP_MODIFY_VARIABLE) {
		sexp_var_index = getTreeNameToSexpVariableIndex(node_text);
	} else if (op_const == OP_SET_VARIABLE_BY_INDEX) {
		if (can_construe_as_integer(node_text)) {
			sexp_var_index = std::atoi(node_text);
		} else if (std::strchr(node_text, '(') && std::strchr(node_text, ')')) {
			// the variable index is itself a variable!
			return OPF_AMBIGUOUS;
		}
		// else: leave sexp_var_index = -1 (falls through to AMBIGUOUS)
	} else {
		Assertion(false,
			"getModifyVariableType: called for non-modify operator '%s' at %d.",
			parent.text.c_str(),
			parent_index);
		return OPF_AMBIGUOUS;
	}

	// if we don't have a valid variable, allow replacement with anything
	if (sexp_var_index < 0) {
		return OPF_AMBIGUOUS;
	}

	const int var_type = Sexp_variables[sexp_var_index].type;

	if ((var_type & SEXP_VARIABLE_BLOCK) || (var_type & SEXP_VARIABLE_NOT_USED)) {
		// assume number so that we can allow tree display of number operators
		return OPF_NUMBER;
	} else if (var_type & SEXP_VARIABLE_NUMBER) {
		return OPF_NUMBER;
	} else if (var_type & SEXP_VARIABLE_STRING) {
		return OPF_AMBIGUOUS;
	} else {
		Assertion(false, "getModifyVariableType: unexpected Sexp_variables[%d].type = 0x%x.", sexp_var_index, var_type);
		return OPF_AMBIGUOUS;
	}
}

int SexpTreeModel::getTreeNameToSexpVariableIndex(const char* tree_name) const
{
	Assertion(tree_name != nullptr, "getTreeNameToSexpVariableIndex: tree_name is null.");

	char var_name[TOKEN_LENGTH];
	const size_t chars_to_copy = std::strcspn(tree_name, "(");

	Assertion(chars_to_copy < TOKEN_LENGTH - 1,
		"getTreeNameToSexpVariableIndex: variable name too long (len=%zu, max=%d).",
		chars_to_copy,
		TOKEN_LENGTH - 1);

	// Copy up to '(' and add null termination
	std::memcpy(var_name, tree_name, chars_to_copy);
	var_name[chars_to_copy] = '\0';

	// Look up index
	return get_index_sexp_variable_name(var_name);
}

// Recursive saver that mirrors the old save_branch.
// Returns index into the global Sexp_nodes pool or -1
int SexpTreeModel::saveBranchRecursive(const SCP_vector<SexpNode>& nodes, int cur, bool at_root)
{
	constexpr int NO_PREVIOUS_NODE = -9;

	int start = -1;
	int last = NO_PREVIOUS_NODE;

	while (cur != -1) {
		int node_idx = -1;

		const auto& t = nodes[cur];

		if (t.type & SEXPT_OPERATOR) {
			// Build operator atom with its child list as first
			node_idx = alloc_sexp(t.text.c_str(),
				SEXP_ATOM,
				SEXP_ATOM_OPERATOR,
				-1,
				saveBranchRecursive(nodes, t.child, /*at_root=*/false));

			// If this operator is NOT the topmost root, wrap it in a LIST
			if (t.parent >= 0 && !at_root) {
				node_idx = alloc_sexp("", SEXP_LIST, SEXP_ATOM_LIST, node_idx, -1);
			}

		} else if (t.type & SEXPT_CONTAINER_NAME) {
			// Validate container exists
			Assertion(get_sexp_container(t.text.c_str()) != nullptr,
				"Attempt to save unknown container %s from SEXP tree. Please report!",
				t.text.c_str());
			node_idx = alloc_sexp(t.text.c_str(), SEXP_ATOM, SEXP_ATOM_CONTAINER_NAME, -1, -1);

		} else if (t.type & SEXPT_CONTAINER_DATA) {
			Assertion(get_sexp_container(t.text.c_str()) != nullptr,
				"Attempt to save unknown container %s from SEXP tree. Please report!",
				t.text.c_str());
			node_idx = alloc_sexp(t.text.c_str(),
				SEXP_ATOM,
				SEXP_ATOM_CONTAINER_DATA,
				saveBranchRecursive(nodes, t.child, /*at_root=*/false),
				-1);

		} else if (t.type & SEXPT_NUMBER) {
			if (t.type & SEXPT_VARIABLE) {
				SCP_string varName;
				varNameFromTreeText(varName, t.text);
				node_idx = alloc_sexp(varName.c_str(), (SEXP_ATOM | SEXP_FLAG_VARIABLE), SEXP_ATOM_NUMBER, -1, -1);
			} else {
				node_idx = alloc_sexp(t.text.c_str(), SEXP_ATOM, SEXP_ATOM_NUMBER, -1, -1);
			}

		} else if (t.type & SEXPT_STRING) {
			if (t.type & SEXPT_VARIABLE) {
				SCP_string varName;
				varNameFromTreeText(varName, t.text);
				node_idx = alloc_sexp(varName.c_str(), (SEXP_ATOM | SEXP_FLAG_VARIABLE), SEXP_ATOM_STRING, -1, -1);
			} else {
				node_idx = alloc_sexp(t.text.c_str(), SEXP_ATOM, SEXP_ATOM_STRING, -1, -1);
			}

		} else {
			// Unknown/invalid type
			Assertion(false, "Unknown or invalid node type (index %d, type %x) while saving SEXP tree.", cur, t.type);
			return -1;
		}

		if (last == NO_PREVIOUS_NODE) {
			start = node_idx;
		} else if (last >= 0) {
			// Stitch the list chain... set rest of previous to current
			Sexp_nodes[last].rest = node_idx;
		}

		last = node_idx;
		Assertion(last != NO_PREVIOUS_NODE, "Internal error: last == NO_PREVIOUS_NODE after allocation.");

		cur = t.next;

		// Legacy save_branch returns immediately after processing the first item at the root level.
		if (at_root) {
			return start;
		}
	}

	return start;
}

int SexpTreeModel::saveTreeToSexp(int model_root) {
	// If caller passes -1, try to find a single top-level root
	if (model_root < 0) {
		// Find the first non unused node with no parent
		for (int i = 0; i < static_cast<int>(_nodes.size()); ++i) {
			if (_nodes[i].type != SEXPT_UNUSED && _nodes[i].parent < 0) {
				model_root = i;
				break;
			}
		}
	}

	// preconditions
	Assertion(model_root >= 0, "saveTreeToSexp requires a valid root node index.");
	Assertion((_nodes[model_root].type & SEXPT_OPERATOR) && (_nodes[model_root].type & SEXPT_VALID), "Root must be a valid operator node.");
	Assertion(_nodes[model_root].next == -1, "Root node must not have a next sibling (found next=%d).", _nodes[model_root].next);

	// Build and return the index of the newly allocated SEXP node in the global pool
	return saveBranchRecursive(_nodes, model_root, /*at_root=*/true);
}

bool SexpTreeModel::queryFalse(int node_index) const {
	// If no node specified, use the top-level root
	if (node_index < 0) {
		for (int i = 0; i < static_cast<int>(_nodes.size()); ++i) {
			if (_nodes[i].type != SEXPT_UNUSED && _nodes[i].parent < 0) {
				node_index = i;
				break;
			}
		}
	}

	Assertion(node_index >= 0, "queryFalse requires a valid root node index.");
	const auto& n = _nodes[node_index];
	Assertion(n.type == (SEXPT_OPERATOR | SEXPT_VALID), "Root must be a valid operator node (type=%x).", n.type);
	Assertion(n.next == -1, "Root node must not have a next sibling (found next=%d).", n.next);

	return get_operator_const(n.text.c_str()) == OP_FALSE;
}

int SexpTreeModel::countArgs(int node_index) const
{
	int count = 0;
	while (node_index >= 0) {
		++count;
		node_index = _nodes[node_index].next;
	}
	return count;
}

int SexpTreeModel::identifyArgType(int node_index) const
{
	int type = -1;

	while (node_index >= 0) {
		const auto& tn = _nodes[node_index];
		Assertion((tn.type & SEXPT_VALID) != 0, "identifyArgType: invalid node %d (type=%x)", node_index, tn.type);

		switch (SEXPT_TYPE(tn.type)) {
			case SEXPT_OPERATOR: {
				const int op_const = get_operator_const(tn.text.c_str());
				Assertion(op_const != 0, "identifyArgType: unknown operator '%s'", tn.text.c_str());
				return query_operator_return_type(op_const);
			}

			case SEXPT_NUMBER:
				return OPR_NUMBER;

			case SEXPT_STRING:
				// Mark as string, but keep scanning siblings
				// in case we can infer a more specific type from an operator.
				type = SEXP_ATOM_STRING;
				break;

			default:
				// Anything else.. keep scanning
				break;
		}

		node_index = tn.next;
	}

	return type;
}

int SexpTreeModel::parentOf(int index) const
{
	Assertion(SCP_vector_inbounds(_nodes, index), "parentOf: index %d out of range.", index);
	return _nodes[index].parent;
}

int SexpTreeModel::firstChild(int index) const
{
	Assertion(SCP_vector_inbounds(_nodes, index), "firstChild: index %d out of range.", index);
	return _nodes[index].child;
}

int SexpTreeModel::nextSibling(int index) const
{
	Assertion(SCP_vector_inbounds(_nodes, index), "nextSibling: index %d out of range.", index);
	return _nodes[index].next;
}

bool SexpTreeModel::isEditable(int index) const
{
	return (nodeFlags(index) & EDITABLE) != 0;
}
void SexpTreeModel::setEditable(int index, bool on)
{
	auto f = nodeFlags(index);
	f = on ? (f | EDITABLE) : (f & ~EDITABLE);
	setNodeFlags(index, f);
}

bool SexpTreeModel::isOperand(int index) const
{
	return (nodeFlags(index) & OPERAND) != 0;
}
void SexpTreeModel::setOperand(int index, bool on)
{
	auto f = nodeFlags(index);
	f = on ? (f | OPERAND) : (f & ~OPERAND);
	setNodeFlags(index, f);
}

bool SexpTreeModel::isCombined(int index) const
{
	return (nodeFlags(index) & COMBINED) != 0;
}
void SexpTreeModel::setCombined(int index, bool on)
{
	auto f = nodeFlags(index);
	f = on ? (f | COMBINED) : (f & ~COMBINED);
	setNodeFlags(index, f);
}

SexpContextMenu SexpTreeModel::queryContextMenu(int node_index) const
{
	return _actions->buildContextMenuModel(node_index);
}

bool SexpTreeModel::executeAction(int node_index, SexpActionId id, const SexpActionParam* param)
{
	return _actions->performAction(node_index, id, param);
}

// generate listing of valid argument values.
// opf = operator format to generate list for
// parent_node = the parent node we are generating list for
// arg_index = argument number of parent this argument will go at
//
// Goober5000 - add the listing from get_listing_opf_sub to the end of a new list containing
// the special argument item, but only if it's a child of a when-argument (or similar) sexp.
// Also only do this if the list has at least one item, because otherwise the argument code
// would have nothing to select from.
SexpListItemPtr SexpTreeModel::buildListingForOpf(int opf, int parent_node, int arg_index)
{
	SexpOpfListBuilder builder(_nodes, _env);
	return builder.buildListing(opf, parent_node, arg_index);
}
