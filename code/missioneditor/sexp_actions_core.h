#pragma once

#include "globalincs/pstypes.h"

class SexpTreeModel;
enum class SexpNodeKind;

// An extension of SexpTreeNode that provides a list of available actions the UI can take on any specific node

// Not currently used but can provide the editor a way to group actions together
enum class SexpContextGroup {
	Node,      // Basic stuff like delete, duplicate, cut/copy/paste, rename/edit text
	Structure, // Organizational actions like move up/down
	Operator,  // operator specific actions
	Data,      // OPF data
	Variable,  // variable data
	Container, // container data
};

// Atomic actions the UI can render as menu items or toolbar buttons
enum class SexpActionId : int {
	None,

	EditText,
	DeleteNode,
	Cut,
	Copy,
	PasteOverwrite,
	PasteAdd,

	AddOperator,
	AddData,

	InsertOperator,
	ReplaceOperator,
	ReplaceData,

	ReplaceVariable,
	ReplaceContainerName,
	ReplaceContainerData,

	// New actions
	MoveUp,
	MoveDown,
	ResetToDefaults, // rebuild args to defaults for this op

	Num_sexp_actions // keep last
};

// Payload for actions that need parameters
struct SexpActionParam {
	int op_index = -1;
	int arg_index = -1;
	int node_type = 0;   // SEXPT_ type bitmask for direct node creation (0 = derive from arg_index/op_index)
	SCP_string text;     // for data-insert actions: the text to insert
};

// One item the UI can render: either a leaf action (no children), or a submenu (has children).
// If 'children' is non-empty, treat this as a submenu header and ignore 'param'.
// If 'children' is empty, this is a leaf: the UI calls executeAction(node_index, id, &param).
struct SexpContextAction {
	SexpContextGroup group;
	SexpActionId id;
	SCP_string label; // display name
	bool enabled = true;

	// For leaf actions: the parameter to pass when this item is selected
	SexpActionParam param;

	// Non-empty => this is a submenu; render children recursively
	SCP_vector<SexpContextAction> children;
};

// The complete answer the model gives to a right click
struct SexpContextMenu {
	int node_index = -1;
	SCP_vector<SexpContextAction> actions;
};

class SexpActionsHandler final {
  public:
	explicit SexpActionsHandler(SexpTreeModel* m) : model(m)
	{
	}

	SexpContextMenu buildContextMenuModel(int node_index) const;
	bool performAction(int node_index, SexpActionId id, const SexpActionParam* param = nullptr);

  private:
	SexpTreeModel* model;

	// Helpers
	int nodeEffectiveType_(int node_index) const;
	static bool opfAcceptsOperator_(int opf) noexcept;
	static bool opfAcceptsPlainData_(int opf) noexcept;
	int expectedOpfForAppend_(int parent_index) const noexcept;

	// Returns true if the operator should not be shown in the menus per legacy rules.
	static bool isHiddenByLegacyRules_(int op_value) noexcept;

	// Build a full categorized operator submenu under parent_action.
	// leaf_id is the SexpActionId assigned to each leaf node.
	// is_enabled_fn(op_index) returns whether that leaf should be enabled.
	// exclude_op_value: if >= 0, skip the operator with this Operators[].value.
	void buildCategorizedOperatorSubmenu_(SexpContextAction* parent_action, SexpActionId leaf_id,
		std::function<bool(int /*op_index*/)> is_enabled_fn, int exclude_op_value = -1) const;

	// Clipboard restore: recursively create model nodes from _clipboard[clip_idx]
	// (and its child chain) as children of model_parent_idx.
	// Returns the first newly created node index, or -1 on failure.
	int restoreClipboardSubtree_(int clip_idx, int model_parent_idx) const;

	// Find the previous sibling of node_index under its parent.
	// Returns -1 if node_index is the first child.
	int findPreviousSibling_(int parent_idx, int node_index) const noexcept;

	// Count how many siblings precede node_index under parent_idx (i.e., its 0-based argument position).
	int argPositionOf_(int parent_idx, int node_index) const noexcept;

	// Check if an action is valid for this node
	bool canEditNode(SexpNodeKind kind, int node_index) const;
	bool canDeleteNode(SexpNodeKind kind, int node_index) const;
	bool canCutNode(SexpNodeKind kind, int node_index) const;
	bool canCopyNode(SexpNodeKind kind, int node_index) const;
	bool canPasteOverrideNode(SexpNodeKind kind, int node_index) const;
	bool canAddOperatorNode(SexpNodeKind kind, int node_index) const;
	bool canAddDataNode(SexpNodeKind kind, int node_index) const;
	bool canPasteAddNode(SexpNodeKind kind, int node_index) const;
	bool canInsertOperatorNode(SexpNodeKind kind, int node_index) const;
	bool canReplaceOperatorNode(SexpNodeKind kind, int node_index) const;
	bool canReplaceDataNode(SexpNodeKind kind, int node_index) const;
	bool canReplaceVariableNode(SexpNodeKind kind, int node_index) const;
	bool canReplaceContainerNameNode(SexpNodeKind kind, int node_index) const;
	bool canReplaceContainerDataNode(SexpNodeKind kind, int node_index) const;

	// Action implementations
	bool editText(int node_index, const char* new_text);
	bool deleteNode(int node_index);
	bool cutNode(int node_index);
	bool copyNode(int node_index);
	bool pasteOverwrite(int node_index);
	bool addOperator(int node_index, const SexpActionParam* p);
	bool addData(int node_index, const SexpActionParam* p);
	bool pasteAdd(int node_index);
	bool insertOperator(int node_index, const SexpActionParam* p);
	bool replaceOperator(int node_index, const SexpActionParam* p);
	bool replaceData(int node_index, const SexpActionParam* p);
	bool replaceVariable(int node_index, const SexpActionParam* p);
	bool replaceContainerName(int node_index, const SexpActionParam* p);
	bool replaceContainerData(int node_index, const SexpActionParam* p);

	bool moveUp(int node_index);
	bool moveDown(int node_index);
	bool toggleNot(int node_index);
	bool resetToDefaults(int node_index);
	bool addArgument(int node_index);
	bool removeArgument(int node_index);
};