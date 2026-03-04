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

	const bool campaignContext = model->_env && model->_env->isCampaignContext();

	// Helper: determine if a given Operators[op_index] should be disabled for campaign context.
	auto isCampaignFiltered = [&](int op_index) -> bool {
		return campaignContext && !usable_in_campaign(Operators[op_index].value);
	};

	// --- Add Operator submenu ---
	if (auto* addOp = getAction(SexpActionId::AddOperator)) {
		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];
			const int opf = expectedOpfForAppend_(node_index);
			if (opf >= 0) {
				const int arg_index = model->countArgs(n.child);

				// Build the set of operators enabled for this slot via OPF listing
				std::unordered_map<int, bool> opValEnabled; // op_value -> enabled
				if (auto list = model->buildListingForOpf(opf, node_index, arg_index)) {
					for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
						if (it->op >= 0)
							opValEnabled[Operators[it->op].value] = true;
					}
				}

				buildCategorizedOperatorSubmenu_(addOp, SexpActionId::AddOperator,
					[&](int op_idx) {
						const int op_value = Operators[op_idx].value;
						if (!opValEnabled.count(op_value))
							return false;
						if (!model->hasDefaultArgumentAvailable(op_idx))
							return false;
						if (isCampaignFiltered(op_idx))
							return false;
						return true;
					});
			}
		}
	}

	// --- Add Data submenu ---
	if (auto* addData = getAction(SexpActionId::AddData)) {
		addData->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];
			const int opf = expectedOpfForAppend_(node_index);
			if (opf >= 0) {
				const int arg_index = model->countArgs(n.child);
				const bool allowNumber = model->opfAcceptsRawNumberInput(opf);
				const bool allowString = model->opfAcceptsRawStringInput(opf);

				// "Number" and "String" typed-entry pseudo-items
				{
					SexpContextAction numLeaf;
					numLeaf.group = SexpContextGroup::Data;
					numLeaf.id = SexpActionId::AddData;
					numLeaf.label = "Number";
					numLeaf.param.op_index = -1;
					numLeaf.param.arg_index = 0; // sentinel: typed number
					numLeaf.enabled = allowNumber;
					addData->children.push_back(std::move(numLeaf));
				}
				{
					SexpContextAction strLeaf;
					strLeaf.group = SexpContextGroup::Data;
					strLeaf.id = SexpActionId::AddData;
					strLeaf.label = "String";
					strLeaf.param.op_index = -1;
					strLeaf.param.arg_index = 1; // sentinel: typed string
					strLeaf.enabled = allowString;
					addData->children.push_back(std::move(strLeaf));
				}

				// Named data choices from the OPF listing (data items only)
				int data_idx = 0;
				if (auto list = model->buildListingForOpf(opf, node_index, arg_index)) {
					for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
						if (it->op < 0) {
							SexpContextAction leaf;
							leaf.group = SexpContextGroup::Data;
							leaf.id = SexpActionId::AddData;
							leaf.label = it->text;
							leaf.param.op_index = data_idx; // index in OPF data list
							leaf.param.arg_index = -1;
							leaf.enabled = true;
							addData->children.push_back(std::move(leaf));
							++data_idx;
						}
					}
				}

				// For container data nodes at the first modifier position, offer list modifier choices
				if ((n.type & SEXPT_CONTAINER_DATA) != 0 && arg_index == 0) {
					const auto* cont = get_sexp_container(n.text.c_str());
					if (cont && cont->is_list()) {
						for (const auto& mod : get_all_list_modifiers()) {
							SexpContextAction leaf;
							leaf.group = SexpContextGroup::Data;
							leaf.id = SexpActionId::AddData;
							leaf.label = mod.name;
							leaf.param.op_index = -1;
							leaf.param.arg_index = -1;
							leaf.param.node_type = SEXPT_MODIFIER | SEXPT_STRING | SEXPT_VALID;
							leaf.param.text = mod.name;
							leaf.enabled = true;
							addData->children.push_back(std::move(leaf));
						}
					}
				}
			}
		}

		addData->enabled = !addData->children.empty();
	}

	// --- Insert Operator submenu ---
	if (auto* insertOp = getAction(SexpActionId::InsertOperator)) {
		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			// Compute the OPF expected at this node's slot (same logic as canInsertOperatorNode)
			int expected_opf = OPF_NONE;
			if (n.parent >= 0) {
				const auto& parent = model->_nodes[n.parent];
				if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
					const int arg_pos = argPositionOf_(n.parent, node_index);
					int op_idx = get_operator_index(parent.text.c_str());
					if (op_idx >= 0)
						expected_opf = query_operator_argument_type(op_idx, arg_pos);
				} else if (parent.type & SEXPT_CONTAINER_DATA) {
					const auto* cont = get_sexp_container(parent.text.c_str());
					if (cont)
						expected_opf = cont->opf_type;
				}
			} else {
				// Root: derive expected OPF from node's own return type
				const int eff_type = nodeEffectiveType_(node_index);
				static const int kRootCandidates[] = {OPF_BOOL, OPF_NUMBER, OPF_STRING, OPF_AMBIGUOUS};
				for (int opf : kRootCandidates) {
					if (sexp_query_type_match(opf, eff_type)) {
						expected_opf = opf;
						break;
					}
				}
				if (expected_opf == OPF_NONE)
					expected_opf = OPF_NULL;
			}

			if (expected_opf != OPF_NONE) {
				buildCategorizedOperatorSubmenu_(insertOp, SexpActionId::InsertOperator,
					[&](int op_idx) {
						// Return type must match the slot
						const int ret_type = query_operator_return_type(op_idx);
						if (!sexp_query_type_match(expected_opf, ret_type))
							return false;
						// Must accept at least 1 argument (to wrap the current node)
						if (Operators[op_idx].min < 1)
							return false;
						// First argument type must match slot (with number/positive dovetail)
						int arg0 = query_operator_argument_type(op_idx, 0);
						if (expected_opf == OPF_NUMBER && arg0 == OPF_POSITIVE)
							arg0 = OPF_NUMBER;
						if (expected_opf == OPF_POSITIVE && arg0 == OPF_NUMBER)
							arg0 = OPF_POSITIVE;
						if (arg0 != expected_opf)
							return false;
						if (isCampaignFiltered(op_idx))
							return false;
						return true;
					});
			}
		}
	}

	// --- Replace Operator submenu ---
	if (auto* repOp = getAction(SexpActionId::ReplaceOperator)) {
		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			// Compute expected OPF and arg_pos for this slot
			int expected_opf = OPF_NONE;
			if (n.parent >= 0) {
				const auto& parent = model->_nodes[n.parent];
				const int arg_pos = argPositionOf_(n.parent, node_index);
				if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
					int parent_op_idx = get_operator_index(parent.text.c_str());
					if (parent_op_idx >= 0)
						expected_opf = query_operator_argument_type(parent_op_idx, arg_pos);
				} else if (parent.type & SEXPT_CONTAINER_DATA) {
					const auto* cont = get_sexp_container(parent.text.c_str());
					if (cont)
						expected_opf = cont->opf_type;
				}
			} else {
				const int eff = nodeEffectiveType_(node_index);
				static const int kRootCandidates[] = {OPF_BOOL, OPF_NUMBER, OPF_STRING, OPF_AMBIGUOUS};
				for (int opf : kRootCandidates) {
					if (sexp_query_type_match(opf, eff)) {
						expected_opf = opf;
						break;
					}
				}
				if (expected_opf == OPF_NONE)
					expected_opf = OPF_NULL;
			}

			if (expected_opf != OPF_NONE) {
				// Build enabled set from OPF listing
				std::unordered_map<int, bool> opValEnabled;
				if (auto list = model->buildListingForOpf(expected_opf, n.parent, arg_pos)) {
					for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
						if (it->op >= 0)
							opValEnabled[Operators[it->op].value] = true;
					}
				}

				// Current operator value (excluded from the replace list)
				int current_op_value = -1;
				if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR)
					current_op_value = get_operator_const(n.text.c_str());

				buildCategorizedOperatorSubmenu_(repOp, SexpActionId::ReplaceOperator,
					[&](int op_idx) {
						const int op_value = Operators[op_idx].value;
						if (!opValEnabled.count(op_value))
							return false;
						if (!model->hasDefaultArgumentAvailable(op_idx))
							return false;
						if (isCampaignFiltered(op_idx))
							return false;
						return true;
					},
					current_op_value);
			}
		}
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
							numLeaf.param.op_index = -1;
							numLeaf.param.arg_index = 0; // sentinel: typed number
							numLeaf.enabled = allowNumber;
							repData->children.push_back(std::move(numLeaf));
						}
						{
							SexpContextAction strLeaf;
							strLeaf.group = SexpContextGroup::Data;
							strLeaf.id = SexpActionId::ReplaceData;
							strLeaf.label = "String";
							strLeaf.param.op_index = -1;
							strLeaf.param.arg_index = 1; // sentinel: typed string
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
									leaf.group = SexpContextGroup::Data;
									leaf.id = SexpActionId::ReplaceData;
									leaf.label = label;
									leaf.param.op_index = -1;
									leaf.param.arg_index = -1;
									leaf.param.node_type = SEXPT_MODIFIER | SEXPT_STRING | SEXPT_VALID;
									leaf.param.text = label;
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
							int data_idx = 0;
							for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
								if (it->op < 0) {
									SexpContextAction leaf;
									leaf.group = SexpContextGroup::Data;
									leaf.id = SexpActionId::ReplaceData;
									leaf.label = it->text;
									leaf.param.op_index = data_idx;
									leaf.param.arg_index = -1;
									leaf.enabled = true;
									repData->children.push_back(std::move(leaf));
									++data_idx;
								}
							}
						}
					}
				}
			}
		}

		repData->enabled = !repData->children.empty();
	}

	// --- Replace Variable submenu ---
	if (auto* repVar = getAction(SexpActionId::ReplaceVariable)) {
		repVar->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			// Only applies to data nodes with a parent
			if (SEXPT_TYPE(n.type) != SEXPT_OPERATOR && n.parent >= 0) {
				const auto& parent = model->_nodes[n.parent];

				// Determine expected OPF for this slot
				int expected_opf = OPF_NONE;
				const int arg_pos = argPositionOf_(n.parent, node_index);

				if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
					int op_idx = get_operator_index(parent.text.c_str());
					if (op_idx >= 0)
						expected_opf = query_operator_argument_type(op_idx, arg_pos);
				} else if (parent.type & SEXPT_CONTAINER_DATA) {
					const auto* cont = get_sexp_container(parent.text.c_str());
					if (cont)
						expected_opf = cont->opf_type;
				}

				// Enable all variables if this is a variable-name slot or nav-point slot
				const bool enableAll = (expected_opf == OPF_VARIABLE_NAME || expected_opf == OPF_NAV_POINT);
				// Enable for modifier nodes beyond the first
				const bool enableAllModifier = (n.type & SEXPT_MODIFIER) && arg_pos > 0;

				// Determine which variable types are acceptable
				bool acceptsString = enableAll || enableAllModifier;
				bool acceptsNumber = enableAll || enableAllModifier;
				switch (expected_opf) {
				case OPF_NUMBER:
				case OPF_POSITIVE:
					acceptsNumber = true;
					break;
				case OPF_BOOL:
				case OPF_NONE:
					// No variables for bool/none slots (unless overridden above)
					break;
				case OPF_AMBIGUOUS:
					acceptsString = true;
					acceptsNumber = true;
					break;
				default:
					acceptsString = true;
					break;
				}

				for (int idx = 0; idx < MAX_SEXP_VARIABLES; ++idx) {
					if (!(Sexp_variables[idx].type & SEXP_VARIABLE_SET))
						continue;
					if (Sexp_variables[idx].type & SEXP_VARIABLE_BLOCK)
						continue;

					const bool isNum = (Sexp_variables[idx].type & SEXP_VARIABLE_NUMBER) != 0;
					const bool isStr = (Sexp_variables[idx].type & SEXP_VARIABLE_STRING) != 0;
					const bool canUse = enableAll || enableAllModifier ||
						(acceptsNumber && isNum) || (acceptsString && isStr);

					SCP_string label = Sexp_variables[idx].variable_name;
					label += " (";
					label += Sexp_variables[idx].text;
					label += ")";

					SexpContextAction leaf;
					leaf.group = SexpContextGroup::Variable;
					leaf.id = SexpActionId::ReplaceVariable;
					leaf.label = std::move(label);
					leaf.param.op_index = idx; // variable index
					leaf.enabled = canUse;
					repVar->children.push_back(std::move(leaf));
				}
			}
		}

		repVar->enabled = !repVar->children.empty();
	}

	// --- Replace Container Name submenu ---
	if (auto* repCN = getAction(SexpActionId::ReplaceContainerName)) {
		repCN->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			if (SEXPT_TYPE(n.type) != SEXPT_OPERATOR && n.parent >= 0) {
				const auto& parent = model->_nodes[n.parent];

				int expected_opf = OPF_NONE;
				int arg_pos = 0;
				for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next)
					++arg_pos;

				if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
					int op_idx = get_operator_index(parent.text.c_str());
					if (op_idx >= 0)
						expected_opf = query_operator_argument_type(op_idx, arg_pos);
				}

				// Only relevant for container-name slot types
				const bool isContainerNameSlot =
					(expected_opf == OPF_CONTAINER_NAME ||
					 expected_opf == OPF_LIST_CONTAINER_NAME ||
					 expected_opf == OPF_MAP_CONTAINER_NAME ||
					 expected_opf == OPF_DATA_OR_STR_CONTAINER);

				if (isContainerNameSlot) {
					const auto& containers = get_all_sexp_containers();
					for (int idx = 0; idx < static_cast<int>(containers.size()); ++idx) {
						const auto& container = containers[idx];

						bool canUse = false;
						switch (expected_opf) {
						case OPF_CONTAINER_NAME:
							canUse = true;
							break;
						case OPF_LIST_CONTAINER_NAME:
							canUse = container.is_list();
							break;
						case OPF_MAP_CONTAINER_NAME:
							canUse = container.is_map();
							break;
						case OPF_DATA_OR_STR_CONTAINER:
							canUse = container.is_of_string_type();
							break;
						default:
							break;
						}

						SexpContextAction leaf;
						leaf.group = SexpContextGroup::Container;
						leaf.id = SexpActionId::ReplaceContainerName;
						leaf.label = container.container_name;
						leaf.param.op_index = idx; // container index
						leaf.enabled = canUse;
						repCN->children.push_back(std::move(leaf));
					}
				}
			}
		}

		repCN->enabled = !repCN->children.empty();
	}

	// --- Replace Container Data submenu ---
	if (auto* repCD = getAction(SexpActionId::ReplaceContainerData)) {
		repCD->children.clear();

		if (kind == SexpNodeKind::RealNode && SCP_vector_inbounds(model->_nodes, node_index)) {
			const auto& n = model->_nodes[node_index];

			if (SEXPT_TYPE(n.type) != SEXPT_OPERATOR && n.parent >= 0) {
				const auto& parent = model->_nodes[n.parent];

				int expected_opf = OPF_NONE;
				int arg_pos = 0;
				for (int c = parent.child; c >= 0 && c != node_index; c = model->_nodes[c].next)
					++arg_pos;

				if (SEXPT_TYPE(parent.type) == SEXPT_OPERATOR) {
					int op_idx = get_operator_index(parent.text.c_str());
					if (op_idx >= 0)
						expected_opf = query_operator_argument_type(op_idx, arg_pos);
				} else if (parent.type & SEXPT_CONTAINER_DATA) {
					const auto* cont = get_sexp_container(parent.text.c_str());
					if (cont)
						expected_opf = cont->opf_type;
				}

				// Container data not applicable to variable-name slots
				if (expected_opf != OPF_VARIABLE_NAME && expected_opf != OPF_NONE) {
					const bool isModifier = (n.type & SEXPT_MODIFIER) != 0;
					// For modifier nodes beyond the first, all containers are valid
					const bool enableAll = isModifier && arg_pos > 0;

					// Determine which container data types are acceptable
					bool acceptsString = enableAll;
					bool acceptsNumber = enableAll;
					switch (expected_opf) {
					case OPF_NUMBER:
					case OPF_POSITIVE:
						acceptsNumber = true;
						break;
					case OPF_BOOL:
					case OPF_FLEXIBLE_ARGUMENT:
					case OPF_DATA_OR_STR_CONTAINER:
						// Not applicable
						break;
					case OPF_AMBIGUOUS:
						acceptsString = true;
						acceptsNumber = true;
						break;
					default:
						acceptsString = true;
						break;
					}

					if (acceptsString || acceptsNumber) {
						const auto& containers = get_all_sexp_containers();
						for (int idx = 0; idx < static_cast<int>(containers.size()); ++idx) {
							const auto& container = containers[idx];

							const bool canUse = enableAll ||
								(acceptsString && any(container.type & ContainerType::STRING_DATA)) ||
								(acceptsNumber && any(container.type & ContainerType::NUMBER_DATA));

							SexpContextAction leaf;
							leaf.group = SexpContextGroup::Container;
							leaf.id = SexpActionId::ReplaceContainerData;
							leaf.label = container.container_name;
							leaf.param.op_index = idx; // container index
							leaf.enabled = canUse;
							repCD->children.push_back(std::move(leaf));
						}
					}
				}
			}
		}

		repCD->enabled = !repCD->children.empty();
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
		return editText(node_index, p ? p->text.c_str() : "");

	case SexpActionId::DeleteNode:
		return deleteNode(node_index);

	case SexpActionId::Cut:
		return cutNode(node_index);

	case SexpActionId::Copy:
		return copyNode(node_index);

	case SexpActionId::PasteOverwrite:
		return pasteOverwrite(node_index);

	case SexpActionId::PasteAdd:
		return pasteAdd(node_index);

	// Operator / Data
	case SexpActionId::AddOperator:
		return addOperator(node_index, p);

	case SexpActionId::AddData:
		return addData(node_index, p);

	case SexpActionId::InsertOperator:
		return insertOperator(node_index, p);

	case SexpActionId::ReplaceOperator:
		return replaceOperator(node_index, p);

	case SexpActionId::ReplaceData:
		return replaceData(node_index, p);

	case SexpActionId::ReplaceVariable:
		return replaceVariable(node_index, p);

	case SexpActionId::ReplaceContainerName:
		return replaceContainerName(node_index, p);

	case SexpActionId::ReplaceContainerData:
		return replaceContainerData(node_index, p);

	// Structure
	case SexpActionId::MoveUp:
		return moveUp(node_index);

	case SexpActionId::MoveDown:
		return moveDown(node_index);

	case SexpActionId::ResetToDefaults:
		return resetToDefaults(node_index);

	default:
		return false;
	}
}

