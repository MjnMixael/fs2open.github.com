#pragma once

/*
 * sexp_tree_core.h
 * Shared, UI-agnostic SEXP tree model for FRED and QtFRED.
 */

// Engine headers (shared between FRED & QtFRED)
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "parse/parselo.h"

#include "globalincs/pstypes.h"   // SCP_string, SCP_vector
#include <functional>
#include <optional>

// tree_node type
#define SEXPT_UNUSED 0x0000
#define SEXPT_UNINIT 0x0001
#define SEXPT_UNKNOWN 0x0002

#define SEXPT_VALID 0x1000
#define SEXPT_TYPE_MASK 0x07ff
#define SEXPT_TYPE(X) (SEXPT_TYPE_MASK & X)

#define SEXPT_OPERATOR 0x0010
#define SEXPT_NUMBER 0x0020
#define SEXPT_STRING 0x0040
#define SEXPT_VARIABLE 0x0080
#define SEXPT_CONTAINER_NAME 0x0100
#define SEXPT_CONTAINER_DATA 0x0200
#define SEXPT_MODIFIER 0x0400

// tree_node flag
#define NOT_EDITABLE 0x00
#define OPERAND 0x01
#define EDITABLE 0x02
#define COMBINED 0x04

struct SexpNode {
	int type = SEXPT_UNUSED;
	int parent = -1;
	int child  = -1;
	int next   = -1;
	int flags  = 0;
	SCP_string text;
};

// Minimal list item used for building argument-choice lists (OPF listings)
struct SexpListItem {
	int type = 0;
	int op   = 0;
	SCP_string text;
	SexpListItem* next = nullptr;

	// Construction helpers
	void set_op(int op_num);
	void set_data(const char* str, int t = (SEXPT_STRING | SEXPT_VALID));

	void add_op(int op_num);
	void add_data(const char* str, int t = (SEXPT_STRING | SEXPT_VALID));
	void add_list(SexpListItem* list);
	void destroy();
};

// Environment/adapter interface for data the model must *read* from the editor/game state.
// Implemented by each the editor and injected into the model.
struct ISexpEnvironment {
	virtual ~ISexpEnvironment() = default;

	// Events editor can pass an override to use the local message list else use Messages[] skipping builtins.
	virtual SCP_vector<SCP_string> getMessageNames() const;

	// Campaign editor can override to provide all loaded missions else use the current mission only.
	virtual SCP_vector<SCP_string> getMissionNames() const;

	// Campaign editor can override this to return true else returns false.
	virtual bool isCampaignContext() const;

	// Examples of lookups the model may need (add as we migrate code):
	/*virtual SCP_vector<SCP_string> getMessages();
	virtual SCP_vector<SCP_string> getPersonaNames();
	virtual SCP_vector<SCP_string> getMissionNames();
	virtual int getRootReturnType() const;

	// Dynamic (Lua) enums
	virtual int getDynamicEnumPosition(const SCP_string& name);
	virtual SCP_vector<SCP_string> getDynamicEnumList(int pos);
	virtual bool isLuaOperator(int op_const) const;
	virtual int getDynamicParameterIndex(const SCP_string& op_name, int arg_index);
	virtual SCP_string getChildEnumSuffix(const SCP_string& op_name, int arg_index);*/
};

// Core SEXP tree model
class SexpTreeModel {
  public:
	SexpTreeModel();
	explicit SexpTreeModel(ISexpEnvironment* env);

	// ---------- Model lifetime & configuration ----------
	void setEnvironment(ISexpEnvironment* env);
	ISexpEnvironment* environment() const;

	// Clears nodes and resets invariants
	void clear();

	// ---------- Node storage & navigation ----------
	// Returns the index of a newly allocated node.
	int allocateNode(int parent = -1, int after_sibling = -1);
	// Releases a node
	void freeNode(int node_index, bool cascade = false);
	// Accessors
	const SexpNode& node(int index) const;
	SexpNode& node(int index);
	int size() const; // number of allocated nodes
	int liveCount() const; // number of non SEXPT_UNUSED nodes

	// ---------- Load/Save ----------
	int loadTreeFromSexp(int sexp_root); // returns root node index in this model
	int saveTreeToSexp(int model_root);  // returns sexp node handle

	// ---------- Semantics & validation ----------
	bool queryFalse(int node_index = -1) const;
	int countArgs(int node_index) const;
	int identifyArgType(int first_arg_node_index) const;

	// ---------- OPF listing ----------
	// Entry point used by editors to populate dropdowns for an argument.
	// Returns the head of a single-linked list (owned by caller; use SexpListItem::destroy).
	SexpListItem* buildListingForOpf(int opf, int parent_node, int arg_index);

  private:
	ISexpEnvironment* _env = nullptr;
	SCP_vector<SexpNode> _nodes;

	static void freeNodeChain(SCP_vector<SexpNode>& nodes, int node);
	static void getCombinedVariableName(SCP_string& out, const char* sexp_var_name);
	static void varNameFromTreeText(SCP_string& out, const SCP_string& text);
	static int saveBranchRecursive(const SCP_vector<SexpNode>& nodes, int cur, bool at_root);

	int loadBranchRecursive(int sexp_idx, int model_parent_idx);
};
