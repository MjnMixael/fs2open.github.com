#include "sexp_actions_core.h"
#include "sexp_tree_core.h"

SexpContextMenu SexpActionsHandler::buildContextMenuModel(int node_index) const
{
	SexpContextMenu out;
	out.node_index = node_index;

	// 1) Build the full menu with everything disabled (so assertions on "expected actions" pass).
	auto add = [&](SexpContextGroup g, SexpActionId id, const char* label) -> int {
		out.actions.push_back({g, id, label, /*enabled*/ false});
		return static_cast<int>(out.actions.size() - 1);
	};
	auto setEnabled = [&](SexpActionId id, bool on) {
		for (auto& a : out.actions)
			if (a.id == id) {
				a.enabled = on;
				break;
			}
	};

	// Node group
	add(SexpContextGroup::Node, SexpActionId::EditText, "Edit Data");
	add(SexpContextGroup::Node, SexpActionId::DeleteNode, "Delete Item");
	add(SexpContextGroup::Node, SexpActionId::DuplicateSubtree, "Duplicate");
	add(SexpContextGroup::Node, SexpActionId::Cut, "Cut");
	add(SexpContextGroup::Node, SexpActionId::Copy, "Copy");
	add(SexpContextGroup::Node, SexpActionId::Paste, "Paste (Overwrite)");

	// Structure group
	add(SexpContextGroup::Structure, SexpActionId::AddChild, "Add Child");
	add(SexpContextGroup::Structure, SexpActionId::AddSiblingAfter, "Add Sibling After");
	add(SexpContextGroup::Structure, SexpActionId::AddSiblingBefore, "Add Sibling Before");
	add(SexpContextGroup::Structure, SexpActionId::MoveUp, "Move Up");
	add(SexpContextGroup::Structure, SexpActionId::MoveDown, "Move Down");

	// Operator group
	const int idxReplace = add(SexpContextGroup::Operator, SexpActionId::ReplaceOperator, "Replace Operator");
	add(SexpContextGroup::Operator, SexpActionId::ToggleNot, "Toggle 'not'");
	add(SexpContextGroup::Operator, SexpActionId::ResetToDefaults, "Reset Arguments to Defaults");

	// Arguments group
	add(SexpContextGroup::Arguments, SexpActionId::AddArgument, "Add Argument");
	add(SexpContextGroup::Arguments, SexpActionId::RemoveArgument, "Remove Last Argument");

	// 2) Synthetic vs real: compute flags and then flip enables.
	const auto kind = (node_index < 0) ? SexpNodeKind::SyntheticRoot : SexpNodeKind::RealNode;

	bool deletable = false; // will be computed (real) or adjusted by env (synthetic)
	bool editable = false;   // only true for real editable nodes
	bool is_root = false;
	bool is_op = false;
	bool has_prev = false;
	bool has_next = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "buildContextMenuModel: bad node");
		const auto& n = model->_nodes[node_index];

		is_root = (n.parent < 0);
		is_op = (SEXPT_TYPE(n.type) == SEXPT_OPERATOR);
		editable = (n.flags & EDITABLE) != 0;

		// Compute can_delete from parent's min-args rule (non-root only).
		if (!is_root) {
			const int parent_idx = n.parent;
			const auto& parent_node = model->_nodes[parent_idx];
			if (SEXPT_TYPE(parent_node.type) == SEXPT_OPERATOR) {
				int op_index = -1;
				for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
					if (Operators[i].text == parent_node.text) {
						op_index = i;
						break;
					}
				}
				if (op_index != -1) {
					const int min_args = Operators[op_index].min;
					const int current_arg_count = model->countArgs(parent_node.child);
					deletable = (current_arg_count > min_args);
				}
			}
		}

		// Siblings for MoveUp/MoveDown
		has_next = (n.next >= 0);
		if (n.parent >= 0) {
			int c = model->_nodes[n.parent].child;
			if (c != node_index) {
				while (model->_nodes[c].next >= 0 && model->_nodes[c].next != node_index) {
					c = model->_nodes[c].next;
				}
				has_prev = (model->_nodes[c].next == node_index);
			}
		}

		// If this is an operator, fill Replace choices and enable operator/argument actions appropriately.
		if (is_op) {
			auto& replace = out.actions[idxReplace]; // ReplaceOperator item
			replace.enabled = true;

			const int cur_op_const = get_operator_const(n.text.c_str());
			const int cur_ret = query_operator_return_type(cur_op_const);
			for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
				if (query_operator_return_type(Operators[i].value) == cur_ret) {
					replace.choices.push_back({/*op_index*/ i, /*arg_index*/ -1});
					replace.choiceText.emplace_back(Operators[i].text);
				}
			}

			// Arguments min/max awareness
			int op_index = -1;
			for (int i = 0; i < static_cast<int>(Operators.size()); ++i)
				if (Operators[i].text == n.text) {
					op_index = i;
					break;
				}

			if (op_index >= 0) {
				const int argc = model->countArgs(n.child);
				const int minA = Operators[op_index].min;
				const int maxA = Operators[op_index].max;
				setEnabled(SexpActionId::AddArgument, (maxA < 0 || argc < maxA));
				setEnabled(SexpActionId::RemoveArgument, (argc > 0 && argc > minA));
			}

			// Quick operator toggles
			setEnabled(SexpActionId::ToggleNot, true);
			setEnabled(SexpActionId::ResetToDefaults, true);
		}
	}

	bool can_edit_text = editable;
	bool can_delete = deletable;
	bool can_duplicate_subtree = kind == SexpNodeKind::RealNode;
	bool can_cut = deletable;
	bool can_copy = kind == SexpNodeKind::RealNode;
	bool can_paste = kind == SexpNodeKind::RealNode;

	bool can_add_child = is_op;
	bool can_add_sib_after = (kind == SexpNodeKind::RealNode) && !is_root;
	bool can_add_sib_before = (kind == SexpNodeKind::RealNode) && !is_root;
	bool can_move_up = has_prev;
	bool can_move_down = has_next;

	// 3) Let the environment tweak enables (e.g., allow Delete on synthetic root).
	// TODO make this work for all actions, not just DeleteNode and Cut
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::EditText, kind, node_index, can_edit_text);
		model->environment()->overrideNodeActionEnabled(SexpActionId::DeleteNode, kind, node_index, can_delete);
		model->environment()->overrideNodeActionEnabled(SexpActionId::DuplicateSubtree, kind, node_index, can_duplicate_subtree);
		model->environment()->overrideNodeActionEnabled(SexpActionId::Cut, kind, node_index, can_cut);
		model->environment()->overrideNodeActionEnabled(SexpActionId::Copy, kind, node_index, can_copy);
		model->environment()->overrideNodeActionEnabled(SexpActionId::Paste, kind, node_index, can_paste);

		model->environment()->overrideNodeActionEnabled(SexpActionId::AddChild, kind, node_index, can_add_child);
		model->environment()->overrideNodeActionEnabled(SexpActionId::AddSiblingAfter, kind, node_index, can_add_sib_after);
		model->environment()->overrideNodeActionEnabled(SexpActionId::AddSiblingBefore, kind, node_index, can_add_sib_before);
		model->environment()->overrideNodeActionEnabled(SexpActionId::MoveUp, kind, node_index, can_move_up);
		model->environment()->overrideNodeActionEnabled(SexpActionId::MoveDown, kind, node_index, can_move_down);
	}

	// 4) Flip enables for Node/Structure groups now that we have final flags.
	setEnabled(SexpActionId::EditText, can_edit_text);
	setEnabled(SexpActionId::DeleteNode, can_delete);
	setEnabled(SexpActionId::DuplicateSubtree, can_duplicate_subtree); // allow duplicate on real nodes
	setEnabled(SexpActionId::Cut, can_cut);
	setEnabled(SexpActionId::Copy, can_copy);
	setEnabled(SexpActionId::Paste, can_paste); // TODO gate later via env clipboard

	// Structure
	setEnabled(SexpActionId::AddChild, can_add_child);
	setEnabled(SexpActionId::AddSiblingAfter, can_add_sib_after);
	setEnabled(SexpActionId::AddSiblingBefore, can_add_sib_before);
	setEnabled(SexpActionId::MoveUp, can_move_up);
	setEnabled(SexpActionId::MoveDown, can_move_down);

	return out;
}

