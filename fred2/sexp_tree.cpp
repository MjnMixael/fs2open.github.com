/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/



#include "stdafx.h"
#include "Sexp_tree.h"
#include "FRED.h"
#include "FREDDoc.h"
#include "Management.h"
#include "parse/sexp.h"
#include "parse/sexp/sexp_lookup.h"
#include "OperatorArgTypeSelect.h"
#include "globalincs/linklist.h"
#include "EventEditor.h"
#include "MissionGoalsDlg.h"
#include "MissionCutscenesDlg.h"
#include "ai/aigoals.h"
#include "ai/ailua.h"
#include "mission/missionmessage.h"
#include "mission/missioncampaign.h"
#include "mission/missionparse.h"
#include "missioneditor/common.h"
#include "CampaignEditorDlg.h"
#include "hud/hudsquadmsg.h"
#include "IgnoreOrdersDlg.h"
#include "stats/medals.h"
#include "controlconfig/controlsconfig.h"
#include "hud/hudgauges.h"
#include "starfield/starfield.h"
#include "nebula/neb.h"
#include "nebula/neblightning.h"
#include "jumpnode/jumpnode.h"
#include "AddVariableDlg.h"
#include "ModifyVariableDlg.h"
#include "gamesnd/eventmusic.h"	// for change-soundtrack
#include "menuui/techmenu.h"	// for intel stuff
#include "weapon/emp.h"
#include "gamesnd/gamesnd.h"
#include "weapon/weapon.h"
#include "hud/hudartillery.h"
#include "iff_defs/iff_defs.h"
#include "mission/missionmessage.h"
#include "sound/ds.h"
#include "globalincs/alphacolors.h"
#include "localization/localize.h"
#include "AddModifyContainerDlg.h"
#include "asteroid/asteroid.h"

#include "missioneditor/sexp_opf_core.h"

#define ID_SEXP_ACTION_BASE 0xE100

#define TREE_NODE_INCREMENT	100

#define MAX_OP_MENUS	30
#define MAX_SUBMENUS	(MAX_OP_MENUS * MAX_OP_MENUS)

#define ID_CONTAINER_NAME_MENU	0xd600
#define ID_CONTAINER_DATA_MENU	0xd800
#define ID_VARIABLE_MENU	0xda00
#define ID_ADD_MENU			0xdc00
#define ID_REPLACE_MENU		0xde00
// note: stay below 0xe000 so we don't collide with MFC defines..

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//********************sexp_tree********************

BEGIN_MESSAGE_MAP(sexp_tree, CTreeCtrl)
	//{{AFX_MSG_MAP(sexp_tree)
	ON_NOTIFY_REFLECT(TVN_BEGINDRAG, OnBegindrag)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_CHAR()
	ON_NOTIFY_REFLECT(TVN_KEYDOWN, OnKeyDown)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

static int Add_count, Replace_count;
static int Modify_variable;

// constructor
sexp_tree::sexp_tree()
	: m_operator_box(help)
{
	m_model = std::make_unique<SexpTreeModel>();
	select_sexp_node = -1;
	root_item = -1;
	m_mode = 0;
	m_dragging = FALSE;
	m_p_image_list = NULL;
	help_box = NULL;
	clear_tree();

	m_operator_popup_active = false;
	m_operator_popup_created = false;
	m_font_height = 0;
	m_font_max_width = 0;
}

sexp_tree::~sexp_tree() = default;

// clears out the tree, so all the nodes are unused.
void sexp_tree::clear_tree(const char *op)
{
	mprintf(("Resetting dynamic tree node limit from to %d...\n", 0));

	m_model->clear();

	if (op) {
		DeleteAllItems();
		if (strlen(op)) {
			m_model->setNode(m_model->allocateNode(-1), (SEXPT_OPERATOR | SEXPT_VALID), op);
			build_tree();
		}
	}
}

// initializes and creates a tree from a given sexp startpoint.
void sexp_tree::load_tree(int index, const char *deflt)
{
	int cur;

	clear_tree();
	root_item = 0;
	if (index < 0) {
		cur = m_model->allocateNode(-1);
		m_model->setNode(cur, (SEXPT_OPERATOR | SEXPT_VALID), deflt);  // setup a default tree if none
		build_tree();
		return;
	}

	if (Sexp_nodes[index].subtype == SEXP_ATOM_NUMBER) {  // handle numbers allender likes to use so much..
		cur = m_model->allocateNode(-1);
		if (atoi(Sexp_nodes[index].text))
			m_model->setNode(cur, (SEXPT_OPERATOR | SEXPT_VALID), "true");
		else
			m_model->setNode(cur, (SEXPT_OPERATOR | SEXPT_VALID), "false");

		build_tree();
		return;
	}

	// assumption: first token is an operator.  I require this because it would cause problems
	// with child/parent relations otherwise, and it should be this way anyway, since the
	// return type of the whole sexp is boolean, and only operators can satisfy this.
	Assert(Sexp_nodes[index].subtype == SEXP_ATOM_OPERATOR);
	load_branch(index, -1);

	print_model_to_debug_output();

	build_tree();
}

void get_combined_variable_name(char *combined_name, const char *sexp_var_name)
{
	int sexp_var_index = get_index_sexp_variable_name(sexp_var_name);

	if (sexp_var_index >= 0)
		sprintf(combined_name, "%s(%s)", Sexp_variables[sexp_var_index].variable_name, Sexp_variables[sexp_var_index].text);
	else
		sprintf(combined_name, "%s(undefined)", sexp_var_name);
}

// creates a tree from a given Sexp_nodes[] point under a given parent.  Recursive.
// Returns the allocated current node.
int sexp_tree::load_branch(int index, int parent)
{
	int cur = -1;
	char combined_var_name[2 * TOKEN_LENGTH + 2];

	while (index != -1) {
		int additional_flags = SEXPT_VALID;

		Assert(Sexp_nodes[index].type != SEXP_NOT_USED);

		if (Sexp_nodes[index].subtype == SEXP_ATOM_LIST) {
			// Flatten list: load its contents under the same parent
			load_branch(Sexp_nodes[index].first, parent);

		} else if (Sexp_nodes[index].subtype == SEXP_ATOM_OPERATOR) {
			// Create the operator node under 'parent'
			cur = m_model->allocateNode(parent);
			if ((index == select_sexp_node) && !flag) {
				select_sexp_node = cur;
				flag = 1;
			}
			m_model->setNode(cur, (SEXPT_OPERATOR | additional_flags), Sexp_nodes[index].text);

			// Recurse into the operator's argument chain, parented to this operator
			load_branch(Sexp_nodes[index].rest, cur);

			// IMPORTANT: Stop at the end of this operator expression
			return cur; // <-- this prevents re-walking args as siblings

		} else if (Sexp_nodes[index].subtype == SEXP_ATOM_NUMBER) {
			cur = m_model->allocateNode(parent);
			if (Sexp_nodes[index].type & SEXP_FLAG_VARIABLE) {
				get_combined_variable_name(combined_var_name, Sexp_nodes[index].text);
				m_model->setNode(cur, (SEXPT_VARIABLE | SEXPT_NUMBER | additional_flags), combined_var_name);
			} else {
				m_model->setNode(cur, (SEXPT_NUMBER | additional_flags), Sexp_nodes[index].text);
			}

		} else if (Sexp_nodes[index].subtype == SEXP_ATOM_STRING) {
			cur = m_model->allocateNode(parent);
			if (Sexp_nodes[index].type & SEXP_FLAG_VARIABLE) {
				get_combined_variable_name(combined_var_name, Sexp_nodes[index].text);
				m_model->setNode(cur, (SEXPT_VARIABLE | SEXPT_STRING | additional_flags), combined_var_name);
			} else {
				m_model->setNode(cur, (SEXPT_STRING | additional_flags), Sexp_nodes[index].text);
			}

		} else if (Sexp_nodes[index].subtype == SEXP_ATOM_CONTAINER_NAME) {
			// Container identifier (by name). This is a string atom, not a modifier.
			Assertion(!(additional_flags & SEXPT_MODIFIER), "Found a container name node %s that is also a container modifier. Please report!", Sexp_nodes[index].text);
			Assertion(get_sexp_container(Sexp_nodes[index].text) != nullptr, "Attempt to load unknown container data %s into SEXP tree. Please report!", Sexp_nodes[index].text);

			cur = m_model->allocateNode(parent);
			m_model->setNode(cur, (SEXPT_CONTAINER_NAME | SEXPT_STRING | additional_flags), Sexp_nodes[index].text);

		} else if (Sexp_nodes[index].subtype == SEXP_ATOM_CONTAINER_DATA) {
			// A concrete container instance; its contents become children.
			Assertion(get_sexp_container(Sexp_nodes[index].text) != nullptr, "Attempt to load unknown container %s into SEXP tree. Please report!", Sexp_nodes[index].text);

			cur = m_model->allocateNode(parent);
			m_model->setNode(cur, (SEXPT_CONTAINER_DATA | SEXPT_STRING | additional_flags), Sexp_nodes[index].text);

			// The container's contents are a list under 'first' so load as children
			load_branch(Sexp_nodes[index].first, cur);

		} else {
			// Handle any other atom kinds you support, unchanged from your current code…
			Assertion(false, "Unhandled SEXP subtype %d in load_branch! Please report!", Sexp_nodes[index].subtype);
		}

		if ((index == select_sexp_node) && !flag) {
			select_sexp_node = cur;
			flag = 1;
		}

		// Advance to the next sibling in the current list
		index = Sexp_nodes[index].rest;
	}

	return cur; // last node created in this sibling chain
}

int sexp_tree::query_false(int node)
{
	if (node < 0)
		node = root_item;

	const auto& n = m_model->node(node);

	Assert(n.type == (SEXPT_OPERATOR | SEXPT_VALID));
	Assert(n.next == -1);  // must make this assumption or else it will confuse code!
	if (get_operator_const(n.text.c_str()) == OP_FALSE){
		return TRUE;
	}

	return FALSE;
}

// builds an sexp of the tree and returns the index of it.  This allocates sexp nodes.
int sexp_tree::save_tree(int node)
{
	if (node < 0)
		node = root_item;

	const auto& n = m_model->node(node);

	Assert(n.type == (SEXPT_OPERATOR | SEXPT_VALID));
	Assert(n.next == -1);  // must make this assumption or else it will confuse code!
	return save_branch(node);
}

// get variable name from sexp_tree node .text
void var_name_from_sexp_tree_text(char *var_name, const char *text)
{
	auto var_name_length = strcspn(text, "(");
	Assert(var_name_length < TOKEN_LENGTH - 1);

	strncpy(var_name, text, var_name_length);
	var_name[var_name_length] = '\0';
}

#define NO_PREVIOUS_NODE -9
// called recursively to save a tree branch and everything under it
// SEXPT_CONTAINER_NAME and SEXPT_MODIFIER require no special handling here
int sexp_tree::save_branch(int cur, int at_root)
{
	int start, node = -1, last = NO_PREVIOUS_NODE;
	char var_name_text[TOKEN_LENGTH];

	start = -1;
	while (cur != -1) {
		auto& n = m_model->node(cur);
		if (n.type & SEXPT_OPERATOR) {
			node = alloc_sexp(n.text.c_str(), SEXP_ATOM, SEXP_ATOM_OPERATOR, -1, save_branch(n.child));

			if ((n.parent >= 0) && !at_root) {
				node = alloc_sexp("", SEXP_LIST, SEXP_ATOM_LIST, node, -1);
			}
		} else if (n.type & SEXPT_CONTAINER_NAME) {
			Assertion(get_sexp_container(n.text.c_str()) != nullptr,
				"Attempt to save unknown container %s from SEXP tree. Please report!",
				n.text.c_str());
			node = alloc_sexp(n.text.c_str(), SEXP_ATOM, SEXP_ATOM_CONTAINER_NAME, -1, -1);
		} else if (n.type & SEXPT_CONTAINER_DATA) {
			Assertion(get_sexp_container(n.text.c_str()) != nullptr,
				"Attempt to save unknown container %s from SEXP tree. Please report!",
				n.text.c_str());
			node = alloc_sexp(n.text.c_str(), SEXP_ATOM, SEXP_ATOM_CONTAINER_DATA, save_branch(n.child), -1);
		} else if (n.type & SEXPT_NUMBER) {
			// allocate number, maybe variable
			if (n.type & SEXPT_VARIABLE) {
				var_name_from_sexp_tree_text(var_name_text, n.text.c_str());
				node = alloc_sexp(var_name_text, (SEXP_ATOM | SEXP_FLAG_VARIABLE), SEXP_ATOM_NUMBER, -1, -1);
			} else {
				node = alloc_sexp(n.text.c_str(), SEXP_ATOM, SEXP_ATOM_NUMBER, -1, -1);
			}
		} else if (n.type & SEXPT_STRING) {
			// allocate string, maybe variable
			if (n.type & SEXPT_VARIABLE) {
				var_name_from_sexp_tree_text(var_name_text, n.text.c_str());
				node = alloc_sexp(var_name_text, (SEXP_ATOM | SEXP_FLAG_VARIABLE), SEXP_ATOM_STRING, -1, -1);
			} else {
				node = alloc_sexp(n.text.c_str(), SEXP_ATOM, SEXP_ATOM_STRING, -1, -1);
			}
		} else {
			Assert(0); // unknown and/or invalid type
		}

		if (last == NO_PREVIOUS_NODE){
			start = node;
		} else if (last >= 0){
			Sexp_nodes[last].rest = node;
		}

		last = node;
		Assert(last != NO_PREVIOUS_NODE);  // should be impossible
		cur = n.next;
		if (at_root){
			return start;
		}
	}

	return start;
}

void sexp_tree::post_load()
{
	if (!flag)
		select_sexp_node = -1;
}

// build or rebuild a CTreeCtrl object with the current tree data
void sexp_tree::build_tree()
{
	if (!flag) {
		select_sexp_node = -1;
	}

	DeleteAllItems();
	m_modelToHandle.clear();

	// Find the actual root of the model (the node with no parent)
	int model_root = -1;
	for (int i = 0; i < m_model->size(); i++) {
		if (m_model->parentOf(i) == -1) {
			model_root = i;
			break;
		}
	}

	if (model_root >= 0) {
		add_sub_tree(model_root, TVI_ROOT);
	}
}

// Create the CTreeCtrl tree from the tree data.  The tree data should already be setup by
// this point.
void sexp_tree::add_sub_tree(int node_index, HTREEITEM parent_handle)
{
	// Base case: if there's no node, we're done.
	if (node_index < 0) {
		return;
	}

	// 1. Get the data for the CURRENT node.
	const SexpNode& n = m_model->node(node_index);

	// 2. Determine the bitmap.
	int bitmap;
	if (n.type & SEXPT_OPERATOR) {
		bitmap = BITMAP_OPERATOR;
	} else if (n.type & SEXPT_VARIABLE) {
		bitmap = BITMAP_VARIABLE;
	} else if (n.type & SEXPT_CONTAINER_NAME) {
		bitmap = BITMAP_CONTAINER_NAME;
	} else if (n.type & SEXPT_CONTAINER_DATA) {
		bitmap = BITMAP_CONTAINER_DATA;
	} else {
		bitmap = get_data_image(node_index);
	}

	// 3. Create the UI item for THIS node and link it.
	HTREEITEM my_handle = insert(n.text.c_str(), bitmap, bitmap, parent_handle);
	SetItemData(my_handle, node_index);
	m_modelToHandle[node_index] = my_handle;

	// 4. Recurse for this node's CHILDREN, passing THIS node's handle as their parent.
	add_sub_tree(m_model->firstChild(node_index), my_handle);

	// 5. Recurse for this node's SIBLINGS, passing the SAME parent handle.
	add_sub_tree(m_model->nextSibling(node_index), parent_handle);
}

// construct tree nodes for an sexp, adding them to the list and returning first node
int sexp_tree::load_sub_tree(int index, bool valid, const char *text)
{
	int cur;

	if (index < 0) {
		cur = m_model->allocateNode(-1);
		m_model->setNode(cur, (SEXPT_OPERATOR | (valid ? SEXPT_VALID : 0)), text);  // setup a default tree if none
		return cur;
	}

	// assumption: first token is an operator.  I require this because it would cause problems
	// with child/parent relations otherwise, and it should be this way anyway, since the
	// return type of the whole sexp is boolean, and only operators can satisfy this.
	Assert(Sexp_nodes[index].subtype == SEXP_ATOM_OPERATOR);
	cur = load_branch(index, -1);

	return cur;
}

void sexp_tree::setup_selected(HTREEITEM h)
{
	if (!h)
		h = GetSelectedItem();

	update_item(h);
}

void sexp_tree::update_item(HTREEITEM h)
{
	item_handle = h;
	item_index = (int)GetItemData(h);
}

void sexp_tree::update_item(int node)
{
	item_index = node;
	if (m_modelToHandle.find(node) != m_modelToHandle.end()) {
		item_handle = m_modelToHandle[node];
	} else {
		item_handle = nullptr;
	}

	// Not sure this should be allowed!!
	Assertion(item_handle != nullptr, "update_item: node %d has no associated tree handle.", node);
}

