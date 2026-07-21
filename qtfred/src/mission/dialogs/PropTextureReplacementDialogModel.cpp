#include "PropTextureReplacementDialogModel.h"

#include "mission/object.h"
#include "model/model.h"
#include "prop/prop.h"

#include <algorithm>

namespace {
const SCP_vector<SCP_string>& get_replaceable_texture_types()
{
	static const SCP_vector<SCP_string> types = []() {
		SCP_vector<SCP_string> out;
		out.reserve(MODEL_TEXTURE_SUFFIXES.size());
		for (const auto& suffix : MODEL_TEXTURE_SUFFIXES) {
			out.emplace_back(suffix.second.substr(1)); // strip leading '-'
		}
		return out;
	}();
	return types;
}

bool is_known_subtexture_type(const SCP_string& type)
{
	return std::any_of(get_replaceable_texture_types().begin(),
		get_replaceable_texture_types().end(),
		[&type](const SCP_string& knownType) { return lcase_equal(type, knownType); });
}
}

namespace fso::fred::dialogs {

PropTextureReplacementDialogModel::PropTextureReplacementDialogModel(QObject* parent, EditorViewport* viewport, int propObjNum)
	: AbstractDialogModel(parent, viewport), _propObjNum(propObjNum)
{
	initializeData();
}

void PropTextureReplacementDialogModel::initializeData()
{
	char texture_file[MAX_FILENAME_LEN];
	char* p = nullptr;
	int duplicate;

	if (!query_valid_object(_propObjNum) || Objects[_propObjNum].type != OBJ_PROP)
		return;
	prop* propp = prop_id_lookup(Objects[_propObjNum].instance);
	if (propp == nullptr)
		return;

	polymodel_instance* pmi = model_get_instance(propp->model_instance_num);
	if (pmi == nullptr)
		return;
	polymodel* pm = model_get(pmi->model_num);
	if (pm == nullptr)
		return;

	_defaultTextures.clear();
	_defaultTextures.resize(pm->n_textures);
	_currentTextures.clear();
	_currentTextures.resize(pm->n_textures);
	_subTypesAvailable.clear();
	_subTypesAvailable.resize(pm->n_textures);
	_replaceMap.clear();
	_replaceMap.resize(pm->n_textures);
	_inheritMap.clear();
	_inheritMap.resize(pm->n_textures);

	// look for textures to populate the list
	for (int i = 0; i < pm->n_textures; i++) {
		// get texture file name
		bm_get_filename(pm->maps[i].textures[0].GetOriginalTexture(), texture_file);

		// skip blank textures
		if (!strlen(texture_file))
			continue;

		// get rid of file extension
		p = strchr(texture_file, '.');
		if (p) {
			*p = 0;
		}

		// check for duplicate textures in list
		duplicate = -1;
		for (size_t k = 0; k < _defaultTextures.size(); k++) {
			if (!stricmp(_defaultTextures[k].c_str(), texture_file)) {
				duplicate = static_cast<int>(k);
				break;
			}
		}

		if (duplicate >= 0)
			continue;

		// make old texture lowercase
		strlwr(texture_file);

		// add it to the field
		_defaultTextures[i] = texture_file;
		_currentTextures[i].insert(std::pair<SCP_string, SCP_string>("main", ""));
		// Get all Available SubTypes
		initSubTypes(pm, i);
	}

	// Two-pass load: collect mains first so subtype inherit detection (pass 2) compares
	// against a fully-loaded main regardless of entry ordering. The type is taken from
	// old_texture's suffix (authoritative -- saveSubMap writes "<base>-<type>") rather than
	// from new_texture, which can legitimately contain hyphens.
	struct DeferredSubtype {
		size_t index;
		SCP_string type;
		SCP_string newText;
	};
	SCP_vector<DeferredSubtype> deferred;

	for (const auto& tr : propp->replacement_textures) {
		if (tr.from_table)
			continue;

		SCP_string oldName = tr.old_texture;

		// Direct match -> main entry (old_texture is the bare base name).
		size_t matchIdx = _defaultTextures.size();
		for (size_t i = 0; i < _defaultTextures.size(); i++) {
			if (lcase_equal(_defaultTextures[i], oldName)) {
				matchIdx = i;
				break;
			}
		}
		if (matchIdx < _defaultTextures.size()) {
			_currentTextures[matchIdx]["main"] = tr.new_texture;
			continue;
		}

		// Fall back to stripping the suffix -> subtype entry.
		auto stripPos = oldName.find_last_of('-');
		if (stripPos == SCP_string::npos)
			continue;
		SCP_string suffix = oldName.substr(stripPos + 1);
		if (!is_known_subtexture_type(suffix))
			continue;
		SCP_string stripped = oldName.substr(0, stripPos);
		for (size_t i = 0; i < _defaultTextures.size(); i++) {
			if (lcase_equal(_defaultTextures[i], stripped)) {
				deferred.push_back({i, suffix, tr.new_texture});
				break;
			}
		}
	}

	for (const auto& d : deferred) {
		const SCP_string& mainName = _currentTextures[d.index]["main"];
		SCP_string inheritedName;
		if (!mainName.empty()) {
			inheritedName = (mainName != "invisible") ? mainName + "-" + d.type : mainName;
		}

		// The dialog's per-type line edit stores the raw new name without the suffix when
		// not inheriting (saveSubMap re-appends it). Strip a matching trailing suffix so a
		// non-inherited round-trip preserves what the user typed.
		SCP_string typedName = d.newText;
		SCP_string typedSuffix = "-" + d.type;
		if (typedName.size() > typedSuffix.size() &&
			lcase_equal(typedName.substr(typedName.size() - typedSuffix.size()), typedSuffix)) {
			typedName = typedName.substr(0, typedName.size() - typedSuffix.size());
		}

		_currentTextures[d.index][d.type] = typedName;
		_replaceMap[d.index][d.type] = true;
		_inheritMap[d.index][d.type] = !inheritedName.empty() && lcase_equal(d.newText, inheritedName);
	}

	modelChanged();
	_modified = false;
}

void PropTextureReplacementDialogModel::initSubTypes(polymodel* model, int mapNum)
{
	for (const auto& type : get_replaceable_texture_types()) {
		_subTypesAvailable[mapNum].insert({type, false});
		_currentTextures[mapNum].insert({type, ""});
		_replaceMap[mapNum].insert({type, false});
		_inheritMap[mapNum].insert({type, true});
	}

	char subMap[MAX_FILENAME_LEN];
	for (int j = 1; j < TM_NUM_TYPES; j++) {
		bm_get_filename(model->maps[mapNum].textures[j].GetOriginalTexture(), subMap);
		char* p = strchr(subMap, '.');
		if (p) {
			*p = 0;
		}
		SCP_string subMapClean = subMap;
		SCP_tolower(subMapClean);
		SCP_string type;
		auto npos = subMapClean.find_last_of('-');
		if (npos != SCP_string::npos) {
			type = subMapClean.substr(npos + 1);
		} else {
			continue;
		}
		if (!type.empty()) {
			if (type == MODEL_TEXTURE_SUFFIX_TRANS.substr(1)) {
				// transparency map, not a replaceable subtype
			} else {
				if (is_known_subtexture_type(type)) {
					_subTypesAvailable[mapNum][type] = true;
				} else {
					error_display(1, "Invalid Map type %s. Check your model's texture names or get a programmer", type.c_str());
				}
			}
		}
	}
}

bool PropTextureReplacementDialogModel::apply()
{
	if (!query_modified())
		return true;

	if (!query_valid_object(_propObjNum) || Objects[_propObjNum].type != OBJ_PROP)
		return true;
	prop* propp = prop_id_lookup(Objects[_propObjNum].instance);
	if (propp == nullptr)
		return true;

	_mainChanged = false;

	SCP_vector<texture_replace> newInstance;

	for (size_t i = 0; i < getSize(); i++) {
		if ((!_currentTextures[i]["main"].empty()) && (_currentTextures[i]["main"] != _defaultTextures[i])) {
			_mainChanged = true;
			SCP_string name = _currentTextures[i]["main"];
			if (testTexture(name)) {
				texture_replace tr;
				memset(&tr, 0, sizeof(tr));
				strcpy_s(tr.old_texture, _defaultTextures[i].c_str());
				strcpy_s(tr.new_texture, name.c_str());
				strcpy_s(tr.ship_name, propp->prop_name);
				tr.new_texture_id = -1;
				tr.from_table = false;
				newInstance.push_back(tr);
			} else {
				auto button = _viewport->dialogProvider->showButtonDialog(DialogType::Error, "Missing Texture",
					"FRED was unable to find Main Texture " + name + "\nAborting at this point",
					{DialogButton::Ok});
				if (button == DialogButton::Ok) {
					return false;
				}
			}
		}
		for (const auto& type : get_replaceable_texture_types()) {
			saveSubMap(i, type, newInstance);
		}
	}

	// preserve the class (from_table) replacements, swap in the freshly built instance set
	SCP_vector<texture_replace> combined;
	for (const auto& tr : propp->replacement_textures) {
		if (tr.from_table)
			combined.push_back(tr);
	}
	combined.insert(combined.end(), newInstance.begin(), newInstance.end());
	propp->replacement_textures = std::move(combined);

	// rebuild the model instance so the viewport reflects the change immediately
	prop_apply_replacement_textures(propp);
	_editor->missionChanged();

	_modified = false;
	return true;
}

void PropTextureReplacementDialogModel::reject() {}

void PropTextureReplacementDialogModel::saveSubMap(size_t index, const SCP_string& type, SCP_vector<texture_replace>& out)
{
	if (!_replaceMap[index][type])
		return;

	SCP_string fullName;
	if (_inheritMap[index][type]) {
		if (!_mainChanged)
			return;
		if (_currentTextures[index]["main"].empty())
			return;
		fullName = (_currentTextures[index]["main"] != "invisible")
			? _currentTextures[index]["main"] + "-" + type
			: _currentTextures[index]["main"];
	} else {
		if (_currentTextures[index][type].empty())
			return;
		fullName = (_currentTextures[index][type] != "invisible")
			? _currentTextures[index][type] + "-" + type
			: _currentTextures[index][type];
	}

	if (!testTexture(fullName)) {
		_viewport->dialogProvider->showButtonDialog(DialogType::Error, "Missing Texture",
			"FRED was unable to find " + fullName + "\nSkipping", {DialogButton::Ok});
		return;
	}

	if (!query_valid_object(_propObjNum) || Objects[_propObjNum].type != OBJ_PROP)
		return;
	prop* propp = prop_id_lookup(Objects[_propObjNum].instance);
	if (propp == nullptr)
		return;

	texture_replace tr;
	memset(&tr, 0, sizeof(tr));
	strcpy_s(tr.old_texture, (_defaultTextures[index] + "-" + type).c_str());
	strcpy_s(tr.new_texture, fullName.c_str());
	strcpy_s(tr.ship_name, propp->prop_name);
	tr.new_texture_id = -1;
	tr.from_table = false;
	out.push_back(tr);
}

bool PropTextureReplacementDialogModel::testTexture(const SCP_string& fullName)
{
	int temp_bmp, temp_frames, temp_fps;
	if (fullName == "invisible") {
		return true;
	} else {
		// try loading the texture (bmpman should take care of eventually unloading it)
		temp_bmp = bm_load(fullName);
		if (temp_bmp < 0) {
			temp_bmp = bm_load_animation(fullName.c_str(), &temp_frames, &temp_fps, nullptr, nullptr, false, true);
		}
		return temp_bmp >= 0;
	}
}

size_t PropTextureReplacementDialogModel::getSize() const
{
	return _defaultTextures.size();
}

SCP_string PropTextureReplacementDialogModel::getDefaultName(size_t index) const
{
	Assertion(index < _defaultTextures.size(), "Texture index out of bounds");
	return _defaultTextures[index];
}

void PropTextureReplacementDialogModel::setMap(size_t index, const SCP_string& type, const SCP_string& newName)
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	auto pos = _currentTextures[index].find(type);
	if (pos == _currentTextures[index].end()) {
		error_display(1, "Tried to set non existant map type %s. Get a programmer", type.c_str());
	} else {
		modify(_currentTextures[index][type], newName);
	}
}

SCP_string PropTextureReplacementDialogModel::getMap(size_t index, const SCP_string& type) const
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	auto pos = _currentTextures[index].find(type);
	if (pos == _currentTextures[index].end()) {
		error_display(1, "Asked for non existant map type %s. Get a programmer", type.c_str());
		return "";
	} else {
		return pos->second;
	}
}

SCP_map<SCP_string, bool> PropTextureReplacementDialogModel::getSubtypesForMap(size_t index) const
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	return _subTypesAvailable[index];
}

SCP_map<SCP_string, bool> PropTextureReplacementDialogModel::getReplace(size_t index) const
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	return _replaceMap[index];
}

SCP_map<SCP_string, bool> PropTextureReplacementDialogModel::getInherit(size_t index) const
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	return _inheritMap[index];
}

void PropTextureReplacementDialogModel::setReplace(size_t index, const SCP_string& type, bool state)
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	modify(_replaceMap[index][type], state);
}

void PropTextureReplacementDialogModel::setInherit(size_t index, const SCP_string& type, bool state)
{
	Assertion(index < _currentTextures.size(), "Texture index out of bounds");
	modify(_inheritMap[index][type], state);
}

} // namespace fso::fred::dialogs
