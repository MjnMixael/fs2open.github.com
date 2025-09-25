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

	auto getAction = [&](SexpActionId id) -> SexpContextAction* {
		for (auto& a : out.actions)
			if (a.id == id)
				return &a;
		return nullptr;
	};

	// Node group
	add(SexpContextGroup::Node, SexpActionId::EditText, "Edit Data");
	add(SexpContextGroup::Node, SexpActionId::DeleteNode, "Delete Item");
	add(SexpContextGroup::Node, SexpActionId::Cut, "Cut");
	add(SexpContextGroup::Node, SexpActionId::Copy, "Copy");
	add(SexpContextGroup::Node, SexpActionId::PasteOverwrite, "Paste (Overwrite)");
	add(SexpContextGroup::Node, SexpActionId::AddOperator, "Add Operator");
	add(SexpContextGroup::Node, SexpActionId::AddData, "Add Data");
	add(SexpContextGroup::Node, SexpActionId::PasteAdd, "Paste (Add Child)");

	// Structure group
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

	bool is_root = false;
	bool is_op = false;
	bool has_prev = false;
	bool has_next = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "buildContextMenuModel: bad node");
		const auto& n = model->_nodes[node_index];

		is_root = (n.parent < 0);
		is_op = (SEXPT_TYPE(n.type) == SEXPT_OPERATOR);

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

	// Seed a single submenu choice so sexp_tree can render submenus now.
	// We'll replace these with real category trees later.
	// TODO Finish this for real

	// Add Operator submenu stub
	if (auto* a = getAction(SexpActionId::AddOperator)) {
		a->choices.push_back({/*op_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// Add Data submenu stub
	if (auto* a = getAction(SexpActionId::AddData)) {
		a->choices.push_back({/*data_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	bool can_move_up = has_prev;
	bool can_move_down = has_next;

	// 3) Let the environment tweak enables (e.g., allow Delete on synthetic root).
	// TODO make this work for all actions, not just DeleteNode and Cut
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::MoveUp, kind, node_index, can_move_up);
		model->environment()->overrideNodeActionEnabled(SexpActionId::MoveDown, kind, node_index, can_move_down);
	}

	// 4) Flip enables for Node/Structure groups now that we have final flags.
	setEnabled(SexpActionId::EditText, canEditNode(kind, node_index));
	setEnabled(SexpActionId::DeleteNode, canDeleteNode(kind, node_index));
	setEnabled(SexpActionId::Cut, canCutNode(kind, node_index));
	setEnabled(SexpActionId::Copy, canCopyNode(kind, node_index));
	setEnabled(SexpActionId::PasteOverwrite, canPasteOverrideNode(kind, node_index));
	setEnabled(SexpActionId::AddOperator, canAddOperatorNode(kind, node_index));
	setEnabled(SexpActionId::AddData, canAddDataNode(kind, node_index));
	setEnabled(SexpActionId::PasteAdd, canPasteAddNode(kind, node_index));

	// Structure
	setEnabled(SexpActionId::MoveUp, can_move_up);
	setEnabled(SexpActionId::MoveDown, can_move_down);

	return out;
}

bool SexpActionsHandler::performAction(int node_index, SexpActionId id, const SexpActionParam* p)
{
	Assertion(SCP_vector_inbounds(model->_nodes, node_index), "performAction: bad node");
	switch (id) {
	// Node
	case SexpActionId::EditText:
		return editText(node_index, ""); // TODO create a way to pass text to this maybe through SexpActionParam

	case SexpActionId::DeleteNode:
		return deleteNode(node_index);

	// Structure
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

int SexpActionsHandler::nodeEffectiveType_(int node_index) const
{
	Assertion(SCP_vector_inbounds(model->_nodes, node_index), "nodeEffectiveType_: bad node");
	const auto& n = model->_nodes[node_index];

	if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR) {
		const int opc = get_operator_const(n.text.c_str());
		return query_operator_return_type(opc);
	}
	// Variables encode their base kind in the type
	if (n.type & SEXPT_VARIABLE) {
		return SEXPT_TYPE(n.type);
	}
	return SEXPT_TYPE(n.type); // number/string/etc.
}

// Map OPF to "would adding an operator make sense?"
bool SexpActionsHandler::opfAcceptsOperator_(int opf) noexcept
{
	// Old FRED enabled operator items whenever an OPF was defined for the slot,
	// except OPF_NONE (no arg) and OPF_NULL (expects a "no-op" literal).
	if (opf < 0)
		return false;
	switch (opf) {
	case OPF_NONE:
		return false; // no argument allowed
	case OPF_NULL:
		return true; // inserts OP_NOP operator (legacy did this)
	default:
		return true; // bool/number/goal/etc. -> operators allowed
	}
}

// Map OPF to "would adding data (number/string/etc.) make sense?"
bool SexpActionsHandler::opfAcceptsPlainData_(int opf) noexcept
{
	if (opf < 0)
		return false;
	switch (opf) {
	case OPF_NONE:
		return false;
	case OPF_NULL:
		return false; // wants OP_NOP, not data
	case OPF_NUMBER:
	case OPF_POSITIVE:
	case OPF_AMBIGUOUS:
	case OPF_CONTAINER_VALUE:
		return true; // legacy enabled Number (and String for some)
	default:
		// Many OPFs are domain-specific (ships, wings, etc.) that surface through "Add Data" menus.
		// Treat other OPFs as data-capable so the UI can present choices from OPF listings.
		return true;
	}
}

// Compute the OPF expected if we append a child to parent_index.
// Returns <0 if no arg can be added (e.g., maxed out).
int SexpActionsHandler::expectedOpfForAppend_(int parent_index) const noexcept
{
	const auto& parent = model->node(parent_index);

	if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
		// Capacity check
		int op_index = -1;
		for (int i = 0; i < (int)Operators.size(); ++i)
			if (Operators[i].text == parent.text) {
				op_index = i;
				break;
			}
		if (op_index < 0)
			return -1;

		const int argc = model->countArgs(parent.child);
		const int maxA = Operators[op_index].max;
		if (!(maxA < 0 || argc < maxA))
			return -1;

		return query_operator_argument_type(op_index, argc);
	}

	if (parent.type & SEXPT_CONTAINER_DATA) {
		// Old FRED special-cased list + AT_INDEX modifier -> number expected; else number/string allowed.
		const auto* cont = get_sexp_container(parent.text.c_str());
		if (!cont)
			return -1;

		// If list + AT_INDEX with only the index present, treat the next thing as NUMBER-only.
		const int modifier = parent.child;
		if (modifier >= 0 && cont->is_list()) {
			const auto& mn = model->node(modifier);
			const int add_count = model->countArgs(modifier);
			if (add_count == 1 && get_list_modifier(mn.text.c_str()) == ListModifier::AT_INDEX) {
				return OPF_NUMBER;
			}
		}

		// Otherwise, legacy enabled both Number and String menus and also operator items for "container multidim"
		// so consider this slot as "string-ish" to allow both data and operators via listings.
		return OPF_AMBIGUOUS;
	}

	// Numbers/strings/etc. are not parents for new children
	return -1;
}

bool SexpActionsHandler::canEditNode(SexpNodeKind kind, int node_index) const
{
	bool can_edit = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canEditNode: bad node");
		const auto& n = model->_nodes[node_index];

		// Base rule: plain numbers/strings editable; everything else not
		if ((SEXPT_TYPE(n.type) == SEXPT_NUMBER) || (SEXPT_TYPE(n.type) == SEXPT_STRING)) {
			can_edit = true;
		} else {
			can_edit = false; // operators, variables, container name/data aren't free-edited
		}

		// Special-case: container parent
		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];
			if (parent.type & SEXPT_CONTAINER_DATA) {
				// Determine container kind (list vs map) from the parent’s text
				const auto* p_container = get_sexp_container(parent.text.c_str());
				if (p_container && p_container->is_list()) {
					// First child (modifier) for list containers is NOT editable
					if (node_index == parent.child) {
						can_edit = false;
					}
				}
			}
		}
	} else {
		can_edit = false;
	}

	// Environment can tweak (e.g., allow editing the synthetic root label)
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::EditText, kind, node_index, can_edit);
	}

	return can_edit;
}

bool SexpActionsHandler::canDeleteNode(SexpNodeKind kind, int node_index) const
{
	bool can_delete = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canDeleteNode: bad node");
		const auto& n = model->_nodes[node_index];

		// Top-level operator at the real root (e.g., "when") is not deletable
		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];

			// Parent is operator so use the required arg rule
			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int op_index = -1;
				for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
					if (Operators[i].text == parent.text) {
						op_index = i;
						break;
					}
				}
				if (op_index != -1) {
					const int min_required = std::max(0, Operators[op_index].min);

					// Find argc and this nodes 0 based position
					int argc = 0, arg_pos = -1;
					for (int c = parent.child; c >= 0; c = model->_nodes[c].next) {
						if (c == node_index)
							arg_pos = argc;
						++argc;
					}

					if (arg_pos >= 0) {
						// Deletable iff parent has > min args AND this arg is not among required ones
						can_delete = (argc > min_required) && (arg_pos >= min_required);
					}
				}
			}
			// Parent is container so first child is protected but everything else is deletable
			else if (parent.type & SEXPT_CONTAINER_DATA) {
				const int first_child = parent.child;
				can_delete = (first_child >= 0) && (node_index != first_child);
			}
		}
	}

	// Allow environment override
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::DeleteNode, kind, node_index, can_delete);
	}

	return can_delete;
}

bool SexpActionsHandler::canCutNode(SexpNodeKind kind, int node_index) const
{
	bool can_cut = canDeleteNode(kind, node_index);

	// Allow environment override cut separetly from delete
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::Cut, kind, node_index, can_cut);
	}

	return can_cut;
}

bool SexpActionsHandler::canCopyNode(SexpNodeKind kind, int node_index) const
{
	// Base rule: real nodes are copyable; synthetic roots are not (unless env says so).
	bool can_copy = (kind == SexpNodeKind::RealNode);

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::Copy, kind, node_index, can_copy);
	}
	return can_copy;
}

// TODO update to support paste overwriting container elements too
bool SexpActionsHandler::canPasteOverrideNode(SexpNodeKind kind, int node_index) const
{
	bool can_paste = false;

	if (kind == SexpNodeKind::RealNode && model->hasClipboard()) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canPasteNode: bad node");
		const auto& n = model->_nodes[node_index];

		// Determine the expected type at this position:
		// - If parent is an operator, use its argument type for this child's arg_pos
		// - Otherwise (including root), use this node's own effective type
		int expected = -1;

		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];
			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int arg_pos = -1, argc = 0;
				for (int c = parent.child; c >= 0; c = model->_nodes[c].next) {
					if (c == node_index)
						arg_pos = argc;
					++argc;
				}
				if (arg_pos >= 0) {
					int op_index = -1;
					for (int i = 0; i < (int)Operators.size(); ++i) {
						if (Operators[i].text == parent.text) {
							op_index = i;
							break;
						}
					}
					if (op_index >= 0) {
						expected = query_operator_argument_type(op_index, arg_pos);
					}
				}
			}
		}

		if (expected < 0) {
			// No operator parent (e.g., root) should match node's own effective type
			expected = nodeEffectiveType_(node_index);
		}

		const int clip_type = model->clipboardReturnType();
		can_paste = (clip_type >= 0) && (expected >= 0) && (clip_type == expected);
	}

	// Environment can still refine (e.g., forbid certain contexts or allow synthetic roots later)
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::PasteOverwrite, kind, node_index, can_paste);
	}
	return can_paste;
}

bool SexpActionsHandler::canAddOperatorNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canAddOperatorNode: bad node");
		const auto& n = model->_nodes[node_index];

		if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR || (n.type & SEXPT_CONTAINER_DATA)) {
			const int opf = expectedOpfForAppend_(node_index);
			enable = (opf >= 0) && opfAcceptsOperator_(opf);
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::AddOperator, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canAddDataNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canAddDataNode: bad node");
		const auto& n = model->_nodes[node_index];

		if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR || (n.type & SEXPT_CONTAINER_DATA)) {
			const int opf = expectedOpfForAppend_(node_index);
			enable = (opf >= 0) && opfAcceptsPlainData_(opf);
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::AddData, kind, node_index, enable);
	}
	return enable;
}

// TODO update to support paste adding to containers too
bool SexpActionsHandler::canPasteAddNode(SexpNodeKind kind, int node_index) const
{
	bool can_add = false;

	// Must be a real node and have something on the clipboard
	if (kind == SexpNodeKind::RealNode && model->hasClipboard()) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canPasteAddNode: bad node");
		const auto& target = model->_nodes[node_index];

		// We only allow adding a child to an OPERATOR node (v1).
		// (Containers / other parents can be supported later if needed.)
		if (SEXPT_TYPE(target.type) == SEXPT_OPERATOR) {
			// Look up operator entry
			int op_index = -1;
			for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
				if (Operators[i].text == target.text) {
					op_index = i;
					break;
				}
			}

			if (op_index != -1) {
				//const int minA = Operators[op_index].min;
				const int maxA = Operators[op_index].max;

				// Where would the new arg go? At the end.
				const int argc = model->countArgs(target.child);
				const int new_pos = argc;

				// Check capacity
				const bool has_space = (maxA < 0) || (argc < maxA);
				if (has_space) {
					// Expected type for that new argument position
					const int expected = query_operator_argument_type(op_index, new_pos);

					// Clipboard root type
					const int clip_type = model->clipboardReturnType();

					// Match required exactly (you can broaden to compatibility later)
					can_add = (clip_type >= 0) && (expected >= 0) && (clip_type == expected);
				}
			}
		}
	}

	// Let the environment tweak (e.g., disallow adding in certain editors)
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::PasteAdd, kind, node_index, can_add);
	}
	return can_add;
}

bool SexpActionsHandler::editText(int node_index, const char* new_text)
{
	/* auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;
	if ((nodes[node_index].flags & EDITABLE) == 0)
		return false;
	nodes[node_index].text = new_text;*/
	return true;
}

bool SexpActionsHandler::deleteNode(int node_index)
{
	if (model->_nodes[node_index].parent < 0)
		return false;
	model->freeNode(node_index, /*cascade=*/false);
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
	int opf = query_operator_argument_type(cur_index, argc);
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