// handler for right mouse button clicks.
// TODO modify this to use sexp_actions_core
void sexp_tree::right_clicked(int mode)
{
	// --- Boilerplate ---
	CPoint mouse;
	GetCursorPos(&mouse);
	CPoint client_point = mouse;
	ScreenToClient(&client_point);
	UINT flags;
	HTREEITEM h_item = HitTest(client_point, &flags);
	if (!h_item)
		return;
	update_item(h_item);

	// --- 1. Query the model for ALL available actions FIRST ---
	SexpContextMenu context_menu_model = m_model->queryContextMenu(nodeIndexForActions(h_item));

	// Helper to find an action in the model's response
	auto find_action = [&](SexpActionId id) -> const SexpContextAction* {
		for (const auto& action : context_menu_model.actions) {
			if (action.id == id)
				return &action;
		}
		return nullptr;
	};

	// --- 2. Build the menu, using the model's labels where available ---
	CMenu menu;
	menu.CreatePopupMenu();

	const SexpContextAction* action_ptr = nullptr;

	// --- Top Group ---
	action_ptr = find_action(SexpActionId::DeleteNode); // TODO conditionally enable for root nodes (event name)
	Assertion(action_ptr, "Action 'DeleteNode' is missing from the context menu model!");
	UINT item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_DELETE, action_ptr->label.c_str());

	action_ptr = find_action(SexpActionId::EditText);
	Assertion(action_ptr, "Action 'EditText' is missing from the context menu model!");
	item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_EDIT_TEXT, action_ptr->label.c_str());

	menu.AppendMenu(MF_STRING, ID_EXPAND_ALL, "Expand All");
	menu.AppendMenu(MF_SEPARATOR);

	// --- Comment/Color Group ---
	bool annotations_added = false;
	if (hasFlag(SexpTreeFlag::EnableAnnotations)) {
		menu.AppendMenu(MF_STRING, ID_EDIT_COMMENT, "Edit Comment");
		annotations_added = true;
	}
	if (hasFlag(SexpTreeFlag::EnableColors)) {
		menu.AppendMenu(MF_STRING, ID_EDIT_BG_COLOR, "Edit Background Color");
		annotations_added = true;
	}
	if (annotations_added) {
		menu.AppendMenu(MF_SEPARATOR);
	}

	// --- Clipboard Group ---
	action_ptr = find_action(SexpActionId::Cut);
	Assertion(action_ptr, "Action 'Cut' is missing from the context menu model!");
	item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_EDIT_CUT, action_ptr->label.c_str());

	action_ptr = find_action(SexpActionId::Copy);
	Assertion(action_ptr, "Action 'Copy' is missing from the context menu model!");
	item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_EDIT_COPY, action_ptr->label.c_str());

	action_ptr = find_action(SexpActionId::PasteOverwrite);
	Assertion(action_ptr, "Action 'Paste Overwrite' is missing from the context menu model!");
	item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_EDIT_PASTE, action_ptr->label.c_str());
	menu.AppendMenu(MF_SEPARATOR);

	// --- Structure and Operator groups ---
	CMenu add_op_submenu;
	add_op_submenu.CreatePopupMenu();
	action_ptr = find_action(SexpActionId::AddOperator);
	Assertion(action_ptr, "Action 'AddOperator' is missing from the context menu model!");
	item_flags = MF_POPUP | MF_GRAYED;

	if (action_ptr->enabled && !action_ptr->choices.empty()) {
		item_flags = MF_POPUP;
		for (size_t i = 0; i < action_ptr->choices.size(); ++i) {
			UINT choice_id = ID_SEXP_ACTION_BASE + MAKELONG(i, 0); // TODO Placeholder ID
			add_op_submenu.AppendMenu(MF_STRING, choice_id, action_ptr->choiceText[i].c_str());
		}
	} else {
		add_op_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no operators available)");
	}
	menu.AppendMenu(item_flags, (UINT_PTR)add_op_submenu.m_hMenu, action_ptr->label.c_str());

	CMenu add_data_submenu;
	add_data_submenu.CreatePopupMenu();
	action_ptr = find_action(SexpActionId::AddData);
	Assertion(action_ptr, "Action 'AddData' is missing from the context menu model!");
	item_flags = MF_POPUP | MF_GRAYED;

	if (action_ptr->enabled && !action_ptr->choices.empty()) {
		item_flags = MF_POPUP;
		for (size_t i = 0; i < action_ptr->choices.size(); ++i) {
			UINT choice_id = ID_SEXP_ACTION_BASE + MAKELONG(i, 1); // TODO Placeholder ID
			add_data_submenu.AppendMenu(MF_STRING, choice_id, action_ptr->choiceText[i].c_str());
		}
	} else {
		add_data_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no operators available)");
	}
	menu.AppendMenu(item_flags, (UINT_PTR)add_data_submenu.m_hMenu, action_ptr->label.c_str());
	menu.AppendMenu(MF_SEPARATOR);

	action_ptr = find_action(SexpActionId::PasteAdd);
	Assertion(action_ptr, "Action 'Paste Add' is missing from the context menu model!");
	item_flags = action_ptr->enabled ? MF_STRING : MF_STRING | MF_GRAYED;
	menu.AppendMenu(item_flags, ID_EDIT_PASTE_SPECIAL, action_ptr->label.c_str());
	menu.AppendMenu(MF_SEPARATOR);

	CMenu insert_op_submenu;
	insert_op_submenu.CreatePopupMenu();
	insert_op_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no operators available)");
	menu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)insert_op_submenu.m_hMenu, "Insert Operator");
	menu.AppendMenu(MF_SEPARATOR);

	// --- Replace Operator (using model label and choices) ---
	CMenu replace_op_submenu;
	replace_op_submenu.CreatePopupMenu();
	action_ptr = find_action(SexpActionId::ReplaceOperator);
	Assertion(action_ptr, "Action 'ReplaceOperator' is missing from the context menu model!");
	item_flags = MF_POPUP | MF_GRAYED;

	if (action_ptr->enabled && !action_ptr->choices.empty()) {
		item_flags = MF_POPUP;
		for (size_t i = 0; i < action_ptr->choices.size(); ++i) {
			UINT choice_id = ID_SEXP_ACTION_BASE + MAKELONG(i, 2); // TODO Placeholder ID
			replace_op_submenu.AppendMenu(MF_STRING, choice_id, action_ptr->choiceText[i].c_str());
		}
	} else {
		replace_op_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no operators available)");
	}
	menu.AppendMenu(item_flags, (UINT_PTR)replace_op_submenu.m_hMenu, action_ptr->label.c_str());

	// ... (The rest of the menu remains the same, using disabled placeholders) ...
	CMenu replace_data_submenu;
	replace_data_submenu.CreatePopupMenu();
	replace_data_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no data available)");
	menu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)replace_data_submenu.m_hMenu, "Replace Data");
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, ID_SEXP_TREE_ADD_VARIABLE, "Add Variable");
	menu.AppendMenu(MF_STRING, ID_SEXP_TREE_MODIFY_VARIABLE, "Modify Variable");

	CMenu replace_var_submenu;
	replace_var_submenu.CreatePopupMenu();
	replace_var_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no variables available)");
	menu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)replace_var_submenu.m_hMenu, "Replace Variable");
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, ID_EDIT_SEXP_TREE_EDIT_CONTAINERS, "Add/Modify Container");

	CMenu replace_container_name_submenu;
	replace_container_name_submenu.CreatePopupMenu();
	replace_container_name_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no containers available)");
	menu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)replace_container_name_submenu.m_hMenu, "Replace Container Name");

	CMenu replace_container_data_submenu;
	replace_container_data_submenu.CreatePopupMenu();
	replace_container_data_submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, "(no containers available)");
	menu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)replace_container_data_submenu.m_hMenu, "Replace Container Data");

	// --- 3. Display the menu ---
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, this);

	//----------------------------------------- REST IS OLD UNREACHABLE CODE----------------------------------------------
	/*int i, j, z, count, op, add_type, replace_type, type = 0, subcategory_id;
	sexp_list_item *list;
	UINT _flags;
	HTREEITEM h;
	POINT click_point, mouse;
	CMenu menu, *mptr, *popup_menu, *add_data_menu = NULL, *replace_data_menu = NULL;
	CMenu *add_op_menu, add_op_submenu[MAX_OP_MENUS];
	CMenu *replace_op_menu, replace_op_submenu[MAX_OP_MENUS];
	CMenu *insert_op_menu, insert_op_submenu[MAX_OP_MENUS];
	CMenu *replace_variable_menu = NULL;
	CMenu *replace_container_name_menu = nullptr;
	CMenu *replace_container_data_menu = nullptr;
	CMenu add_op_subcategory_menu[MAX_SUBMENUS];
	CMenu replace_op_subcategory_menu[MAX_SUBMENUS];
	CMenu insert_op_subcategory_menu[MAX_SUBMENUS];

	m_mode = mode;
	add_instance = replace_instance = -1;
	Assert((int)op_menu.size() < MAX_OP_MENUS);
	Assert((int)op_submenu.size() < MAX_SUBMENUS);

	GetCursorPos(&mouse);
	click_point = mouse;
	ScreenToClient(&click_point);
	h = HitTest(CPoint(click_point), &_flags);  // find out what they clicked on

	if (h && menu.LoadMenu(IDR_MENU_EDIT_SEXP_TREE)) {
		update_help(h);
		popup_menu = menu.GetSubMenu(0);
		ASSERT(popup_menu != NULL);
		//SelectDropTarget(h);  // WTF: Why was this here???

		add_op_menu = replace_op_menu = insert_op_menu = NULL;

		// get pointers to several key popup menus we'll need to modify
		i = popup_menu->GetMenuItemCount();
		while (i--) {
			if ( (mptr = popup_menu->GetSubMenu(i)) > 0 ) {
				char buf[256];
				popup_menu->GetMenuString(i, buf, sizeof(buf), MF_BYPOSITION);

				if (!stricmp(buf, "add operator")) {
					add_op_menu = mptr;

				} else if (!stricmp(buf, "replace operator")) {
					replace_op_menu = mptr;

				} else if (!stricmp(buf, "add data")) {
					add_data_menu = mptr;

				} else if (!stricmp(buf, "replace data")) {
					replace_data_menu = mptr;

				} else if (!stricmp(buf, "insert operator")) {
					insert_op_menu = mptr;

				} else if (!stricmp(buf, "replace variable")) {
					replace_variable_menu = mptr;

				} else if (!stricmp(buf, "replace container name")) {
					replace_container_name_menu = mptr;

				} else if (!stricmp(buf, "replace container data")) {
					replace_container_data_menu = mptr;
				}
			}
		}

		// add popup menus for all the operator categories
		for (i=0; i<(int)op_menu.size(); i++)
		{
			add_op_submenu[i].CreatePopupMenu();
			replace_op_submenu[i].CreatePopupMenu();
			insert_op_submenu[i].CreatePopupMenu();

			add_op_menu->AppendMenu(MF_POPUP, (UINT) add_op_submenu[i].m_hMenu, op_menu[i].name.c_str());
			replace_op_menu->AppendMenu(MF_POPUP, (UINT) replace_op_submenu[i].m_hMenu, op_menu[i].name.c_str());
			insert_op_menu->AppendMenu(MF_POPUP, (UINT) insert_op_submenu[i].m_hMenu, op_menu[i].name.c_str());
		}

		// get rid of the placeholders we needed to ensure popup menus stayed popup menus,
		// i.e. MSDEV will convert empty popup menus into normal menu items.
		add_op_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);
		replace_op_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);
		insert_op_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);
		replace_variable_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);
		replace_container_name_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);
		replace_container_data_menu->DeleteMenu(ID_PLACEHOLDER, MF_BYCOMMAND);

		// get item_index
		update_item(h);

		// annotations only work in the event editor
		if (m_mode == MODE_EVENTS)
		{
			menu.EnableMenuItem(ID_EDIT_COMMENT, MF_ENABLED);
			menu.EnableMenuItem(ID_EDIT_BG_COLOR, MF_ENABLED);
		}
		else
		{
			menu.EnableMenuItem(ID_EDIT_COMMENT, MF_GRAYED);
			menu.EnableMenuItem(ID_EDIT_BG_COLOR, MF_GRAYED);
		}

		
		//Goober5000 - allow variables in all modes;
		//the restriction seems unnecessary IMHO
		//
		// Do SEXP_VARIABLE stuff here.
		//if (m_mode != MODE_EVENTS)
		//{
		//	// only allow variables in event mode
		//	menu.EnableMenuItem(ID_SEXP_TREE_ADD_VARIABLE, MF_GRAYED);
		//	menu.EnableMenuItem(ID_SEXP_TREE_MODIFY_VARIABLE, MF_GRAYED);
		//}
		//else

		{
			menu.EnableMenuItem(ID_SEXP_TREE_ADD_VARIABLE, MF_ENABLED);
			menu.EnableMenuItem(ID_SEXP_TREE_MODIFY_VARIABLE, MF_ENABLED);
			
			// check not root (-1)
			if (item_index >= 0) {
				// get type of sexp_tree item clicked on
				type = get_type(h);

				auto& n = m_model->node(item_index);

				int parent = n.parent;
				if (parent >= 0) {
					op = get_operator_index(n.text.c_str());
					auto& parent_n = m_model->node(parent);
					Assertion(op >= 0 || parent_n.type & SEXPT_CONTAINER_DATA,
						"Encountered unknown SEXP operator %s. Please report!",
						parent_n.text.c_str());
					int first_arg = parent_n.child;

					// get arg count of item to replace
					Replace_count = 0;
					int parent_index = m_model->parentOf(item_index);

					// Ensure the item has a parent before trying to find its position.
					if (parent_index >= 0) {
						// Start counting from the first child of the parent.
						int current_sibling = m_model->firstChild(parent_index);

						// Walk the sibling list until we find our item.
						while (current_sibling != -1 && current_sibling != item_index) {
							Replace_count++;
							current_sibling = m_model->nextSibling(current_sibling);
						}
					}

					int op_type = 0;

					if (op >= 0) {
						op_type =
							query_operator_argument_type(op, Replace_count); // check argument type at this position
					} else {
						Assertion(parent_n.type & SEXPT_CONTAINER_DATA,
							"Unknown SEXP operator %s. Please report!",
							parent_n.text.c_str());
						const auto* p_container = get_sexp_container(parent_n.text.c_str());
						Assertion(p_container != nullptr,
							"Found modifier for unknown container %s. Please report!",
							parent_n.text.c_str());
						op_type = p_container->opf_type;
					}
					Assertion(op_type > 0,
						"Found invalid operator type %d for node with text %s. Please report!",
						op_type,
						parent_n.text.c_str());

					// special case don't allow replace data for variable names
					// Goober5000 - why?  the only place this happens is when replacing the ambiguous argument in
					// modify-variable with a variable, which seems legal enough.
					//if (op_type != OPF_AMBIGUOUS) {

						// Goober5000 - given the above, we have to figure out what type this stands for
						if (op_type == OPF_AMBIGUOUS)
						{
							int modify_type = get_modify_variable_type(parent);
							if (modify_type == OPF_NUMBER) {
								type = SEXPT_NUMBER;
							} else if (modify_type == OPF_AMBIGUOUS) {
								type = SEXPT_STRING;
							} else {
								Int3();
								auto& first_arg_n = m_model->node(first_arg);
								type = first_arg_n.type;
							}
						}

						// Goober5000 - certain types accept both integers and a list of strings
						if (op_type == OPF_GAME_SND || op_type == OPF_FIREBALL || op_type == OPF_WEAPON_BANK_NUMBER)
						{
							type = SEXPT_NUMBER | SEXPT_STRING;
						}

						// jg18 - container values (container data/map keys) can be anything
						// the type is checked in check_sexp_syntax()
						if (op_type == OPF_CONTAINER_VALUE)
						{
							type = SEXPT_NUMBER | SEXPT_STRING;
						}
						
						if ( (type & SEXPT_STRING) || (type & SEXPT_NUMBER) ) {

							int max_sexp_vars = MAX_SEXP_VARIABLES;
							// prevent collisions in id numbers: ID_VARIABLE_MENU + 512 = ID_ADD_MENU
							Assert(max_sexp_vars < 512);

							for (int idx=0; idx<max_sexp_vars; idx++) {
								if (Sexp_variables[idx].type & SEXP_VARIABLE_SET) {
									// skip block variables
									if (Sexp_variables[idx].type & SEXP_VARIABLE_BLOCK) {
										continue;
									}

									UINT flags = MF_STRING | MF_GRAYED;
									// maybe gray flag MF_GRAYED

									// get type -- gray "string" or number accordingly
									if ( type & SEXPT_STRING ) {
										if ( Sexp_variables[idx].type & SEXP_VARIABLE_STRING ) {
											flags &= ~MF_GRAYED;
										}
									}
									if ( type & SEXPT_NUMBER ) {
										if ( Sexp_variables[idx].type & SEXP_VARIABLE_NUMBER ) {
											flags &= ~MF_GRAYED;
										}
									}

									// if modify-variable and changing variable, enable all variables
									if (op_type == OPF_VARIABLE_NAME) {
										Modify_variable = 1;
										flags &= ~MF_GRAYED;
									} else {
										Modify_variable = 0;
									}

									// enable navsystem always
									if (op_type == OPF_NAV_POINT)
										flags &= ~MF_GRAYED;

									// enable all for container multidimensionality
									if ((type & SEXPT_MODIFIER) && Replace_count > 0)
										flags &= ~MF_GRAYED;

									if (!( (idx + 3) % 30)) {
										flags |= MF_MENUBARBREAK;
									}

									char buf[128];
									// append list of variable names and values
									// set id as ID_VARIABLE_MENU + idx
									sprintf(buf, "%s (%s)", Sexp_variables[idx].variable_name, Sexp_variables[idx].text);

									replace_variable_menu->AppendMenu(flags, (ID_VARIABLE_MENU + idx), buf);
								}
							}

							// Replace Container Name submenu
							if (is_container_name_opf_type(op_type) || op_type == OPF_DATA_OR_STR_CONTAINER) {
								int container_name_index = 0;
								for (const auto &container : get_all_sexp_containers()) {
									UINT flags = MF_STRING | MF_GRAYED;

									if (op_type == OPF_CONTAINER_NAME) {
										// allow all containers
										flags &= ~MF_GRAYED;
									} else if ((op_type == OPF_LIST_CONTAINER_NAME) && container.is_list()) {
										flags &= ~MF_GRAYED;
									} else if ((op_type == OPF_MAP_CONTAINER_NAME) && container.is_map()) {
										flags &= ~MF_GRAYED;
									} else if ((op_type == OPF_DATA_OR_STR_CONTAINER) &&
											   container.is_of_string_type()) {
										flags &= ~MF_GRAYED;
									}

									replace_container_name_menu->AppendMenu(flags,
										(ID_CONTAINER_NAME_MENU + container_name_index++),
										container.container_name.c_str());
								}
							}

							// Replace Container Data submenu
							// disallowed on variable-type SEXP args, to prevent FSO/FRED crashes
							// also disallowed for special argument options (not supported for now)
							// op < 0 means we're on a container modifier, and nested Replace Container Data is allowed
							if (op_type != OPF_VARIABLE_NAME && (op < 0 || !is_argument_provider_op(Operators[op].value))) {
								int container_data_index = 0;
								for (const auto &container : get_all_sexp_containers()) {
									UINT flags = MF_STRING | MF_GRAYED;

									if ((type & SEXPT_STRING) && any(container.type & ContainerType::STRING_DATA)) {
										flags &= ~MF_GRAYED;
									}

									if ((type & SEXPT_NUMBER) && any(container.type & ContainerType::NUMBER_DATA)) {
										flags &= ~MF_GRAYED;
									}

									// enable all for container multidimensionality
									if ((n.type & SEXPT_MODIFIER) && Replace_count > 0)
										flags &= ~MF_GRAYED;

									replace_container_data_menu->AppendMenu(flags,
										(ID_CONTAINER_DATA_MENU + container_data_index++),
										container.container_name.c_str());
								}
							}
						}
					//}
				}
			}

			// can't modify if no variables
			if (sexp_variable_count() == 0) {
				menu.EnableMenuItem(ID_SEXP_TREE_MODIFY_VARIABLE, MF_GRAYED);
			}
		}

		// add all the submenu items first
		for (i=0; i<(int)op_submenu.size(); i++)
		{
			add_op_subcategory_menu[i].CreatePopupMenu();
			replace_op_subcategory_menu[i].CreatePopupMenu();
			insert_op_subcategory_menu[i].CreatePopupMenu();
			
			for (j=0; j<(int)op_menu.size(); j++)
			{
				if (op_menu[j].id == category_of_subcategory(op_submenu[i].id))
				{
					add_op_submenu[j].AppendMenu(MF_POPUP, (UINT) add_op_subcategory_menu[i].m_hMenu, op_submenu[i].name.c_str());
					replace_op_submenu[j].AppendMenu(MF_POPUP, (UINT) replace_op_subcategory_menu[i].m_hMenu, op_submenu[i].name.c_str());
					insert_op_submenu[j].AppendMenu(MF_POPUP, (UINT) insert_op_subcategory_menu[i].m_hMenu, op_submenu[i].name.c_str());
					break;	// only 1 category valid
				}
			}
		}

		// add operator menu items to the various CATEGORY submenus they belong in
		for (i=0; i<(int)Operators.size(); i++)
		{
			// add only if it is not in a subcategory
			subcategory_id = get_subcategory(Operators[i].value);
			if (subcategory_id == OP_SUBCATEGORY_NONE)
			{
				// put it in the appropriate menu
				for (j=0; j<(int)op_menu.size(); j++)
				{
					if (op_menu[j].id == get_category(Operators[i].value))
					{
						switch (Operators[i].value) {
// Commented out by Goober5000 to allow these operators to be selectable
//#ifdef NDEBUG
//							// various campaign operators
//							case OP_WAS_PROMOTION_GRANTED:
//							case OP_WAS_MEDAL_GRANTED:
//							case OP_GRANT_PROMOTION:
//							case OP_GRANT_MEDAL:
//							case OP_TECH_ADD_SHIP:
//							case OP_TECH_ADD_WEAPON:
//							case OP_TECH_ADD_INTEL_XSTR:
//							case OP_TECH_REMOVE_INTEL_XSTR:
//							case OP_TECH_RESET_TO_DEFAULT:
//#endif

							// hide these operators per GitHub issue #6400
							case OP_GET_VARIABLE_BY_INDEX:
							case OP_SET_VARIABLE_BY_INDEX:
							case OP_COPY_VARIABLE_FROM_INDEX:
							case OP_COPY_VARIABLE_BETWEEN_INDEXES:

							// unlike the various campaign operators, these are deprecated
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
								j = (int)op_menu.size();	// don't allow these operators to be visible
								break;
						}

						if (j < (int)op_menu.size()) {
							add_op_submenu[j].AppendMenu(MF_STRING | MF_GRAYED, Operators[i].value, Operators[i].text.c_str());
							replace_op_submenu[j].AppendMenu(MF_STRING | MF_GRAYED, Operators[i].value | OP_REPLACE_FLAG, Operators[i].text.c_str());
							insert_op_submenu[j].AppendMenu(MF_STRING, Operators[i].value | OP_INSERT_FLAG, Operators[i].text.c_str());
						}

						break;	// only 1 category valid
					}
				}
			}
			// if it is in a subcategory, handle it
			else
			{
				// put it in the appropriate submenu
				for (j=0; j<(int)op_submenu.size(); j++)
				{
					if (op_submenu[j].id == subcategory_id)
					{
						switch (Operators[i].value) {
// Commented out by Goober5000 to allow these operators to be selectable
//#ifdef NDEBUG
//							// various campaign operators
//							case OP_WAS_PROMOTION_GRANTED:
//							case OP_WAS_MEDAL_GRANTED:
//							case OP_GRANT_PROMOTION:
//							case OP_GRANT_MEDAL:
//							case OP_TECH_ADD_SHIP:
//							case OP_TECH_ADD_WEAPON:
//							case OP_TECH_ADD_INTEL_XSTR:
//							case OP_TECH_REMOVE_INTEL_XSTR:
//							case OP_TECH_RESET_TO_DEFAULT:
//#endif

							// hide these operators per GitHub issue #6400
							case OP_GET_VARIABLE_BY_INDEX:
							case OP_SET_VARIABLE_BY_INDEX:
							case OP_COPY_VARIABLE_FROM_INDEX:
							case OP_COPY_VARIABLE_BETWEEN_INDEXES:

							// unlike the various campaign operators, these are deprecated
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
								j = (int)op_submenu.size();	// don't allow these operators to be visible
								break;
						}

						if (j < (int)op_submenu.size()) {
							add_op_subcategory_menu[j].AppendMenu(MF_STRING | MF_GRAYED, Operators[i].value, Operators[i].text.c_str());
							replace_op_subcategory_menu[j].AppendMenu(MF_STRING | MF_GRAYED, Operators[i].value | OP_REPLACE_FLAG, Operators[i].text.c_str());
							insert_op_subcategory_menu[j].AppendMenu(MF_STRING, Operators[i].value | OP_INSERT_FLAG, Operators[i].text.c_str());
						}

						break;	// only 1 subcategory valid
					}
				}
			}
		}

		// find local index (i) of current item (from its handle)
		SelectItem(h);
		update_item(h);

		// special case: item is a ROOT node, and a label that can be edited (not an item in the sexp tree)
		if ((item_index == -1) && (m_mode & ST_LABELED_ROOT)) {
			if (m_mode & ST_ROOT_EDITABLE) {
				menu.EnableMenuItem(ID_EDIT_TEXT, MF_ENABLED);
			} else {
				menu.EnableMenuItem(ID_EDIT_TEXT, MF_GRAYED);
			}

			// disable copy, insert op
			menu.EnableMenuItem(ID_EDIT_COPY, MF_GRAYED);
			for (j=0; j<(int)Operators.size(); j++) {
				menu.EnableMenuItem(Operators[j].value | OP_INSERT_FLAG, MF_GRAYED);
			}

			gray_menu_tree(popup_menu);
			popup_menu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, this);
			return;
		}

		Assert(item_index != -1);  // handle not found, which should be impossible.
		auto& n = m_model->node(item_index);
		if (!(n.flags & EDITABLE)) {
			menu.EnableMenuItem(ID_EDIT_TEXT, MF_GRAYED);
		}

		if (n.parent == -1) {  // root node
			menu.EnableMenuItem(ID_DELETE, MF_GRAYED);  // can't delete the root item.
		}

		//		if ((n.flags & OPERAND) && (n.flags & EDITABLE))  // expandable?
		//	menu.EnableMenuItem(ID_SPLIT_LINE, MF_ENABLED);
		//
		//z = n.child;
		//auto& cn = model->node(z);
		//if (z != -1 && cn.next == -1 && cn.child == -1)
		//	menu.EnableMenuItem(ID_SPLIT_LINE, MF_ENABLED);
		//
		//int pz = n.parent;
		//auto& pn = model->node(pz);
		//z = pn.child;
		//auto& sz = model->node(z);
		//if (z != -1 && sz.next == -1 && sz.child == -1)
		//	menu.EnableMenuItem(ID_SPLIT_LINE, MF_ENABLED);

		// change enabled status of 'add' type menu options.
		add_type = 0;

		// container multidimensionality
		if (n.type & SEXPT_CONTAINER_DATA) {
			// using local var for add count to avoid breaking implicit assumptions about Add_count
			const int modifier_node = n.child;
			Assertion(modifier_node != -1,
				"No modifier found for container data node %s. Please report!",
				n.text.c_str());
			const int modifier_add_count = m_model->countArgs(modifier_node);

			const auto *p_container = get_sexp_container(n.text.c_str());
			Assertion(p_container,
				"Found modifier for unknown container %s. Please report!", n.text.c_str());

			auto& modifier_n = m_model->node(modifier_node);
			if (modifier_add_count == 1 && p_container->is_list() &&
				get_list_modifier(modifier_n.text.c_str()) == ListModifier::AT_INDEX) {
				// only valid value is a list index
				add_type = OPR_NUMBER;
				menu.EnableMenuItem(ID_ADD_NUMBER, MF_ENABLED);
			} else {
				// container multidimensionality
				add_type = OPR_STRING;

				// the next thing we want to add could literally be any legal key for any map or the legal entries for a list container
				// so give the FREDder a hand and offer the list modifiers, but only the FREDder can know if they're relevant
				list = get_container_multidim_modifiers(item_index);

				if (list) {
					sexp_list_item *ptr = nullptr;

					int data_idx = 0;
					ptr = list;
					while (ptr) {
						if (ptr->op >= 0) {
							// enable operators with correct return type
							menu.EnableMenuItem(Operators[ptr->op].value, MF_ENABLED);
						} else {
							// add data
							if ((data_idx + 3) % 30) {
								add_data_menu->AppendMenu(MF_STRING | MF_ENABLED, ID_ADD_MENU + data_idx, ptr->text.c_str());
							} else {
								add_data_menu->AppendMenu(MF_MENUBARBREAK | MF_STRING | MF_ENABLED, ID_ADD_MENU + data_idx, ptr->text.c_str());
							}
						}

						data_idx++;
						ptr = ptr->next;
					}
				}

				menu.EnableMenuItem(ID_ADD_NUMBER, MF_ENABLED);
				menu.EnableMenuItem(ID_ADD_STRING, MF_ENABLED);
			}
		} else if (n.flags & OPERAND)	{
			add_type = OPR_STRING;
			int child = n.child;
			Add_count = m_model->countArgs(child);
			op = get_operator_index(n.text.c_str());
			Assert(op >= 0);

			// get listing of valid argument values and add to menus
			type = query_operator_argument_type(op, Add_count);
			list = get_listing_opf(type, item_index, Add_count);
			if (list) {
				sexp_list_item *ptr;

				int data_idx = 0;
				ptr = list;
				while (ptr) {
					if (ptr->op >= 0) {
						// enable operators with correct return type
						menu.EnableMenuItem(Operators[ptr->op].value, MF_ENABLED);

					} else {
						UINT flags = MF_STRING | MF_ENABLED;

						if (!((data_idx + 3) % 30)) {
							flags |= MF_MENUBARBREAK;
						}

						// add data
						if (type == OPF_VARIABLE_NAME) {
							char buf[128];
							sprintf(buf, "%s (%s)", Sexp_variables[data_idx].variable_name, Sexp_variables[data_idx].text);
							add_data_menu->AppendMenu(flags, ID_ADD_MENU + data_idx, buf);
						} else {
							add_data_menu->AppendMenu(flags, ID_ADD_MENU + data_idx, ptr->text.c_str());
						}
					}

					data_idx++;
					ptr = ptr->next;
				}
			}

			// special handling for the non-string formats
			if (type == OPF_NONE) {  // an argument can't be added
				add_type = 0;

			} else if (type == OPF_NULL) {  // arguments with no return values
				add_type = OPR_NULL;

			// Goober5000
			} else if (type == OPF_FLEXIBLE_ARGUMENT) {
				add_type = OPR_FLEXIBLE_ARGUMENT;
		
			} else if (type == OPF_NUMBER) {  // takes numbers
				add_type = OPR_NUMBER;
				menu.EnableMenuItem(ID_ADD_NUMBER, MF_ENABLED);

			} else if (type == OPF_POSITIVE) {  // takes non-negative numbers
				add_type = OPR_POSITIVE;
				menu.EnableMenuItem(ID_ADD_NUMBER, MF_ENABLED);

			} else if (type == OPF_BOOL) {  // takes true/false bool values
				add_type = OPR_BOOL;

			} else if (type == OPF_AI_GOAL) {
				add_type = OPR_AI_GOAL;

			} else if (type == OPF_CONTAINER_VALUE) {
				// allow both strings and numbers
				// types are checked in check_sepx_syntax()
				menu.EnableMenuItem(ID_ADD_NUMBER, MF_ENABLED);
			}

			// add_type unchanged from above
			if (add_type == OPR_STRING && !is_container_name_opf_type(type)) {
				menu.EnableMenuItem(ID_ADD_STRING, MF_ENABLED);
			}

			list->destroy();
		}

		// disable operators that do not have arguments available
		for (j=0; j<(int)Operators.size(); j++) {
			if (!query_default_argument_available(j)) {
				menu.EnableMenuItem(Operators[j].value, MF_GRAYED);
			}
		}


		// change enabled status of 'replace' type menu options.
		replace_type = 0;
		int parent = n.parent;
		if (parent >= 0) {
			replace_type = OPR_STRING;
			auto& parent_n = m_model->node(parent);
			op = get_operator_index(parent_n.text.c_str());
			Assertion(op >= 0 || parent_n.type & SEXPT_CONTAINER_DATA,
				"Encountered unknown SEXP operator %s. Please report!",
				parent_n.text.c_str());
			int first_arg = parent_n.child;
			count = m_model->countArgs(parent_n.child);

			if (op >= 0) {
				// already at minimum number of arguments?
				if (count <= Operators[op].min) {
					menu.EnableMenuItem(ID_DELETE, MF_GRAYED);
				}
			} else if ((parent_n.type & SEXPT_CONTAINER_DATA) && (item_index == first_arg)) {
				// a container data node's initial modifier can't be deleted
				Assertion(n.type & SEXPT_MODIFIER,
					"Container data %s node's first modifier %s is not a modifier. Please report!",
					parent_n.text.c_str(),
					n.text.c_str());
				menu.EnableMenuItem(ID_DELETE, MF_GRAYED);
			}

			// get arg count of item to replace
			Replace_count = 0;
			int parent_index = m_model->parentOf(item_index);

			// Ensure the item has a parent before trying to find its position.
			if (parent_index >= 0) {
				// Start counting from the first child of the parent.
				int current_sibling = m_model->firstChild(parent_index);

				// Walk the sibling list until we find our item.
				while (current_sibling != -1 && current_sibling != item_index) {
					Replace_count++;
					current_sibling = m_model->nextSibling(current_sibling);
				}
			}

			if (op >= 0) {
				// maybe gray delete
				for (i = Replace_count + 1; i < count; i++) {
					if (query_operator_argument_type(op, i - 1) != query_operator_argument_type(op, i)) {
						menu.EnableMenuItem(ID_DELETE, MF_GRAYED);
						break;
					}
				}

				type = query_operator_argument_type(op, Replace_count); // check argument type at this position
			} else {
				Assertion(parent_n.type & SEXPT_CONTAINER_DATA,
					"Unknown SEXP operator %s. Please report!",
					parent_n.text.c_str());
				const auto* p_container = get_sexp_container(parent_n.text.c_str());
				Assertion(p_container != nullptr,
					"Found modifier for unknown container %s. Please report!",
					parent_n.text.c_str());
				type = p_container->opf_type;
			}

			// special case reset type for ambiguous
			if (type == OPF_AMBIGUOUS) {
				type = get_modify_variable_type(parent);
			}

			// Container modifiers use their own list of possible arguments
			if (n.type & SEXPT_MODIFIER) {
				const auto* p_container = get_sexp_container(parent_n.text.c_str());
				Assertion(p_container != nullptr,
					"Found modifier for unknown container %s. Please report!",
					parent_n.text.c_str());
				const int first_modifier = parent_n.child;
				auto& modifier_n = m_model->node(first_modifier);
				if (Replace_count == 1 && p_container->is_list() &&
					get_list_modifier(modifier_n.text.c_str()) == ListModifier::AT_INDEX) {
					// only valid value is a list index (number)
					list = nullptr;
					replace_type = OPR_NUMBER;
				} else {
					list = get_container_modifiers(parent);
				}
			} else {
				list = get_listing_opf(type, parent, Replace_count);
			}

			// special case don't allow replace data for variable or container names
			if ((type != OPF_VARIABLE_NAME) && !is_container_name_opf_type(type) && list) {
				sexp_list_item *ptr;

				int data_idx = 0;
				ptr = list;
				while (ptr) {
					if (ptr->op >= 0) {
						menu.EnableMenuItem(Operators[ptr->op].value | OP_REPLACE_FLAG, MF_ENABLED);

					} else {
						if ( (data_idx + 3) % 30)
							replace_data_menu->AppendMenu(MF_STRING | MF_ENABLED, ID_REPLACE_MENU + data_idx, ptr->text.c_str());
						else
							replace_data_menu->AppendMenu(MF_MENUBARBREAK | MF_STRING | MF_ENABLED, ID_REPLACE_MENU + data_idx, ptr->text.c_str());
					}

					data_idx++;
					ptr = ptr->next;
				}
			}

			if (type == OPF_NONE) {  // takes no arguments
				replace_type = 0;

			} else if (type == OPF_NUMBER) {  // takes numbers
				replace_type = OPR_NUMBER;
				menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);

			} else if (type == OPF_POSITIVE) {  // takes non-negative numbers
				replace_type = OPR_POSITIVE;
				menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);

			} else if (type == OPF_BOOL) {  // takes true/false bool values
				replace_type = OPR_BOOL;

			} else if (type == OPF_NULL) {  // takes operator that doesn't return a value
				replace_type = OPR_NULL;
			} else if (type == OPF_AI_GOAL) {
				replace_type = OPR_AI_GOAL;
			}

			// Goober5000
			else if (type == OPF_FLEXIBLE_ARGUMENT) {
				replace_type = OPR_FLEXIBLE_ARGUMENT;
			}
			// Goober5000
			else if (type == OPF_GAME_SND || type == OPF_FIREBALL || type == OPF_WEAPON_BANK_NUMBER) {
				// even though these default to strings, we allow replacing them with index values
				replace_type = OPR_POSITIVE;
				menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);

			} else if (type == OPF_CONTAINER_VALUE) {
				// allow strings and numbers
				// type is checked in check_sexp_syntax()
				menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
			}

			// default to string, except for container names
			if (replace_type == OPR_STRING && !is_container_name_opf_type(type)) {
				menu.EnableMenuItem(ID_REPLACE_STRING, MF_ENABLED);
			}

			if (op >= 0) { // skip when handling "replace container data"
				// modify string or number if (modify_variable)
				if (Operators[op].value == OP_MODIFY_VARIABLE) {
					int modify_type = get_modify_variable_type(parent);

					if (modify_type == OPF_NUMBER) {
						menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
						menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
					}
					// no change for string type
				}
				else if (Operators[op].value == OP_SET_VARIABLE_BY_INDEX) {
					// it depends on which argument we are modifying
					// first argument is always a number
					if (Replace_count == 0) {
						menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
						menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
					}
					// second argument could be anything
					else {
						int modify_type = get_modify_variable_type(parent);

						if (modify_type == OPF_NUMBER) {
							menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
							menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
						}
						// no change for string type
					}
				}
			}

			if (n.type & SEXPT_MODIFIER) {
				Assertion(n.type & SEXPT_CONTAINER_DATA,
					"Container modifier found whose parent %s is not a container. Please report!",
					n.text.c_str());
				const int first_modifier_node = parent_n.child;
				Assertion(first_modifier_node != -1,
					"Container data node named %s has no modifier. Please report!",
					parent_n.text.c_str());
				const auto *p_container = get_sexp_container(parent_n.text.c_str());
				Assertion(p_container,
					"Attempt to get first modifier for unknown container %s. Please report!",
					parent_n.text.c_str());
				const auto &container = *p_container;

				auto& first_modifier_n = m_model->node(first_modifier_node);

				if (Replace_count == 0) {
					if (container.is_list()) {
						// the only valid values are either the list modifiers or Replace Variable/Cotnainer Data with string data
						menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_GRAYED);
						menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
						menu.EnableMenuItem(ID_EDIT_TEXT, MF_GRAYED);
					} else if (container.is_map()) {
						if (any(container.type & ContainerType::STRING_KEYS)) {
							menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_GRAYED);
							menu.EnableMenuItem(ID_REPLACE_STRING, MF_ENABLED);
						} else if (any(container.type & ContainerType::NUMBER_KEYS)) {
							menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
							menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
						} else {
							UNREACHABLE("Map container with type %d has unknown key type", (int)container.type);
						}
					} else {
						UNREACHABLE("Unknown container type %d", (int)container.type);
					}
				} else if (Replace_count == 1 && container.is_list() &&
						   get_list_modifier(first_modifier_n.text.c_str()) ==
							   ListModifier::AT_INDEX) {
					// only valid value is a list index
					menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
					menu.EnableMenuItem(ID_REPLACE_STRING, MF_GRAYED);
				} else {
					// multidimensional modifiers can be anything, including possibly a list modifier
					// the value can be validated only at runtime (i.e., in-mission)
					menu.EnableMenuItem(ID_REPLACE_NUMBER, MF_ENABLED);
					menu.EnableMenuItem(ID_REPLACE_STRING, MF_ENABLED);
				}
			}

			list->destroy();

		} else {  // top node, so should be a Boolean type.
			if (m_mode == MODE_EVENTS) {  // return type should be null
				replace_type = OPR_NULL;
				for (j=0; j<(int)Operators.size(); j++)
					if (query_operator_return_type(j) == OPR_NULL)
						menu.EnableMenuItem(Operators[j].value | OP_REPLACE_FLAG, MF_ENABLED);

			} else {
				replace_type = OPR_BOOL;
				for (j=0; j<(int)Operators.size(); j++)
					if (query_operator_return_type(j) == OPR_BOOL)
						menu.EnableMenuItem(Operators[j].value | OP_REPLACE_FLAG, MF_ENABLED);
			}
		}

		// disable operators that do not have arguments available
		for (j=0; j<(int)Operators.size(); j++) {
			if (!query_default_argument_available(j)) {
				menu.EnableMenuItem(Operators[j].value | OP_REPLACE_FLAG, MF_GRAYED);
			}
		}


		// change enabled status of 'insert' type menu options.
		z = n.parent;
		Assert(z >= -1);
		if (z != -1) {
			auto& nn = m_model->node(z);
			op = get_operator_index(nn.text.c_str());
			Assertion(op != -1 || nn.type & SEXPT_CONTAINER_DATA,
				"Encountered unknown SEXP operator %s. Please report!",
				nn.text.c_str());
			j = nn.child;
			count = 0;
			while (j != item_index) {
				count++;
				auto& tn = m_model->node(j);
				j = tn.next;
			}

			if (op >= 0) {
				type = query_operator_argument_type(op, count);
			} else {
				Assertion(nn.type & SEXPT_CONTAINER_DATA,
					"Unknown SEXP operator %s. Please report!", nn.text.c_str());
				const auto* p_container = get_sexp_container(nn.text.c_str());
				Assertion(p_container != nullptr,
					"Found modifier for unknown container %s. Please report!",
					nn.text.c_str());
				type = p_container->opf_type;
			}
		} else {
			if (m_mode == MODE_EVENTS)
				type = OPF_NULL;
			else
				type = OPF_BOOL;
		}

		for (j=0; j<(int)Operators.size(); j++) {
			z = query_operator_return_type(j);
			if (!sexp_query_type_match(type, z) || (Operators[j].min < 1))
				menu.EnableMenuItem(Operators[j].value | OP_INSERT_FLAG, MF_GRAYED);

			z = query_operator_argument_type(j, 0);
			if ((type == OPF_NUMBER) && (z == OPF_POSITIVE))
				z = OPF_NUMBER;

			// Goober5000's number hack
			if ((type == OPF_POSITIVE) && (z == OPF_NUMBER))
				z = OPF_POSITIVE;

			if (z != type)
				menu.EnableMenuItem(Operators[j].value | OP_INSERT_FLAG, MF_GRAYED);
		}

		// disable operators that do not have arguments available
		for (j=0; j<(int)Operators.size(); j++) {
			if (!query_default_argument_available(j)) {
				menu.EnableMenuItem(Operators[j].value | OP_INSERT_FLAG, MF_GRAYED);
			}
		}


		// disable non campaign operators if in campaign mode
		for (j=0; j<(int)Operators.size(); j++) {
			z = 0;
			if (m_mode == MODE_CAMPAIGN) {
				if (!usable_in_campaign(Operators[j].value))
					z = 1;
			}

			if (z) {
				menu.EnableMenuItem(Operators[j].value, MF_GRAYED);
				menu.EnableMenuItem(Operators[j].value | OP_REPLACE_FLAG, MF_GRAYED);
				menu.EnableMenuItem(Operators[j].value | OP_INSERT_FLAG, MF_GRAYED);
			}
		}

		if ((Sexp_clipboard > -1) && (Sexp_nodes[Sexp_clipboard].type != SEXP_NOT_USED)) {
			Assert(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_LIST);
			Assertion(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_CONTAINER_NAME,
				"Attempt to use container name %s from SEXP clipboard. Please report!",
				Sexp_nodes[Sexp_clipboard].text);

			if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_OPERATOR) {
				j = get_operator_const(CTEXT(Sexp_clipboard));
				Assert(j);
				z = query_operator_return_type(j);

				if ((z == OPR_POSITIVE) && (replace_type == OPR_NUMBER))
					z = OPR_NUMBER;

				// Goober5000's number hack
				if ((z == OPR_NUMBER) && (replace_type == OPR_POSITIVE))
					z = OPR_POSITIVE;

				if (replace_type == z)
					menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);

				z = query_operator_return_type(j);
				if ((z == OPR_POSITIVE) && (add_type == OPR_NUMBER))
					z = OPR_NUMBER;

				if (add_type == z)
					menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);

			} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_CONTAINER_DATA) {
				// TODO: check for strictly typed container keys/data
				const auto *p_container = get_sexp_container(Sexp_nodes[Sexp_clipboard].text);
				// if-check in case the container was renamed/deleted after the container data was cut/copied
				if (p_container != nullptr) {
					const auto &container = *p_container;
					if (any(container.type & ContainerType::NUMBER_DATA)) {
						// there's no way to check for OPR_POSITIVE, since the value
						// is known only in-mission, so we'll handle OPR_NUMBER only
						if (replace_type == OPR_NUMBER)
							menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);
						if (add_type == OPR_NUMBER)
							menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);
					} else if (any(container.type & ContainerType::STRING_DATA)) {
						if (replace_type == OPR_STRING && !is_container_name_opf_type(type))
							menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);
						if (add_type == OPR_STRING && !is_container_name_opf_type(type))
							menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);
					} else {
						UNREACHABLE("Unknown container data type %d", (int)container.type);
					}
				}

			} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_NUMBER) {
				if ((replace_type == OPR_POSITIVE) && (atoi(CTEXT(Sexp_clipboard)) > -1))
					menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);

				else if (replace_type == OPR_NUMBER)
					menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);

				if ((add_type == OPR_POSITIVE) && (atoi(CTEXT(Sexp_clipboard)) > -1))
					menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);

				else if (add_type == OPR_NUMBER)
					menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);

			} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_STRING) {
				if (replace_type == OPR_STRING && !is_container_name_opf_type(type))
					menu.EnableMenuItem(ID_EDIT_PASTE, MF_ENABLED);

				if (add_type == OPR_STRING && !is_container_name_opf_type(type))
					menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_ENABLED);

			} else
				Int3();  // unknown and/or invalid sexp type
		}

		if (!(menu.GetMenuState(ID_DELETE, MF_BYCOMMAND) & MF_GRAYED))
			menu.EnableMenuItem(ID_EDIT_CUT, MF_ENABLED);

		// all of the following restrictions may be revisited in the future
		if (n.type & (SEXPT_MODIFIER | SEXPT_CONTAINER_NAME)) {
			// modifiers and container names don't support cut/copy/paste
			menu.EnableMenuItem(ID_EDIT_CUT, MF_GRAYED);
			menu.EnableMenuItem(ID_EDIT_COPY, MF_GRAYED);
			menu.EnableMenuItem(ID_EDIT_PASTE, MF_GRAYED);
		}
		// can't use else-if here, because container data is a valid modifier
		if (n.type & SEXPT_CONTAINER_DATA) {
			// container data nodes don't support add-pasting modifiers
			menu.EnableMenuItem(ID_EDIT_PASTE_SPECIAL, MF_GRAYED);
		}

		gray_menu_tree(popup_menu);
		popup_menu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, this);
	}*/
}

