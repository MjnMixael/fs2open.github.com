#include "sexp_actions_core.h"
#include "sexp_tree_core.h"

#include "globalincs/pstypes.h"

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
	add(SexpContextGroup::Operator, SexpActionId::AddOperator, "Add Operator");
	add(SexpContextGroup::Data, SexpActionId::AddData, "Add Data");
	add(SexpContextGroup::Node, SexpActionId::PasteAdd, "Paste (Add Child)");
	add(SexpContextGroup::Operator, SexpActionId::InsertOperator, "Insert Operator");
	add(SexpContextGroup::Operator, SexpActionId::ReplaceOperator, "Replace Operator");
	add(SexpContextGroup::Data, SexpActionId::ReplaceData, "Replace Data");
	add(SexpContextGroup::Variable, SexpActionId::ReplaceVariable, "Replace Variable");
	add(SexpContextGroup::Container, SexpActionId::ReplaceContainerName, "Replace Container Name");
	add(SexpContextGroup::Container, SexpActionId::ReplaceContainerData, "Replace Container Data");

	// TODO Rest are future enhancements not yet implemented.
	add(SexpContextGroup::Structure, SexpActionId::MoveUp, "Move Up");
	add(SexpContextGroup::Structure, SexpActionId::MoveDown, "Move Down");
	add(SexpContextGroup::Operator, SexpActionId::ResetToDefaults, "Reset Arguments to Defaults");

	// 2) Synthetic vs real: compute flags and then flip enables.
	const auto kind = (node_index < 0) ? SexpNodeKind::SyntheticRoot : SexpNodeKind::RealNode;

	// Seed a single submenu choice so sexp_tree can render submenus now.
	// We'll replace these with real category trees later.
	// TODO Finish this for real

	// --- Add Operator submenu (legacy-style: build all, then enable a subset) ---
	if (auto* addOp = getAction(SexpActionId::AddOperator)) {
		addOp->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			const int parent_index = node_index; // appending under selected node
			const int opf = expectedOpfForAppend_(parent_index);
			if (opf >= 0) {
				const int arg_index = model->countArgs(n.child);

				auto& catRoots = addOp->children; // top-level category nodes

				// Maps to avoid searching by label and to avoid pointer invalidation
				// category_id -> index in addOp->children
				std::unordered_map<int, int> catIndexById;
				catIndexById.reserve(op_menu.size());

				// subIndexByCat[category_id][sub_id] -> index in addOp->children[catIdx].children
				std::unordered_map<int, std::unordered_map<int, int>> subIndexByCat;
				subIndexByCat.reserve(op_submenu.size());

				// Helper: ensure category header exists (by id) and return its index
				auto ensureCategoryById = [&](int category_id, const SCP_string& label) -> int {
					auto it = catIndexById.find(category_id);
					if (it != catIndexById.end())
						return it->second;

					SexpContextAction cat;
					cat.group = SexpContextGroup::Operator;
					cat.id = SexpActionId::None; // header node
					cat.label = label;
					cat.enabled = false; // will be set after enable pass
					catRoots.push_back(std::move(cat));
					int idx = (int)catRoots.size() - 1;
					catIndexById[category_id] = idx;
					return idx;
				};

				// Helper: ensure subcategory header exists (by sub_id) under category index
				auto ensureSubcategoryById = [&](int catIdx, int sub_id, const SCP_string& label) -> int {
					auto& subMap = subIndexByCat[catIdx];
					auto it = subMap.find(sub_id);
					if (it != subMap.end())
						return it->second;

					SexpContextAction sub;
					sub.group = SexpContextGroup::Operator;
					sub.id = SexpActionId::None; // header node
					sub.label = label;
					sub.enabled = false;

					catRoots[catIdx].children.push_back(std::move(sub));
					int idx = (int)catRoots[catIdx].children.size() - 1;
					subMap[sub_id] = idx;
					return idx;
				};

				// Build label lookups from sexp.cpp tables
				std::unordered_map<int, SCP_string> catLabel, subLabel;
				catLabel.reserve(op_menu.size());
				for (const auto& c : op_menu)
					catLabel[c.id] = c.name;

				subLabel.reserve(op_submenu.size());
				for (const auto& sc : op_submenu)
					subLabel[sc.id] = sc.name;

				// 1) Build all headers (categories and subcategories)
				for (const auto& c : op_menu) {
					(void)ensureCategoryById(c.id, c.name);
				}
				for (const auto& sc : op_submenu) {
					int parent_cat_id = category_of_subcategory(sc.id);
					auto it = catLabel.find(parent_cat_id);
					if (it == catLabel.end())
						continue; // safety
					int catIdx = ensureCategoryById(parent_cat_id, it->second);
					(void)ensureSubcategoryById(catIdx, sc.id, sc.name);
				}

				// Map operator VALUE -> leaf pointer for the enable pass
				std::unordered_map<int, SexpContextAction*> opValueToLeaf;
				opValueToLeaf.reserve(Operators.size());

				// Helper to append a disabled operator leaf under a parent menu
				auto appendOperatorLeaf = [&](SexpContextAction& parentMenu, int op_value, const SCP_string& name) {
					SexpContextAction leaf;
					leaf.group = SexpContextGroup::Operator;
					leaf.id = SexpActionId::InsertOperator;
					leaf.label = name;
					leaf.enabled = false; // start disabled
					parentMenu.children.push_back(std::move(leaf));
					opValueToLeaf[op_value] = &parentMenu.children.back();
				};

				auto isHiddenByLegacyRules = [&](int op_value) -> bool {
					switch (op_value) {
					case OP_GET_VARIABLE_BY_INDEX:
					case OP_SET_VARIABLE_BY_INDEX:
					case OP_COPY_VARIABLE_FROM_INDEX:
					case OP_COPY_VARIABLE_BETWEEN_INDEXES:
					case OP_HITS_LEFT_SUBSYSTEM:
					case OP_CUTSCENES_SHOW_SUBTITLE:
					case OP_ORDER:
					case OP_TECH_ADD_INTEL:
					case OP_TECH_REMOVE_INTEL:
					case OP_HUD_GAUGE_SET_ACTIVE:
					case OP_HUD_ACTIVATE_GAUGE_TYPE:
					case OP_JETTISON_CARGO_DELAY:
					case OP_STRING_CONCATENATE:
					case OP_SET_OBJECT_SPEED_X:
					case OP_SET_OBJECT_SPEED_Y:
					case OP_SET_OBJECT_SPEED_Z:
					case OP_DISTANCE:
					case OP_SCRIPT_EVAL:
					case OP_TRIGGER_SUBMODEL_ANIMATION:
					case OP_ADD_BACKGROUND_BITMAP:
					case OP_ADD_SUN_BITMAP:
					case OP_JUMP_NODE_SET_JUMPNODE_NAME:
					case OP_KEY_RESET:
					case OP_SET_ASTEROID_FIELD:
					case OP_SET_DEBRIS_FIELD:
					case OP_NEBULA_TOGGLE_POOF:
					case OP_NEBULA_FADE_POOF:
						return true;
					default:
						return false;
					}
				};

				// 2) Append EVERY visible operator to its category/subcategory (disabled)
				for (int i = 0; i < (int)Operators.size(); ++i) {
					const int op_value = Operators[i].value;
					if (isHiddenByLegacyRules(op_value))
						continue;

					const int sub_id = get_subcategory(op_value);
					if (sub_id == OP_SUBCATEGORY_NONE) {
						const int cat_id = get_category(op_value);
						auto it = catLabel.find(cat_id);
						if (it == catLabel.end())
							continue;
						int catIdx = ensureCategoryById(cat_id, it->second);
						appendOperatorLeaf(catRoots[catIdx], op_value, Operators[i].text);
					} else {
						const int parent_cat_id = category_of_subcategory(sub_id);
						auto itCat = catLabel.find(parent_cat_id);
						auto itSub = subLabel.find(sub_id);
						if (itCat == catLabel.end() || itSub == subLabel.end())
							continue;

						int catIdx = ensureCategoryById(parent_cat_id, itCat->second);
						int subIdx = ensureSubcategoryById(catIdx, sub_id, itSub->second);
						appendOperatorLeaf(catRoots[catIdx].children[subIdx], op_value, Operators[i].text);
					}
				}

				// 3) Enable pass: operators present in the OPF listing for this slot
				if (auto list = model->buildListingForOpf(opf, parent_index, arg_index)) {
					for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
						if (it->op >= 0) {
							const int op_value = Operators[it->op].value;
							auto f = opValueToLeaf.find(op_value);
							if (f != opValueToLeaf.end()) {
								f->second->enabled = true;
							}
						}
					}
				}

				// Final rule: disable operators that lack default arguments
				for (int i = 0; i < (int)Operators.size(); ++i) {
					auto f = opValueToLeaf.find(Operators[i].value);
					if (f != opValueToLeaf.end()) {
						if (!model->hasDefaultArgumentAvailable(i)) {
							f->second->enabled = false;
						}
					}
				}

				// Propagate enabled up so empty submenus show disabled
				std::function<bool(SexpContextAction&)> propagateEnabled = [&](SexpContextAction& node) -> bool {
					if (node.children.empty())
						return node.enabled;
					bool any = false;
					for (auto& ch : node.children)
						any = propagateEnabled(ch) || any;
					node.enabled = any;
					return node.enabled;
				};
				for (auto& cat : catRoots)
					propagateEnabled(cat);

				addOp->enabled = !addOp->children.empty();
			}
		}
	}

	// Add Data submenu stub
	if (auto* addData = getAction(SexpActionId::AddData)) {
		addData->children.clear();

		// Only meaningful on a real node
		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			// If the selected node cannot accept a child, no submenu is generated.
			const int opf = expectedOpfForAppend_(node_index);
			if (opf >= 0) {
				// Next arg index is current argc
				const int arg_index = model->countArgs(n.child);

				const bool allowNumber = model->opfAcceptsRawNumberInput(opf);
				const bool allowString = model->opfAcceptsRawStringInput(opf);

				// Pseudo-items for typed entry.
				{
					SexpContextAction numLeaf;
					numLeaf.group = SexpContextGroup::Data;
					numLeaf.id = SexpActionId::AddData; // action id placeholder; not wired yet
					numLeaf.label = "Number";
					numLeaf.enabled = allowNumber;
					addData->children.push_back(std::move(numLeaf));
				}
				{
					SexpContextAction strLeaf;
					strLeaf.group = SexpContextGroup::Data;
					strLeaf.id = SexpActionId::AddData; // action id placeholder; not wired yet
					strLeaf.label = "String";
					strLeaf.enabled = allowString;
					addData->children.push_back(std::move(strLeaf));
				}

				// Ask the model for addable choices at this slot
				if (auto list = model->buildListingForOpf(opf, node_index, arg_index)) {
					for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
						// Only list data items (ignore operator entries)
						if (it->op < 0) {
							SexpContextAction leaf;
							leaf.group = SexpContextGroup::Data;
							leaf.id = SexpActionId::AddData; // action to perform later
							leaf.label = it->text;           // shown in submenu
							leaf.enabled = true;

							// For now we do not pass the chosen string through SexpActionParam.
							// This proof wires up labels so the UI renders the nested submenu.
							// Next step: extend SexpActionParam or add a dispatch to pass the chosen text.

							addData->children.push_back(std::move(leaf));
						}
					}
				}

				// Container helpers
				if ((n.type & SEXPT_CONTAINER_DATA) != 0) {
					// Legacy FRED offered these under Add Data to help author keys/indices.
					auto addMod = [&](const char* label) {
						SexpContextAction leaf;
						leaf.group = SexpContextGroup::Operator; // these are operators
						leaf.id = SexpActionId::InsertOperator;  // Do these need a special action id?
						leaf.label = label;
						leaf.enabled = true; // keep simple; refine later if needed
						addData->children.push_back(std::move(leaf));
					};

					addMod("Get_First");
					addMod("Get_Last");
					addMod("Remove_First");
					addMod("Remove_Last");
					addMod("Get_Random");
					addMod("Remove_Random");
					addMod("At");
				}
			}
		}

		// If there are no children, keep Add Data disabled so UI can gray it out.
		addData->enabled = !addData->children.empty();
	}

	// Insert Operator submenu stub
	if (auto* a = getAction(SexpActionId::InsertOperator)) {
		a->choices.push_back({/*data_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// Replace Operator submenu stub
	if (auto* a = getAction(SexpActionId::ReplaceOperator)) {
		a->choices.push_back({/*data_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// Replace Data submenu
	if (auto* repData = getAction(SexpActionId::ReplaceData)) {
		repData->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& cur = model->_nodes[node_index];

			// Replace Data only applies to data nodes (not operators)
			if (SEXPT_TYPE(cur.type) != SEXPT_OPERATOR) {
				const int parent_index = cur.parent;
				if (SCP_vector_inbounds(model->_nodes, parent_index)) {
					const auto& p = model->_nodes[parent_index];

					// Arg index of this node within parent
					const int arg_index = model->findArgIndex(parent_index, node_index);

					// OPF of the slot we are replacing: parent operator + arg_index
					int opf = -1;
					if (SEXPT_TYPE(p.type) == SEXPT_OPERATOR) {
						const int op_index = get_operator_index(p.text.c_str()); // index into Operators[]
						if (op_index >= 0) {
							opf = query_operator_argument_type(op_index, arg_index); // pass INDEX, not value
						} else {
							// Unknown operator name; safest is to disable choices
							opf = OPF_NONE;
						}
					} else if (p.type & SEXPT_CONTAINER_DATA) {
						// For container data parent, use your same helper that Add Data uses,
						// but computed for this slot if you have it; otherwise, fall back:
						// - AT_INDEX first-modifier special (list index) -> OPF_NUMBER
						// - general multidim -> OPF_AMBIGUOUS (string or number)
						const auto* cont = get_sexp_container(p.text.c_str());
						const int first_mod = p.child;
						if (cont && cont->is_list() && arg_index == 1 && first_mod >= 0 &&
							get_list_modifier(model->_nodes[first_mod].text.c_str()) == ListModifier::AT_INDEX) {
							opf = OPF_NUMBER;
						} else {
							opf = OPF_AMBIGUOUS;
						}
					}

					if (opf >= 0) {
						bool allowNumber = model->opfAcceptsRawNumberInput(opf);
						bool allowString = model->opfAcceptsRawStringInput(opf);

						if (p.type & SEXPT_CONTAINER_DATA) {
							const auto* cont = get_sexp_container(p.text.c_str());
							// arg_index here is the child-in-parent index of the node being replaced
							if (cont) {
								if (cont->is_list()) {
									if (arg_index == 0) {
										// First parameter of a list: neither typed Number nor String
										allowNumber = false;
										allowString = false;
									} else {
										// Subsequent parameters: both allowed
										allowNumber = true;
										allowString = true;
									}
								} else if (cont->is_map()) {
									if (arg_index == 0) {
										// First parameter of a map: String only
										allowNumber = false;
										allowString = true;
									} else {
										// Subsequent parameters: both allowed
										allowNumber = true;
										allowString = true;
									}
								}
							}
						}

						// Pseudo-items first
						{
							SexpContextAction numLeaf;
							numLeaf.group = SexpContextGroup::Data;
							numLeaf.id = SexpActionId::ReplaceData;
							numLeaf.label = "Number";
							numLeaf.enabled = allowNumber;
							repData->children.push_back(std::move(numLeaf));
						}
						{
							SexpContextAction strLeaf;
							strLeaf.group = SexpContextGroup::Data;
							strLeaf.id = SexpActionId::ReplaceData;
							strLeaf.label = "String";
							strLeaf.enabled = allowString;
							repData->children.push_back(std::move(strLeaf));
						}

						// If parent is container data, inject container modifiers per legacy rules
						if (p.type & SEXPT_CONTAINER_DATA) {
							const auto* cont = get_sexp_container(p.text.c_str());
							const int first_mod = p.child;

							bool suppress_modifiers_for_at_index =
								cont && cont->is_list() && arg_index == 1 && first_mod >= 0 &&
								get_list_modifier(model->_nodes[first_mod].text.c_str()) == ListModifier::AT_INDEX;

							if (!suppress_modifiers_for_at_index) {
								auto addMod = [&](const char* label) {
									SexpContextAction leaf;
									leaf.group = SexpContextGroup::Operator; // these are operators
									leaf.id = SexpActionId::InsertOperator;  // correct tag for later
									leaf.label = label;
									leaf.enabled = true;
									repData->children.push_back(std::move(leaf));
								};

								addMod("Get_First");
								addMod("Get_Last");
								addMod("Remove_First");
								addMod("Remove_Last");
								addMod("Get_Random");
								addMod("Remove_Random");
								addMod("At");
							}
						}

						// TODO for variables we should not return the list?? Strange... but that's how FRED did it
						// OPF-driven data choices for this exact slot (exclude operators)
						if (auto list = model->buildListingForOpf(opf, parent_index, arg_index)) {
							for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
								if (it->op < 0) {
									SexpContextAction leaf;
									leaf.group = SexpContextGroup::Data;
									leaf.id = SexpActionId::ReplaceData;
									leaf.label = it->text;
									leaf.enabled = true;
									repData->children.push_back(std::move(leaf));
								}
							}
						}
					}
				}
			}
		}

		repData->enabled = !repData->children.empty();
	}

	// Replace Variable submenu stub
	if (auto* a = getAction(SexpActionId::ReplaceVariable)) {
		a->choices.push_back({/*var_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// Replace Container Name submenu stub
	if (auto* a = getAction(SexpActionId::ReplaceContainerName)) {
		a->choices.push_back({/*container_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// Replace Container Data submenu stub
	if (auto* a = getAction(SexpActionId::ReplaceContainerData)) {
		a->choices.push_back({/*container_index*/ -1, /*arg_index*/ -1});
		a->choiceText.emplace_back("Choose…");
	}

	// 3) Flip enables for Node/Structure groups now that we have final flags.
	setEnabled(SexpActionId::EditText, canEditNode(kind, node_index));
	setEnabled(SexpActionId::DeleteNode, canDeleteNode(kind, node_index));
	setEnabled(SexpActionId::Cut, canCutNode(kind, node_index));
	setEnabled(SexpActionId::Copy, canCopyNode(kind, node_index));
	setEnabled(SexpActionId::PasteOverwrite, canPasteOverrideNode(kind, node_index));
	setEnabled(SexpActionId::AddOperator, canAddOperatorNode(kind, node_index));
	setEnabled(SexpActionId::AddData, canAddDataNode(kind, node_index));
	setEnabled(SexpActionId::PasteAdd, canPasteAddNode(kind, node_index));
	setEnabled(SexpActionId::InsertOperator, canInsertOperatorNode(kind, node_index));
	setEnabled(SexpActionId::ReplaceOperator, canReplaceOperatorNode(kind, node_index));
	setEnabled(SexpActionId::ReplaceData, canReplaceDataNode(kind, node_index));
	setEnabled(SexpActionId::ReplaceVariable, canReplaceVariableNode(kind, node_index));
	setEnabled(SexpActionId::ReplaceContainerName, canReplaceContainerNameNode(kind, node_index));
	setEnabled(SexpActionId::ReplaceContainerData, canReplaceContainerDataNode(kind, node_index));

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

	case SexpActionId::ResetToDefaults:
		return resetToDefaults(node_index);

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