// --- Static/shared helpers ---

bool SexpActionsHandler::isHiddenByLegacyRules_(int op_value) noexcept
{
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
}

void SexpActionsHandler::buildCategorizedOperatorSubmenu_(SexpContextAction* parent_action,
	SexpActionId leaf_id,
	std::function<bool(int)> is_enabled_fn,
	int exclude_op_value) const
{
	parent_action->children.clear();

	// Build label lookups
	std::unordered_map<int, SCP_string> catLabel, subLabel;
	catLabel.reserve(op_menu.size());
	for (const auto& c : op_menu)
		catLabel[c.id] = c.name;
	subLabel.reserve(op_submenu.size());
	for (const auto& sc : op_submenu)
		subLabel[sc.id] = sc.name;

	auto& catRoots = parent_action->children;
	std::unordered_map<int, int> catIndexById;
	std::unordered_map<int, std::unordered_map<int, int>> subIndexByCat;

	auto ensureCategory = [&](int cat_id) -> int {
		auto it = catIndexById.find(cat_id);
		if (it != catIndexById.end())
			return it->second;
		SexpContextAction cat;
		cat.group = SexpContextGroup::Operator;
		cat.id = SexpActionId::None;
		{ auto it2 = catLabel.find(cat_id); cat.label = (it2 != catLabel.end()) ? it2->second : "Unknown"; }
		cat.enabled = false; // propagated later
		catRoots.push_back(std::move(cat));
		int idx = static_cast<int>(catRoots.size()) - 1;
		catIndexById[cat_id] = idx;
		return idx;
	};

	auto ensureSubcategory = [&](int catIdx, int sub_id) -> int {
		auto& subMap = subIndexByCat[catIdx];
		auto it = subMap.find(sub_id);
		if (it != subMap.end())
			return it->second;
		SexpContextAction sub;
		sub.group = SexpContextGroup::Operator;
		sub.id = SexpActionId::None;
		{ auto it2 = subLabel.find(sub_id); sub.label = (it2 != subLabel.end()) ? it2->second : "Unknown"; }
		sub.enabled = false;
		catRoots[catIdx].children.push_back(std::move(sub));
		int idx = static_cast<int>(catRoots[catIdx].children.size()) - 1;
		subMap[sub_id] = idx;
		return idx;
	};

	// Build all category/subcategory headers
	for (const auto& c : op_menu)
		ensureCategory(c.id);
	for (const auto& sc : op_submenu) {
		int parent_cat_id = category_of_subcategory(sc.id);
		auto it = catLabel.find(parent_cat_id);
		if (it == catLabel.end())
			continue;
		int catIdx = ensureCategory(parent_cat_id);
		ensureSubcategory(catIdx, sc.id);
	}

	// Add each visible operator as a leaf
	for (int i = 0; i < static_cast<int>(Operators.size()); ++i) {
		const int op_value = Operators[i].value;
		if (isHiddenByLegacyRules_(op_value))
			continue;
		if (exclude_op_value >= 0 && op_value == exclude_op_value)
			continue;

		SexpContextAction leaf;
		leaf.group = SexpContextGroup::Operator;
		leaf.id = leaf_id;
		leaf.label = Operators[i].text;
		leaf.param.op_index = i;
		leaf.enabled = is_enabled_fn(i);

		const int sub_id = get_subcategory(op_value);
		if (sub_id == OP_SUBCATEGORY_NONE) {
			const int cat_id = get_category(op_value);
			auto it = catLabel.find(cat_id);
			if (it == catLabel.end())
				continue;
			int catIdx = ensureCategory(cat_id);
			catRoots[catIdx].children.push_back(std::move(leaf));
		} else {
			const int parent_cat_id = category_of_subcategory(sub_id);
			auto itCat = catLabel.find(parent_cat_id);
			if (itCat == catLabel.end())
				continue;
			int catIdx = ensureCategory(parent_cat_id);
			int subIdx = ensureSubcategory(catIdx, sub_id);
			catRoots[catIdx].children[subIdx].children.push_back(std::move(leaf));
		}
	}

	// Propagate enabled upward: a parent is enabled iff any child is enabled
	std::function<bool(SexpContextAction&)> propagateEnabled = [&](SexpContextAction& a) -> bool {
		if (a.children.empty())
			return a.enabled;
		bool any = false;
		for (auto& ch : a.children)
			any = propagateEnabled(ch) || any;
		a.enabled = any;
		return a.enabled;
	};
	for (auto& cat : catRoots)
		propagateEnabled(cat);

	parent_action->enabled = !catRoots.empty() &&
		std::any_of(catRoots.begin(), catRoots.end(), [](const SexpContextAction& c) { return c.enabled; });
}