bool SexpActionsHandler::performAction(int node_index, SexpActionId id, const SexpActionParam* p)
{
	Assertion(SCP_vector_inbounds(model->_nodes, node_index), "performAction: bad node");
	switch (id) {
	// Node
	case SexpActionId::DeleteNode:
		return deleteNode(node_index);

	case SexpActionId::DuplicateSubtree:
		return duplicateSubtree(node_index);

	// Structure
	case SexpActionId::AddChild:
		return addChild(node_index);

	case SexpActionId::AddSiblingBefore:
		return addSiblingBefore(node_index);

	case SexpActionId::AddSiblingAfter:
		return addSiblingAfter(node_index);

	case SexpActionId::MoveUp:
		return moveUp(node_index);

	case SexpActionId::MoveDown:
		return moveDown(node_index);

	// Operator
	case SexpActionId::ReplaceOperator:
		return replaceOperator(node_index, p);

	case SexpActionId::ToggleNot:
		return toggleNot(node_index);

	case SexpActionId::ResetToDefaults:
		return resetToDefaults(node_index);

	// Arguments
	case SexpActionId::AddArgument:
		return addArgument(node_index);

	case SexpActionId::RemoveArgument:
		return removeArgument(node_index);

	default:
		return false;
	}
}

bool SexpActionsHandler::deleteNode(int node_index)
{
	if (model->_nodes[node_index].parent < 0)
		return false;
	model->freeNode(node_index, /*cascade=*/false);
	return true;
}