bool SexpActionsHandler::canInsertOperatorNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canInsertOperatorNode: bad node");
		const auto& n = model->_nodes[node_index];

		// ---- 1) Figure out the expected OPF for THIS position ----
		int expected_opf = OPF_NONE;

		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];

			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				// find arg position
				int arg_pos = 0;
				for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next)
					++arg_pos;

				// find operator INDEX and query slot OPF by index (no warning)
				int op_index = -1;
				for (int i = 0; i < (int)Operators.size(); ++i)
					if (Operators[i].text == parent.text) {
						op_index = i;
						break;
					}

				if (op_index >= 0)
					expected_opf = query_operator_argument_type(op_index, arg_pos);
			} else if (parent.type & SEXPT_CONTAINER_DATA) {
				if (const auto* cont = get_sexp_container(parent.text.c_str()))
					expected_opf = cont->opf_type;
			}
		} else {
			// Real root: choose an OPF that matches the current expression type using the matcher.
			const int eff_type = nodeEffectiveType_(node_index);
			// Try common OPFs in a stable order (same trick old code effectively used).
			static const int kCandidates[] = {OPF_BOOL, OPF_NUMBER, OPF_STRING, OPF_AMBIGUOUS};
			for (int opf : kCandidates) {
				if (sexp_query_type_match(opf, eff_type)) {
					expected_opf = opf;
					break;
				}
			}

			// legacy-friendly fallback so roots like "when" still allow Insert
			if (expected_opf == OPF_NONE) {
				expected_opf = OPF_NULL;
			}
		}

		// ---- 2) If we have a meaningful OPF, check if ANY operator fits the insert pattern ----
		if (expected_opf >= 0 && expected_opf != OPF_NONE) {
			for (int j = 0; j < (int)Operators.size(); ++j) {
				// Return type must be compatible with the slot OPF
				const int ret_type = query_operator_return_type(j); // j = operator index
				if (!sexp_query_type_match(expected_opf, ret_type))
					continue;

				// Needs at least one argument to "wrap" the current node
				if (Operators[j].min < 1)
					continue;

				// First arg OPF must match the slot OPF (with number/positive dovetail)
				int arg0 = query_operator_argument_type(j, 0);
				if (expected_opf == OPF_NUMBER && arg0 == OPF_POSITIVE)
					arg0 = OPF_NUMBER;
				if (expected_opf == OPF_POSITIVE && arg0 == OPF_NUMBER)
					arg0 = OPF_POSITIVE;

				if (arg0 == expected_opf) {
					enable = true;
					break;
				}
			}
		}
	}

	// Single env tweak point, consistent with your other helpers
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::InsertOperator, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canReplaceOperatorNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canReplaceOperatorNode: bad node");
		const auto& n = model->_nodes[node_index];

		// 1. Determine the expected OPF for the node's current position.
		int expected_opf = OPF_NONE;
		int arg_pos = 0;

		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];
			for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next) {
				arg_pos++;
			}

			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int parent_idx = -1;
				for (int i = 0; i < (int)Operators.size(); ++i) {
					if (Operators[i].text == parent.text) {
						parent_idx = i;
						break;
					}
				}
				if (parent_idx >= 0) {
					expected_opf = query_operator_argument_type(parent_idx, arg_pos);
				}
			} else if (parent.type & SEXPT_CONTAINER_DATA) {
				if (const auto* cont = get_sexp_container(parent.text.c_str())) {
					expected_opf = cont->opf_type;
				}
			}
		} else {
			// It's a root node. Determine OPF based on its own return type.
			const int eff = nodeEffectiveType_(node_index);
			static const int kCandidates[] = {OPF_BOOL, OPF_NUMBER, OPF_STRING, OPF_AMBIGUOUS};
			for (int opf : kCandidates) {
				if (sexp_query_type_match(opf, eff)) {
					expected_opf = opf;
					break;
				}
			}
			if (expected_opf == OPF_NONE)
				expected_opf = OPF_NULL;
		}

		// 2. Find if any operator is a valid choice for this slot.
		if (expected_opf != OPF_NONE) {
			SexpListItemPtr list = model->buildListingForOpf(expected_opf, n.parent, arg_pos);
			if (list) {
				if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR) {
					// Current node is an operator, so find a DIFFERENT operator in the list.
					const int current_op_val = get_operator_const(n.text.c_str());
					for (SexpListItem* item = list.get(); item != nullptr; item = item->next) {
						if (item->op >= 0 && Operators[item->op].value != current_op_val) {
							enable = true;
							break;
						}
					}
				} else {
					// Current node is data, so ANY operator in the list is a valid replacement.
					for (SexpListItem* item = list.get(); item != nullptr; item = item->next) {
						if (item->op >= 0) {
							enable = true;
							break;
						}
					}
				}
			}
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceOperator, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canReplaceDataNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canReplaceDataNode: bad node");
		const auto& n = model->_nodes[node_index];

		// "Replace Data" is not for root nodes (which must be operators).
		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];

			// 1. Determine the expected OPF for this node's slot.
			int expected_opf = OPF_NONE;
			int arg_pos = 0;
			for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next) {
				arg_pos++;
			}

			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int parent_op_idx = -1;
				for (int i = 0; i < (int)Operators.size(); ++i) {
					if (Operators[i].text == parent.text) {
						parent_op_idx = i;
						break;
					}
				}
				if (parent_op_idx >= 0) {
					expected_opf = query_operator_argument_type(parent_op_idx, arg_pos);
				}
			} else if (parent.type & SEXPT_CONTAINER_DATA) {
				if (const auto* cont = get_sexp_container(parent.text.c_str())) {
					expected_opf = cont->opf_type;
				}
			}

			// 2. Decide enablement based on the OPF type.
			switch (expected_opf) {
			// These types allow free-form user input, so replacement should always be possible.
			case OPF_NUMBER:
			case OPF_POSITIVE:
			case OPF_STRING:
			case OPF_ANYTHING:
			case OPF_AMBIGUOUS:
			case OPF_CONTAINER_VALUE:
			case OPF_DATA_OR_STR_CONTAINER:
			case OPF_MESSAGE_OR_STRING:
				enable = true;
				break;

			// No argument is expected here, so no replacement is possible.
			case OPF_NONE:
				enable = false;
				break;

			// For all other types (Variables, Containers, Ships, etc.), enable the action
			// if the choice list generated for this slot contains at least one data item.
			default: {
				SexpListItemPtr list = model->buildListingForOpf(expected_opf, n.parent, arg_pos);
				if (list) {
					for (SexpListItem* item = list.get(); item != nullptr; item = item->next) {
						if (item->op < 0) { // item->op < 0 signifies a data item.
							enable = true;
							break;
						}
					}
				}
				break;
			}
			}
		}
	}

	// Allow the environment to make the final decision.
	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceData, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canReplaceVariableNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode && model->liveCount() > 0) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canReplaceVariableNode: bad node");
		const auto& n = model->_nodes[node_index];

		// This action should only apply to data-like nodes, not operators.
		if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR) {
			if (model->environment()) {
				model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceVariable,
					kind,
					node_index,
					enable);
			}
			return enable; // Return false for operators.
		}

		int expected_opf = OPF_NONE;

		// 1. Determine the expected OPF for the node's slot.
		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];
			int arg_pos = 0;
			for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next) {
				arg_pos++;
			}

			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int parent_idx = -1;
				for (int i = 0; i < (int)Operators.size(); ++i) {
					if (Operators[i].text == parent.text) {
						parent_idx = i;
						break;
					}
				}
				if (parent_idx >= 0) {
					expected_opf = query_operator_argument_type(parent_idx, arg_pos);
				}
			} else if (parent.type & SEXPT_CONTAINER_DATA) {
				if (const auto* cont = get_sexp_container(parent.text.c_str())) {
					expected_opf = cont->opf_type;
				}
			}
		}

		// 2. Determine if the slot can accept a string or number variable.
		bool is_string_slot = false;
		bool is_number_slot = false;

		switch (expected_opf) {
		case OPF_NUMBER:
		case OPF_POSITIVE:
			is_number_slot = true;
			break;

		case OPF_BOOL:
		case OPF_NONE:
			break;

		case OPF_AMBIGUOUS:
			is_string_slot = true;
			is_number_slot = true;
			break;

		default:
			is_string_slot = true;
			break;
		}

		if (is_string_slot || is_number_slot) {
			// 3. Check if there is at least one defined variable of a matching type.
			for (int i = 0; i < MAX_SEXP_VARIABLES; i++) {
				if (Sexp_variables[i].type & SEXP_VARIABLE_SET) {
					if (is_string_slot && (Sexp_variables[i].type & SEXP_VARIABLE_STRING)) {
						enable = true;
						break;
					}
					if (is_number_slot && (Sexp_variables[i].type & SEXP_VARIABLE_NUMBER)) {
						enable = true;
						break;
					}
				}
			}
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceVariable, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canReplaceContainerNameNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode && model->liveCount() > 0) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canReplaceContainerNameNode: bad node");
		const auto& n = model->_nodes[node_index];

		// This action is only valid on a node that has a parent.
		if (n.parent >= 0) {
			const auto& parent = model->_nodes[n.parent];

			// 1. Determine the expected OPF for this node's slot.
			int expected_opf = OPF_NONE;
			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int arg_pos = 0;
				for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next) {
					arg_pos++;
				}
				int parent_idx = -1;
				for (int i = 0; i < (int)Operators.size(); ++i) {
					if (Operators[i].text == parent.text) {
						parent_idx = i;
						break;
					}
				}
				if (parent_idx >= 0) {
					expected_opf = query_operator_argument_type(parent_idx, arg_pos);
				}
			}

			// 2. Check if the OPF is for a container name.
			if (expected_opf == OPF_CONTAINER_NAME || expected_opf == OPF_LIST_CONTAINER_NAME ||
				expected_opf == OPF_MAP_CONTAINER_NAME || expected_opf == OPF_DATA_OR_STR_CONTAINER) {
				// 3. Check if there is at least one container of the correct type defined.
				const auto& all_containers = get_all_sexp_containers();
				for (const auto& container : all_containers) {
					bool match = false;
					switch (expected_opf) {
					case OPF_CONTAINER_NAME:
						match = true;
						break;
					case OPF_LIST_CONTAINER_NAME:
						match = container.is_list();
						break;
					case OPF_MAP_CONTAINER_NAME:
						match = container.is_map();
						break;
					case OPF_DATA_OR_STR_CONTAINER:
						match = container.is_of_string_type();
						break;
					}
					if (match) {
						enable = true;
						break;
					}
				}
			}
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceContainerName, kind, node_index, enable);
	}
	return enable;
}