int SexpActionsHandler::restoreClipboardSubtree_(int clip_idx, int model_parent_idx) const
{
	const auto& cb_nodes = model->_clipboard;
	if (clip_idx < 0 || clip_idx >= static_cast<int>(cb_nodes.size()))
		return -1;

	int first = -1;
	int prev_sibling = -1;

	// Walk the sibling chain starting at clip_idx
	for (int c = clip_idx; c >= 0; c = cb_nodes[c].next) {
		const auto& cn = cb_nodes[c];
		// Allocate new model node as child of model_parent_idx, after prev_sibling
		int new_node = model->allocateNode(model_parent_idx, prev_sibling);
		model->setNode(new_node, cn.type, cn.text.c_str());
		model->applyDefaultFlags(new_node);
		// Restore children recursively
		if (cn.child >= 0)
			restoreClipboardSubtree_(cn.child, new_node);
		if (first < 0)
			first = new_node;
		prev_sibling = new_node;
	}
	return first;
}

int SexpActionsHandler::findPreviousSibling_(int parent_idx, int node_index) const noexcept
{
	const auto& nodes = model->_nodes;
	int prev = -1;
	for (int c = nodes[parent_idx].child; c >= 0 && c != node_index; c = nodes[c].next)
		prev = c;
	return prev;
}

