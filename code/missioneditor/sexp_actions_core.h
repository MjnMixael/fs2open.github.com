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
	// node
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
};

// A single item the UI can render
struct SexpContextAction {
	SexpContextGroup group;
	SexpActionId id;
	SCP_string label; // display name
	bool enabled = true;

	// If this is a chooser action, the UI might draw a submenu using 'choices'
	SCP_vector<SexpActionParam> choices; // list of operators to choose from
	SCP_vector<SCP_string> choiceText;   // same size as choices (labels) //TODO remove this
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