bool SexpActionsHandler::duplicateSubtree(int node_index)
{
	// TODO: implement subtree clone
	return false;
}

bool SexpActionsHandler::addChild(int node_index)
{
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;

	// Next argument position is current argc
	int argc = 0;
	for (int c = nodes[node_index].child; c >= 0; c = nodes[c].next)
		++argc;

	return addChildAt(node_index, argc);
}

bool SexpActionsHandler::addChildAt(int parent_node, int arg_pos)
{
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, parent_node))
		return false;
	if (SEXPT_TYPE(nodes[parent_node].type) != SEXPT_OPERATOR)
		return false;

	// Operator table index
	int op_index = -1;
	for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
		if (Operators[i].text == nodes[parent_node].text) {
			op_index = i;
			break;
		}
	}
	if (op_index < 0)
		return false;

	// argc / max
	const int maxA = Operators[op_index].max; // -1 == unbounded
	int argc = 0;
	for (int c = nodes[parent_node].child; c >= 0; c = nodes[c].next)
		++argc;
	if (maxA >= 0 && argc >= maxA)
		return false;

	// Clamp arg_pos into [0, argc]
	if (arg_pos < 0)
		arg_pos = 0;
	if (arg_pos > argc)
		arg_pos = argc;

	const int op_const = Operators[op_index].value;
	const int opf = query_operator_argument_type(op_const, arg_pos);
	if (opf < 0)
		return false;

	// Create default DETACHED node
	const int newn = model->createDefaultArgForOpf(opf, /*parent*/ -1, op_index, arg_pos, parent_node);
	nodes[newn].parent = parent_node;

	// Splice before the current node at arg_pos (or append if arg_pos == argc)
	int prev = -1, cur = nodes[parent_node].child, pos = 0;
	while (cur >= 0 && pos < arg_pos) {
		prev = cur;
		cur = nodes[cur].next;
		++pos;
	}

	if (prev < 0) {
		// insert at head
		nodes[newn].next = nodes[parent_node].child;
		nodes[parent_node].child = newn;
	} else {
		nodes[newn].next = nodes[prev].next; // which is 'cur'
		nodes[prev].next = newn;
	}

	return true;
}