int SexpActionsHandler::argPositionOf_(int parent_idx, int node_index) const noexcept
{
	const auto& nodes = model->_nodes;
	int pos = 0;
	for (int c = nodes[parent_idx].child; c >= 0 && c != node_index; c = nodes[c].next)
		++pos;
	return pos;
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
				// Determine container kind (list vs map) from the parent�s text
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
	auto& nodes = model->_nodes;
	if (!SCP_vector_inbounds(nodes, node_index))
		return false;
	if ((nodes[node_index].flags & EDITABLE) == 0)
		return false;
	nodes[node_index].text = new_text;
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
// --- Clipboard actions ---

bool SexpActionsHandler::cutNode(int node_index)
{
	if (!model->captureClipboardFromNode(node_index))
		return false;
	return deleteNode(node_index);
}

bool SexpActionsHandler::copyNode(int node_index)
{
	return model->captureClipboardFromNode(node_index);
}

bool SexpActionsHandler::pasteOverwrite(int node_index)
{
	if (!model->hasClipboard())
		return false;
	const auto& cb = model->_clipboard;
	if (cb.empty())
		return false;

	// Free current children of this node
	const int old_child = model->_nodes[node_index].child;
	if (old_child >= 0) {
		model->_nodes[node_index].child = -1;
		SexpTreeModel::freeNodeChain(model->_nodes, old_child);
	}

	// Replace node content with clipboard root
	model->setNode(node_index, cb[0].type, cb[0].text.c_str());
	model->applyDefaultFlags(node_index);

	// Restore clipboard children under node_index
	if (cb[0].child >= 0)
		restoreClipboardSubtree_(cb[0].child, node_index);

	return true;
}

bool SexpActionsHandler::pasteAdd(int node_index)
{
	if (!model->hasClipboard())
		return false;
	const auto& cb = model->_clipboard;
	if (cb.empty())
		return false;

	// Add clipboard root as a new child of node_index
	const int new_node = model->allocateNode(node_index);
	model->setNode(new_node, cb[0].type, cb[0].text.c_str());
	model->applyDefaultFlags(new_node);

	// Restore clipboard children under new_node
	if (cb[0].child >= 0)
		restoreClipboardSubtree_(cb[0].child, new_node);

	return true;
}

// --- Operator / data add actions ---

bool SexpActionsHandler::addOperator(int node_index, const SexpActionParam* p)
{
	if (!p || p->op_index < 0 || p->op_index >= static_cast<int>(Operators.size()))
		return false;
	const int new_node = model->makeOperatorNode(p->op_index, node_index);
	if (new_node < 0)
		return false;
	model->ensureOperatorArity(new_node, p->op_index);
	return true;
}

bool SexpActionsHandler::addData(int node_index, const SexpActionParam* p)
{
	if (!p)
		return false;

	int new_type = 0;
	SCP_string item_text;
	bool editable = false;

	if (p->arg_index == 0) {
		// Typed number: create editable "number" placeholder
		new_type = SEXPT_NUMBER | SEXPT_VALID;
		item_text = "number";
		editable = true;
	} else if (p->arg_index == 1) {
		// Typed string: create editable "string" placeholder
		new_type = SEXPT_STRING | SEXPT_VALID;
		item_text = "string";
		editable = true;
	} else if (p->op_index >= 0) {
		// Named data item: re-query OPF listing for the next slot
		const int opf = expectedOpfForAppend_(node_index);
		if (opf < 0)
			return false;
		const int arg_count = model->countArgs(model->_nodes[node_index].child);
		auto list = model->buildListingForOpf(opf, node_index, arg_count);
		if (!list)
			return false;
		int idx = p->op_index;
		bool found = false;
		for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
			if (it->op < 0) {
				if (idx == 0) {
					new_type = it->type;
					item_text = it->text;
					found = true;
					break;
				}
				--idx;
			}
		}
		if (!found)
			return false;
	} else if (p->node_type != 0 && !p->text.empty()) {
		// Direct text with explicit type (e.g., container modifier)
		new_type = p->node_type;
		item_text = p->text;
	} else {
		return false;
	}

	const int new_node = model->allocateNode(node_index);
	model->setNode(new_node, new_type, item_text.c_str());
	model->applyDefaultFlags(new_node);
	if (editable)
		model->setEditable(new_node, true);
	return true;
}

