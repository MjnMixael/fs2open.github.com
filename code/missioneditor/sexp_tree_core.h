#pragma once

/*
 * sexp_tree_core.h
 * Shared, UI-agnostic SEXP tree model for FRED and QtFRED.
 */

// Engine headers (shared between FRED & QtFRED)
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "parse/parselo.h"

#include "globalincs/pstypes.h"
#include "sexp_actions_core.h"
#include "sexp_opf_core.h"

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

enum class ArgBucket {
	STR,
	NUM,
	BOOL
};

enum class SexpNodeKind {
	SyntheticRoot, // e.g., event name row
	RealNode       // actual SEXP node in the model
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

	// Allows the environment to override the default enabled state of an action.
	virtual void overrideNodeActionEnabled(SexpActionId id, SexpNodeKind kind, int node_index, bool& is_enabled) const;

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
	friend class SexpActionsHandler; 

	SexpTreeModel();
	explicit SexpTreeModel(ISexpEnvironment* env);
	~SexpTreeModel();

	// Model lifetime & configuration
	void setEnvironment(ISexpEnvironment* env);
	ISexpEnvironment* environment() const;

	// Clears nodes and resets invariants
	void clear();

	// Node storage & navigation
	// Returns the index of a newly allocated node.
	int allocateNode(int parent = -1, int after_sibling = -1);
	// Releases a node
	void freeNode(int node_index, bool cascade = false);
	// Accessors
	const SexpNode& node(int index) const;
	SexpNode& node(int index);
	int size() const; // number of allocated nodes
	int liveCount() const; // number of non SEXPT_UNUSED nodes

	// Model editing primitives
	void setNode(int index, int type, const char* text);
	void detachFromParent(int index);                        // does not change index.child subtree
	void appendAsChild(int parent_index, int index);         // appends existing node under parent
	void moveBranch(int source_index, int new_parent_index); // move whole subtree under new parent

	// Create an OPERATOR node from Operators[op_index] and append under (parent, after_sibling).
	// Returns the new node index.
	int makeOperatorNode(int op_index, int parent = -1, int after_sibling = -1);

	// Replace an existing operator node with another operator (by op_index).
	// Keeps existing child list, then enforces min/max arity and default args.
	void replaceOperator(int node_index, int new_op_index);

	// Ensure the operator node has at least min args (append defaults) and
	// at most max args (trim tail), mirroring legacy semantics for min/max.
	void ensureOperatorArity(int op_node, int op_index);

	// Load/Save
	int loadTreeFromSexp(int sexp_root); // returns root node index in this model
	int saveTreeToSexp(int model_root);  // returns sexp node handle

	// Semantics & validation
	bool queryFalse(int node_index = -1) const;
	int countArgs(int node_index) const;
	int identifyArgType(int first_arg_node_index) const;

	// Simple traversal helpers
	int parentOf(int index) const;
	int firstChild(int index) const;
	int nextSibling(int index) const;

	// Flags
	void applyDefaultFlags(int index);
	bool isEditable(int index) const;
	void setEditable(int index, bool on);
	bool isOperand(int index) const;
	void setOperand(int index, bool on);
	bool isCombined(int index) const;
	void setCombined(int index, bool on);

	// Actions
	SexpContextMenu queryContextMenu(int node_index) const;
	bool executeAction(int node_index, SexpActionId id, const SexpActionParam* param = nullptr);

	// OPF listing
	// Entry point used by editors to populate dropdowns for an argument.
	SexpListItemPtr buildListingForOpf(int opf, int parent_node, int arg_index);

  private:
	ISexpEnvironment* _env = nullptr;
	SCP_vector<SexpNode> _nodes;
	std::unique_ptr<SexpActionsHandler> _actions;

	static void freeNodeChain(SCP_vector<SexpNode>& nodes, int node);
	static void getCombinedVariableName(SCP_string& out, const char* sexp_var_name);
	static void varNameFromTreeText(SCP_string& out, const SCP_string& text);
	int getModifyVariableType(int parent_index) const;
	int getTreeNameToSexpVariableIndex(const char* tree_name) const;
	static int saveBranchRecursive(const SCP_vector<SexpNode>& nodes, int cur, bool at_root);
	static int find_operator_index_by_value(int value);

	int nodeFlags(int index) const;
	void setNodeFlags(int index, int flags);
	// Compute recommended flags for a node's current type/text.
	// (Call this after setNode/replaceOperator/etc. to initialize defaults.)
	int computeDefaultFlagsFor(const SexpNode& n) const;

	ArgBucket opf_to_bucket(int opf) const;
	ArgBucket node_to_bucket(const SexpNode& n) const;
	bool argsCompatibleWithOperator(int op_node, int new_op_index) const;
	
	// Create a default argument node for the OPF/type of operator `op_index` at arg position `arg_i`.
	// - `parent` is the model index of the operator to attach the new arg under
	// - `context_index` is the "current" tree index used by legacy helpers (e.g., get_modify_variable_type)
	int createDefaultArgForOpf(int opf, int parent, int op_index, int arg_i, int context_index);

	int loadBranchRecursive(int sexp_idx, int model_parent_idx);
};