bool SexpActionsHandler::canReplaceContainerDataNode(SexpNodeKind kind, int node_index) const
{
	bool enable = false;

	if (kind == SexpNodeKind::RealNode && model->liveCount() > 0) {
		Assertion(SCP_vector_inbounds(model->_nodes, node_index), "canReplaceContainerDataNode: bad node");
		const auto& n = model->_nodes[node_index];

		// This action should only apply to data-like nodes, not operators.
		if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR) {
			if (model->environment()) {
				model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceContainerData,
					kind,
					node_index,
					enable);
			}
			return enable; // Return false for operators.
		}

		// This action requires a parent context to determine the slot type.
		if (n.parent >= 0) {
			// 1. Determine the expected OPF for the node's slot.
			int expected_opf = OPF_NONE;
			const auto& parent = model->_nodes[n.parent];
			if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
				int arg_pos = 0;
				for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next) {
					arg_pos++;
				}
				int parent_idx = -1;
				for (int i = 0; i < (int)Operators.size(); ++i) {
					if (Operators[i].text == parent.text) {
						parent_idx = i;
						break;
					}
				}
				if (parent_idx >= 0) {
					expected_opf = query_operator_argument_type(parent_idx, arg_pos);
				}
			} else if (parent.type & SEXPT_CONTAINER_DATA) {
				if (const auto* cont = get_sexp_container(parent.text.c_str())) {
					expected_opf = cont->opf_type;
				}
			}

			// 2. Correctly determine if the slot accepts a string or number.
			bool is_string_slot = false;
			bool is_number_slot = false;

			switch (expected_opf) {
			case OPF_NUMBER:
			case OPF_POSITIVE:
				is_number_slot = true;
				break;

			case OPF_BOOL:
			case OPF_NONE:
			case OPF_VARIABLE_NAME:
			case OPF_FLEXIBLE_ARGUMENT:
			case OPF_DATA_OR_STR_CONTAINER:
				break;

			case OPF_AMBIGUOUS:
				is_string_slot = true;
				is_number_slot = true;
				break;

			default:
				is_string_slot = true;
				break;
			}

			if (is_string_slot || is_number_slot) {
				// 3. Check if there is at least one container that provides the matching data type.
				const auto& all_containers = get_all_sexp_containers();
				for (const auto& container : all_containers) {
					if (is_string_slot && any(container.type & ContainerType::STRING_DATA)) {
						enable = true;
						break;
					}
					if (is_number_slot && any(container.type & ContainerType::NUMBER_DATA)) {
						enable = true;
						break;
					}
				}
			}
		}
	}

	if (model->environment()) {
		model->environment()->overrideNodeActionEnabled(SexpActionId::ReplaceContainerData, kind, node_index, enable);
	}
	return enable;
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