bool SexpActionsHandler::insertOperator(int node_index, const SexpActionParam* p)
{
	if (!p || p->op_index < 0 || p->op_index >= static_cast<int>(Operators.size()))
		return false;

	auto& nodes = model->_nodes;
	const int parent = nodes[node_index].parent;
	const int after  = nodes[node_index].next;
	const int prev   = (parent >= 0) ? findPreviousSibling_(parent, node_index) : -1;

	// Detach node_index from its current position in the parent's child list
	if (parent >= 0) {
		if (prev < 0)
			nodes[parent].child = after;
		else
			nodes[prev].next = after;
	}
	nodes[node_index].next   = -1;
	nodes[node_index].parent = -1; // temporarily detached

	// Create the new wrapping operator (detached)
	const int new_op = model->allocateNode(-1, -1);
	model->setNode(new_op, SEXPT_OPERATOR | SEXPT_VALID, Operators[p->op_index].text.c_str());
	model->applyDefaultFlags(new_op);

	// Make node_index the first child of new_op
	nodes[new_op].child      = node_index;
	nodes[node_index].parent = new_op;

	// Splice new_op into the parent at node_index's old position
	nodes[new_op].parent = parent;
	nodes[new_op].next   = after;
	if (parent >= 0) {
		if (prev < 0)
			nodes[parent].child = new_op;
		else
			nodes[prev].next = new_op;
	}

	// Fill remaining required args (positions 1..min-1 already counted node_index at 0)
	model->ensureOperatorArity(new_op, p->op_index);

	return true;
}