bool SexpActionsHandler::addSiblingAfter(int node_index)
{
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;

	const int parent = nodes[node_index].parent;
	if (parent < 0)
		return false;

	// parent must be an operator
	if (SEXPT_TYPE(nodes[parent].type) != SEXPT_OPERATOR)
		return false;

	// find operator table index from parent's text
	int op_index = -1;
	for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
		if (Operators[i].text == nodes[parent].text) {
			op_index = i;
			break;
		}
	}
	if (op_index < 0)
		return false;

	// current argc and max
	const int maxA = Operators[op_index].max; // -1 = unbounded
	int argc = 0;
	for (int c = nodes[parent].child; c >= 0; c = nodes[c].next)
		++argc;
	if (maxA >= 0 && argc >= maxA)
		return false; // would exceed max

	// determine the argument index we are inserting at: pos = index(node) + 1
	int arg_pos = 0;
	for (int c = nodes[parent].child; c >= 0 && c != node_index; c = nodes[c].next)
		++arg_pos;
	if (nodes[parent].child < 0 || nodes[node_index].parent != parent)
		return false; // safety

	arg_pos += 1;

	// type for that arg position
	const int op_const = Operators[op_index].value;
	const int opf = query_operator_argument_type(op_const, arg_pos);
	if (opf < 0)
		return false; // no arg allowed at this position

	// build default arg as a DETACHED node (parent = -1), then splice
	const int newn = model->createDefaultArgForOpf(opf, /*parent*/ -1, op_index, arg_pos, parent);

	// splice after node_index
	nodes[newn].parent = parent;
	nodes[newn].next = nodes[node_index].next;
	nodes[node_index].next = newn;

	return true;
}

bool SexpActionsHandler::addSiblingBefore(int node_index)
{
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;

	const int parent = nodes[node_index].parent;
	if (parent < 0)
		return false;

	if (SEXPT_TYPE(nodes[parent].type) != SEXPT_OPERATOR)
		return false;

	// operator table index
	int op_index = -1;
	for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
		if (Operators[i].text == nodes[parent].text) {
			op_index = i;
			break;
		}
	}
	if (op_index < 0)
		return false;

	// current argc and max
	const int maxA = Operators[op_index].max; // -1 = unbounded
	int argc = 0;
	for (int c = nodes[parent].child; c >= 0; c = nodes[c].next)
		++argc;
	if (maxA >= 0 && argc >= maxA)
		return false;

	// find previous sibling and arg_pos (the position this node currently occupies)
	int prev = -1, cur = nodes[parent].child, arg_pos = 0;
	while (cur >= 0 && cur != node_index) {
		prev = cur;
		cur = nodes[cur].next;
		++arg_pos;
	}
	if (cur != node_index)
		return false; // not under this parent?

	// type for that arg position
	const int op_const = Operators[op_index].value;
	const int opf = query_operator_argument_type(op_const, arg_pos);
	if (opf < 0)
		return false;

	// create default DETACHED node, then splice before
	const int newn = model->createDefaultArgForOpf(opf, /*parent*/ -1, op_index, arg_pos, parent);

	nodes[newn].parent = parent;

	if (prev >= 0) {
		nodes[newn].next = nodes[prev].next; // which is node_index
		nodes[prev].next = newn;
	} else {
		// insert at head
		nodes[newn].next = nodes[parent].child; // current head (node_index)
		nodes[parent].child = newn;
	}

	return true;
}

bool SexpActionsHandler::moveUp(int node_index)
{
	auto& nodes = model->_nodes;

	if (!SCP_vector_inbounds(nodes, node_index))
		return false;
	const int parent = nodes[node_index].parent;
	if (parent < 0)
		return false; // root can't move within siblings

	// Find prevprev, prev, and cur=node_index under parent
	int prevprev = -1, prev = -1, cur = nodes[parent].child;
	while (cur >= 0 && cur != node_index) {
		prevprev = prev;
		prev = cur;
		cur = nodes[cur].next;
	}
	if (prev < 0)
		return false; // already first or not found

	const int a = prev;          // previous sibling
	const int b = node_index;    // current node
	const int c = nodes[b].next; // node after current

	// Relink: prevprev -> b -> a -> c
	if (prevprev < 0)
		nodes[parent].child = b;
	else
		nodes[prevprev].next = b;

	nodes[b].next = a;
	nodes[a].next = c;

	return true;
}