// identify what type of argument this is.  You call it with the node of the first argument
// of an operator.  It will search through enough of the arguments to determine what type of
// data they are.
int sexp_tree::identify_arg_type(int node)
{
	int type = -1;

	while (node != -1) {
		auto& n = m_model->node(node);
		Assert(n.type & SEXPT_VALID);
		switch (SEXPT_TYPE(n.type)) {
			case SEXPT_OPERATOR:
				type = get_operator_const(n.text.c_str());
				Assert(type);
				return query_operator_return_type(type);

			case SEXPT_NUMBER:
				return OPR_NUMBER;

			case SEXPT_STRING:  // either a ship or a wing
				type = SEXP_ATOM_STRING;
				break;  // don't return, because maybe we can narrow selection down more.
		}

		node = n.next;
	}

	return type;
}

// determine if an item should be editable.  This doesn't actually edit the label.
int sexp_tree::edit_label(HTREEITEM h, bool *is_operator)
{
	if (is_operator != nullptr)
		*is_operator = false;

	int model_index = (int)GetItemData(h);

	// Check if tree root
	if (model_index < 0 || model_index >= m_model->size()) {
		if (m_mode & ST_ROOT_EDITABLE) {
			return 1;
		}

		return 0;
	}

	auto& n = m_model->node(model_index);

	// Operators are editable
	if (n.type & SEXPT_OPERATOR) {
		if (is_operator != nullptr)
			*is_operator = true;
		return 1;
	}

	// Variables and containers must be edited through dialog box
	if (n.type & (SEXPT_VARIABLE | SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA)) {
		return 0;
	}

	// Don't edit if not flaged as editable
	if (!(n.flags & EDITABLE)) {
		return 0;
	}

	// Otherwise, allow editing
	return 1;

/*
	if (n.flags & OPERAND) {
		data = n.child;
		auto& cn = model->node(data);

		SetItemText(h, n.text.c_str());
		n.flags = OPERAND;
		HTREEITEM item_handle = insert(cn.text.c_str(), cn.type, cn.flags, h);
		cn.flags = EDITABLE;
		Expand(h, TVE_EXPAND);
		SelectItem(item_handle);
		return 2;
	}
*/
}

void sexp_tree::edit_comment(HTREEITEM h)
{
	// Not implemented in the base class
}

void sexp_tree::edit_bg_color(HTREEITEM h)
{
	// Not implemented in the base class
}

// given a tree node, returns the argument type it should be.
// OPF_NULL means no value (or a "void" value) is returned.  OPF_NONE means there shouldn't be any argument at this position at all.
int sexp_tree::query_node_argument_type(int node) const
{
	auto& n = m_model->node(node);

	int parent_node = n.parent;
	if (parent_node < 0) {		// parent nodes are -1 for a top-level operator like 'when'
		return OPF_NULL;
	}

	int argnum = find_argument_number(parent_node, node);
	if (argnum < 0) {
		return OPF_NONE;
	}

	auto& parent_n = m_model->node(parent_node);
	int op_num = get_operator_index(parent_n.text.c_str());
	if (op_num < 0) {
		return OPF_NONE;
	}

	return query_operator_argument_type(op_num, argnum);
}

int sexp_tree::end_label_edit(TVITEMA &item)
{
	if (!item.pszText)
		return 0;

	HTREEITEM h = item.hItem; 
	SCP_string str(item.pszText);
	int r = 1;	
	bool update_node = true; 

	int model_index = (int)GetItemData(h);

	if (model_index < 0 || model_index >= m_model->size()) {
		if (m_mode == MODE_EVENTS) {
			// This part interacts with the dialog, not the tree, so it remains.
			Assert(Event_editor_dlg);
			Event_editor_dlg->handler(ROOT_RENAMED, model_index, str.c_str());
			return 1;
		} else {
			Int3(); // root labels shouldn't have been editable!
		}
	}

	auto& n = m_model->node(model_index);

	if (n.type & SEXPT_OPERATOR) {
		// We need a model-aware version of match_closest_operator
		auto op = match_closest_operator(str, model_index);
		if (op.empty())
			return 0;

		SetItemText(h, op.c_str());
		str = op;

		int op_num = get_operator_index(op.c_str());
		if (op_num >= 0) {
			// This function will now modify the model and rebuild the tree
			add_or_replace_operator(op_num, 1);
		}

		// Since add_or_replace_operator rebuilds the tree, we don't continue.
		return 0;
	} else if (n.type & SEXPT_NUMBER) {
		// We need a model-aware version of query_node_argument_type
		if (query_node_argument_type(model_index) == OPF_POSITIVE) {
			int val = atoi(str.c_str());
			if (val < 0) {
				MessageBox("Can not enter a negative value", "Invalid Number", MB_ICONEXCLAMATION);
				update_node = false;
			}
		}
	}

	// Error checking would not hurt here
	if (update_node) {
		*modified = 1;
		// This replaces the old strncpy into tree_nodes[node].text
		m_model->setNode(model_index, n.type, str.c_str());
	} else {
		// If the edit was invalid, revert the text in the edit control
		item.pszText = const_cast<char*>(n.text.c_str());
		return 1;
	}

	return r;
}

// Look for the valid operator that is the closest match for 'str' and return the operator
// number of it.  What operators are valid is determined by 'node', and an operator is valid
// if it is allowed to fit at position 'node'
//
const SCP_string &sexp_tree::match_closest_operator(const SCP_string &str, int node)
{
	int z, op, arg_num, opf;

	auto& n = m_model->node(node);

	z = n.parent;
	if (z < 0) {
		return str;
	}

	auto& parent_n = m_model->node(z);

	op = get_operator_index(parent_n.text.c_str());
	if (op < 0)
		return str;

	// determine which argument we are of the parent
	arg_num = find_argument_number(z, node);
	opf = query_operator_argument_type(op, arg_num);	// check argument type at this position

	// find the best operator
	int best = sexp_match_closest_operator(str, opf);
	if (best < 0)
	{
		Warning(LOCATION, "Unable to find an operator match for string '%s' and argument type %d", str.c_str(), opf);
		return str;
	}
	return Operators[best].text;
}