// --- Data replacement actions ---

bool SexpActionsHandler::replaceData(int node_index, const SexpActionParam* p)
{
	if (!p)
		return false;

	int new_type = 0;
	SCP_string item_text;
	bool editable = false;

	if (p->arg_index == 0) {
		// Typed number: editable placeholder, preserve MODIFIER flag if set
		new_type = SEXPT_NUMBER | SEXPT_VALID;
		if (model->_nodes[node_index].type & SEXPT_MODIFIER)
			new_type |= SEXPT_MODIFIER;
		item_text = "number";
		editable  = true;
	} else if (p->arg_index == 1) {
		// Typed string: editable placeholder, preserve MODIFIER flag if set
		new_type = SEXPT_STRING | SEXPT_VALID;
		if (model->_nodes[node_index].type & SEXPT_MODIFIER)
			new_type |= SEXPT_MODIFIER;
		item_text = "string";
		editable  = true;
	} else if (p->op_index >= 0) {
		// Named data item: re-query OPF listing at the same slot
		const int parent_index = model->_nodes[node_index].parent;
		if (parent_index < 0)
			return false;
		const int arg_index = model->findArgIndex(parent_index, node_index);

		// Determine the OPF for this slot
		int opf = -1;
		const auto& par = model->_nodes[parent_index];
		if (SEXPT_TYPE(par.type) == SEXPT_OPERATOR) {
			const int op_idx = get_operator_index(par.text.c_str());
			if (op_idx >= 0)
				opf = query_operator_argument_type(op_idx, arg_index);
		} else if (par.type & SEXPT_CONTAINER_DATA) {
			const auto* cont = get_sexp_container(par.text.c_str());
			if (cont)
				opf = cont->opf_type;
		}
		if (opf < 0)
			return false;

		auto list = model->buildListingForOpf(opf, parent_index, arg_index);
		if (!list)
			return false;

		int idx   = p->op_index;
		bool found = false;
		for (SexpListItem* it = list.get(); it != nullptr; it = it->next) {
			if (it->op < 0) {
				if (idx == 0) {
					new_type  = it->type;
					item_text = it->text;
					found     = true;
					break;
				}
				--idx;
			}
		}
		if (!found)
			return false;
	} else if (p->node_type != 0 && !p->text.empty()) {
		// Direct replacement with explicit type (e.g., container modifier)
		new_type  = p->node_type;
		item_text = p->text;
	} else {
		return false;
	}

	// Free current children of this node
	const int old_child = model->_nodes[node_index].child;
	if (old_child >= 0) {
		model->_nodes[node_index].child = -1;
		SexpTreeModel::freeNodeChain(model->_nodes, old_child);
	}

	model->setNode(node_index, new_type, item_text.c_str());
	model->applyDefaultFlags(node_index);
	if (editable)
		model->setEditable(node_index, true);
	return true;
}

