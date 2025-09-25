#pragma once

#include "globalincs/pstypes.h"

class SexpTreeModel;
enum class SexpNodeKind;

// An extension of SexpTreeNode that provides a list of available actions the UI can take on any specific node

// High level buckets for menu grouping in UI
enum class SexpContextGroup {
	Node,      // delete, duplicate, cut/copy/paste, rename/edit text
	Structure, // add child/sibling, move up/down
	Operator,  // replace operator, set defaults
	Arguments  // add/remove args
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

	// structure
	MoveUp,
	MoveDown,

	// operator
	ReplaceOperator,
	ToggleNot,
	ResetToDefaults, // rebuild args to defaults for this op

	// arguments
	AddArgument,
	RemoveArgument,

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

	// Check if an action is valid for this node
	bool canEditNode(SexpNodeKind kind, int node_index) const;
	bool canDeleteNode(SexpNodeKind kind, int node_index) const;
	bool canCutNode(SexpNodeKind kind, int node_index) const;
	bool canCopyNode(SexpNodeKind kind, int node_index) const;
	bool canPasteOverrideNode(SexpNodeKind kind, int node_index) const;

	bool canPasteAddNode(SexpNodeKind kind, int node_index) const;

	// Action implementations
	bool editText(int node_index, const char* new_text);
	bool deleteNode(int node_index);
	bool cutNode(int node_index);
	bool copyNode(int node_index);
	bool pasteOverwrite(int node_index);

	bool pasteAdd(int node_index);
	bool moveUp(int node_index);
	bool moveDown(int node_index);
	bool replaceOperator(int node_index, const SexpActionParam* p);
	bool toggleNot(int node_index);
	bool resetToDefaults(int node_index);
	bool addArgument(int node_index);
	bool removeArgument(int node_index);
};