bool SexpActionsHandler::moveDown(int node_index)
{
	// must have a parent and a next sibling to move down
	if (model->_nodes[node_index].parent < 0)
		return false;

	const int parent = model->_nodes[node_index].parent;
	const int a = node_index;            // current node
	const int b = model->_nodes[a].next; // next sibling

	if (b < 0)
		return false; // already last

	// find previous sibling of 'a' (prev), if any
	int prev = -1;
	int cur = model->_nodes[parent].child;
	while (cur >= 0 && cur != a) {
		prev = cur;
		cur = model->_nodes[cur].next;
	}
	// cur should be 'a' now

	const int c = model->_nodes[b].next; // node after 'b'

	// relink: prev -> b -> a -> c
	if (prev < 0)
		model->_nodes[parent].child = b;
	else
		model->_nodes[prev].next = b;

	model->_nodes[b].next = a;
	model->_nodes[a].next = c;

	return true;
}

bool SexpActionsHandler::replaceOperator(int node_index, const SexpActionParam* p)
{
	if (!p)
		return false;
	model->replaceOperator(node_index, p->op_index);
	return true;
}

bool SexpActionsHandler::toggleNot(int node_index)
{
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;

	// Helper: find previous sibling (under known parent)
	auto find_prev = [&](int parent, int idx) -> int {
		int prev = -1, cur = nodes[parent].child;
		while (cur >= 0 && cur != idx) {
			prev = cur;
			cur = nodes[cur].next;
		}
		return (cur == idx) ? prev : -1;
	};

	const bool is_op = (SEXPT_TYPE(nodes[node_index].type) == SEXPT_OPERATOR);
	const int cur_op = is_op ? get_operator_const(nodes[node_index].text.c_str()) : -1;

	// ---------- Unwrap: (not X) -> X ----------
	if (cur_op == OP_NOT) {
		const int child = nodes[node_index].child;
		if (child < 0)
			return false; // nothing to unwrap
		if (nodes[child].next >= 0)
			return false; // only unwrap single-arg NOT

		// In-place replace NOT with its child subtree (works for root or non-root)
		// Capture child's fields
		const int grand = nodes[child].child;

		// Replace this node's type/text with child's
		model->setNode(node_index, nodes[child].type, nodes[child].text.c_str());

		// Adopt child's children as our own
		nodes[node_index].child = grand;
		// Fix parent pointers for entire child chain
		int k = grand;
		while (k >= 0) {
			nodes[k].parent = node_index;
			k = nodes[k].next;
		}

		// "Delete" the old child node without disturbing links we just rewired
		nodes[child].type = SEXPT_UNUSED;
		nodes[child].parent = -1;
		nodes[child].child = -1;
		nodes[child].next = -1;
		nodes[child].text.clear();
		return true;
	}

	// ---------- Wrap: X -> (not X) ----------
	// Build a detached NOT node
	int not_op_index = -1;
	for (int i = 0; i < (int)Operators.size(); ++i) {
		if (Operators[i].value == OP_NOT) {
			not_op_index = i;
			break;
		}
	}
	if (not_op_index < 0)
		return false;

	const int parent = nodes[node_index].parent;

	if (parent < 0) {
		// Root case: replace node in place with NOT, move original content under it
		// (Assumes root has no next sibling; if it does, we still work, but saving enforces single root later.)
		// Create a detached clone of the original node's content
		int clone = model->allocateNode(-1, -1); // detached
		// Copy content into clone
		model->setNode(clone, nodes[node_index].type, nodes[node_index].text.c_str());
		nodes[clone].child = nodes[node_index].child;
		nodes[clone].next = -1;
		nodes[clone].parent = node_index;

		// Fix children's parent from node_index -> clone
		int k = nodes[clone].child;
		while (k >= 0) {
			nodes[k].parent = clone;
			k = nodes[k].next;
		}

		// Turn the current node into NOT and adopt the clone
		model->setNode(node_index, (SEXPT_OPERATOR | SEXPT_VALID), Operators[not_op_index].text.c_str());
		nodes[node_index].child = clone;

		// Ensure the wrapped node is the ONLY child of NOT
		nodes[clone].next = -1;
		return true;
	} else {
		// Non-root: splice NOT before node_index in parent's child list, then reparent node under NOT
		const int prev = find_prev(parent, node_index);
		const int after = nodes[node_index].next;

		// Create a detached NOT node
		const int not_node = model->allocateNode(-1, -1); // detached
		model->setNode(not_node, (SEXPT_OPERATOR | SEXPT_VALID), Operators[not_op_index].text.c_str());

		// Splice NOT into the sibling chain at node's current position
		nodes[not_node].parent = parent;
		nodes[not_node].next = node_index;

		if (prev < 0)
			nodes[parent].child = not_node;
		else
			nodes[prev].next = not_node;

		// Detach node from siblings and make it the ONLY child of NOT
		nodes[node_index].parent = not_node;
		nodes[node_index].next = -1;
		nodes[not_node].child = node_index;

		// NOT keeps original 'after' as its next sibling via the node we just detached:
		// because we inserted NOT *before* node_index and then removed node_index from the chain,
		// the sibling after NOT is still whatever followed node_index originally (via nodes[not_node].next ==
		// node_index initially). Adjust NOT's next to what 'node_index' used to point to:
		nodes[not_node].next = after;

		return true;
	}
}

