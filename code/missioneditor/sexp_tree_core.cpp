/*
 * sexp_tree_core.cpp
 * Shared, UI-agnostic SEXP tree model for FRED and QtFRED.
 */

#include "sexp_tree_core.h"
#include "sexp_opf_core.h"

#include "mission/missiongoals.h"
#include "mission/missionmessage.h"
#include "mission/missionparse.h"

static constexpr int kNodeIncrement = 100; // mirrors TREE_NODE_INCREMENT

// -------- SexpListItem --------

void SexpListItem::set_op(int op_num) {
	if (op_num >= FIRST_OP) { // do we have an op value instead of an op number (index)?
		for (int i = 0; i < (int)Operators.size(); i++)
			if (op_num == Operators[i].value)
				op_num = i; // convert op value to op number
	}

	op = op_num;

	Assertion(SCP_vector_inbounds(Operators, op), "Invalid operator number %d", op);

	text = Operators[op].text;
	type = (SEXPT_OPERATOR | SEXPT_VALID);
}
void SexpListItem::set_data(const char* str, int t) {
	op = -1;
	text = str ? str : "";
	type = t;
}
void SexpListItem::add_op(int op_num) {
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;

	auto* n = new SexpListItem();
	n->set_op(op_num);
	tail->next = n;
}
void SexpListItem::add_data(const char* str, int t) {
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;

	auto* n = new SexpListItem();
	n->set_data(str, t);
	tail->next = n;
}
void SexpListItem::add_list(SexpListItem* list) {
	if (!list) return;

	// Append the entire list as-is to preserve order
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;
	tail->next = list;
}
void SexpListItem::destroy() {
	SexpListItem* p = this;
	while (p) {
		SexpListItem* next = p->next;
		delete p;
		p = next;
	}
}

// -------- ISexpEnvironment defaults --------

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

// -------- SexpTreeModel --------
SexpTreeModel::SexpTreeModel() = default;
SexpTreeModel::SexpTreeModel(ISexpEnvironment* env) : _env(env) {}

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
	Assertion((_nodes[model_root].type & SEXPT_OPERATOR) && (_nodes[model_root].type & SEXPT_VALID),
		"Root must be a valid operator node.");
	Assertion(_nodes[model_root].next == -1,
		"Root node must not have a next sibling (found next=%d).",
		_nodes[model_root].next);

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

// generate listing of valid argument values.
// opf = operator format to generate list for
// parent_node = the parent node we are generating list for
// arg_index = argument number of parent this argument will go at
//
// Goober5000 - add the listing from get_listing_opf_sub to the end of a new list containing
// the special argument item, but only if it's a child of a when-argument (or similar) sexp.
// Also only do this if the list has at least one item, because otherwise the argument code
// would have nothing to select from.
SexpListItem* SexpTreeModel::buildListingForOpf(int opf, int parent_node, int arg_index)
{
	SexpOpfListBuilder builder(_nodes, _env);
	return builder.buildListing(opf, parent_node, arg_index);
}