void sexp_tree::start_operator_edit(HTREEITEM h)
{
	if (m_operator_popup_active)
		return;

	// this can get out of sync if we add an event and then try to edit an operator in a different event
	update_item(h);

	auto& n = m_model->node(item_index);

	// sanity checks
	Assertion(item_handle == h, "Mismatch between item handle and the handle being edited!");

	// we are editing an operator, so find out which type it should be
	auto opf_type = (sexp_opf_t)query_node_argument_type(item_index);

	// do first-time setup
	if (!m_operator_popup_created)
	{
		// text metrics
		TEXTMETRIC tm;
		auto dc = GetDC();
		dc->GetTextMetrics(&tm);
		m_font_height = tm.tmHeight;
		dc->SelectObject(GetFont());
		m_font_max_width = 0;
		for (auto& op : Operators)
		{
			auto font_extent = dc->GetTextExtent(op.text.c_str());
			if (font_extent.cx > m_font_max_width)
				m_font_max_width = font_extent.cx;
		}

		// adjust for scroll bar and border edge
		m_font_max_width += GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXEDGE);
	}

	// calculate position and size of the dropdown
	RECT item_rect, dropdown_rect;
	GetItemRect(h, &item_rect, TRUE);
	dropdown_rect.top = item_rect.top;
	dropdown_rect.left = item_rect.left;
	dropdown_rect.right = dropdown_rect.left + m_font_max_width;
	dropdown_rect.bottom = dropdown_rect.top + m_font_height * 10;

	// create or just position it
	if (!m_operator_popup_created)
	{
		m_operator_box.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_SIMPLE | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED, dropdown_rect, this, IDC_SEXP_POPUP_LIST);
		m_operator_box.SetFont(GetFont());
		m_operator_popup_created = true;
	}
	else
	{
		m_operator_box.MoveWindow(&dropdown_rect);
	}

	m_operator_box.refresh_popup_operators(opf_type, n.text.c_str());

	m_operator_box.ShowWindow(SW_SHOWNORMAL);
	m_operator_box.SetFocus();
	m_operator_popup_active = true;
}

void sexp_tree::end_operator_edit(bool confirm)
{
	if (!m_operator_popup_active)
		return;

	m_operator_box.cleanup(confirm);
	m_operator_box.ShowWindow(SW_HIDE);
	m_operator_popup_active = false;
}

// this really only handles messages generated by the right click popup menu
// now it also handles messages from the operator combo box
BOOL sexp_tree::OnCommand(WPARAM wParam, LPARAM lParam)
{
	int i, z, id, data, node, op;
	sexp_list_item *list, *ptr;

	/*if ((item_index >= 0) && (item_index < total_nodes) && !tree_nodes.empty())
		item_handle = tree_nodes[item_index].handle;*/ // This is redundant as item_handle is always set when item_index is set

	id = LOWORD(wParam);
	data = HIWORD(wParam);

#ifndef NDEBUG
	// Sanity check: None of these #defines used in this function should overlap with any SEXP operators, or mysterious bugs will ensue
	// note: this won't catch operator values plus OP_REPLACE_FLAG or OP_INSERT_FLAG
	switch (id)
	{
		case ID_SEXP_TREE_ADD_VARIABLE:
		case ID_SEXP_TREE_MODIFY_VARIABLE:
		case ID_EDIT_SEXP_TREE_EDIT_CONTAINERS:
		case ID_EDIT_COPY:
		case ID_EDIT_PASTE:
		case ID_EDIT_PASTE_SPECIAL:
		case ID_SPLIT_LINE:
		case ID_EXPAND_ALL:
		case ID_EDIT_TEXT:
		case ID_EDIT_COMMENT:
		case ID_EDIT_BG_COLOR:
		case ID_REPLACE_NUMBER:
		case ID_REPLACE_STRING:
		case ID_ADD_STRING:
		case ID_ADD_NUMBER:
		case ID_EDIT_CUT:
		case ID_DELETE:
		case IDC_SEXP_POPUP_LIST:
			Assertion(id >= sexp::operator_upper_bound(), "A resource definition (%d) must not overlap with an operator value!", id);
			break;

		default:
			if ((id >= ID_VARIABLE_MENU) && (id < ID_VARIABLE_MENU + 511)
				|| (id >= ID_ADD_MENU) && (id < ID_ADD_MENU + 511)
				|| (id >= ID_REPLACE_MENU) && (id < ID_REPLACE_MENU + 511)
				|| (id >= ID_CONTAINER_NAME_MENU) && (id < ID_CONTAINER_NAME_MENU + 511)
				|| (id >= ID_CONTAINER_DATA_MENU) && (id < ID_CONTAINER_DATA_MENU + 511))
			{
				Assertion(id >= sexp::operator_upper_bound(), "A resource definition (%d) must not overlap with an operator value!", id);
			}
			break;
	}
#endif // !NDEBUG

	// Add variable
	if (id == ID_SEXP_TREE_ADD_VARIABLE) {
		CAddVariableDlg dlg;
		dlg.DoModal();

		if ( dlg.m_create ) {

			// set type
			int type;
			if ( dlg.m_type_number ) {
				type = SEXP_VARIABLE_NUMBER;
			} else {
				type = SEXP_VARIABLE_STRING;
			}

			if ( dlg.m_type_network_variable ) {
				type |= SEXP_VARIABLE_NETWORK;
			}

			if ( dlg.m_type_on_mission_progress) {
				type |= SEXP_VARIABLE_SAVE_ON_MISSION_PROGRESS;
			} else if ( dlg.m_type_on_mission_close) {
				type |= SEXP_VARIABLE_SAVE_ON_MISSION_CLOSE;
			}

			if (dlg.m_type_eternal) {
				type |= SEXP_VARIABLE_SAVE_TO_PLAYER_FILE;
			}

			// add variable
			sexp_add_variable(dlg.m_default_value, dlg.m_variable_name, type);

			// sort variable
			sexp_variable_sort();
		}
		return 1;
	}

	// Modify variable
	if (id == ID_SEXP_TREE_MODIFY_VARIABLE) {
		CModifyVariableDlg dlg;

		// get sexp_variable index for item index
		dlg.m_start_index = get_item_index_to_var_index();

		// get pointer to tree
		dlg.m_p_sexp_tree = this;

		dlg.DoModal();

		Assert( !(dlg.m_deleted && dlg.m_do_modify) );

		if (dlg.m_deleted) {
			// find index in sexp_variable list
			int sexp_var_index = get_index_sexp_variable_name(dlg.m_cur_variable_name);
			Assert(sexp_var_index != -1);

			// delete from list
			sexp_variable_delete(sexp_var_index);

			// sort list
			sexp_variable_sort();

			// delete from sexp_tree, replacing with "number" or "string" as needed
			// further error checking from add_data()
			delete_sexp_tree_variable(dlg.m_cur_variable_name);

			return 1;
		}

		if (dlg.m_do_modify) {
			// check sexp_tree -- warn on type
			// find index and change either (1) name, (2) type, (3) value
			int sexp_var_index = get_index_sexp_variable_name(dlg.m_old_var_name);
			Assert(sexp_var_index != -1);

			// save old name, since name may be modified
			char old_name[TOKEN_LENGTH];
			strcpy_s(old_name, Sexp_variables[sexp_var_index].variable_name);

			// set type
			int type;
			if (dlg.m_type_number) {
				type = SEXP_VARIABLE_NUMBER;
			} else {
				type = SEXP_VARIABLE_STRING;
			}

			if ( dlg.m_type_network_variable ) {
				type |= SEXP_VARIABLE_NETWORK;
			}

			if ( dlg.m_type_on_mission_progress) {
				type |= SEXP_VARIABLE_SAVE_ON_MISSION_PROGRESS;
			} else if ( dlg.m_type_on_mission_close) {
				type |= SEXP_VARIABLE_SAVE_ON_MISSION_CLOSE;
			}

			if (dlg.m_type_eternal) {
				type |= SEXP_VARIABLE_SAVE_TO_PLAYER_FILE;
			}

			// update sexp_variable
			sexp_fred_modify_variable(dlg.m_default_value, dlg.m_cur_variable_name, sexp_var_index, type);

			// modify sexp_tree
			modify_sexp_tree_variable(old_name, sexp_var_index);

			// Don't sort until after modify, since modify uses index
			if (dlg.m_modified_name) {
				sexp_variable_sort();
			}

			return 1;
		}

		// no change
		return 1;
	}

	// Add/Modify Container
	if (id == ID_EDIT_SEXP_TREE_EDIT_CONTAINERS) {
		CAddModifyContainerDlg dlg(*this);

		dlg.DoModal();

		bool renamed_anything = false;
		for (const auto &renamed_container : dlg.get_renamed_containers()) {
			const SCP_string &old_name = renamed_container.first;
			const SCP_string &new_name = renamed_container.second;
			if (rename_container_nodes(old_name, new_name)) {
				renamed_anything = true;
			}
		}

		if (renamed_anything) {
			*modified = 1;
		}

		return 1;
	}

	// check if REPLACE_VARIABLE_MENU
	if ( (id >= ID_VARIABLE_MENU) && (id < ID_VARIABLE_MENU + 511)) {

		Assert(item_index >= 0);

		// get index into list of type valid variables
		int var_idx = id - ID_VARIABLE_MENU;
		Assert( (var_idx >= 0) && (var_idx < MAX_SEXP_VARIABLES) );

		int type = get_type(item_handle);
		Assert( (type & SEXPT_NUMBER) || (type & SEXPT_STRING) );

		// don't do type check for modify-variable or OPF_CONTAINER_VALUE (can be either type)
		if (Modify_variable || query_node_argument_type(item_index) == OPF_CONTAINER_VALUE) {
			if (Sexp_variables[var_idx].type & SEXP_VARIABLE_NUMBER) {
				type = SEXPT_NUMBER;
			} else if (Sexp_variables[var_idx].type & SEXP_VARIABLE_STRING) {
				type = SEXPT_STRING;
			} else {
				Int3();	// unknown type
			}

		} else {	
			// verify type in tree is same as type in Sexp_variables array
			if (type & SEXPT_NUMBER) {
				Assert(Sexp_variables[var_idx].type & SEXP_VARIABLE_NUMBER);
			}

			if (type & SEXPT_STRING) {
				Assert( (Sexp_variables[var_idx].type & SEXP_VARIABLE_STRING) );
			}
		}

		// Replace data
		replace_variable_data(var_idx, (type | SEXPT_VARIABLE));

		return 1;
	}


	if ((id >= ID_ADD_MENU) && (id < ID_ADD_MENU + 511)) {
		auto saved_id = id;
		Assert(item_index >= 0);

		int type = 0;

		auto& n = m_model->node(item_index);

		if (n.type & SEXPT_CONTAINER_DATA) {
			list = get_container_multidim_modifiers(item_index);
		} else {
			op = get_operator_index(n.text.c_str());
			Assert(op >= 0);

			type = query_operator_argument_type(op, Add_count);
			list = get_listing_opf(type, item_index, Add_count);
		}
		Assert(list);

		id -= ID_ADD_MENU;
		ptr = list;
		while (id) {
			id--;
			ptr = ptr->next;
			Assert(ptr);
		}

		Assert((SEXPT_TYPE(ptr->type) != SEXPT_OPERATOR) && (ptr->op < 0));
		expand_operator(item_index);
		auto n_handle = add_data(ptr->text.c_str(), ptr->type);
		node = (int)GetItemData(n_handle);
		list->destroy();

		// bolted-on ugly hack
		if (type == OPF_VARIABLE_NAME) {
			auto var_idx = saved_id - ID_ADD_MENU;
			auto saved_item_index = item_index;

			if (Sexp_variables[var_idx].type & SEXP_VARIABLE_NUMBER) {
				type = SEXPT_NUMBER;
			}
			else if (Sexp_variables[var_idx].type & SEXP_VARIABLE_STRING) {
				type = SEXPT_STRING;
			}
			else {
				UNREACHABLE("Unknown sexp variable type");
			}

			update_item(node);
			replace_variable_data(var_idx, (type | SEXPT_VARIABLE));
			update_item(saved_item_index);
		}

		return 1;
	}

	if ((id >= ID_REPLACE_MENU) && (id < ID_REPLACE_MENU + 511)) {
		Assert(item_index >= 0);

		auto& n = m_model->node(item_index);

		Assert(n.parent >= 0);

		if (n.type & SEXPT_MODIFIER) {
			list = get_container_modifiers(n.parent);
		} else {
			auto& parent_n = m_model->node(n.parent);
			op = get_operator_index(parent_n.text.c_str());
			Assert(op >= 0);

			auto type = query_operator_argument_type(op, Replace_count); // check argument type at this position
			list = get_listing_opf(type, n.parent, Replace_count);
		}
		Assert(list);

		id -= ID_REPLACE_MENU;
		ptr = list;
		while (id) {
			id--;
			ptr = ptr->next;
			Assert(ptr);
		}

		Assert((SEXPT_TYPE(ptr->type) != SEXPT_OPERATOR) && (ptr->op < 0));
		expand_operator(item_index);
		replace_data(ptr->text.c_str(), ptr->type);
		list->destroy();
		return 1;
	}

	if ((id >= ID_CONTAINER_NAME_MENU) && (id < ID_CONTAINER_NAME_MENU + 511)) {
		Assertion(item_index >= 0, "Attempt to Replace Container Name with no node selected. Please report!");

		auto& n = m_model->node(item_index);

		const auto &containers = get_all_sexp_containers();
		const int container_index = id - ID_CONTAINER_NAME_MENU;
		Assertion((container_index >= 0) && (container_index < (int)containers.size()),
			"Unknown Container Index %d. Please report!",
			container_index);

		const int type = get_type(item_handle);
		Assertion(type & SEXPT_STRING,
			"Attempt to replace container name on non-string node %s with type %d. Please report!",
			n.text.c_str(),
			type);

		replace_container_name(containers[container_index]);
	}

	if ((id >= ID_CONTAINER_DATA_MENU) && (id < ID_CONTAINER_DATA_MENU + 511)) {
		Assertion(item_index >= 0, "Attempt to Replace Container Data with no node selected. Please report!");

		const auto &containers = get_all_sexp_containers();
		const int container_index = id - ID_CONTAINER_DATA_MENU;
		Assertion((container_index >= 0) && (container_index < (int)containers.size()),
			"Unknown Container Index %d. Please report!",
			container_index);

		int type = get_type(item_handle);
		Assertion((type & SEXPT_NUMBER) || (type & SEXPT_STRING),
			"Attempt to use Replace Container Data on a non-data node. Please report!");

		// variable/container name don't mix with container data
		// DISCUSSME: what about variable name as SEXP arg type?
		type &= ~(SEXPT_VARIABLE | SEXPT_CONTAINER_NAME);
		replace_container_data(containers[container_index], (type | SEXPT_CONTAINER_DATA), true, true, true);

		expand_branch(item_handle);
	}

	for (op=0; op<(int)Operators.size(); op++) {
		if (id == Operators[op].value) {
			add_or_replace_operator(op);
			return 1;
		}

		if (id == (Operators[op].value | OP_REPLACE_FLAG)) {
			add_or_replace_operator(op, 1);
			expand_branch(item_handle); 
			return 1;
		}

		if (id == (Operators[op].value | OP_INSERT_FLAG)) {
			int flags;

			auto& n = m_model->node(item_index);

			z = n.parent;
			flags = n.flags;
			node = m_model->allocateNode(z, item_index);
			m_model->setNode(node, (SEXPT_OPERATOR | SEXPT_VALID), Operators[op].text.c_str());
			auto& new_n = m_model->node(node);
			new_n.flags = flags;

			HTREEITEM parent_handle = GetParentItem(item_handle);
			if (parent_handle == NULL) {
				// This handles the special cases for top-level Goals/Events
				if (m_mode == MODE_GOALS || m_mode == MODE_EVENTS || m_mode == MODE_CAMPAIGN) {
					// This part remains the same as it interacts with the dialog, not the tree data.
					if (m_mode == MODE_GOALS) {
						Goal_editor_dlg->insert_handler(item_index, node);
					} else if (m_mode == MODE_EVENTS) {
						Event_editor_dlg->insert_handler(item_index, node);
					} else if (m_mode == MODE_CAMPAIGN) {
						Campaign_tree_formp->insert_handler(item_index, node);
					}
				} else {
					parent_handle = TVI_ROOT;
					root_item = node;
				}
			}

			//HTREEITEM insert_before_handle = item_handle;
			HTREEITEM new_item_handle = insert(Operators[op].text.c_str(), BITMAP_OPERATOR, BITMAP_OPERATOR, parent_handle, TVI_FIRST);
			SetItemData(new_item_handle, node);
			m_modelToHandle[node] = new_item_handle;

			update_item(node);

			move_branch(item_index, node);

			for (i=1; i<Operators[op].min; i++)
				add_default_operator(op, i);

			Expand(item_handle, TVE_EXPAND);
			*modified = 1;
			return 1;
		}
	}

	auto& n = m_model->node(item_index);
	switch (id) {
		case ID_EDIT_COPY:
			NodeCopy();
			return 1;

		case ID_EDIT_PASTE:
			NodeReplacePaste();
			return 1;

		case ID_EDIT_PASTE_SPECIAL:  // add paste, instead of replace.
			NodeAddPaste();
			return 1;

/*		case ID_SPLIT_LINE:
			if ((n.flags & OPERAND) && (n.flags & EDITABLE))  // expandable?
				expand_operator(item_index);
			else
				merge_operator(item_index);

			return 1;*/

		case ID_EXPAND_ALL:
			expand_branch(item_handle);
			return 1;

		case ID_EDIT_TEXT:
			if (edit_label(item_handle)) {
				*modified = 1;
				EditLabel(item_handle);
			}
			return 1;

		case ID_EDIT_COMMENT:
			edit_comment(item_handle);
			return 1;

		case ID_EDIT_BG_COLOR:
			edit_bg_color(item_handle);
			return 1;

		case ID_REPLACE_NUMBER:
			expand_operator(item_index);
			if (n.type & SEXPT_MODIFIER) {
				replace_data("number", (SEXPT_NUMBER | SEXPT_MODIFIER | SEXPT_VALID));
			} else {
				replace_data("number", (SEXPT_NUMBER | SEXPT_VALID));
			}
			EditLabel(item_handle);
			return 1;
	
		case ID_REPLACE_STRING:
			expand_operator(item_index);
			if (n.type & SEXPT_MODIFIER) {
				replace_data("string", (SEXPT_STRING | SEXPT_MODIFIER | SEXPT_VALID));
			} else {
				replace_data("string", (SEXPT_STRING | SEXPT_VALID));
			}
			EditLabel(item_handle);
			return 1;

		case ID_ADD_STRING:	{
			HTREEITEM theNode;

			if (n.type & SEXPT_CONTAINER_DATA) {
				theNode = add_data("string", (SEXPT_STRING | SEXPT_MODIFIER | SEXPT_VALID));
			} else {
				theNode = add_data("string", (SEXPT_STRING | SEXPT_VALID));
			}
			EditLabel(theNode);
			return 1;
		}

		case ID_ADD_NUMBER:	{
			HTREEITEM theNode;

			if (n.type & SEXPT_CONTAINER_DATA) {
				theNode = add_data("number", (SEXPT_NUMBER | SEXPT_MODIFIER | SEXPT_VALID));
			} else {
				theNode = add_data("number", (SEXPT_NUMBER | SEXPT_VALID));
			}
			EditLabel(theNode);
			return 1;
		}

		case ID_EDIT_CUT:
			NodeCut();
			return 1;

		case ID_DELETE:
			NodeDelete();
			return 1;

		case IDC_SEXP_POPUP_LIST:
		{
			bool command_handled = false;

			switch (data)
			{
				case CBN_SELCHANGE:
				{
					int index = m_operator_box.GetCurSel();
					if (index >= 0)
					{
						if (m_operator_box.IsItemEnabled(index))
						{
							op = m_operator_box.GetOpIndex(index);

							// close the popup
							end_operator_edit(true);

							// do the operator replacement
							add_or_replace_operator(op, 1);
							expand_branch(item_handle);
						}
						// if the selected item wasn't enabled, do nothing
					}
					else
						end_operator_edit(false);

					command_handled = true;
					break;
				}

				case CBN_KILLFOCUS:
				{
					if (m_operator_popup_active && m_operator_box.PressedEnter())
					{
						op = m_operator_box.GetOpIndex(-1);

						// close the popup
						end_operator_edit(true);

						// do the operator replacement
						add_or_replace_operator(op, 1);
						expand_branch(item_handle);
					}
					else
						end_operator_edit(false);

					command_handled = true;
					break;
				}

				default:
					break;
			}

			if (command_handled)
				return TRUE;
		}
	}
	
	return CTreeCtrl::OnCommand(wParam, lParam);
}

void sexp_tree::NodeCut()
{
	if (item_index < 0)
		return;

	NodeCopy();
	NodeDelete();
}

void sexp_tree::NodeDelete()
{
	int parent, theNode;
	HTREEITEM h_parent;

	if ((m_mode & ST_ROOT_DELETABLE) && (item_index == -1)) {
		item_index = (int)GetItemData(item_handle);
		if (m_mode == MODE_GOALS) {
			Assert(Goal_editor_dlg);
			theNode = Goal_editor_dlg->handler(ROOT_DELETED, item_index);

		}else if (m_mode == MODE_CUTSCENES) {
			Assert(Cutscene_editor_dlg);
			theNode = Cutscene_editor_dlg->handler(ROOT_DELETED, item_index);

		} else if (m_mode == MODE_EVENTS) {
			Assert(Event_editor_dlg);
			theNode = Event_editor_dlg->handler(ROOT_DELETED, item_index);

		} else {
			Assert(m_mode == MODE_CAMPAIGN);
			theNode = Campaign_tree_formp->handler(ROOT_DELETED, item_index);
		}

		m_model->freeNode(theNode);
		DeleteItem(item_handle);
		*modified = 1;
		return;
	}

	auto& n = m_model->node(item_index);
	h_parent = GetParentItem(item_handle);
	parent = n.parent;

	// can't delete the root node
	if (parent < 0)
		return;

	Assert(parent != -1 && item_handle == h_parent);
	m_model->freeNode(item_index);
	DeleteItem(item_handle);

	//auto& parent_n = m_model->node(parent);
	theNode = n.child;
/*			if (node != -1 && n.next == -1 && n.child == -1) {
		sprintf(buf, "%s %s", parent_n.text.c_str(), n.text.c_str());
		SetItem(h_parent, TVIF_TEXT, buf, 0, 0, 0, 0, 0);
		parent_n.flags = OPERAND | EDITABLE;
		n.flags = COMBINED;
		DeleteItem(tree_nodes[node].handle???);
	}*/

	*modified = 1;
}

// TODO move the clipboard into the model
void sexp_tree::NodeCopy()
{
	if (item_index < 0)
		return;

	// If a clipboard already exist, unmark it as persistent and free old clipboard
	if (Sexp_clipboard != -1) {
		sexp_unmark_persistent(Sexp_clipboard);
		free_sexp2(Sexp_clipboard);
	}

	// Allocate new clipboard and mark persistent
	Sexp_clipboard = save_branch(item_index, 1);
	sexp_mark_persistent(Sexp_clipboard);
}