bool SexpActionsHandler::replaceVariable(int node_index, const SexpActionParam* p)
{
	if (!p || p->op_index < 0 || p->op_index >= MAX_SEXP_VARIABLES)
		return false;
	if (!(Sexp_variables[p->op_index].type & SEXP_VARIABLE_SET))
		return false;

	const int var_idx = p->op_index;

	// Determine base SEXPT type from variable type
	int base_type = 0;
	if (Sexp_variables[var_idx].type & SEXP_VARIABLE_NUMBER)
		base_type = SEXPT_NUMBER;
	else if (Sexp_variables[var_idx].type & SEXP_VARIABLE_STRING)
		base_type = SEXPT_STRING;
	else
		return false;

	const int new_type = SEXPT_VALID | SEXPT_VARIABLE | base_type;

	// Build combined text: "varname(value)"
	SCP_string new_text = Sexp_variables[var_idx].variable_name;
	new_text += "(";
	new_text += Sexp_variables[var_idx].text;
	new_text += ")";

	// Free current children
	const int old_child = model->_nodes[node_index].child;
	if (old_child >= 0) {
		model->_nodes[node_index].child = -1;
		SexpTreeModel::freeNodeChain(model->_nodes, old_child);
	}

	model->setNode(node_index, new_type, new_text.c_str());
	model->applyDefaultFlags(node_index);
	model->setEditable(node_index, false);
	return true;
}

