/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

#ifndef _SEXP_TREE_H
#define _SEXP_TREE_H

// 4786 is identifier truncated to 255 characters (happens all the time in Microsoft #includes) -- Goober5000
#pragma warning(disable: 4786)

#include "OperatorComboBox.h"
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "parse/parselo.h"

class SexpTreeModel;
struct SexpListItem;

// Goober5000 - it's dynamic now
//#define MAX_SEXP_TREE_SIZE 500
//#define MAX_SEXP_TREE_SIZE 1050
//#define MAX_SEXP_TREE_SIZE ((MAX_SEXP_NODES)*2/3)

// tree_node type
#define SEXPT_UNUSED	0x0000
#define SEXPT_UNINIT	0x0001
#define SEXPT_UNKNOWN	0x0002

#define SEXPT_VALID		0x1000
#define SEXPT_TYPE_MASK	0x07ff
#define SEXPT_TYPE(X)	(SEXPT_TYPE_MASK & X)

#define SEXPT_OPERATOR	0x0010
#define SEXPT_NUMBER	0x0020
#define SEXPT_STRING	0x0040
#define SEXPT_VARIABLE	0x0080
#define SEXPT_CONTAINER_NAME	0x0100
#define SEXPT_CONTAINER_DATA	0x0200
#define SEXPT_MODIFIER	0x0400

// tree_node flag
#define NOT_EDITABLE	0x00
#define OPERAND			0x01
#define EDITABLE		0x02
#define COMBINED		0x04

// Bitmaps
#define BITMAP_OPERATOR			0
#define BITMAP_DATA				1
#define BITMAP_VARIABLE			2
#define BITMAP_ROOT				3
#define BITMAP_ROOT_DIRECTIVE	4
#define BITMAP_CHAIN			5
#define BITMAP_CHAIN_DIRECTIVE	6
#define BITMAP_GREEN_DOT		7
#define BITMAP_BLACK_DOT		8
#define BITMAP_BLUE_DOT			BITMAP_ROOT
#define BITMAP_RED_DOT			BITMAP_ROOT_DIRECTIVE
#define BITMAP_NUMBERED_DATA	9
// There are 20 number bitmaps, 9 to 28, counting by 5s from 0 to 95
#define BITMAP_COMMENT			29
#define BITMAP_CONTAINER_NAME	30
#define BITMAP_CONTAINER_DATA	31


// tree behavior modes (or tree subtype)
#define ST_LABELED_ROOT		0x10000
#define ST_ROOT_DELETABLE	0x20000
#define ST_ROOT_EDITABLE	0x40000

#define MODE_GOALS		(1 | ST_LABELED_ROOT | ST_ROOT_DELETABLE)
#define MODE_EVENTS		(2 | ST_LABELED_ROOT | ST_ROOT_DELETABLE | ST_ROOT_EDITABLE)
#define MODE_CAMPAIGN	(3 | ST_LABELED_ROOT | ST_ROOT_DELETABLE)
#define MODE_CUTSCENES	(4 | ST_LABELED_ROOT | ST_ROOT_DELETABLE)

// various tree operations notification codes (to be handled by derived class)
#define ROOT_DELETED	1
#define ROOT_RENAMED	2

/*
 * Notes: An sexp_tree_item is basically a node in a tree.  The sexp_tree is an array of
 * these node items.
 */

class sexp_tree_item
{
public:
	sexp_tree_item() : type(SEXPT_UNUSED) {}

	int type;
	int parent;	// pointer to parent of this item
	int child;	// pointer to first child of this item
	int next;	// pointer to next sibling
	int flags;
	char text[2 * TOKEN_LENGTH + 2];
	HTREEITEM handle;
};

class sexp_list_item
{
public:
	int type;
	int op;
	SCP_string text;
	sexp_list_item *next;

	sexp_list_item() : next(nullptr) {}

	void set_op(int op_num);
	void set_data(const char *str, int t = (SEXPT_STRING | SEXPT_VALID));

	void add_op(int op_num);
	void add_data(const char *str, int t = (SEXPT_STRING | SEXPT_VALID));
	void add_list(sexp_list_item *list);

	void shallow_copy(const sexp_list_item *src);
	void destroy();
};

class sexp_tree : public CTreeCtrl
{
public:
	sexp_tree();
	virtual ~sexp_tree();

