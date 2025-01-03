#include "doc_html.h"
#include "utils/string_utils.h"

#include <regex>

namespace scripting {
namespace {

static SCP_vector<std::pair<SCP_string, SCP_string>> Hook_variables;

void ade_output_type_link(FILE* fp, const ade_type_info& type_info)
{
	switch (type_info.getType()) {
	case ade_type_info_type::Empty:
		fputs("nil", fp);
		break;
	case ade_type_info_type::Simple:
		fputs(type_info.getIdentifier(), fp);
		break;
	case ade_type_info_type::Tuple: {
		bool first = true;
		for (const auto& type : type_info.elements()) {
			if (!first) {
				fprintf(fp, ", ");
			}
			first = false;

			ade_output_type_link(fp, type);
		}
		break;
	}
	case ade_type_info_type::Array:
		fputs("{ ", fp);
		ade_output_type_link(fp, type_info.arrayType());
		fputs(" ... }", fp);
		break;
	case ade_type_info_type::Map:
		fputs("table", fp);

		if (!type_info.elements().empty() && type_info.elements().size() == 2) {
			fprintf(fp, "<");
			ade_output_type_link(fp, type_info.elements()[0]);
			fprintf(fp, ", ");
			ade_output_type_link(fp, type_info.elements()[1]);
			fprintf(fp, ">");
		}
		break;
	case ade_type_info_type::Iterator:
		ade_output_type_link(fp, type_info.arrayType());
		fputs("[]", fp);
		break;
	case ade_type_info_type::Alternative: {
		bool first = true;
		for (const auto& type : type_info.elements()) {
			if (!first) {
				fputs(" | ", fp);
			}
			first = false;

			ade_output_type_link(fp, type);
		}
		break;
	}
	case ade_type_info_type::Function: {
		const auto& elements = type_info.elements();
		fputs("fun(", fp);
		int count = 1;
		for (auto iter = elements.begin() + 1; iter != elements.end(); ++iter) {
			if (count > 1) {
				fputs(", ", fp);
			}
			SCP_string name = iter->getName();
			SCP_string paramName = name.empty() ? "param" + std::to_string(count) : name;

			// Handle special strings
			if (paramName == "local") {
				paramName = "localVal";
			}

			if (paramName == "end") {
				paramName = "endVal";
			}

			if (iter->getType() == ade_type_info_type::Varargs) {
				paramName = "...";
			}

			fprintf(fp, "%s: ", paramName.c_str());
			ade_output_type_link(fp, *iter);
			count++;
		}
		fputs(")", fp);
		break;
	}
	case ade_type_info_type::Generic: {
		const auto& elements = type_info.elements();
		ade_output_type_link(fp, elements.front());
		if (elements.size() > 1) {
			fputs("<", fp);
			bool first = true;
			for (auto iter = elements.begin() + 1; iter != elements.end(); ++iter) {
				if (!first) {
					fputs(", ", fp);
				}
				first = false;

				ade_output_type_link(fp, *iter);
			}
			fputs(">", fp);
		}
		break;
	}
	case ade_type_info_type::Varargs: {
		ade_output_type_link(fp, type_info.elements().front());
		//fputs("...", fp);
		break;
	}
	default:
		Assertion(false, "Unhandled type!");
		break;
	}
}

SCP_string escapeNewlines(const SCP_string& str)
{
	SCP_string result = str;
	std::replace(result.begin(), result.end(), '\r', ' '); // Replace carriage returns
	std::replace(result.begin(), result.end(), '\n', ' '); // Replace newlines
	return result;
}

SCP_string buildFullPath(const SCP_vector<const DocumentationElement*>& parents, bool func = false)
{
	SCP_string fullPath;
	if (!parents.empty()) {
		for (size_t i = 0; i < parents.size(); ++i) {
			SCP_string name = parents[i]->shortName.empty() ? parents[i]->name : parents[i]->shortName; // Use shortName if available, otherwise name
			fullPath += name;
			if (i < parents.size() - 1) { // Not the last element
				fullPath += ".";
			} else if (func && parents[i]->type == ElementType::Class) { // Last element and is a class
				fullPath += ":";
			} else { // Last element and is not a class (e.g., library)
				fullPath += ".";
			}
		}
	}
	return fullPath;
}

// This is kind of a hacky check but it's the best we can do given the quirks of the FSO API
bool isTableLikeLibrary(const DocumentationElement* el)
{
	if (!el) {
		return false; // Null pointer safety check
	}

	return el->shortName.empty() && !el->children.empty() && util::isStringOneOf(el->children[0]->name, {"__indexer", "__len"});
}

// Function to output a Library
void outputLibrary(FILE* fp, const DocumentationElement* el, const SCP_vector<const DocumentationElement*>& parents)
{
	SCP_string fullPath = buildFullPath(parents);
	SCP_string libraryName = el->shortName.empty() ? el->name : el->shortName;

	if (isTableLikeLibrary(el)) {
		// Document as a table of objects
		const auto first = static_cast<DocumentationElementFunction*>(el->children.front().get());
		fprintf(fp, "\n--- @field %s table<string, ", el->name.c_str());
		ade_output_type_link(fp, first->returnType);
		fprintf(fp, "> %s", escapeNewlines(el->description).c_str());
	} else {
		// Document as a regular library
		fprintf(fp, "\n\n--- %s: %s", el->name.c_str(), escapeNewlines(el->description).c_str());
		fprintf(fp, "\n--- @class %s", el->name.c_str());
	}
}


// Function to output a Class
void outputClass(FILE* fp, const DocumentationElement* el, const SCP_vector<const DocumentationElement*>& parents)
{
	SCP_string escapedDescription;
	if (!el->description.empty()) {
		escapedDescription = escapeNewlines(el->description);
	}

	fprintf(fp, "\n\n-- %s object: %s", el->name.c_str(), escapedDescription.c_str());
	fprintf(fp, "\n%s = {}", el->name.c_str());
	fprintf(fp, "\n--- @class %s", el->name.c_str());
}

// Function to output a Function/Operator
void outputFunction(FILE* fp, const DocumentationElement* el, const SCP_vector<const DocumentationElement*>& parents)
{
	const auto funcEl = static_cast<const DocumentationElementFunction*>(el);
	if (funcEl) {
		for (const auto& overload : funcEl->overloads) {
			SCP_string functionName = el->name.empty() ? el->shortName : el->name;
			
			// Functions
			if (functionName.rfind("__", 0) != 0) {
				// Handle regular functions
				fprintf(fp, "\n--- @field %s fun(", functionName.c_str());

				// Check if the most immediate parent is a Library
				bool isLibraryParent = !parents.empty() && parents.back()->type == ElementType::Library;

				int count = 1;
				if (!isLibraryParent) {
					// Include self: self if the parent is not a Library
					fputs("self: self", fp);
					count++;
				}

				// List function arguments
				for (const auto& arg : overload.arguments) {
					if (count > 1) {
						fputs(", ", fp);
					}
					SCP_string paramName = arg.name.empty() ? "param" + std::to_string(count) : arg.name;

					// Handle special strings
					if (paramName == "local") {
						paramName = "localVal";
					}

					if (paramName == "end") {
						paramName = "endVal";
					}

					if (arg.optional) {
						paramName += "?";
					}

					if (arg.type.getType() == ade_type_info_type::Varargs) {
						paramName = "...";
					}

					fprintf(fp, "%s: ", paramName.c_str());
					ade_output_type_link(fp, arg.type);
					count++;
				}

				// Add return type
				fputs("): ", fp);
				ade_output_type_link(fp, funcEl->returnType);

				if (!el->deprecationMessage.empty()) {
					SCP_string escapedMessage = escapeNewlines(el->deprecationMessage);
					SCP_string version = gameversion::format_version(el->deprecationVersion);
					fprintf(fp, " DEPRECATED %s: %s -- ", version.c_str(), escapedMessage.c_str());
				}

				// Add description
				if (!el->description.empty()) {
					SCP_string escapedDescription = escapeNewlines(el->description);
					fprintf(fp, " %s", escapedDescription.c_str());
				}

			// Metamethods
			} else {
				// skip these for now because lua language server doesn't support them as they are technically implicit in Lua
				if (util::isStringOneOf(functionName, {"__indexer", "__len", "__tostring", "__eq", "__newindex"})) {
					return;
				}

				functionName.erase(0, 2);
				fprintf(fp, "\n--- @operator %s(", functionName.c_str());

				//for (const auto& arg : overload.arguments) {
					//ade_output_type_link(fp, arg.type);
				//}

				// Documentation seems to only allow one argument for a metamethod?
				// Still need to solve this whole metamethod thing...
				ade_output_type_link(fp, overload.arguments[0].type);

				fputs("): ", fp);
				ade_output_type_link(fp, funcEl->returnType);

				if (!el->deprecationMessage.empty()) {
					SCP_string escapedMessage = escapeNewlines(el->deprecationMessage);
					SCP_string version = gameversion::format_version(el->deprecationVersion);
					fprintf(fp, " DEPRECATED %s: %s -- ", version.c_str(), escapedMessage.c_str());
				}

				// Add description
				if (!el->description.empty()) {
					SCP_string escapedDescription = escapeNewlines(el->description);
					fprintf(fp, " # %s", escapedDescription.c_str());
				}

			}
		}
	}
}


// Function to output a Property
void outputProperty(FILE* fp, const DocumentationElement* el, const SCP_vector<const DocumentationElement*>& parents)
{
	if (el) {
		const auto propEl = static_cast<const DocumentationElementProperty*>(el);
		SCP_string propName = propEl->name.empty() ? propEl->shortName : propEl->name;
		// Mark fields as optional to suppress downstream warnings if a script is creating a dummy version of the type
		propName += "?";

		fprintf(fp, "\n--- @field %s ", propName.c_str());
		ade_output_type_link(fp, propEl->getterType);

		if (!propEl->returnDocumentation.empty()) {
			fprintf(fp, " %s", propEl->returnDocumentation.c_str());
		}

		if (!el->deprecationMessage.empty()) {
			SCP_string escapedMessage = escapeNewlines(el->deprecationMessage);
			SCP_string version = gameversion::format_version(el->deprecationVersion);
			fprintf(fp, " DEPRECATED %s: %s -- ", version.c_str(), escapedMessage.c_str());
		}

		fprintf(fp, " %s", propEl->description.empty() ? "No description available." : propEl->description.c_str());
	}
}

void parseFunctionSignature(SCP_string& input)
{
	// Regular expression to match the input format
	std::regex functionRegex(R"(function\((.*?)\s(\w+)\)\s->\s(.+))");
	std::smatch match;

	if (std::regex_match(input, match, functionRegex)) {
		// Extract parameter types, parameter name, and return type
		SCP_string paramTypes = match[1].str(); // e.g., "number | string | nil"
		SCP_string paramName = match[2].str();  // e.g., "result"
		SCP_string returnType = match[3].str(); // e.g., "nil"

		// Format the output as desired
		input =  "fun(" + paramName + ": " + paramTypes + "): " + returnType;
	}
}

void OutputElement(FILE* fp, const std::unique_ptr<DocumentationElement>& el, const SCP_vector<const DocumentationElement*>& parents)
{
	if (!el || (el->name.empty() && el->shortName.empty())) {
		Warning(LOCATION, "Found ade_table_entry with no name or shortname");
		return;
	}

	bool skip_this = false;
	
	if (!skip_this) {
		switch (el->type) {
		case ElementType::Library:
			outputLibrary(fp, el.get(), parents);
			break;
		case ElementType::Class:
			outputClass(fp, el.get(), parents);
			break;
		case ElementType::Function:
		case ElementType::Operator:
			outputFunction(fp, el.get(), parents);
			break;
		case ElementType::Property:
			outputProperty(fp, el.get(), parents);
			break;
		default:
			Warning(LOCATION, "Unknown type '%d' passed to ade_table_entry::OutputMeta", static_cast<int>(el->type));
			break;
		}
	}

	for (const auto& child : el->children) {
		SCP_vector<const DocumentationElement*> newParents(parents);
		newParents.push_back(el.get());
		OutputElement(fp, child, newParents);
	}

	// Hook variables has a special case
	if (el->name == "HookVariables") {
		for (const auto var : Hook_variables) {
			SCP_string type = var.second;
			if (type.find("function") != SCP_string::npos) {
				parseFunctionSignature(type);
			}
			fprintf(fp, "\n--- @field %s? %s", var.first.c_str(), type.c_str());
		}
	}

	// After processing all children, initialize the table if necessary
	if (el->type == ElementType::Library && !isTableLikeLibrary(el.get())) {
		SCP_string fullPath = buildFullPath(parents);
		SCP_string libraryName = el->shortName.empty() ? el->name : el->shortName;
		fprintf(fp, "\n%s%s = {}", fullPath.c_str(), libraryName.c_str());
	}
}

void OutputLuaMeta(FILE* fp, const ScriptingDocumentation& doc)
{
	// First pass: Output classes
	for (const auto& el : doc.elements) {
		if (el && el->type == ElementType::Class) {
			SCP_vector<const DocumentationElement*> emptyParents;
			OutputElement(fp, el, emptyParents);
		}
	}
	
	// Second pass: Output everything else (libraries, functions, fields, etc.)
	for (const auto& el : doc.elements) {
		if (el && el->type != ElementType::Class) { // Skip classes this time
			SCP_vector<const DocumentationElement*> emptyParents;
			OutputElement(fp, el, emptyParents);
		}
	}

	fputs("\n", fp);
}

template <typename Container>
static void output_hook_variable_list(FILE* fp, const Container& vars)
{
	fputs("--\n", fp);
	for (const auto& param : vars) {
		fprintf(fp,
			"--- %s %s %s\n",
			param.name,
			param.type.getIdentifier(),
			param.description);
	}
	fputs("--\n", fp);
}

template <typename Container>
static void output_hook_conditions_list(FILE* fp, const Container& vars)
{
	fputs("--\n", fp);
	for (const auto& param : vars) {
		fprintf(fp,
			"--      %s\n--         Description: %s\n",
			param.first.c_str(),
			param.second->documentation.c_str());
	}
	fputs("--\n", fp);
}

} // namespace

void output_luastub_doc(const ScriptingDocumentation& doc, const SCP_string& filename)
{
	FILE* fp = fopen(filename.c_str(), "w");

	if (fp == nullptr) {
		return;
	}

	// Header
	fprintf(fp, "-- Lua Stub File\n");
	fprintf(fp, "-- Generated for FSO v%s (%s)\n\n", FS_VERSION_FULL, doc.name.c_str());

	//***Version info
	fprintf(fp, "-- Lua Version: %s\n", LUA_RELEASE);
	fputs("---@meta\n", fp);

	// Conditions
	/* Skipped in luadoc for now
	fprintf(fp, "-- Conditions:\n");
	for (const auto& condition : doc.conditions) {
		fprintf(fp, "-- %s\n", condition.c_str());
	}
	fprintf(fp, "\n");*/

	// Actions
	/* Skipped in luadoc for now
	fprintf(fp, "-- Actions:\n");
	for (const auto& action : doc.actions) {
		fprintf(fp, "-- %s:\n", action.name.c_str());
		if (!action.description.empty()) {
			fprintf(fp, "--   %s\n", action.description.c_str());
			// Only write this for hooks with descriptions since this information is useless for legacy hooks
			if (action.overridable) {
				fputs("--   This hook is overridable\n", fp);
			} else {
				fputs("--   This hook is NOT overridable\n", fp);
			}
			if (action.deprecation) {
				if (action.deprecation->level_hook == HookDeprecationOptions::DeprecationLevel::LEVEL_ERROR) {
					fprintf(fp,
						"--   REMOVED starting with version %d.%d.%d.\n",
						action.deprecation->deprecatedSince.major,
						action.deprecation->deprecatedSince.minor,
						action.deprecation->deprecatedSince.build);
				} else if (action.deprecation->level_override == HookDeprecationOptions::DeprecationLevel::LEVEL_ERROR) {
					fprintf(fp,
						"--   DEPRECATED (+Override removed) starting with version %d.%d.%d.\n",
						action.deprecation->deprecatedSince.major,
						action.deprecation->deprecatedSince.minor,
						action.deprecation->deprecatedSince.build);
				} else {
					fprintf(fp,
						"--   DEPRECATED starting with version %d.%d.%d.\n",
						action.deprecation->deprecatedSince.major,
						action.deprecation->deprecatedSince.minor,
						action.deprecation->deprecatedSince.build);
				}
			}
			if (!action.parameters.empty()) {
				fputs("--   Hook Variables:\n", fp);
				output_hook_variable_list(fp, action.parameters);
			}
			if (!action.conditions.empty()) {
				fputs("--   Hook-specific Conditions:\n", fp);
				output_hook_conditions_list(fp, action.conditions);
			}
		}

		fprintf(fp, "\n");
	}*/

	//***Options
	/* Skipped in luadoc for now
	fprintf(fp, "-- In Game Options:\n");
	for (const auto& option : doc.options) {
		fprintf(fp, "-- %s:\n", option.title.c_str());
		fprintf(fp, "--   %s\n", option.key.c_str());
		fprintf(fp, "--   %s\n", option.description.c_str());
	}
	fputs("\n", fp);*/

	// Get all the hook variables to print and save them to the vector
	for (const auto& action : doc.actions) {
		if (!action.description.empty()) {
			if (!action.parameters.empty()) {
				for (const auto& param : action.parameters) {
					// Check if an entry with the same name already exists
					auto it = std::find_if(Hook_variables.begin(),
						Hook_variables.end(),
						[&param](const std::pair<std::string, std::string>& entry) {
							return entry.first == param.name; // Compare the names
						});

					// Only add if not found
					if (it == Hook_variables.end()) {
						Hook_variables.push_back(std::make_pair(param.name, param.type.getIdentifier()));
					}
				}
			}
		}
	}

	OutputLuaMeta(fp, doc);

	fputs("\n", fp);

	//***Enumerations
	fprintf(fp, "-- Enumerations\n");
	for (const auto& enumeration : doc.enumerations) {
		// Cyborg17 -- Omit the deprecated flag
		if (enumeration.name == "VM_EXTERNAL_CAMERA_LOCKED") {
			continue;
		}
		// WMC - For now, print to the file without the description.
		fprintf(fp, "--- @const %s\n", enumeration.name.c_str());
		fprintf(fp, "%s = enumeration\n", enumeration.name.c_str());
	}
	fputs("\n", fp);

	fclose(fp);

	Hook_variables.clear();
}

} // namespace scripting