bool SexpActionsHandler::replaceContainerName(int node_index, const SexpActionParam* p)
{
	if (!p || p->op_index < 0)
		return false;

	const auto& containers = get_all_sexp_containers();
	if (p->op_index >= static_cast<int>(containers.size()))
		return false;

	const auto& container = containers[p->op_index];
	const int new_type = SEXPT_VALID | SEXPT_STRING | SEXPT_CONTAINER_NAME;

	// Free current children
	const int old_child = model->_nodes[node_index].child;
	if (old_child >= 0) {
		model->_nodes[node_index].child = -1;
		SexpTreeModel::freeNodeChain(model->_nodes, old_child);
	}

	model->setNode(node_index, new_type, container.container_name.c_str());
	model->applyDefaultFlags(node_index);
	model->setEditable(node_index, false);
	return true;
}

bool SexpActionsHandler::replaceContainerData(int node_index, const SexpActionParam* p)
{
	if (!p || p->op_index < 0)
		return false;

	const auto& containers = get_all_sexp_containers();
	if (p->op_index >= static_cast<int>(containers.size()))
		return false;

	const auto& container = containers[p->op_index];

	// Preserve existing string/number base type if possible, otherwise derive from container
	int base_type = model->_nodes[node_index].type & (SEXPT_NUMBER | SEXPT_STRING);
	if (!base_type) {
		if (any(container.type & ContainerType::STRING_DATA))
			base_type = SEXPT_STRING;
		else if (any(container.type & ContainerType::NUMBER_DATA))
			base_type = SEXPT_NUMBER;
		else
			base_type = SEXPT_STRING; // fallback
	}

	const int new_type = SEXPT_VALID | SEXPT_CONTAINER_DATA | base_type;

	// Free current children
	const int old_child = model->_nodes[node_index].child;
	if (old_child >= 0) {
		model->_nodes[node_index].child = -1;
		SexpTreeModel::freeNodeChain(model->_nodes, old_child);
	}

	model->setNode(node_index, new_type, container.container_name.c_str());
	model->applyDefaultFlags(node_index);
	model->setEditable(node_index, false);

	// Add default modifier child
	int mod_type = SEXPT_VALID | SEXPT_MODIFIER;
	SCP_string mod_text;
	if (container.is_map()) {
		if (any(container.type & ContainerType::STRING_KEYS)) {
			mod_type |= SEXPT_STRING;
			mod_text = SEXP_NONE_STRING;
		} else if (any(container.type & ContainerType::NUMBER_KEYS)) {
			mod_type |= SEXPT_NUMBER;
			mod_text = "0";
		}
	} else if (container.is_list()) {
		mod_type |= SEXPT_STRING;
		mod_text = get_list_modifier_name(ListModifier::GET_FIRST);
	}
	if (!mod_text.empty()) {
		const int mod_node = model->allocateNode(node_index);
		model->setNode(mod_node, mod_type, mod_text.c_str());
		model->applyDefaultFlags(mod_node);
	}

	return true;
}