	int find_text(const char *text, int *find);
	int query_restricted_opf_range(int opf);
	void verify_and_fix_arguments(int node);
	void post_load();
	void update_help(HTREEITEM h);
	static const char *help(int code);
	HTREEITEM insert(LPCTSTR lpszItem, int image = BITMAP_ROOT, int sel_image = BITMAP_ROOT, HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
	HTREEITEM handle(int node);
	int get_type(HTREEITEM h);
	void setup(CEdit *ptr = NULL);
	int query_false(int node = -1);
	int add_default_operator(int op, int argnum);
	int get_default_value(sexp_list_item *item, char *text_buf, int op, int i);
	int query_default_argument_available(int op);
	int query_default_argument_available(int op, int i);
	void move_root(HTREEITEM source, HTREEITEM dest, bool insert_before);
	void move_branch(int source, int parent = -1);
	HTREEITEM move_branch(HTREEITEM source, HTREEITEM parent = TVI_ROOT, HTREEITEM after = TVI_LAST);
	void copy_branch(HTREEITEM source, HTREEITEM parent = TVI_ROOT, HTREEITEM after = TVI_LAST);
	void setup_selected(HTREEITEM h = NULL);
	void add_or_replace_operator(int op, int replace_flag = 0);
//	void replace_one_arg_operator(const char *op, const char *data, int type);
	void replace_operator(const char *op);
	void replace_data(const char *data, int type);
	void replace_variable_data(int var_idx, int type);
	void replace_container_name(const sexp_container &container);
	void replace_container_data(const sexp_container &container,
		int type,
		bool test_child_nodes,
		bool delete_child_nodes,
		bool set_default_modifier);
	void add_default_modifier(const sexp_container &container);
	void link_modified(int *ptr);
	void ensure_visible(int node);
	int node_error(int node, const char *msg, int *bypass);
	void expand_branch(HTREEITEM h);
	void expand_operator(int node);
	void merge_operator(int node);
	int end_label_edit(TVITEMA &item);
	int edit_label(HTREEITEM h, bool *is_operator = nullptr);
	virtual void edit_comment(HTREEITEM h);
	virtual void edit_bg_color(HTREEITEM h);
	int identify_arg_type(int node);
	void right_clicked(int mode = 0);
	int ctree_size;
	virtual void build_tree();
	void clear_tree(const char *op = NULL);
	int save_tree(int node = -1);
	void load_tree(int index, const char *deflt = "true");
	void add_operator(const char *op, HTREEITEM h = TVI_ROOT);
	HTREEITEM add_data(const char* data, int type);
	int add_variable_data(const char *data, int type);
	int add_container_name(const char *container_name);
	void add_container_data(const char *container_name);
	void add_sub_tree(int node, HTREEITEM root);
	int load_sub_tree(int index, bool valid, const char *text);
	void hilite_item(int node);
	const SCP_string &match_closest_operator(const SCP_string &str, int node);
	void delete_sexp_tree_variable(const char *var_name);
	void modify_sexp_tree_variable(const char *old_name, int sexp_var_index);
	int get_item_index_to_var_index();
	int get_tree_name_to_sexp_variable_index(const char *tree_name);
	int get_modify_variable_type(int parent);
	int get_variable_count(const char *var_name);
	int get_loadout_variable_count(int var_index);

	// Karajorma/jg18
	int get_container_usage_count(const SCP_string &container_name) const;
	bool rename_container_nodes(const SCP_string &old_name, const SCP_string &new_name);
	bool is_matching_container_node(int node, const SCP_string &container_name) const;
	bool is_container_name_argument(int node) const;
	static bool is_container_name_opf_type(int op_type);

	// Goober5000
	int find_argument_number(int parent_node, int child_node) const;
	int find_ancestral_argument_number(int parent_op, int child_node) const;
	int query_node_argument_type(int node) const;

	//WMC
	int get_sibling_place(int node);
	int get_data_image(int node);


	sexp_list_item *get_listing_opf(int opf, int parent_node, int arg_index);

	// container modifier options for container data nodes
	sexp_list_item *get_container_modifiers(int con_data_node) const;
	sexp_list_item *get_list_container_modifiers() const;
	sexp_list_item *get_map_container_modifiers(int con_data_node) const;
	sexp_list_item *get_container_multidim_modifiers(int con_data_node) const;

	bool is_node_eligible_for_special_argument(int parent_node) const;

	int m_mode;
	int item_index;
	int select_sexp_node;  // used to select an sexp item on dialog box open.
	BOOL		m_dragging;
	HTREEITEM	m_h_drag;
	HTREEITEM	m_h_drop;
	CImageList	*m_p_image_list;
	CEdit *help_box;
	CEdit *mini_help_box;
	CPoint m_pt;
	OperatorComboBox m_operator_box;
	SCP_unordered_map<int, HTREEITEM> m_modelToHandle;

	void start_operator_edit(HTREEITEM h);
	void end_operator_edit(bool confirm);

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(sexp_tree)
	public:
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

	void update_item(HTREEITEM handle);
	void update_item(int node);

	// NEW STUFF-------------------------------------------------------------------------------------------------
	// The shared, UI-agnostic model
	std::unique_ptr<SexpTreeModel> m_model;

	// Ensure model exists and is loaded from current Sexp_nodes root.
	void rebuild_model_from_sexp(int index, const char* deflt);

	// Convert a model SexpListItem chain into legacy sexp_list_item chain
	static sexp_list_item* copy_from_model_list(const SexpListItem* src);

	// Get the node index for a given tree item handle's actions
	int nodeIndexForActions(HTREEITEM h) const;

	// TODO delete these before merge!
	void print_model_to_debug_output();
	void print_model_recursive(int node_index, int indent);

	// Generated message map functions
protected:
	//{{AFX_MSG(sexp_tree)
	afx_msg void OnBegindrag(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG

	virtual void NodeCut();
	virtual void NodeDelete();
	virtual void NodeCopy();
	virtual void NodeReplacePaste();
	virtual void NodeAddPaste();

	int load_branch(int index, int parent);
	int save_branch(int cur, int at_root = 0);

	int flag;
	int *modified;
	bool m_operator_popup_active;
	bool m_operator_popup_created;
	int m_font_height;
	int m_font_max_width;

	HTREEITEM item_handle;
	int root_item;
	// these 2 variables are used to help location data sources.  Sometimes looking up
	// valid data can require complex code just to get to an index that is required to
	// locate data.  These are set up in right_clicked() to try and short circuit having
	// to do the lookup again in the code that actually does the adding or replacing of
	// the data if it's selected.
	int add_instance;  // a source reference index indicator for adding data
	int replace_instance;  // a source reference index indicator for replacing data

	DECLARE_MESSAGE_MAP()
};

#endif