bool SexpActionsHandler::resetToDefaults(int node_index)
{
	// rebuild arg list respecting min/max
	const int cur_index = [&] {
		for (int i = 0; i < static_cast<int>(Operators.size()); ++i)
			if (Operators[i].text == model->_nodes[node_index].text)
				return i;
		return -1;
	}();
	if (cur_index < 0)
		return false;
	// clear children
	const int to_free = model->_nodes[node_index].child;
	model->_nodes[node_index].child = -1;
	if (to_free >= 0)
		model->freeNode(to_free, true);
	model->ensureOperatorArity(node_index, cur_index);
	return true;
}

bool SexpActionsHandler::addArgument(int node_index)
{
	const int cur_index = [&] {
		for (int i = 0; i < static_cast<int>(Operators.size()); ++i)
			if (Operators[i].text == model->_nodes[node_index].text)
				return i;
		return -1;
	}();

	if (cur_index < 0)
		return false;
	// Force-add one more, respecting max via ensureOperatorArity
	// (Just extend min by 1 temporarily: simplest is to append default for next index)
	int argc = model->countArgs(model->_nodes[node_index].child);
	int opf = query_operator_argument_type(Operators[cur_index].value, argc);
	if (opf < 0)
		return false; // no extra allowed
	const int added = model->createDefaultArgForOpf(opf, node_index, cur_index, argc, node_index);
	// link at end
	int tail = model->_nodes[node_index].child;
	if (tail < 0)
		model->_nodes[node_index].child = added;
	else {
		while (model->_nodes[tail].next >= 0)
			tail = model->_nodes[tail].next;
		model->_nodes[tail].next = added;
	}
	return true;
}

bool SexpActionsHandler::removeArgument(int node_index)
{
	// remove last arg if > min
	const int cur_index = [&] {
		for (int i = 0; i < static_cast<int>(Operators.size()); ++i)
			if (Operators[i].text == model->_nodes[node_index].text)
				return i;
		return -1;
	}();
	if (cur_index < 0)
		return false;
	int argc = model->countArgs(model->_nodes[node_index].child);
	if (argc <= Operators[cur_index].min)
		return false;
	// cut tail
	int prev = -1, cur = model->_nodes[node_index].child;
	while (model->_nodes[cur].next >= 0) {
		prev = cur;
		cur = model->_nodes[cur].next;
	}
	if (prev < 0)
		model->_nodes[node_index].child = -1;
	else
		model->_nodes[prev].next = -1;
	model->freeNode(cur, true);
	return true;
}