void sexp_tree::NodeReplacePaste()
{
	if (item_index < 0 || Sexp_clipboard < 0)
		return;

	int i;

	// the following assumptions are made..
	Assert(Sexp_nodes[Sexp_clipboard].type != SEXP_NOT_USED);
	Assert(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_LIST);
	Assertion(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_CONTAINER_NAME,
		"Attempt to use container name %s from SEXP clipboard. Please report!",
		Sexp_nodes[Sexp_clipboard].text);

	auto& n = m_model->node(item_index);

	if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_OPERATOR) {
		expand_operator(item_index);
		replace_operator(CTEXT(Sexp_clipboard));
		if (Sexp_nodes[Sexp_clipboard].rest != -1) {
			load_branch(Sexp_nodes[Sexp_clipboard].rest, item_index);
			i = n.child;
			while (i != -1) {
				add_sub_tree(i, item_handle);
				auto& child_n = m_model->node(i);
				i = child_n.next;
			}
		}

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_CONTAINER_DATA) {
		expand_operator(item_index);
		const auto *p_container = get_sexp_container(Sexp_nodes[Sexp_clipboard].text);
		Assertion(p_container,
			"Attempt to paste unknown container %s. Please report!",
			Sexp_nodes[Sexp_clipboard].text);
		const auto &container = *p_container;
		// this should always be true, but just in case
		const bool has_modifiers = (Sexp_nodes[Sexp_clipboard].first != -1);
		int new_type = n.type & ~(SEXPT_VARIABLE | SEXPT_CONTAINER_NAME) | SEXPT_CONTAINER_DATA;
		replace_container_data(container, new_type, false, true, !has_modifiers);
		if (has_modifiers) {
			load_branch(Sexp_nodes[Sexp_clipboard].first, item_index);
			i = n.child;
			while (i != -1) {
				add_sub_tree(i, item_handle);
				auto& child_n = m_model->node(i);
				i = child_n.next;
			}
		} else {
			add_default_modifier(container);
		}

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_NUMBER) {
		Assert(Sexp_nodes[Sexp_clipboard].rest == -1);
		if (Sexp_nodes[Sexp_clipboard].type & SEXP_FLAG_VARIABLE) {
			int var_idx = get_index_sexp_variable_name(Sexp_nodes[Sexp_clipboard].text);
			Assert(var_idx > -1);
			replace_variable_data(var_idx, (SEXPT_VARIABLE | SEXPT_NUMBER | SEXPT_VALID));
		}
		else {
			expand_operator(item_index);
			replace_data(CTEXT(Sexp_clipboard), (SEXPT_NUMBER | SEXPT_VALID));
		}

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_STRING) {
		Assert(Sexp_nodes[Sexp_clipboard].rest == -1);
		if (Sexp_nodes[Sexp_clipboard].type & SEXP_FLAG_VARIABLE) {
			int var_idx = get_index_sexp_variable_name(Sexp_nodes[Sexp_clipboard].text);
			Assert(var_idx > -1);
			replace_variable_data(var_idx, (SEXPT_VARIABLE | SEXPT_STRING | SEXPT_VALID));
		}
		else {
			expand_operator(item_index);
			replace_data(CTEXT(Sexp_clipboard), (SEXPT_STRING | SEXPT_VALID));
		}

	} else
		Assert(0);  // unknown and/or invalid sexp type

	expand_branch(item_handle);
}

void sexp_tree::NodeAddPaste()
{
	if (item_index < 0 || Sexp_clipboard < 0)
		return;

	int i;

	// the following assumptions are made..
	Assert(Sexp_nodes[Sexp_clipboard].type != SEXP_NOT_USED);
	Assert(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_LIST);
	Assertion(Sexp_nodes[Sexp_clipboard].subtype != SEXP_ATOM_CONTAINER_NAME,
		"Attempt to use container name %s from SEXP clipboard. Please report!",
		Sexp_nodes[Sexp_clipboard].text);

	auto& n = m_model->node(item_index);

	if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_OPERATOR) {
		expand_operator(item_index);
		add_operator(CTEXT(Sexp_clipboard));
		if (Sexp_nodes[Sexp_clipboard].rest != -1) {
			load_branch(Sexp_nodes[Sexp_clipboard].rest, item_index);
			i = n.child;
			while (i != -1) {
				add_sub_tree(i, item_handle);
				auto& child_n = m_model->node(i);
				i = child_n.next;
			}
		}

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_CONTAINER_DATA) {
		expand_operator(item_index);
		add_container_data(Sexp_nodes[Sexp_clipboard].text);
		const int modifier_node = Sexp_nodes[Sexp_clipboard].first;
		if (modifier_node != -1) {
			load_branch(modifier_node, item_index);
			i = n.child;
			while (i != -1) {
				add_sub_tree(i, item_handle);
				auto& child_n = m_model->node(i);
				i = child_n.next;
			}
		} else {
			// this shouldn't happen, but just in case
			const auto *p_container = get_sexp_container(Sexp_nodes[Sexp_clipboard].text);
			Assertion(p_container,
				"Attempt to add-paste unknown container %s. Please report!",
				Sexp_nodes[Sexp_clipboard].text);
			add_default_modifier(*p_container);
		}

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_NUMBER) {
		Assert(Sexp_nodes[Sexp_clipboard].rest == -1);
		expand_operator(item_index);
		add_data(CTEXT(Sexp_clipboard), (SEXPT_NUMBER | SEXPT_VALID));

	} else if (Sexp_nodes[Sexp_clipboard].subtype == SEXP_ATOM_STRING) {
		Assert(Sexp_nodes[Sexp_clipboard].rest == -1);
		expand_operator(item_index);
		add_data(CTEXT(Sexp_clipboard), (SEXPT_STRING | SEXPT_VALID));

	} else
		Assert(0);  // unknown and/or invalid sexp type

	expand_branch(item_handle);
}

// adds to or replaces (based on passed in flag) the current operator
void sexp_tree::add_or_replace_operator(int op, int replace_flag)
{
	int i, op_index, op2;

	auto& n = m_model->node(item_index);

	op_index = item_index;
	if (replace_flag) {
		if (n.type & SEXPT_OPERATOR) {  // are both operators?
			op2 = get_operator_index(n.text.c_str());
			Assert(op2 >= 0);
			i = m_model->countArgs(n.child);
			if ((i >= Operators[op].min) && (i <= Operators[op].max)) {  // are old num args valid?
				while (i--)
					if (query_operator_argument_type(op2, i) != query_operator_argument_type(op, i))  // does each arg match expected type?
						break;

				if (i < 0) {  // everything is ok, so we can keep old arguments with new operator
					m_model->setNode(item_index, (SEXPT_OPERATOR | SEXPT_VALID), Operators[op].text.c_str());
					SetItemText(item_handle, Operators[op].text.c_str());
					n.flags = OPERAND;
					return;
				}
			}
		}

		replace_operator(Operators[op].text.c_str());

	} else
		add_operator(Operators[op].text.c_str());

	// fill in all the required (minimum) arguments with default values
	for (i=0; i<Operators[op].min; i++)
		add_default_operator(op, i);

	Expand(item_handle, TVE_EXPAND);
}

// initialize node, type operator
//
void sexp_list_item::set_op(int op_num)
{
	int i;

	if (op_num >= FIRST_OP) {  // do we have an op value instead of an op number (index)?
		for (i=0; i<(int)Operators.size(); i++)
			if (op_num == Operators[i].value)
				op_num = i;  // convert op value to op number
	}

	op = op_num;
	text = Operators[op].text;
	type = (SEXPT_OPERATOR | SEXPT_VALID);
}

// initialize node, type data
// Defaults: t = SEXPT_STRING
//
void sexp_list_item::set_data(const char *str, int t)
{
	op = -1;
	text = str;
	type = t;
}

// add a node to end of list
//
void sexp_list_item::add_op(int op_num)
{
	sexp_list_item *item, *ptr;

	item = new sexp_list_item;
	ptr = this;
	while (ptr->next)
		ptr = ptr->next;

	ptr->next = item;
	item->set_op(op_num);
}

// add a node to end of list
// Defaults: t = SEXPT_STRING
//
void sexp_list_item::add_data(const char *str, int t)
{
	sexp_list_item *item, *ptr;

	item = new sexp_list_item;
	ptr = this;
	while (ptr->next)
		ptr = ptr->next;

	ptr->next = item;
	item->set_data(str, t);
}

// add an sexp list to end of another list (join lists)
//
void sexp_list_item::add_list(sexp_list_item *list)
{
	sexp_list_item *ptr;

	ptr = this;
	while (ptr->next)
		ptr = ptr->next;

	ptr->next = list;
}

// free all nodes of list
//
void sexp_list_item::destroy()
{
	sexp_list_item *ptr, *ptr2;

	ptr = this;
	while (ptr) {
		ptr2 = ptr->next;

		delete ptr;
		ptr = ptr2;
	}
}

int sexp_tree::add_default_operator(int op_index, int argnum)
{
	char buf[256];
	int index;
	sexp_list_item item;
	HTREEITEM h;

	h = item_handle;
	index = item_index;
	if (get_default_value(&item, buf, op_index, argnum))
		return -1;

	if (item.type & SEXPT_OPERATOR) {
		Assert((item.op >= 0) && (item.op < (int)Operators.size()));
		add_or_replace_operator(item.op);
		item_index = index;
		item_handle = h;

	} else {
		// special case for sexps that take variables
		const int op_type = query_operator_argument_type(op_index, argnum);
		if (op_type == OPF_VARIABLE_NAME) {
			int sexp_var_index = get_index_sexp_variable_name(item.text);
			Assert(sexp_var_index != -1);
			int type = SEXPT_VALID | SEXPT_VARIABLE;
			if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_STRING) {
				type |= SEXPT_STRING;
			} else if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NUMBER) {
				type |= SEXPT_NUMBER;
			} else {
				Int3();
			}

			char node_text[2*TOKEN_LENGTH + 2];
			sprintf(node_text, "%s(%s)", item.text.c_str(), Sexp_variables[sexp_var_index].text);
			add_variable_data(node_text, type);
		}
		else if (item.type & SEXPT_CONTAINER_NAME) {
			Assertion(is_container_name_opf_type(op_type) || op_type == OPF_DATA_OR_STR_CONTAINER,
				"Attempt to add default container name for a node of non-container type (%d). Please report!",
				op_type);
			add_container_name(item.text.c_str());
		}
		// modify-variable data type depends on type of variable being modified
		// (we know this block is handling the second argument since it's not OPF_VARIABLE_NAME)
		else if (Operators[op_index].value == OP_MODIFY_VARIABLE) {
			// the the variable name
			char buf2[256];
			Assert(argnum == 1);
			sexp_list_item temp_item;
			get_default_value(&temp_item, buf2, op_index, 0);
			int sexp_var_index = get_index_sexp_variable_name(temp_item.text);
			Assert(sexp_var_index != -1);

			// from name get type
			int temp_type = Sexp_variables[sexp_var_index].type;
			int type = 0;
			if (temp_type & SEXP_VARIABLE_NUMBER) {
				type = SEXPT_VALID | SEXPT_NUMBER;
			} else if (temp_type & SEXP_VARIABLE_STRING) {
				type = SEXPT_VALID | SEXPT_STRING;
			} else {
				Int3();
			}
			add_data(item.text.c_str(), type);
		}
		// all other sexps and parameters
		else {
			add_data(item.text.c_str(), item.type);
		}
	}

	return 0;
}

int sexp_tree::get_default_value(sexp_list_item *item, char *text_buf, int op, int i)
{
	const char *str = NULL;
	int type, index;
	sexp_list_item *list;
	HTREEITEM h;

	h = item_handle;
	index = item_index;
	type = query_operator_argument_type(op, i);
	switch (type)
	{
		case OPF_NULL:
			item->set_op(OP_NOP);
			return 0;

		case OPF_BOOL:
			item->set_op(OP_TRUE);
			return 0;

		case OPF_ANYTHING:
			if (Operators[op].value == OP_INVALIDATE_ARGUMENT || Operators[op].value == OP_VALIDATE_ARGUMENT)
				item->set_data(SEXP_ARGUMENT_STRING);	// this is almost always what you want for these sexps
			else
				item->set_data("<any data>");
			return 0;

		case OPF_DATA_OR_STR_CONTAINER:
			item->set_data("<any data or string container>");
			return 0;

		case OPF_NUMBER:
		case OPF_POSITIVE:
		case OPF_AMBIGUOUS:
			// if the top level operators is an AI goal, and we are adding the last number required,
			// assume that this number is a priority and make it 89 instead of 1.
			if ((query_operator_return_type(op) == OPR_AI_GOAL) && (i == (Operators[op].min - 1)))
			{
				item->set_data("89", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (((Operators[op].value == OP_HAS_DOCKED_DELAY) || (Operators[op].value == OP_HAS_UNDOCKED_DELAY) || (Operators[op].value == OP_TIME_DOCKED) || (Operators[op].value == OP_TIME_UNDOCKED)) && (i == 2))
			{
				item->set_data("1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if ((Operators[op].value == OP_SHIP_TYPE_DESTROYED) || (Operators[op].value == OP_GOOD_SECONDARY_TIME))
			{
				item->set_data("100", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_SET_SUPPORT_SHIP)
			{
				item->set_data("-1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if ( (Operators[op].value == OP_SHIP_TAG) && (i == 1) || (Operators[op].value == OP_TRIGGER_SUBMODEL_ANIMATION) && (i == 3) )
			{
				item->set_data("1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_EXPLOSION_EFFECT)
			{
				int temp;
				char sexp_str_token[TOKEN_LENGTH];

				switch (i)
				{
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
						temp = (int)EMP_DEFAULT_INTENSITY;
						break;
					case 12:
						temp = (int)EMP_DEFAULT_TIME;
						break;
					default:
						temp = 0;
						break;
				}

				sprintf(sexp_str_token, "%d", temp);
				item->set_data(sexp_str_token, (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_WARP_EFFECT)
			{
				int temp;
				char sexp_str_token[TOKEN_LENGTH];

				switch (i)
				{
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

				sprintf(sexp_str_token, "%d", temp);
				item->set_data(sexp_str_token, (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_CHANGE_BACKGROUND)
			{
				item->set_data("1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_ADD_BACKGROUND_BITMAP || Operators[op].value == OP_ADD_BACKGROUND_BITMAP_NEW)
			{
				int temp = 0;
				char sexp_str_token[TOKEN_LENGTH];

				switch (i)
				{
					case 4:
					case 5:
						temp = 100;
						break;

					case 6:
					case 7:
						temp = 1;
						break;
				}

				sprintf(sexp_str_token, "%d", temp);
				item->set_data(sexp_str_token, (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_ADD_SUN_BITMAP || Operators[op].value == OP_ADD_SUN_BITMAP_NEW)
			{
				int temp = 0;
				char sexp_str_token[TOKEN_LENGTH];

				if (i==4)
					temp = 100;

				sprintf(sexp_str_token, "%d", temp);
				item->set_data(sexp_str_token, (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_MISSION_SET_NEBULA)
			{
				if (i == 0)
					item->set_data("1", (SEXPT_NUMBER | SEXPT_VALID));
				else
					item->set_data("3000", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_MODIFY_VARIABLE)
			{
				if (get_modify_variable_type(index) == OPF_NUMBER)
					item->set_data("0", (SEXPT_NUMBER | SEXPT_VALID));
				else
					item->set_data("<any data>", (SEXPT_STRING | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_MODIFY_VARIABLE_XSTR)
			{
				if (i == 1)
					item->set_data("<any data>", (SEXPT_STRING | SEXPT_VALID));
				else
					item->set_data("-1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_SET_VARIABLE_BY_INDEX)
			{
				if (i == 0)
					item->set_data("0", (SEXPT_NUMBER | SEXPT_VALID));
				else
					item->set_data("<any data>", (SEXPT_STRING | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_JETTISON_CARGO_NEW)
			{
				item->set_data("25", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else if (Operators[op].value == OP_TECH_ADD_INTEL_XSTR || Operators[op].value == OP_TECH_REMOVE_INTEL_XSTR)
			{
				item->set_data("-1", (SEXPT_NUMBER | SEXPT_VALID));
			}
			else
			{
				item->set_data("0", (SEXPT_NUMBER | SEXPT_VALID));
			}

			return 0;

		// Goober5000 - special cases that used to be numbers but are now hybrids
		case OPF_GAME_SND:
		{
			gamesnd_id sound_index;

			if ((Operators[op].value == OP_EXPLOSION_EFFECT))
			{
				sound_index = GameSounds::SHIP_EXPLODE_1;
			}
			else if ((Operators[op].value == OP_WARP_EFFECT))
			{
				sound_index = (i == 8) ? GameSounds::CAPITAL_WARP_IN : GameSounds::CAPITAL_WARP_OUT;
			}

			if (sound_index.isValid())
			{
				game_snd *snd = gamesnd_get_game_sound(sound_index);
				if (can_construe_as_integer(snd->name.c_str()))
					item->set_data(snd->name.c_str(), (SEXPT_NUMBER | SEXPT_VALID));
				else
					item->set_data(snd->name.c_str(), (SEXPT_STRING | SEXPT_VALID));
				return 0;
			}

			// if no hardcoded default, just use the listing default
			break;
		}

		// Goober5000 - ditto
		case OPF_FIREBALL:
		{
			int fireball_index = -1;

			if (Operators[op].value == OP_EXPLOSION_EFFECT)
			{
				fireball_index = FIREBALL_MEDIUM_EXPLOSION;
			}
			else if (Operators[op].value == OP_WARP_EFFECT)
			{
				fireball_index = FIREBALL_WARP;
			}

			if (fireball_index >= 0)
			{
				char *unique_id = Fireball_info[fireball_index].unique_id;
				if (strlen(unique_id) > 0)
					item->set_data(unique_id, (SEXPT_STRING | SEXPT_VALID));
				else
				{
					char num_str[NAME_LENGTH];
					sprintf(num_str, "%d", fireball_index);
					item->set_data(num_str, (SEXPT_NUMBER | SEXPT_VALID));
				}
				return 0;
			}

			// if no hardcoded default, just use the listing default
			break;
		}

		// new default value
		case OPF_PRIORITY:
			item->set_data("Normal", (SEXPT_STRING | SEXPT_VALID));
			return 0;
	}

	list = get_listing_opf(type, index, i);

	// Goober5000 - the way this is done is really stupid, so stupid hacks are needed to deal with it
	// this particular hack is necessary because the argument string should never be a default
	if (list && list->text == SEXP_ARGUMENT_STRING)
	{
		sexp_list_item *first_ptr;

		first_ptr = list;
		list = list->next;

		delete first_ptr;
	}

	if (list)
	{
		// copy the information from the list to the passed-in item
		*item = *list;

		// but use the provided text buffer
		strcpy(text_buf, list->text.c_str());
		item->text = text_buf;

		// get rid of the list, since we're done with it
		list->destroy();
		item->next = NULL;

		return 0;
	}

	// catch anything that doesn't have a default value.  Just describe what should be here instead
	switch (type)
	{
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
			//str = "<any allied>";
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
			str = const_cast<char*>(font::FontManager::getFont(0)->getName().c_str());
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

	item->set_data(str, (SEXPT_STRING | SEXPT_VALID));
	return 0;
}

int sexp_tree::query_default_argument_available(int op)
{
	int i;

	Assert(op >= 0);
	for (i=0; i<Operators[op].min; i++)
		if (!query_default_argument_available(op, i))
			return 0;

	return 1;
}

int sexp_tree::query_default_argument_available(int op, int i)
{	
	int j, type;
	object *ptr;

	type = query_operator_argument_type(op, i);
	switch (type) {
		case OPF_NONE:
		case OPF_NULL:
		case OPF_BOOL:
		case OPF_NUMBER:
		case OPF_POSITIVE:
		case OPF_IFF:
		case OPF_AI_CLASS:
		case OPF_WHO_FROM:
		case OPF_PRIORITY:
		case OPF_SHIP_TYPE:
		case OPF_SUBSYSTEM:		
		case OPF_AWACS_SUBSYSTEM:
		case OPF_ROTATING_SUBSYSTEM:
		case OPF_TRANSLATING_SUBSYSTEM:
		case OPF_SUBSYSTEM_TYPE:
		case OPF_DOCKER_POINT:
		case OPF_DOCKEE_POINT:
		case OPF_AI_GOAL:
		case OPF_KEYPRESS:
		case OPF_AI_ORDER:
		case OPF_SKILL_LEVEL:
		case OPF_MEDAL_NAME:
		case OPF_WEAPON_NAME:
		case OPF_INTEL_NAME:
		case OPF_SHIP_CLASS_NAME:
		case OPF_HUGE_WEAPON:
		case OPF_JUMP_NODE_NAME:
		case OPF_AMBIGUOUS:
		case OPF_CARGO:
		case OPF_ARRIVAL_LOCATION:
		case OPF_DEPARTURE_LOCATION:
		case OPF_ARRIVAL_ANCHOR_ALL:
		case OPF_SUPPORT_SHIP_CLASS:
		case OPF_SHIP_WITH_BAY:
		case OPF_SOUNDTRACK_NAME:
		case OPF_STRING:
		case OPF_FLEXIBLE_ARGUMENT:
		case OPF_ANYTHING:
		case OPF_DATA_OR_STR_CONTAINER:
		case OPF_SKYBOX_MODEL_NAME:
		case OPF_SKYBOX_FLAGS:
		case OPF_SHIP_OR_NONE:
		case OPF_SUBSYSTEM_OR_NONE:
		case OPF_SHIP_WING_POINT_OR_NONE:
		case OPF_SUBSYS_OR_GENERIC:
		case OPF_BACKGROUND_BITMAP:
		case OPF_SUN_BITMAP:
		case OPF_NEBULA_STORM_TYPE:
		case OPF_NEBULA_POOF:
		case OPF_TURRET_TARGET_ORDER:
		case OPF_TURRET_TYPE:
		case OPF_POST_EFFECT:
		case OPF_TARGET_PRIORITIES:
		case OPF_ARMOR_TYPE:
		case OPF_DAMAGE_TYPE:
		case OPF_FONT:
		case OPF_HUD_ELEMENT:
		case OPF_SOUND_ENVIRONMENT:
		case OPF_SOUND_ENVIRONMENT_OPTION:
		case OPF_EXPLOSION_OPTION:
		case OPF_AUDIO_VOLUME_OPTION:
		case OPF_WEAPON_BANK_NUMBER:
		case OPF_MESSAGE_OR_STRING:
		case OPF_BUILTIN_HUD_GAUGE:
		case OPF_CUSTOM_HUD_GAUGE:
		case OPF_ANY_HUD_GAUGE:
		case OPF_SHIP_EFFECT:
		case OPF_ANIMATION_TYPE:
		case OPF_SHIP_FLAG:
		case OPF_WING_FLAG:
		case OPF_NEBULA_PATTERN:
		case OPF_NAV_POINT:
		case OPF_TEAM_COLOR:
		case OPF_GAME_SND:
		case OPF_FIREBALL:
		case OPF_SPECIES:
		case OPF_LANGUAGE:
		case OPF_FUNCTIONAL_WHEN_EVAL_TYPE:
		case OPF_ANIMATION_NAME:
		case OPF_CONTAINER_VALUE:
		case OPF_WING_FORMATION:
		case OPF_CHILD_LUA_ENUM:
		case OPF_MESSAGE_TYPE:
			return 1;

		case OPF_SHIP:
		case OPF_SHIP_WING:
		case OPF_SHIP_POINT:
		case OPF_SHIP_WING_POINT:
		case OPF_SHIP_WING_WHOLETEAM:
		case OPF_SHIP_WING_SHIPONTEAM_POINT:
			ptr = GET_FIRST(&obj_used_list);
			while (ptr != END_OF_LIST(&obj_used_list)) {
				if (ptr->type == OBJ_SHIP || ptr->type == OBJ_START)
					return 1;

				ptr = GET_NEXT(ptr);
			}

			return 0;

		case OPF_SHIP_NOT_PLAYER:
		case OPF_ORDER_RECIPIENT:
			ptr = GET_FIRST(&obj_used_list);
			while (ptr != END_OF_LIST(&obj_used_list)) {
				if (ptr->type == OBJ_SHIP)
					return 1;

				ptr = GET_NEXT(ptr);
			}

			return 0;

		case OPF_WING:
			for (j=0; j<MAX_WINGS; j++)
				if (Wings[j].wave_count)
					return 1;

			return 0;

		case OPF_PERSONA:
			return Personas.empty() ? 0 : 1;

		case OPF_POINT:
		case OPF_WAYPOINT_PATH:
			return Waypoint_lists.empty() ? 0 : 1;

		case OPF_MISSION_NAME:
			if (m_mode != MODE_CAMPAIGN) {
				if (!(*Mission_filename))
					return 0;

				return 1;
			}

			if (Campaign.num_missions > 0)
				return 1;

			return 0;

		case OPF_GOAL_NAME: {
			int value;

			value = Operators[op].value;

			if (m_mode == MODE_CAMPAIGN)
				return 1;

			// need to be sure that previous-goal functions are available.  (i.e. we are providing a default argument for them)
			else if ((value == OP_PREVIOUS_GOAL_TRUE) || (value == OP_PREVIOUS_GOAL_FALSE) || (value == OP_PREVIOUS_GOAL_INCOMPLETE) || !Mission_goals.empty())
				return 1;

			return 0;
		}

		case OPF_EVENT_NAME: {
			int value;

			value = Operators[op].value;
			if (m_mode == MODE_CAMPAIGN)
				return 1;

			// need to be sure that previous-event functions are available.  (i.e. we are providing a default argument for them)
			else if ((value == OP_PREVIOUS_EVENT_TRUE) || (value == OP_PREVIOUS_EVENT_FALSE) || (value == OP_PREVIOUS_EVENT_INCOMPLETE) || !Mission_events.empty())
				return 1;

			return 0;
		}

		case OPF_MESSAGE:
			if (m_mode == MODE_EVENTS) {
				Assert(Event_editor_dlg);
				if (Event_editor_dlg->current_message_name(0))
					return 1;

			} else {
				if (Num_messages > Num_builtin_messages)
					return 1;
			}

			return 0;

		case OPF_VARIABLE_NAME:
			return (sexp_variable_count() > 0) ? 1 : 0;

		case OPF_SSM_CLASS:
			return Ssm_info.empty() ? 0 : 1;

		case OPF_MISSION_MOOD:
			return Builtin_moods.empty() ? 0 : 1;

		case OPF_CONTAINER_NAME:
			return get_all_sexp_containers().empty() ? 0 : 1;

		case OPF_LIST_CONTAINER_NAME:
			for (const auto &container : get_all_sexp_containers()) {
				if (container.is_list()) {
					return 1;
				}
			}
			return 0;

		case OPF_MAP_CONTAINER_NAME:
			for (const auto &container : get_all_sexp_containers()) {
				if (container.is_map()) {
					return 1;
				}
			}
			return 0;

		case OPF_ASTEROID_TYPES:
			if (!get_list_valid_asteroid_subtypes().empty()) {
				return 1;
			}
			return 0;

		case OPF_DEBRIS_TYPES:
			for (const auto& this_asteroid : Asteroid_info) {
				if (this_asteroid.type == ASTEROID_TYPE_DEBRIS) {
					return 1;
				}
			}
			return 0;

		case OPF_MOTION_DEBRIS:
			if (Motion_debris_info.size() > 0) {
				return 1;
			}
			return 0;

		case OPF_BOLT_TYPE:
			if (Bolt_types.size() > 0) {
				return 1;
			}
			return 0;

		case OPF_TRAITOR_OVERRIDE:
			return Traitor_overrides.empty() ? 0 : 1;

		case OPF_LUA_GENERAL_ORDER:
			return (ai_lua_get_num_general_orders() > 0) ? 1 : 0;

		case OPF_MISSION_CUSTOM_STRING:
			return The_mission.custom_strings.empty() ? 0 : 1;

		default:
			if (!Dynamic_enums.empty()) {
				if ((type - First_available_opf_id) < (int)Dynamic_enums.size()) {
					return 1;
				} else {
					UNREACHABLE("Unhandled SEXP argument type!");
				}
			} else {
				UNREACHABLE("Unhandled SEXP argument type!");
			}

	}

	return 0;
}

// expand a combined line (one with an operator and its one argument on the same line) into
// 2 lines.
void sexp_tree::expand_operator(int node)
{
	int data;
	HTREEITEM h;

	auto& n = m_model->node(node);

	if (n.flags & COMBINED) {
		node = n.parent;
		auto& parent_n = m_model->node(node);
		Assert((parent_n.flags & OPERAND) && (parent_n.flags & EDITABLE));
	}

	if ((n.flags & OPERAND) && (n.flags & EDITABLE)) {  // expandable?
		Assert(n.type & SEXPT_OPERATOR);
		h = m_modelToHandle[node]; // How to get this handle?
		data = n.child;
		auto& data_n = m_model->node(data);
		Assert(data != -1 && data_n.next == -1 && data_n.child == -1);

		SetItem(h, TVIF_TEXT, n.text.c_str(), 0, 0, 0, 0, 0);
		n.flags = OPERAND;
		int bmap = get_data_image(data);
		/*auto handle = */insert(data_n.text.c_str(), bmap, bmap, h);
		data_n.flags = EDITABLE;
		Expand(h, TVE_EXPAND);
	}
}

// expand a CTreeCtrl branch and all of its children
void sexp_tree::expand_branch(HTREEITEM h)
{
	Expand(h, TVE_EXPAND);
	h = GetChildItem(h);
	while (h) {
		expand_branch(h);
		h = GetNextSiblingItem(h);
	}
}

void sexp_tree::merge_operator(int node)
{
/*	char buf[256];
	int child;

	if (tree_nodes[node].flags == EDITABLE)  // data
		node = tree_nodes[node].parent;

	if (node != -1) {
		child = tree_nodes[node].child;
		if (child != -1 && tree_nodes[child].next == -1 && tree_nodes[child].child == -1) {
			sprintf(buf, "%s %s", tree_nodes[node].text, tree_nodes[child].text);
			SetItemText(tree_nodes[node].handle, buf);
			tree_nodes[node].flags = OPERAND | EDITABLE;
			tree_nodes[child].flags = COMBINED;
			DeleteItem(tree_nodes[child].handle);
			tree_nodes[child].handle = NULL;
			return;
		}
	}*/
}

// add a data node under operator pointed to by item_index
HTREEITEM sexp_tree::add_data(const char* data, int type)
{
	int node;

	expand_operator(item_index);
	node = m_model->allocateNode(item_index);
	m_model->setNode(node, type, data);

	auto& n = m_model->node(node);

	int bmap = get_data_image(node);
	HTREEITEM handle = insert(data, bmap, bmap, item_handle);
	n.flags = EDITABLE;
	*modified = 1;
	return handle;
}

// add a (variable) data node under operator pointed to by item_index
int sexp_tree::add_variable_data(const char *data, int type)
{
	int node;

	Assert(type & SEXPT_VARIABLE);

	expand_operator(item_index);
	node = m_model->allocateNode(item_index);
	m_model->setNode(node, type, data);
	auto& n = m_model->node(node);
	/*auto handle = */insert(data, BITMAP_VARIABLE, BITMAP_VARIABLE, item_handle);
	n.flags = NOT_EDITABLE;
	*modified = 1;
	return node;
}

// add a container name node under operator pointed to by item_index
int sexp_tree::add_container_name(const char *container_name)
{
	Assertion(container_name != nullptr, "Attempt to add null container name. Please report!");
	Assertion(get_sexp_container(container_name) != nullptr,
		"Attempt to add unknown container name %s. Please report!",
		container_name);

	expand_operator(item_index);
	int node = m_model->allocateNode(item_index);
	m_model->setNode(node, (SEXPT_VALID | SEXPT_CONTAINER_NAME | SEXPT_STRING), container_name);
	/*auto handle = */insert(container_name, BITMAP_CONTAINER_NAME, BITMAP_CONTAINER_NAME, item_handle);
	auto& n = m_model->node(node);
	n.flags = NOT_EDITABLE;
	*modified = 1;
	return node;
}

// add a (container) data node under operator pointed to by item_index
void sexp_tree::add_container_data(const char *container_name)
{
	Assertion(container_name != nullptr, "Attempt to add null container. Please report!");
	Assertion(get_sexp_container(container_name) != nullptr,
		"Attempt to add unknown container %s. Please report!",
		container_name);
	const int node = m_model->allocateNode(item_index);
	m_model->setNode(node, (SEXPT_VALID | SEXPT_CONTAINER_DATA | SEXPT_STRING), container_name);
	/*auto handle = */insert(container_name, BITMAP_CONTAINER_DATA, BITMAP_CONTAINER_DATA, item_handle);
	auto& n = m_model->node(node);
	n.flags = NOT_EDITABLE;
	update_item(node);
	*modified = 1;
}

// add an operator under operator pointed to by item_index.  Updates item_index to point
// to this new operator.
void sexp_tree::add_operator(const char *op, HTREEITEM h)
{
	int node;
	
	if (item_index == -1) {
		node = m_model->allocateNode(-1);
		m_model->setNode(node, (SEXPT_OPERATOR | SEXPT_VALID), op);
		item_handle = insert(op, BITMAP_OPERATOR, BITMAP_OPERATOR, h);
		SetItemData(item_handle, node);
	} else {
		expand_operator(item_index);
		node = m_model->allocateNode(item_index);
		m_model->setNode(node, (SEXPT_OPERATOR | SEXPT_VALID), op);
		item_handle = insert(op, BITMAP_OPERATOR, BITMAP_OPERATOR, item_handle);
		SetItemData(item_handle, node);
	}

	m_modelToHandle[node] = item_handle;

	auto& n = m_model->node(node);

	n.flags = OPERAND;
	update_item(node);
	*modified = 1;
}

// add an operator with one argument under operator pointed to by item_index.  This function
// exists because the one arg case is a special case.  The operator and argument is
// displayed on the same line.
/*void sexp_tree::add_one_arg_operator(char *op, char *data, int type)
{
	char str[80];
	int node1, node2;
	
	expand_operator(item_index);
	node1 = allocate_node(item_index);
	node2 = allocate_node(node1);
	set_node(node1, SEXPT_OPERATOR, op);
	set_node(node2, type, data);
	sprintf(str, "%s %s", op, data);
	tree_nodes[node1].handle = insert(str, tree_nodes[item_index].handle);
	tree_nodes[node1].flags = OPERAND | EDITABLE;
	tree_nodes[node2].flags = COMBINED;
	*modified = 1;
}*/

/*
int sexp_tree::verify_tree(int *bypass)
{
	return verify_tree(0, bypass);
}

// check the sexp tree for errors.  Return -1 if error, or 0 if no errors.  If an error
// is found, item_index = node of error.
int sexp_tree::verify_tree(int node, int *bypass)
{
	int i, type, count, op, type2, op2, argnum = 0;

	if (!total_nodes)
		return 0;  // nothing to check

	Assert(node >= 0 && node < tree_nodes.size());
	Assert(tree_nodes[node].type == SEXPT_OPERATOR);

	op = get_operator_index(tree_nodes[node].text);
	if (op == -1)
		return node_error(node, "Unknown operator", bypass);

	count = count_args(tree_nodes[node].child);
	if (count < Operators[op].min)
		return node_error(node, "Too few arguments for operator", bypass);
	if (count > Operators[op].max)
		return node_error(node, "Too many arguments for operator", bypass);

	node = tree_nodes[node].child;  // get first argument
	while (node != -1) {
		type = query_operator_argument_type(op, argnum);
		Assert(tree_nodes[node].type & SEXPT_VALID);
		if (tree_nodes[node].type == SEXPT_OPERATOR) {
			if (verify_tree(node) == -1)
				return -1;

			op2 = get_operator_index(tree_nodes[node].text);  // no error checking, because it was done in the call above.
			type2 = query_operator_return_type(op2);

		} else if (tree_nodes[node].type == SEXPT_NUMBER) {
			char *ptr;

			type2 = OPR_NUMBER;
			ptr = tree_nodes[node].text;
			while (*ptr)
				if (!isdigit(*ptr++))
					return node_error(node, "Number is invalid", bypass);

		} else if (tree_nodes[node].type == SEXPT_STRING) {
			type2 = SEXP_ATOM_STRING;

		} else
			Assert(0);  // unknown and invalid sexp node type.

		switch (type) {
			case OPF_NUMBER:
				if (type2 != OPR_NUMBER)
					return node_error(node, "Number or number return type expected here", bypass);

				break;

			case OPF_SHIP:
				if (type2 == SEXP_ATOM_STRING)
					if (ship_name_lookup(tree_nodes[node].text, 1) == -1)
						type2 = 0;

				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Ship name expected here", bypass);

				break;

			case OPF_WING:
				if (type2 == SEXP_ATOM_STRING)
					if (wing_name_lookup(tree_nodes[node].text) == -1)
						type2 = 0;

				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Wing name expected here", bypass);

				break;

			case OPF_SHIP_WING:
				if (type2 == SEXP_ATOM_STRING)
					if (ship_name_lookup(tree_nodes[node].text, 1) == -1)
						if (wing_name_lookup(tree_nodes[node].text) == -1)
							type2 = 0;

				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Ship or wing name expected here", bypass);

				break;

			case OPF_BOOL:
				if (type2 != OPR_BOOL)
					return node_error(node, "Boolean return type expected here", bypass);

				break;

			case OPF_NULL:
				if (type2 != OPR_NULL)
					return node_error(node, "No return type operator expected here", bypass);

				break;

			case OPF_POINT:
				if (type2 != SEXP_ATOM_STRING || verify_vector(tree_nodes[node].text))
					return node_error(node, "3d coordinate expected here", bypass);

				break;

			case OPF_SUBSYSTEM:
			case OPF_AWACS_SUBSYSTEM:
			case OPF_ROTATING_SUBSYSTEM:
			case OPF_TRANSLATING_SUBSYSTEM:
				if (type2 == SEXP_ATOM_STRING)
					if (ai_get_subsystem_type(tree_nodes[node].text) == SUBSYSTEM_UNKNOWN)
						type2 = 0;

				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Subsystem name expected here", bypass);

				break;

			case OPF_IFF:
				if (type2 == SEXP_ATOM_STRING) {
					for (i=0; i<Num_iffs; i++)
						if (!stricmp(Team_names[i], tree_nodes[node].text))
							break;
				}

				if (i == Num_iffs)
					return node_error(node, "Iff team type expected here", bypass);

				break;

			case OPF_AI_GOAL:
				if (type2 != OPR_AI_GOAL)
					return node_error(node, "Ai goal return type expected here", bypass);

				break;

			case OPF_FLEXIBLE_ARGUMENT:
				if (type2 != OPR_FLEXIBLE_ARGUMENT)
					return node_error(node, "Flexible argument return type expected here", bypass);
				
				break;

			case OPF_ANYTHING:
				break;

			case OPF_DOCKER_POINT:
				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Docker docking point name expected here", bypass);

				break;

			case OPF_DOCKEE_POINT:
				if (type2 != SEXP_ATOM_STRING)
					return node_error(node, "Dockee docking point name expected here", bypass);

				break;
		}

		node = tree_nodes[node].next;
		argnum++;
	}

	return 0;
}
*/

// display an error message and position to point of error (a node)
int sexp_tree::node_error(int node, const char *msg, int *bypass)
{
	char text[512];

	if (bypass)
		*bypass = 1;

	update_item(node);
	auto& n = m_model->node(node);
	if (n.flags & COMBINED)
		item_handle = m_modelToHandle[n.parent];

	ensure_visible(node);
	SelectItem(item_handle);
	sprintf(text, "%s\n\nContinue checking for more errors?", msg);
	if (MessageBox(text, "Sexp error", MB_YESNO | MB_ICONEXCLAMATION) == IDNO)
		return -1;
	else
		return 0;
}

void sexp_tree::hilite_item(int node)
{
	ensure_visible(node);
	SelectItem(m_modelToHandle[node]);
}

// because the MFC function EnsureVisible() doesn't do what it says it does, I wrote this.
void sexp_tree::ensure_visible(int node)
{
	auto& n = m_model->node(node);
	if (n.parent != -1)
		ensure_visible(n.parent);  // expand all parents first

	if (n.child != -1)  // expandable?
		Expand(m_modelToHandle[node], TVE_EXPAND); // expand this item
}

void sexp_tree::link_modified(int *ptr)
{
	modified = ptr;
}

void get_variable_default_text_from_variable_text(const char *text, char *default_text)
{
	const char *start;

	// find '('
	start = strstr(text, "(");
	Assert(start);
	start++;

	// get length and copy all but last char ")"
	auto len = strlen(start);
	strncpy(default_text, start, len-1);

	// add null termination
	default_text[len-1] = '\0';
}

void get_variable_name_from_sexp_tree_node_text(const char *text, char *var_name)
{
	auto length = strcspn(text, "(");

	strncpy(var_name, text, length);
	var_name[length] = '\0';
}

int sexp_tree::get_modify_variable_type(int parent)
{
	int sexp_var_index = -1;

	Assert(parent >= 0);
	auto& n = m_model->node(parent);
	int op_const = get_operator_const(n.text.c_str());

	Assert(n.child >= 0);
	auto& child_n = m_model->node(n.child);
	auto node_text = child_n.text;

	if ( op_const == OP_MODIFY_VARIABLE ) {
		sexp_var_index = get_tree_name_to_sexp_variable_index(node_text.c_str());
	}
	else if ( op_const == OP_SET_VARIABLE_BY_INDEX ) {
		if (can_construe_as_integer(node_text.c_str())) {
			sexp_var_index = atoi(node_text.c_str());
		}
		else if (strchr(node_text.c_str(), '(') && strchr(node_text.c_str(), ')')) {
			// the variable index is itself a variable!
			return OPF_AMBIGUOUS;
		}
	} else {
		Int3();  // should not be called otherwise
	}

	// if we don't have a valid variable, allow replacement with anything
	if (sexp_var_index < 0)
		return OPF_AMBIGUOUS;

	if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_BLOCK || Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NOT_USED) {
		// assume number so that we can allow tree display of number operators
		return OPF_NUMBER;
	} else if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NUMBER) {
		return OPF_NUMBER;
	} else if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_STRING) {
		return OPF_AMBIGUOUS;
	} else {
		Int3();
		return 0;
	}
}


void sexp_tree::verify_and_fix_arguments(int node)
{
	int op_index, arg_num, type, tmp;
	static int here_count = 0;
	sexp_list_item *list, *ptr;
	bool is_variable_arg = false; 

	if (here_count)
		return;

	here_count++;
	SexpNode& n = m_model->node(node);
	op_index = get_operator_index(n.text.c_str());
	if (op_index < 0)
		return;

	tmp = item_index;

	arg_num = 0;
	update_item(n.child);
	while (item_index >= 0) {
		n = m_model->node(item_index);
		// get listing of valid argument values for node item_index
		type = query_operator_argument_type(op_index, arg_num);
		// special case for modify-variable
		if (type == OPF_AMBIGUOUS) {
			is_variable_arg = true;
			type = get_modify_variable_type(node);
		}
		if (n.type & SEXPT_CONTAINER_DATA) {
			// we don't care if the data matches
			// TODO: revisit if/when strictly typed data becomes supported
			update_item(n.next);
			arg_num++;
			continue;
		}
		if (query_restricted_opf_range(type)) {
			list = get_listing_opf(type, node, arg_num);
			if (!list && (arg_num >= Operators[op_index].min)) {
				m_model->freeNode(item_index, 1);
				update_item(tmp);
				here_count--;
				return;
			}

			if (list) {
				// get a pointer to n.text for normal value
				// or default variable value if variable
				SCP_string text_ptr;
				char default_variable_text[TOKEN_LENGTH];
				if (n.type & SEXPT_VARIABLE) {
					// special case for SEXPs which can modify a variable 
					if (type == OPF_VARIABLE_NAME) {
						// make text_ptr to start - before '('
						get_variable_name_from_sexp_tree_node_text(n.text.c_str(), default_variable_text);
						text_ptr = default_variable_text;
					} else {
						// only the type needs checking for variables. It's up the to the FREDder to ensure the value is valid
						get_variable_name_from_sexp_tree_node_text(n.text.c_str(), default_variable_text);						
						int sexp_var_index = get_index_sexp_variable_name(default_variable_text);
						bool types_match = false; 
						Assert(sexp_var_index != -1);

						switch (type) {
							case OPF_NUMBER:
							case OPF_POSITIVE:
								if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NUMBER) {
									types_match = true; 
								}
								break; 

							default: 
								if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_STRING) {
									types_match = true; 
								}
						}
						
						if (types_match) {
							// on to the next argument
							update_item(n.next);
							arg_num++;
							continue; 
						}
						else {
							// shouldn't really be getting here unless someone has been hacking the mission in a text editor
							get_variable_default_text_from_variable_text(n.text.c_str(), default_variable_text);
							text_ptr = default_variable_text;
						}
					}
				} else {
					text_ptr = n.text;
				}

				ptr = list;
				while (ptr) {
					// make sure text is not NULL
					// check that proposed text is valid for operator
					if ( !stricmp(ptr->text.c_str(), text_ptr.c_str()) )
						break;

					ptr = ptr->next;
				}

				if (!ptr) {  // argument isn't in list of valid choices, 
					if (list->op >= 0) {
						replace_operator(list->text.c_str());
					} else {
						replace_data(list->text.c_str(), list->type);
					}
				}

			} else {
				bool invalid = false;
				if (type == OPF_AMBIGUOUS) {
					if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR) {
						invalid = true;
					}
				} else {
					if (SEXPT_TYPE(n.type) != SEXPT_OPERATOR) {
						invalid = true;
					}
				}

				if (invalid) {
					replace_data("<Invalid>", (SEXPT_STRING | SEXPT_VALID));
				}
			}

			if (n.type & SEXPT_OPERATOR)
				verify_and_fix_arguments(item_index);
			
		}
		
		//fix the node if it is the argument for modify-variable
		if (is_variable_arg //&& 
		//	!(tree_nodes[item_index].type & SEXPT_OPERATOR || tree_nodes[item_index].type & SEXPT_VARIABLE ) 
			) {
			switch (type) {
				case OPF_AMBIGUOUS:
					n.type |= SEXPT_STRING;
					n.type &= ~SEXPT_NUMBER;
					break; 

				case OPF_NUMBER:
					n.type |= SEXPT_NUMBER; 
					n.type &= ~SEXPT_STRING;
					break;

				default:
					Int3();
			}
		}

		update_item(n.next);
		arg_num++;
	}

	update_item(tmp);
	here_count--;
}

void sexp_tree::replace_data(const char *data, int type)
{
	int node;
	HTREEITEM h;

	auto& n = m_model->node(item_index);

	node = n.child;
	if (node != -1)
		m_model->freeNode(node);

	n.child = -1;
	h = item_handle;
	while (ItemHasChildren(h))
		DeleteItem(GetChildItem(h));

	m_model->setNode(item_index, type, data);
	SetItemText(h, data);
	int bmap = get_data_image(item_index);
	SetItemImage(h, bmap, bmap);
	n.flags = EDITABLE;

	// check remaining data beyond replaced data for validity (in case any of it is dependent on data just replaced)
	verify_and_fix_arguments(n.parent);

	*modified = 1;
	update_help(GetSelectedItem());
}


// Replaces data with sexp_variable type data
void sexp_tree::replace_variable_data(int var_idx, int type)
{
	int node;
	HTREEITEM h;
	char buf[128];

	Assert(type & SEXPT_VARIABLE);

	auto& n = m_model->node(item_index);

	node = n.child;
	if (node != -1)
		m_model->freeNode(node);

	n.child = -1;
	h = item_handle;
	while (ItemHasChildren(h)) {
		DeleteItem(GetChildItem(h));
	}

	// Assemble name
	sprintf(buf, "%s(%s)", Sexp_variables[var_idx].variable_name, Sexp_variables[var_idx].text);

	m_model->setNode(item_index, type, buf);
	SetItemText(h, buf);
	SetItemImage(h, BITMAP_VARIABLE, BITMAP_VARIABLE);
	n.flags = NOT_EDITABLE;

	// check remaining data beyond replaced data for validity (in case any of it is dependent on data just replaced)
	verify_and_fix_arguments(n.parent);

	*modified = 1;
	update_help(GetSelectedItem());
}

void sexp_tree::replace_container_name(const sexp_container &container)
{
	HTREEITEM h = item_handle;

	auto& n = m_model->node(item_index);

	// clean up any child nodes
	int node = n.child;
	if (node != -1)
		m_model->freeNode(node);
	n.child = -1;

	while (ItemHasChildren(h)) {
		DeleteItem(GetChildItem(h));
	}

	m_model->setNode(item_index, (SEXPT_VALID | SEXPT_STRING | SEXPT_CONTAINER_NAME), container.container_name.c_str());
	SetItemImage(h, BITMAP_CONTAINER_NAME, BITMAP_CONTAINER_NAME);
	SetItemText(h, container.container_name.c_str());
	n.flags = NOT_EDITABLE;

	*modified = 1;
	update_help(GetSelectedItem());
}

void sexp_tree::replace_container_data(const sexp_container &container,
	int type,
	bool test_child_nodes,
	bool delete_child_nodes,
	bool set_default_modifier)
{
	HTREEITEM h = item_handle;

	auto& n = m_model->node(item_index);

	// if this is already a container of the right type, don't alter the child nodes
	if (test_child_nodes && (n.type & SEXPT_CONTAINER_DATA)) {
		if (container.is_list()) {
			const auto *p_old_container = get_sexp_container(n.text.c_str());

			Assertion(p_old_container != nullptr,
				"Attempt to Replace Container Data of unknown previous container %s. Please report!",
				n.text.c_str());

			if (p_old_container->is_list()) {
				// TODO: check for strictly typed data here

				if (container.opf_type == p_old_container->opf_type) {
					delete_child_nodes = false;
					set_default_modifier = false;
				}
			}
		}
	}

	if (delete_child_nodes) {
		int node = n.child;
		if (node != -1)
			m_model->freeNode(node);

		n.child = -1;
		while (ItemHasChildren(h)) {
			DeleteItem(GetChildItem(h));
		}
	}

	m_model->setNode(item_index, type, container.container_name.c_str());
	SetItemImage(h, BITMAP_CONTAINER_DATA, BITMAP_CONTAINER_DATA);
	SetItemText(h, container.container_name.c_str());
	n.flags = NOT_EDITABLE;

	if (set_default_modifier) {
		add_default_modifier(container);
	}

	*modified = 1;
	update_help(GetSelectedItem());
}


void sexp_tree::add_default_modifier(const sexp_container &container)
{
	sexp_list_item item;

	int type_to_use = (SEXPT_VALID | SEXPT_MODIFIER);

	if (container.is_map()) {
		if (any(container.type & ContainerType::STRING_KEYS)) {
			item.set_data("<any string>");
			type_to_use |= SEXPT_STRING;
		} else if (any(container.type & ContainerType::NUMBER_KEYS)) {
			item.set_data("0");
			type_to_use |= SEXPT_NUMBER;
		} else {
			UNREACHABLE("Unknown map container key type %d", (int)container.type);
		}
	} else if (container.is_list()) {
		item.set_data(get_all_list_modifiers()[0].name);
		type_to_use |= SEXPT_STRING;
	} else {
		UNREACHABLE("Unknown container type %d", (int)container.type);
	}

	item.type = type_to_use;
	add_data(item.text.c_str(), item.type);
}

void sexp_tree::replace_operator(const char *op)
{
	int node;
	HTREEITEM h;

	auto& n = m_model->node(item_index);

	node = n.child;
	if (node != -1)
		m_model->freeNode(node);

	n.child = -1;
	h = item_handle;
	while (ItemHasChildren(h))
		DeleteItem(GetChildItem(h));

	m_model->setNode(item_index, (SEXPT_OPERATOR | SEXPT_VALID), op);
	SetItemText(h, op);
	n.flags = OPERAND;
	*modified = 1;
	update_help(GetSelectedItem());

	// hack added at Allender's request.  If changing ship in an ai-dock operator, re-default
	// docking point.
}

/*void sexp_tree::replace_one_arg_operator(char *op, char *data, int type)
{
	char str[80];
	int node;
	HTREEITEM h;

	node = tree_nodes[item_index].child;
	if (node != -1)
		free_node2(node);

	tree_nodes[item_index].child = -1;
	h = tree_nodes[item_index].handle;
	while (ItemHasChildren(h))
		DeleteItem(GetChildItem(h));
	
	node = allocate_node(item_index);
	set_node(item_index, SEXPT_OPERATOR, op);
	set_node(node, type, data);
	sprintf(str, "%s %s", op, data);
	SetItemText(h, str);
	tree_nodes[item_index].flags = OPERAND | EDITABLE;
	tree_nodes[node].flags = COMBINED;
	*modified = 1;
	update_help(GetSelectedItem());
}*/

// moves a whole sexp tree branch to a new position under 'parent' and after 'after'.
// The expansion state is preserved, and node handles are updated.
void sexp_tree::move_branch(int source, int parent)
{
	// if no source, skip everything
	if (source != -1) {
		// --- 1. Detach the source node from its old parent in the model ---
		SexpNode& source_n = m_model->node(source);
		int old_parent_index = source_n.parent;

		if (old_parent_index != -1) {
			SexpNode& old_parent_n = m_model->node(old_parent_index);
			if (old_parent_n.child == source) {
				// It was the first child, so just point the parent to the next sibling.
				old_parent_n.child = source_n.next;
			} else {
				// Find the sibling right before the source node.
				int prev_sibling_index = old_parent_n.child;
				while (prev_sibling_index != -1 && m_model->node(prev_sibling_index).next != source) {
					prev_sibling_index = m_model->node(prev_sibling_index).next;
				}

				if (prev_sibling_index != -1) {
					// Splice the source node out of the list.
					m_model->node(prev_sibling_index).next = source_n.next;
				}
			}
		}

		// --- 2. Attach the source node to its new parent in the model ---
		source_n.parent = parent;
		source_n.next = -1; // It's now the last sibling in its new list.

		if (parent != -1) { // -1 means it's becoming a root, so no attachment needed.
			SexpNode& new_parent_n = m_model->node(parent);
			if (new_parent_n.child == -1) {
				// New parent has no children, so this becomes the first.
				new_parent_n.child = source;
			} else {
				// Find the last child of the new parent and append the source node.
				int tail_index = new_parent_n.child;
				while (m_model->node(tail_index).next != -1) {
					tail_index = m_model->node(tail_index).next;
				}
				m_model->node(tail_index).next = source;
			}
		}

		// --- 3. Now, surgically modify the UI to match the model change ---
		HTREEITEM source_handle = m_modelToHandle[source];
		HTREEITEM parent_handle = (parent != -1) ? m_modelToHandle[parent] : TVI_ROOT;

		// This recursive call to the other move_branch will handle the UI surgery.
		move_branch(source_handle, parent_handle);
	}
}

HTREEITEM sexp_tree::move_branch(HTREEITEM source, HTREEITEM parent, HTREEITEM after)
{
	int image1, image2;
	HTREEITEM h = 0, child, next;

	if (source) {
		// The 'for' loop to find the handle is gone, as we already have 'source'.
		// The 'if/else' is also gone because we don't need to write to tree_nodes anymore.
		GetItemImage(source, image1, image2);
		h = insert(GetItemText(source), image1, image2, parent, after);

		// This part is correct. You transfer the model index to the new UI item.
		int node_data = (int)GetItemData(source);
		SetItemData(h, node_data);
		m_modelToHandle[node_data] = h;

		// The rest of the function remains the same.
		child = GetChildItem(source);
		while (child) {
			next = GetNextSiblingItem(child);
			move_branch(child, h);
			child = next;
		}

		if (GetItemState(source, TVIS_EXPANDED) & TVIS_EXPANDED)
			Expand(h, TVE_EXPAND);

		DeleteItem(source);
	}

	return h;
}

void sexp_tree::copy_branch(HTREEITEM source, HTREEITEM parent, HTREEITEM after)
{
	// NOTE: This function does not modify the underlying SexpTreeModel.
	// It only copies the UI items, which will cause the UI and data to become desynchronized.

	int image1, image2;
	HTREEITEM h, child;

	if (source) {
		// The 'for' loop and 'if/else' block are removed. We already have the source handle.
		GetItemImage(source, image1, image2);
		h = insert(GetItemText(source), image1, image2, parent, after);

		// This part correctly transfers the model index to the new UI item.
		int node_data = (int)GetItemData(source);
		SetItemData(h, node_data);
		m_modelToHandle[node_data] = h;

		// The recursive call to copy children remains.
		child = GetChildItem(source);
		while (child) {
			copy_branch(child, h);
			child = GetNextSiblingItem(child);
		}

		if (GetItemState(source, TVIS_EXPANDED) & TVIS_EXPANDED)
			Expand(h, TVE_EXPAND);
	}
}

void sexp_tree::move_root(HTREEITEM source, HTREEITEM dest, bool insert_before)
{
	HTREEITEM h, after = dest;

	Assert(!GetParentItem(source));
	Assert(!GetParentItem(dest));

	if (insert_before)
	{
		// since we can only insert after something, find the item previous to the destination; or indicate the first item if there is no previous item
		after = GetNextItem(dest, TVGN_PREVIOUS);
		if (after == nullptr)
			after = TVI_FIRST;
	}

	h = move_branch(source, TVI_ROOT, after);
	SelectItem(h);
	SelectItem(h);
	*modified = 1;
}

void sexp_tree::OnBegindrag(NMHDR* pNMHDR, LRESULT* pResult) 
{
	UINT flags = 0;

//	ScreenToClient(&m_pt);
	ASSERT(!m_dragging);
	m_h_drag = HitTest(m_pt, &flags);
	m_h_drop = NULL;

	if (!m_mode || GetParentItem(m_h_drag))
		return;

	ASSERT(m_p_image_list == NULL);
	m_p_image_list = CreateDragImage(m_h_drag);  // get the image list for dragging
	if (!m_p_image_list)
		return;

	m_p_image_list->DragShowNolock(TRUE);
	m_p_image_list->SetDragCursorImage(0, CPoint(0, 0));
	m_p_image_list->BeginDrag(0, CPoint(0,0));
	m_p_image_list->DragMove(m_pt);
	m_p_image_list->DragEnter(this, m_pt);
	SetCapture();
	m_dragging = TRUE;
}

void sexp_tree::OnLButtonDown(UINT nFlags, CPoint point) 
{
	m_pt = point;
	CTreeCtrl::OnLButtonDown(nFlags, point);
}

void sexp_tree::OnMouseMove(UINT nFlags, CPoint point) 
{
	HTREEITEM hitem = NULL;
	UINT flags = 0;

	if (m_dragging) {
		ASSERT(m_p_image_list != NULL);
		m_p_image_list->DragMove(point);
		if ((hitem = HitTest(point, &flags)) != NULL)
			if (!GetParentItem(hitem)) {
				m_p_image_list->DragLeave(this);
				SelectDropTarget(hitem);
				m_h_drop = hitem;
				m_p_image_list->DragEnter(this, point);
			}
	}

	CTreeCtrl::OnMouseMove(nFlags, point);
}

void sexp_tree::OnLButtonUp(UINT nFlags, CPoint point) 
{
	int node1, node2;

	if (m_dragging) {
		ASSERT(m_p_image_list != NULL);
		m_p_image_list->DragLeave(this);
		m_p_image_list->EndDrag();
		delete m_p_image_list;
		m_p_image_list = NULL;

		if (m_h_drop && m_h_drag != m_h_drop) {
			Assert(m_h_drag);
			node1 = (int)GetItemData(m_h_drag);
			node2 = (int)GetItemData(m_h_drop);

			// If we're moving up, insert before the dropped item.  If we're moving down,
			// insert after the dropped item.  The idea is to always end up where we dropped.
			bool insert_before = false;
			for (auto h = m_h_drag; h != nullptr; h = GetNextItem(h, TVGN_PREVIOUS))
			{
				if (h == m_h_drop)
				{
					insert_before = true;
					break;
				}
			}

			move_root(m_h_drag, m_h_drop, insert_before);

			if (m_mode == MODE_GOALS) {
				Assert(Goal_editor_dlg);
				Goal_editor_dlg->move_handler(node1, node2, insert_before);

			} else if (m_mode == MODE_EVENTS) {
				Assert(Event_editor_dlg);
				Event_editor_dlg->move_handler(node1, node2, insert_before);

			} else if (m_mode == MODE_CAMPAIGN) {
				Assert(Campaign_tree_formp);
				Campaign_tree_formp->move_handler(node1, node2, insert_before);

			} else
				UNREACHABLE("Unhandled dialog mode!");

		} else
			MessageBeep(0);

		ReleaseCapture();
		m_dragging = FALSE;
		SelectDropTarget(NULL);
	}

	CTreeCtrl::OnLButtonUp(nFlags, point);
}

const static UINT Numbered_data_bitmaps[] = {
	IDB_DATA_00,
	IDB_DATA_05,
	IDB_DATA_10,
	IDB_DATA_15,
	IDB_DATA_20,
	IDB_DATA_25,
	IDB_DATA_30,
	IDB_DATA_35,
	IDB_DATA_40,
	IDB_DATA_45,
	IDB_DATA_50,
	IDB_DATA_55,
	IDB_DATA_60,
	IDB_DATA_65,
	IDB_DATA_70,
	IDB_DATA_75,
	IDB_DATA_80,
	IDB_DATA_85,
	IDB_DATA_90,
	IDB_DATA_95
};

void sexp_tree::setup(CEdit *ptr)
{
	CImageList *pimagelist;
	CBitmap bitmap;

	help_box = ptr;
	if (help_box) {
		int stops[2] = { 10, 30 };

		help_box -> SetTabStops(2, (LPINT) stops);
	}

	pimagelist = GetImageList(TVSIL_NORMAL);
	if (!pimagelist) {
		pimagelist = new CImageList();
		pimagelist->Create(16, 16, TRUE/*bMask*/, 2, 22);

		//*****Add generic images
		bitmap.LoadBitmap(IDB_OPERATOR);
		pimagelist->Add(&bitmap, (COLORREF) 0xFFFFFF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_DATA);
		pimagelist->Add(&bitmap, (COLORREF) 0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_VARIABLE);
		pimagelist->Add(&bitmap, (COLORREF) 0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_ROOT);
		pimagelist->Add(&bitmap, (COLORREF) 0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_ROOT_DIRECTIVE);
		pimagelist->Add(&bitmap, (COLORREF) 0xFFFFFF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_CHAINED);
		pimagelist->Add(&bitmap, (COLORREF) 0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_CHAINED_DIRECTIVE);
		pimagelist->Add(&bitmap, (COLORREF) 0xFFFFFF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_GREEN_DOT);
		pimagelist->Add(&bitmap, (COLORREF) 0xFFFFFF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_BLACK_DOT);
		pimagelist->Add(&bitmap, (COLORREF) 0xFFFFFF);
		bitmap.DeleteObject();

		//*****Add numbered data entries
		int num = sizeof(Numbered_data_bitmaps)/sizeof(UINT);
		int i = 0;
		for(i = 0; i < num; i++)
		{
			bitmap.LoadBitmap(Numbered_data_bitmaps[i]);
			pimagelist->Add(&bitmap, (COLORREF) 0xFF00FF);
			bitmap.DeleteObject();
		}

		bitmap.LoadBitmap(IDB_COMMENT);
		pimagelist->Add(&bitmap, (COLORREF)0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_CONTAINER_NAME);
		pimagelist->Add(&bitmap, (COLORREF)0xFF00FF);
		bitmap.DeleteObject();

		bitmap.LoadBitmap(IDB_CONTAINER_DATA);
		pimagelist->Add(&bitmap, (COLORREF)0xFF00FF);
		bitmap.DeleteObject();

		SetImageList(pimagelist, TVSIL_NORMAL);
	}
}
//#define BITMAP_OPERATOR 0
//#define BITMAP_DATA 1
//#define BITMAP_VARIABLE 2
//#define BITMAP_ROOT 3
//#define BITMAP_ROOT_DIRECTIVE 4
//#define BITMAP_CHAIN 5
//#define BITMAP_CHAIN_DIRECTIVE 6
//#define BITMAP_GREEN_DOT 7
//#define BITMAP_BLACK_DOT 8


HTREEITEM sexp_tree::insert(LPCTSTR lpszItem, int image, int sel_image, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	return InsertItem(lpszItem, image, sel_image, hParent, hInsertAfter);

}

void sexp_tree::OnDestroy() 
{
	CImageList *pimagelist;

	pimagelist = GetImageList(TVSIL_NORMAL);
	if (pimagelist) {
		pimagelist->DeleteImageList();
		delete pimagelist;
	}

	CTreeCtrl::OnDestroy();
}

HTREEITEM sexp_tree::handle(int node)
{
	return m_modelToHandle[node];
}

const char *sexp_tree::help(int code)
{
	int i;

	i = (int)Sexp_help.size();
	while (i--) {
		if (Sexp_help[i].id == code)
			break;
	}

	if (i >= 0)
		return Sexp_help[i].help.c_str();

	return NULL;
}

// get type of item clicked on
int sexp_tree::get_type(HTREEITEM h)
{
	int model_index = (int)GetItemData(h);

	if (h == NULL || model_index < 0 || model_index >= m_model->size()) {
		return -1;
	}

	return m_model->node(model_index).type;
}


void sexp_tree::update_help(HTREEITEM h)
{
	int i, j, z, c, code, index, sibling_place;
	CString text;

	for (i=0; i<(int)Operators.size(); i++) {
		for (j=0; j<(int)op_menu.size(); j++) {
			if (get_category(Operators[i].value) == op_menu[j].id) {
				if (!help(Operators[i].value)) {
					mprintf(("Allender!  If you add new sexp operators, add help for them too! :) Sexp %s has no help.\n", Operators[i].text.c_str()));
				}
			}
		}
	}

	help_box = (CEdit *) GetParent()->GetDlgItem(IDC_HELP_BOX);
	if (!help_box || !::IsWindow(help_box->m_hWnd))
		return;

	mini_help_box = (CEdit *) GetParent()->GetDlgItem(IDC_MINI_HELP_BOX);
	if (mini_help_box && !::IsWindow(mini_help_box->m_hWnd))
		return;

	i = (int)GetItemData(h);

	int thisIndex = event_annotation_lookup(h);
	SCP_string nodeComment;

	if (thisIndex >= 0) {
		if (!Event_annotations[thisIndex].comment.empty()) {
			nodeComment = "Node Comments:\r\n   " + Event_annotations[thisIndex].comment;
		}
	} else {
		nodeComment = "";
	}

	auto& n = m_model->node(i);

	if ((i >= (int)m_model->size()) || !n.type) {
		help_box->SetWindowText(nodeComment.c_str());
		if (mini_help_box)
			mini_help_box->SetWindowText("");
		return;
	}

	// Now that we're done with top level nodes we can add the empty lines because
	// everything else below is supposed to have help text
	if (!nodeComment.empty())
		nodeComment.insert(0, "\r\n\r\n");

	if (SEXPT_TYPE(n.type) == SEXPT_OPERATOR)
	{
		if (mini_help_box)
			mini_help_box->SetWindowText("");
	}
	else
	{
		z = n.parent;
		if (z < 0) {
			Warning(LOCATION, "Sexp data \"%s\" has no parent!", n.text.c_str());
			return;
		}

		auto& parent_n = m_model->node(z);
		code = get_operator_const(parent_n.text.c_str());
		index = get_operator_index(parent_n.text.c_str());
		sibling_place = get_sibling_place(i) + 1;	//We want it to start at 1

		//*****Minihelp box
		if((SEXPT_TYPE(n.type) == SEXPT_NUMBER) || (SEXPT_TYPE(n.type) == SEXPT_STRING) && sibling_place > 0)
		{
			char buffer[10240] = {""};

			//Get the help for the current operator
			const char *helpstr = help(code);
			bool display_number = true;

			//If a help string exists, try to display it
			if(helpstr != NULL)
			{
				char searchstr[32];
				const char *loc=NULL, *loc2=NULL;

				if(loc == NULL)
				{
					sprintf(searchstr, "\n%d:", sibling_place);
					loc = strstr(helpstr, searchstr);
				}

				if(loc == NULL)
				{
					sprintf(searchstr, "\t%d:", sibling_place);
					loc = strstr(helpstr, searchstr);
				}
				if(loc == NULL)
				{
					sprintf(searchstr, " %d:", sibling_place);
					loc = strstr(helpstr, searchstr);
				}
				if(loc == NULL)
				{
					sprintf(searchstr, "%d:", sibling_place);
					loc = strstr(helpstr, searchstr);
				}
				if(loc == NULL)
				{
					loc = strstr(helpstr, "Rest:");
				}
				if(loc == NULL)
				{
					loc = strstr(helpstr, "All:");
				}

				if(loc != NULL)
				{
					//Skip whitespace
					while(*loc=='\r' || *loc == '\n' || *loc == ' ' || *loc == '\t') loc++;

					//Find EOL
					loc2 = strpbrk(loc, "\r\n");
					if(loc2 != NULL)
					{
						size_t size = loc2-loc;
						strncpy(buffer, loc, size);
						if(size < sizeof(buffer))
						{
							buffer[size] = '\0';
						}
						display_number = false;
					}
					else
					{
						strcpy_s(buffer, loc);
						display_number = false;
					}
				}
			}

			//Display argument number
			if(display_number)
			{
				sprintf(buffer, "%d:", sibling_place);
			}

			if (mini_help_box)
				mini_help_box->SetWindowText(buffer);
		}

		if (index >= 0) {
			c = 0;
			j = parent_n.child;
			while ((j >= 0) && (j != i)) {
				auto& sibling_n = m_model->node(j);
				j = sibling_n.next;
				c++;
			}

			Assert(j >= 0);
			// If the node is a message then display it
			if (query_operator_argument_type(index, c) == OPF_MESSAGE) {
				for (j=0; j<Num_messages; j++)
					if (!stricmp(Messages[j].name, n.text.c_str())) {
						text.Format("Message Text:\r\n%s%s", Messages[j].message, nodeComment.c_str());
						help_box->SetWindowText((LPCSTR)text);
						return;
					}
			}
			
			// If the node is a ship flag, then display the flag's description
			if (query_operator_argument_type(index, c) == OPF_SHIP_FLAG) {
				Object::Object_Flags object_flag = Object::Object_Flags::NUM_VALUES;
				Ship::Ship_Flags ship_flag = Ship::Ship_Flags::NUM_VALUES;
				Mission::Parse_Object_Flags parse_obj_flag = Mission::Parse_Object_Flags::NUM_VALUES;
				AI::AI_Flags ai_flag = AI::AI_Flags::NUM_VALUES;
				SCP_string desc;

				sexp_check_flag_arrays(n.text.c_str(), object_flag, ship_flag, parse_obj_flag, ai_flag);

				// Ship flags are pulled from multiple categories, so we have to search them all. Ew.
				if (object_flag != Object::Object_Flags::NUM_VALUES){
					for (size_t m = 0; m < (size_t)Num_object_flag_names; m++) {
						if (object_flag == Object_flag_descriptions[m].flag) {
							desc = Object_flag_descriptions[m].flag_desc;
							break;
						}
					}
				}

				if (ship_flag != Ship::Ship_Flags::NUM_VALUES) {
					for (size_t m = 0; m < (size_t)Num_ship_flag_names; m++) {
						if (ship_flag == Ship_flag_descriptions[m].flag) {
							desc = Ship_flag_descriptions[m].flag_desc;
							break;
						}
					}
				}

				if (ai_flag != AI::AI_Flags::NUM_VALUES) {
					for (size_t m = 0; m < (size_t)Num_ai_flag_names; m++) {
						if (ai_flag == Ai_flag_descriptions[m].flag) {
							desc = Ai_flag_descriptions[m].flag_desc;
							break;
						}
					}
				}

				// Only check through parse object flags if we haven't found anything yet
				if (desc.empty()) {
					if (parse_obj_flag != Mission::Parse_Object_Flags::NUM_VALUES) {
						for (size_t m = 0; m < (size_t)Num_parse_object_flags; m++) {
							if (parse_obj_flag == Parse_object_flag_descriptions[m].def) {
								desc = Parse_object_flag_descriptions[m].flag_desc;
								break;
							}
						}
					}
				}

				//If we still didn't find anything, say so!
				if (desc.empty())
					desc = "Unknown flag. Let a coder know!";

				text.Format("%s", desc.c_str());
				help_box->SetWindowText((LPCSTR)text);
				return;
			}

			// If the node is a wing flag, then display the flag's description
			if (query_operator_argument_type(index, c) == OPF_WING_FLAG) {
				Ship::Wing_Flags wing_flag = Ship::Wing_Flags::NUM_VALUES;
				SCP_string desc;

				sexp_check_flag_array(n.text.c_str(), wing_flag);

				if (wing_flag != Ship::Wing_Flags::NUM_VALUES) {
					for (size_t m = 0; m < (size_t)Num_wing_flag_names; m++) {
						if (wing_flag == Wing_flag_descriptions[m].flag) {
							desc = Wing_flag_descriptions[m].flag_desc;
							break;
						}
					}
				}

				// If we still didn't find anything, say so!
				if (desc.empty())
					desc = "Unknown flag. Let a coder know!";

				text.Format("%s", desc.c_str());
				help_box->SetWindowText((LPCSTR)text);
				return;
			}
		}

		i = z;
	}

	code = get_operator_const(n.text.c_str());
	auto str = help(code);
	if (!str) {
		text.Format("No help available%s", nodeComment.c_str());
	} else {
		text.Format("%s%s", str, nodeComment.c_str());
	}

	help_box->SetWindowText((LPCSTR)text);
}

// find list of sexp_tree nodes with text
// stuff node indices into find[]
int sexp_tree::find_text(const char *text, int *find)
{
	int i;
	int find_count;

	// initialize find
	for (i=0; i<MAX_SEARCH_MESSAGE_DEPTH; i++) {
		find[i] = -1;
	}

	find_count = 0;

	for (i=0; i<(int)m_model->size(); i++) {
		auto& n = m_model->node(i);
		// only look at used and editable nodes
		if ((n.flags & EDITABLE && (n.type != SEXPT_UNUSED))) {
			// find the text
			if (!stricmp(n.text.c_str(), text)) {
				find[find_count++] = i;

				// don't exceed max count - array bounds
				if (find_count == MAX_SEARCH_MESSAGE_DEPTH) {
					break;
				}
			}
		}
	}

	return find_count;
}

// This solution was found at https://community.notepad-plus-plus.org/topic/21158/way-to-disallow-copying-text/7
//   Another interesting thing with the script's execution is that if you don't allow Notepad++ to process the Ctrl+c
//   keycombo as a WM_KEYDOWN event, you'll get an "ETX" (hex code = 0x03) character in your document at the caret
//   position. This occurs because, with Notepad++ ignoring the Ctrl+c as a command, it thinks that you want to embed
//   a control character with value "3" into the doc (it gets a WM_CHAR message to that effect, which it would normally
//   filter out on its own). So... in the case of preventing copying data, we have to remove any Ctrl+c or Ctrl+x
//   characters that might get inserted; we do this by processing the WM_CHAR message and filtering those as well.
void sexp_tree::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
}

void sexp_tree::OnKeyDown(NMHDR *pNMHDR, LRESULT *pResult) 
{
	int key;
	TV_KEYDOWN *pTVKeyDown = (TV_KEYDOWN *) pNMHDR;

	key = pTVKeyDown->wVKey;

	// Handle clipboard operations for the sexp_tree and its subclasses.  The *proper* way to do this
	// would be to catch the WM_CUT, WM_COPY, and WM_PASTE messages, but I wasn't able to get it to work
	// despite considerable research and effort.  So this achieves the same result by just catching
	// the shortcut keys instead of the messages they produce.  The shortcut keys are still sent to
	// the window, so the OnChar handler is necessary to catch them and prevent an error beep.
	// 
	// Only capture keys on the nodes when we're not currently editing their text
	//
	if (GetEditControl() == nullptr)
	{
		if (GetKeyState(VK_CONTROL) & 0x8000)
		{
			// Currently the item_index and item_handle are only updated
			// when right-clicking, so we need to do another update
			// before processing the key press.  Ideally they should be
			// updated when the selection changes, but there are some
			// hidden side-effects to making it selection-dependent
			// that are difficult to track down.			
			if (key == 'X')
			{
				update_item(GetSelectedItem());
				NodeCut();
			}
			else if (key == 'C')
			{
				update_item(GetSelectedItem());
				NodeCopy();
			}
			else if (key == 'V')
			{
				if (GetKeyState(VK_SHIFT) & 0x8000) 
				{
					update_item(GetSelectedItem());
					NodeReplacePaste();
				}
				else 
				{
					update_item(GetSelectedItem());
					auto orig_handle = item_handle;
					NodeAddPaste();
					// when using the keyboard shortcut, stay on the original node after pasting
					SelectItem(orig_handle);
					update_item(orig_handle);
				}
			}
		}
	}

	if (key == VK_SPACE)
		EditLabel(GetSelectedItem());

	*pResult = 0;
}

// Determine if a given opf code has a restricted argument range (i.e. has a specific, limited
// set of argument values, or has virtually unlimited possibilities.  For example, boolean values
// only have true or false, so it is restricted, but a number could be anything, so it's not.
//
int sexp_tree::query_restricted_opf_range(int opf)
{
	switch (opf) {
		case OPF_NUMBER:
		case OPF_POSITIVE:
		case OPF_WHO_FROM:

		// Goober5000 - these are needed too (otherwise the arguments revert to their defaults)
		case OPF_STRING:
		case OPF_ANYTHING:
		case OPF_CONTAINER_VALUE: // jg18
		case OPF_DATA_OR_STR_CONTAINER: // jg18
			return 0;
	}

	return 1;
}

sexp_list_item* sexp_tree::get_listing_opf(int opf, int parent_node, int arg_index)
{
	auto model_list = m_model->buildListingForOpf(opf, parent_node, arg_index);

	return copy_from_model_list(model_list.get());
}

// Goober5000
int sexp_tree::find_argument_number(int parent_node, int child_node) const
{
	int arg_num = 0;
	const SexpNode& n = m_model->node(parent_node);
	int current_node = n.child;

	while (current_node >= 0) {
		if (current_node == child_node) {
			return arg_num;
		}

		arg_num++;
		// Correctly get the *current* node to find its *next* sibling
		const SexpNode& current_n = m_model->node(current_node);
		current_node = current_n.next;
	}

	return -1;
}

// Goober5000
// backtrack through parents until we find the operator matching
// parent_op, then find the argument we went through
int sexp_tree::find_ancestral_argument_number(int parent_op, int child_node) const
{
	if(child_node == -1)
		return -1;

	int parent_node;
	int current_node;

	current_node = child_node;
	auto& n = m_model->node(current_node);
	parent_node = n.parent;

	while (parent_node >= 0)
	{
		auto& parent_n = m_model->node(parent_node);
		// check if the parent operator is the one we're looking for
		if (get_operator_const(parent_n.text.c_str()) == parent_op)
			return find_argument_number(parent_node, current_node);

		// continue iterating up the tree
		current_node = parent_node;
		n = m_model->node(current_node);
		parent_node = n.parent;
	}

	// not found
	return -1;
}

/**
* Gets the proper data image for the tree item's place
* in its parent hierarchy.
*/
int sexp_tree::get_data_image(int node)
{
	int count = get_sibling_place(node) + 1;

	if (count <= 0) {
		return BITMAP_DATA;
	}

	if (count % 5) {
		return BITMAP_DATA;
	}

	int idx = (count % 100) / 5;

	int num = sizeof(Numbered_data_bitmaps)/sizeof(UINT);

	if (idx > num) {
		return BITMAP_DATA;
	}

	return BITMAP_NUMBERED_DATA + idx;
}

sexp_list_item *sexp_tree::get_container_modifiers(int con_data_node) const
{
	Assertion(con_data_node != -1, "Attempt to get modifiers for invalid container node. Please report!");

	auto& n = m_model->node(con_data_node);

	Assertion(n.type & SEXPT_CONTAINER_DATA,
		"Attempt to get modifiers for non-container data node %s. Please report!",
		n.text.c_str());

	const auto *p_container = get_sexp_container(n.text.c_str());
	Assertion(p_container,
		"Attempt to get modifiers for unknown container %s. Please report!",
		n.text.c_str());
	const auto &container = *p_container;

	sexp_list_item head;
	sexp_list_item *list = nullptr;

	if (container.is_list()) {
		list = get_list_container_modifiers();
	} else if (container.is_map()) {
		// start the list with "<argument>" if relevant
		if (is_node_eligible_for_special_argument(con_data_node) &&
			any(container.type & ContainerType::STRING_KEYS)) {
			head.add_data(SEXP_ARGUMENT_STRING, (SEXPT_VALID | SEXPT_STRING | SEXPT_MODIFIER));
		}

		list = get_map_container_modifiers(con_data_node);
	} else {
		UNREACHABLE("Unknown container type %d", (int)p_container->type);
	}

	if (list) {
		head.add_list(list);
	}

	return head.next;
}

int sexp_tree::get_sibling_place(int node)
{
	// 1. Get the parent of the node from the model.
	int parent_index = m_model->parentOf(node);

	// 2. If there's no parent, it's a root node and has no sibling place.
	if (parent_index < 0) {
		return -1;
	}

	int count = 0;
	// 3. Start with the first child of the parent.
	int current_sibling_index = m_model->firstChild(parent_index);

	// 4. Walk the sibling list until we find our node.
	while (current_sibling_index != -1) {
		if (current_sibling_index == node) {
			return count; // Found it, return the position.
		}

		count++;
		current_sibling_index = m_model->nextSibling(current_sibling_index);
	}

	// This case should ideally not be reached if the tree is consistent.
	return -1;
}

sexp_list_item *sexp_tree::get_list_container_modifiers() const
{
	sexp_list_item head;

	for (const auto &modifier : get_all_list_modifiers()) {
		head.add_data(modifier.name, SEXPT_VALID | SEXPT_MODIFIER | SEXPT_STRING);
	}

	return head.next;
}

// FIXME TODO: if you use this function with remove-from-map SEXP, don't use SEXPT_MODIFIER
sexp_list_item *sexp_tree::get_map_container_modifiers(int con_data_node) const
{
	sexp_list_item head;

	auto& n = m_model->node(con_data_node);

	Assertion(n.type & SEXPT_CONTAINER_DATA,
		"Found map modifier for non-container data node %s. Please report!",
		n.text.c_str());

	const auto *p_container = get_sexp_container(n.text.c_str());
	Assertion(p_container != nullptr,
		"Found map modifier for unknown container %s. Please report!", n.text.c_str());

	const auto &container = *p_container;
	Assertion(container.is_map(),
		"Found map modifier for non-map container %s with type %d. Please report!",
		n.text.c_str(),
		(int)container.type);

	int type = SEXPT_VALID | SEXPT_MODIFIER;
	if (any(container.type & ContainerType::STRING_KEYS)) {
		type |= SEXPT_STRING;
	} else if (any(container.type & ContainerType::NUMBER_KEYS)) {
		type |= SEXPT_NUMBER;
	} else {
		UNREACHABLE("Unknown map container key type %d", (int)container.type);
	}

	for (const auto &kv_pair : container.map_data) {
		head.add_data(kv_pair.first.c_str(), type);
	}

	return head.next;
}

// get potential options for container multidimensional modifiers
// the value could be either string or number, checked in-mission
sexp_list_item *sexp_tree::get_container_multidim_modifiers(int con_data_node) const
{
	auto& n = m_model->node(con_data_node);
	
	Assertion(con_data_node != -1,
		"Attempt to get multidimensional modifiers for invalid container node. Please report!");
	Assertion(n.type & SEXPT_CONTAINER_DATA,
		"Attempt to get multidimensional modifiers for non-container data node %s. Please report!",
		n.text.c_str());

	sexp_list_item head;

	if (is_node_eligible_for_special_argument(con_data_node)) {
		head.add_data(SEXP_ARGUMENT_STRING, (SEXPT_VALID | SEXPT_STRING | SEXPT_MODIFIER));
	}

	// the FREDder might want to use a list modifier
	sexp_list_item *list = get_list_container_modifiers();

	head.add_list(list);

	return head.next;
}

// given a node's parent, check if node is eligible for being used with the special argument
bool sexp_tree::is_node_eligible_for_special_argument(int parent_node) const
{
	Assertion(parent_node != -1,
		"Attempt to access invalid parent node for special arg eligibility check. Please report!");

	const int w_arg = find_ancestral_argument_number(OP_WHEN_ARGUMENT, parent_node);
	const int e_arg = find_ancestral_argument_number(OP_EVERY_TIME_ARGUMENT, parent_node);
	return w_arg >= 1 || e_arg >= 1; /* || the same for any future _ARGUMENT sexps */
}

// Deletes sexp_variable from sexp_tree.
// resets tree to not include given variable, and resets text and type
void sexp_tree::delete_sexp_tree_variable(const char *var_name)
{
	char search_str[64];
	char replace_text[TOKEN_LENGTH];
	
	sprintf(search_str, "%s(", var_name);

	// store old item index
	int old_item_index = item_index;

	for (int idx=0; idx<(int)m_model->size(); idx++) {
		auto& n = m_model->node(idx);
		if (n.type & SEXPT_VARIABLE) {
			if ( strstr(n.text.c_str(), search_str) != NULL ) {

				// check type is number or string
				Assert( (n.type & SEXPT_NUMBER) || (n.type & SEXPT_STRING) );

				// reset type as not variable
				int type = n.type &= ~SEXPT_VARIABLE;

				// reset text
				if (n.type & SEXPT_NUMBER) {
					strcpy_s(replace_text, "number");
				} else {
					strcpy_s(replace_text, "string");
				}

				// set item_index and replace data
				update_item(idx);
				replace_data(replace_text, type);
			}
		}
	}

	// restore item_index
	update_item(old_item_index);
}


// Modify sexp_tree for a change in sexp_variable (name, type, or default value)
void sexp_tree::modify_sexp_tree_variable(const char *old_name, int sexp_var_index)
{
	char search_str[64];
	int type;

	Assert(Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_SET);
	Assert( (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NUMBER) || (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_STRING) );

	// Get type for sexp_tree node
	if (Sexp_variables[sexp_var_index].type & SEXP_VARIABLE_NUMBER) {
		type = (SEXPT_NUMBER | SEXPT_VALID);
	} else {
		type = (SEXPT_STRING | SEXPT_VALID);
	}
															
	// store item index;
	int old_item_index = item_index;

	// Search string in sexp_tree nodes
	sprintf(search_str, "%s(", old_name);

	for (int idx=0; idx<(int)m_model->size(); idx++) {
		auto& n = m_model->node(idx);
		if (n.type & SEXPT_VARIABLE) {
			if ( strstr(n.text.c_str(), search_str) != NULL ) {
				// temp set item_index
				update_item(item_index);

				// replace variable data
				replace_variable_data(sexp_var_index, (type | SEXPT_VARIABLE));
			}
		}
	}

	// restore item_index
	update_item(old_item_index);
}


// convert from item_index to sexp_variable index, -1 if not
int sexp_tree::get_item_index_to_var_index()
{
	// check valid item index and node is a variable
	auto& n = m_model->node(item_index);
	if ( (item_index > 0) && (n.type & SEXPT_VARIABLE) ) {

		return get_tree_name_to_sexp_variable_index(n.text.c_str());
	} else {
		return -1;
	}
}

int sexp_tree::get_tree_name_to_sexp_variable_index(const char *tree_name)
{
	char var_name[TOKEN_LENGTH];

	auto chars_to_copy = strcspn(tree_name, "(");
	Assert(chars_to_copy < TOKEN_LENGTH - 1);

	// Copy up to '(' and add null termination
	strncpy(var_name, tree_name, chars_to_copy);
	var_name[chars_to_copy] = '\0';

	// Look up index
	return get_index_sexp_variable_name(var_name);
}

int sexp_tree::get_variable_count(const char *var_name)
{
	int idx;
	int count = 0;
	char compare_name[64];

	// get name to compare
	strcpy_s(compare_name, var_name);
	strcat_s(compare_name, "(");

	// look for compare name
	for (idx=0; idx<(int)m_model->size(); idx++) {
		auto& n = m_model->node(idx);
		if (n.type & SEXPT_VARIABLE) {
			if ( strstr(n.text.c_str(), compare_name) ) {
				count++;
			}
		}
	}

	return count;
}

// Returns the number of times a variable with this name has been used by player loadout
int sexp_tree::get_loadout_variable_count(int var_index)
{
	// we shouldn't be being passed the index of variables that do not exist
	Assert (var_index >= 0 && var_index < MAX_SEXP_VARIABLES); 

	int idx;
	int count = 0; 

	for (int i=0; i < MAX_TVT_TEAMS; i++) {
		for(idx=0; idx<Team_data[i].num_ship_choices; idx++) {
			if (!strcmp(Team_data[i].ship_list_variables[idx], Sexp_variables[var_index].variable_name)) {
				count++; 
			}

			if (!strcmp(Team_data[i].ship_count_variables[idx], Sexp_variables[var_index].variable_name)) {
				count++;
			}
		}

		for (idx=0; idx<Team_data[i].num_weapon_choices; idx++) {
			if (!strcmp(Team_data[i].weaponry_pool_variable[idx], Sexp_variables[var_index].variable_name)) {
				count++;
			}
			if (!strcmp(Team_data[i].weaponry_amount_variable[idx], Sexp_variables[var_index].variable_name)) {
				count++;
			}
		}
	}
	
	return count; 
}

int sexp_tree::get_container_usage_count(const SCP_string &container_name) const
{
	int count = 0;

	for (int node_idx = 0; node_idx < (int)m_model->size(); node_idx++) {
		if (is_matching_container_node(node_idx, container_name)) {
			count++;
		}
	}

	return count;
}

bool sexp_tree::rename_container_nodes(const SCP_string &old_name, const SCP_string &new_name)
{
	Assertion(!old_name.empty(),
		"Attempt to rename container nodes looking for empty name. Please report!");
	Assertion(!new_name.empty(),
		"Attempt to rename container nodes with empty name. Please report!");
	Assertion(new_name.length() <= sexp_container::NAME_MAX_LENGTH,
		"Attempt to rename container nodes with name %s that is too long (%d > %d). Please report!",
		new_name.c_str(),
		(int)new_name.length(),
		sexp_container::NAME_MAX_LENGTH);

	bool renamed_anything = false;

	for (int node_idx = 0; node_idx < (int)m_model->size(); node_idx++) {
		auto& n = m_model->node(node_idx);
		if (is_matching_container_node(node_idx, old_name)) {
			n.text = new_name;
			SetItemText(m_modelToHandle[node_idx], new_name.c_str());
			renamed_anything = true;
		}
	}

	return renamed_anything;
}

bool sexp_tree::is_matching_container_node(int node, const SCP_string &container_name) const
{
	auto& n = m_model->node(node);
	return (n.type & SEXPT_VALID) &&
		   (n.type & (SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA)) &&
		   !stricmp(n.text.c_str(), container_name.c_str());
}

bool sexp_tree::is_container_name_argument(int node) const
{
	Assertion(node >= 0 && node < (int)m_model->size(),
		"Attempt to check if out-of-range node %d is a container name argument. Please report!",
		node);

	auto& n = m_model->node(node);

	if (n.parent == -1) {
		return false;
	}

	const int arg_opf_type = query_node_argument_type(node);
	return is_container_name_opf_type(arg_opf_type);
}

bool sexp_tree::is_container_name_opf_type(const int op_type)
{
	return (op_type == OPF_CONTAINER_NAME) ||
		   (op_type == OPF_LIST_CONTAINER_NAME) ||
		   (op_type == OPF_MAP_CONTAINER_NAME);
}

// NEW FUNCTIONS-------------------------------------------------------------------

void sexp_tree::setFlags(SexpTreeFlag flags)
{
	_flagsBits = static_cast<SexpTreeFlagsBits>(flags);
}

void sexp_tree::addFlags(SexpTreeFlag flags)
{
	_flagsBits |= static_cast<SexpTreeFlagsBits>(flags);
}

void sexp_tree::clearFlags(SexpTreeFlag flags)
{
	_flagsBits &= ~static_cast<SexpTreeFlagsBits>(flags);
}

SexpTreeFlag sexp_tree::flags() const
{
	return static_cast<SexpTreeFlag>(_flagsBits);
}

bool sexp_tree::hasFlag(SexpTreeFlag flg) const
{
	return (_flagsBits & static_cast<SexpTreeFlagsBits>(flg)) != 0;
}

void sexp_tree::rebuild_model_from_sexp(int index, const char* def)
{
	// Build from the same root index the legacy tree uses
	const int model_root = m_model->loadTreeFromSexp(index);

	// If callers requested a non true default operator
	if (index < 0 && def && strcmp(def, "true") != 0) {
		const int new_op = get_operator_index(def);
		if (new_op >= 0) {
			m_model->replaceOperator(model_root, new_op);
		}
	}
}

sexp_list_item* sexp_tree::copy_from_model_list(const SexpListItem* src)
{
	sexp_list_item* head = nullptr;
	sexp_list_item* tail = nullptr;

	for (auto p = src; p; p = p->next) {
		auto* n = new sexp_list_item();
		n->type = p->type;
		n->op = p->op;
		n->text = p->text.c_str();
		n->next = nullptr;

		if (!head)
			head = n;
		else
			tail->next = n;
		tail = n;
	}
	return head;
}

void sexp_tree::print_model_recursive(int node_index, int indent)
{
	if (node_index < 0) {
		return;
	}

	// Create an indentation string
	SCP_string indent_str(indent * 2, ' ');

	// Get the node and print its info
	const SexpNode& n = m_model->node(node_index);
	mprintf(("%sNode %d: '%s' (Parent: %d, Next: %d, Child: %d)\n",
		indent_str.c_str(),
		node_index,
		n.text.c_str(),
		n.parent,
		n.next,
		n.child));

	// Recurse for children
	print_model_recursive(m_model->firstChild(node_index), indent + 1);

	// Recurse for siblings
	print_model_recursive(m_model->nextSibling(node_index), indent);
}

void sexp_tree::print_model_to_debug_output()
{
	mprintf(("--- Dumping SexpTreeModel State ---\n"));
	int model_root = -1;
	for (int i = 0; i < m_model->size(); i++) {
		if (m_model->parentOf(i) == -1) {
			model_root = i;
			// Don't break; print all roots if there are multiple for some reason
			print_model_recursive(model_root, 0);
		}
	}
	mprintf(("--- End of Dump ---\n"));
}

int sexp_tree::nodeIndexForActions(HTREEITEM h) const
{
	// For Events Editor or other "named root" trees, return -1 for the root item as it's not a real node
	if (!GetParentItem(h)) {
		return -1; // synthetic event root
	}
	return static_cast<int>(GetItemData(h));
}