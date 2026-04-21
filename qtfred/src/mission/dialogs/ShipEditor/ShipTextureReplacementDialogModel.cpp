#include "ShipTextureReplacementDialogModel.h"

#include "mission/object.h"
#include "model/model.h"

namespace {
bool has_trailing_transparency_suffix(const SCP_string& texture_name)
{
	if (texture_name.length() <= MODEL_TEXTURE_SUFFIX_TRANS.length()) {
		return false;
	}

	return lcase_equal(texture_name.substr(texture_name.length() - MODEL_TEXTURE_SUFFIX_TRANS.length()),
		MODEL_TEXTURE_SUFFIX_TRANS);
}

SCP_string texture_suffix_to_type_name(const SCP_string& suffix)
{
	return suffix.substr(1);
}

SCP_string build_subtype_texture_name(const SCP_string& texture_name, const SCP_string& type)
{
	if (texture_name == "invisible") {
		return texture_name;
	}

	if (has_trailing_transparency_suffix(texture_name)) {
		auto base_name = texture_name.substr(0, texture_name.length() - MODEL_TEXTURE_SUFFIX_TRANS.length());
		return base_name + "-" + type + MODEL_TEXTURE_SUFFIX_TRANS;
	}

	return texture_name + "-" + type;
}
}

namespace fso {
	namespace fred {
		namespace dialogs {
			ShipTextureReplacementDialogModel::ShipTextureReplacementDialogModel(QObject* parent, EditorViewport* viewport, bool multi) :
				AbstractDialogModel(parent, viewport)
			{
				initialiseData(multi);
			}

			void ShipTextureReplacementDialogModel::initialiseData(bool multi)
			{
				m_multi = multi;
				char texture_file[MAX_FILENAME_LEN];
				char* p = nullptr;
				int duplicate;
				polymodel* pm = model_get(Ship_info[Ships[_editor->cur_ship].ship_info_index].model_num);
				defaultTextures.clear();
				defaultTextures.resize(pm->n_textures);
				currentTextures.clear();
				currentTextures.resize(pm->n_textures);
				subTypesAvailable.clear();
				subTypesAvailable.resize(pm->n_textures);
				replaceMap.clear();
				replaceMap.resize(pm->n_textures);
				inheritMap.clear();
				inheritMap.resize(pm->n_textures);

				// look for textures to populate the list
				for (int i = 0; i < pm->n_textures; i++) {
					// get texture file name
					bm_get_filename(pm->maps[i].textures[0].GetOriginalTexture(), texture_file);

					// skip blank textures
					if (!strlen(texture_file))
						continue;

					// get rid of file extension
					p = strchr(texture_file, '.');
					if (p)
					{
						//mprintf(( "ignoring extension on file '%s'\n", texture_file ));
						*p = 0;
					}

					// check for duplicate textures in list
					duplicate = -1;
					for (size_t k = 0; k < defaultTextures.size(); k++)
					{
						if (!stricmp(defaultTextures[k].c_str(), texture_file))
						{
							duplicate = static_cast<int>(k);
							break;
						}
					}

					if (duplicate >= 0)
						continue;

					// make old texture lowercase
					strlwr(texture_file);

					// add it to the field
					defaultTextures[i] = texture_file;
					currentTextures[i].insert(std::pair<SCP_string, SCP_string>("main", ""));
					//Get all Available SubTypes
					initSubTypes(pm, i);

				}

				if (!m_multi) {
					for (auto& Fred_texture_replacement : Fred_texture_replacements)
					{
						if (!stricmp(Ships[_editor->cur_ship].ship_name, Fred_texture_replacement.ship_name) && !(Fred_texture_replacement.from_table))
						{
							// old_texture is stored as the bare base name by this dialog (no type suffix).
							// However, entries loaded from old mission files may have a type suffix
							// (e.g. "fenris-body-misc"), so fall back to stripping if no direct match.
							SCP_string pureName = Fred_texture_replacement.old_texture;

							// Find the matching default texture slot.
							// Try direct match first; fall back to stripping the last '-' segment
							// for old mission-file entries that stored old_texture with a type suffix.
							size_t matchIdx = defaultTextures.size();
							for (size_t i = 0; i < defaultTextures.size(); i++) {
								if (lcase_equal(defaultTextures[i], pureName)) {
									matchIdx = i;
									break;
								}
							}
							if (matchIdx == defaultTextures.size()) {
								auto stripPos = pureName.find_last_of('-');
								if (stripPos != SCP_string::npos) {
									SCP_string stripped = pureName.substr(0, stripPos);
									for (size_t i = 0; i < defaultTextures.size(); i++) {
										if (lcase_equal(defaultTextures[i], stripped)) {
											matchIdx = i;
											break;
										}
									}
								}
							}

							if (matchIdx < defaultTextures.size())
							{
								size_t i = matchIdx;
								{
									SCP_string newText = Fred_texture_replacement.new_texture;
									SCP_string type;
									{
										SCP_string typeParseText = newText;
										auto has_trans_suffix = has_trailing_transparency_suffix(typeParseText);
										if (has_trans_suffix) {
											typeParseText = typeParseText.substr(0, typeParseText.length() - MODEL_TEXTURE_SUFFIX_TRANS.length());
										}

										// Only treat the suffix as a type if it's a known sub-texture type.
										// Texture names themselves can contain hyphens (e.g. "fighter01-01a"),
										// so we must not blindly strip the last segment.
										for (const auto& suffix : MODEL_KNOWN_TEXTURE_SUFFIXES) {
											if (typeParseText.length() > suffix.length()
												&& lcase_equal(typeParseText.substr(typeParseText.length() - suffix.length()), suffix)) {
												type = texture_suffix_to_type_name(suffix);
												newText = typeParseText.substr(0, typeParseText.length() - suffix.length());
												if (has_trans_suffix) {
													newText += MODEL_TEXTURE_SUFFIX_TRANS;
												}
												break;
											}
										}
									}
									if (!type.empty()) {
										if (type == "misc") {
											currentTextures[i]["misc"] = newText;
											replaceMap[i]["misc"] = true;
											inheritMap[i]["misc"] = (newText == pureName);
										}
										if (type == "shine") {
											currentTextures[i]["shine"] = newText;
											replaceMap[i]["shine"] = true;
											inheritMap[i]["shine"] = (newText == pureName);

										}
										if (type == "glow") {
											currentTextures[i]["glow"] = newText;
											replaceMap[i]["glow"] = true;
											inheritMap[i]["glow"] = (newText == pureName);

										}
										if (type == "normal") {
											currentTextures[i]["normal"] = newText;
											replaceMap[i]["normal"] = true;
											inheritMap[i]["normal"] = (newText == pureName);

										}
										if (type == "height") {
											currentTextures[i]["height"] = newText;
											replaceMap[i]["height"] = true;
											inheritMap[i]["height"] = (newText == pureName);

										}
										if (type == "ao") {
											currentTextures[i]["ao"] = newText;
											replaceMap[i]["ao"] = true;
											inheritMap[i]["ao"] = (newText == pureName);

										}
										if (type == "reflect") {
											currentTextures[i]["reflect"] = newText;
											replaceMap[i]["reflect"] = true;
											inheritMap[i]["reflect"] = (newText == pureName);

										}
									}
									else {
										currentTextures[i]["main"] = newText;
									}

								}
							}
						}
					}
				}
				modelChanged();
			}
			void ShipTextureReplacementDialogModel::initSubTypes(polymodel* model, int MapNum)
			{
				for (const auto& subtype : MODEL_KNOWN_TEXTURE_SUFFIXES) {
					auto type_name = texture_suffix_to_type_name(subtype);
					subTypesAvailable[MapNum].insert(std::pair<SCP_string, bool>(type_name, false));
					currentTextures[MapNum].insert(std::pair<SCP_string, SCP_string>(type_name, ""));
					replaceMap[MapNum].insert(std::pair<SCP_string, bool>(type_name, false));
					inheritMap[MapNum].insert(std::pair<SCP_string, bool>(type_name, true));
				}
				char subMap[MAX_FILENAME_LEN];
				//init saftly, probly not necessary
				for (int j = 1; j < TM_NUM_TYPES; j++) {
					bm_get_filename(model->maps[MapNum].textures[j].GetOriginalTexture(), subMap);
					char* p = strchr(subMap, '.');
					if (p)
					{
						//mprintf(( "ignoring extension on file '%s'\n", texture_file ));
						*p = 0;
					}
					SCP_string subMapClean = subMap;
					SCP_tolower(subMapClean);
					if (has_trailing_transparency_suffix(subMapClean)) {
						subMapClean = subMapClean.substr(0, subMapClean.length() - MODEL_TEXTURE_SUFFIX_TRANS.length());
					}
					bool known = false;
					for (const auto& suffix : MODEL_KNOWN_TEXTURE_SUFFIXES) {
						if (subMapClean.length() > suffix.length()
							&& lcase_equal(subMapClean.substr(subMapClean.length() - suffix.length()), suffix)) {
							subTypesAvailable[MapNum][texture_suffix_to_type_name(suffix)] = true;
							known = true;
							break;
						}
					}
					if (!known) {
						error_display(1, "Invalid Map type in texture %s. Check your model's texture names or get a programmer",
							subMapClean.c_str());
					}
				}
			}

			bool ShipTextureReplacementDialogModel::apply()
			{
				if (query_modified()) {
					for (size_t i = 0; i < getSize(); i++) {
						if ((!currentTextures[i]["main"].empty()) && (currentTextures[i]["main"] != defaultTextures[i])) {
							mainChanged = true;
							SCP_string name = currentTextures[i]["main"];
							if (testTexture(name)) {
								SCP_vector<texture_replace>::iterator ii, end;
								end = Fred_texture_replacements.end();
								if (!m_multi) {
									end = Fred_texture_replacements.end();
									for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
									{
										if (!stricmp(ii->ship_name, Ships[_editor->cur_ship].ship_name))
										{
											do {
												end--;
											} while (end != ii && !stricmp(end->ship_name, Ships[_editor->cur_ship].ship_name));
											if (end == ii)
												break;
											texture_set(&(*ii), &(*end));
										}
									}

									if (end != Fred_texture_replacements.end())
										Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

									// now put the new entries on the end of the list
									texture_replace tr;
									strcpy_s(tr.old_texture, defaultTextures[i].c_str());
									strcpy_s(tr.new_texture, name.c_str());
									strcpy_s(tr.ship_name, Ships[_editor->cur_ship].ship_name);
									tr.new_texture_id = -1;
									tr.from_table = false;

									// assign to global FRED array
									Fred_texture_replacements.push_back(tr);
									_editor->missionChanged();
								}
								else {
									object* objp = nullptr;
									objp = GET_FIRST(&obj_used_list);
									while (objp != END_OF_LIST(&obj_used_list)) {
										if (((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) &&
											(objp->flags[Object::Object_Flags::Marked])) {
											Assert((objp->type == OBJ_SHIP) || (objp->type == OBJ_START));
											auto shipp = &Ships[get_ship_from_obj(objp)];
											end = Fred_texture_replacements.end();
											for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
											{
												if (!stricmp(ii->ship_name, shipp->ship_name))
												{
													do {
														end--;
													} while (end != ii && !stricmp(end->ship_name, shipp->ship_name));
													if (end == ii)
														break;
													texture_set(&(*ii), &(*end));
												}
											}
											if (end != Fred_texture_replacements.end())
												Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

											// now put the new entries on the end of the list
											texture_replace tr;
											strcpy_s(tr.old_texture, defaultTextures[i].c_str());
											strcpy_s(tr.new_texture, name.c_str());
											strcpy_s(tr.ship_name, shipp->ship_name);
											tr.new_texture_id = -1;
											tr.from_table = false;

											// assign to global FRED array
											Fred_texture_replacements.push_back(tr);
											_editor->missionChanged();
										}

										objp = GET_NEXT(objp);
									}
								}
							}
							else {
								auto button = _viewport->dialogProvider->showButtonDialog(DialogType::Error, "Missing Texture", "FRED was unable to find Main Texture %s \n Aborting at this point",
									{ DialogButton::Ok });
								if (button == DialogButton::Ok) {
									return false;
								}
							}
						}
						for (const auto& subtype : MODEL_KNOWN_TEXTURE_SUFFIXES) {
							saveSubMap(i, texture_suffix_to_type_name(subtype));
						}
						_editor->missionChanged();
					}
					_editor->missionChanged();
					return true;
				}
				else {
					return true;
				}
			}
			void ShipTextureReplacementDialogModel::reject()
			{
			}
			texture_replace* ShipTextureReplacementDialogModel::texture_set(texture_replace* dest, const texture_replace* src)
			{
				dest->new_texture_id = src->new_texture_id;
				strcpy_s(dest->ship_name, src->ship_name);
				strcpy_s(dest->old_texture, src->old_texture);
				strcpy_s(dest->new_texture, src->new_texture);
				dest->from_table = src->from_table;

				return dest;
			}

			void ShipTextureReplacementDialogModel::saveSubMap(const size_t index, const SCP_string& type) {
				SCP_string fullName;
				if (replaceMap[index][type]) {
					if (inheritMap[index][type]) {
						if (mainChanged) {
								if (!currentTextures[index]["main"].empty()) {
									fullName = build_subtype_texture_name(currentTextures[index]["main"], type);
									if (testTexture(fullName)) {
										SCP_vector<texture_replace>::iterator ii, end;
									end = Fred_texture_replacements.end();
									if (!m_multi) {
										end = Fred_texture_replacements.end();
										for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
										{
											if (!stricmp(ii->ship_name, Ships[_editor->cur_ship].ship_name))
											{
												do {
													end--;
												} while (end != ii && !stricmp(end->ship_name, Ships[_editor->cur_ship].ship_name));
												if (end == ii)
													break;
												texture_set(&(*ii), &(*end));
											}
										}

										if (end != Fred_texture_replacements.end())
											Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

										// now put the new entries on the end of the list
										texture_replace tr;
										strcpy_s(tr.old_texture, defaultTextures[index].c_str());
										strcpy_s(tr.new_texture, fullName.c_str());
										strcpy_s(tr.ship_name, Ships[_editor->cur_ship].ship_name);
										tr.new_texture_id = -1;
										tr.from_table = false;

										// assign to global FRED array
										Fred_texture_replacements.push_back(tr);
										_editor->missionChanged();
									}
									else {
										object* objp = nullptr;
										objp = GET_FIRST(&obj_used_list);
										while (objp != END_OF_LIST(&obj_used_list)) {
											if (((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) &&
												(objp->flags[Object::Object_Flags::Marked])) {
												Assert((objp->type == OBJ_SHIP) || (objp->type == OBJ_START));
												auto shipp = &Ships[get_ship_from_obj(objp)];
												end = Fred_texture_replacements.end();
												for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
												{
													if (!stricmp(ii->ship_name, shipp->ship_name))
													{
														do {
															end--;
														} while (end != ii && !stricmp(end->ship_name, shipp->ship_name));
														if (end == ii)
															break;
														texture_set(&(*ii), &(*end));
													}
												}

												if (end != Fred_texture_replacements.end())
													Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

												// now put the new entries on the end of the list
												texture_replace tr;
												strcpy_s(tr.old_texture, defaultTextures[index].c_str());
												strcpy_s(tr.new_texture, fullName.c_str());
												strcpy_s(tr.ship_name, shipp->ship_name);
												tr.new_texture_id = -1;
												tr.from_table = false;

												// assign to global FRED array
												Fred_texture_replacements.push_back(tr);
												_editor->missionChanged();
											}

											objp = GET_NEXT(objp);
										}
									}
								}
								else {
									auto button = _viewport->dialogProvider->showButtonDialog(DialogType::Error, "Missing Texture", "FRED was unable to find %s \n Skipping",
										{ DialogButton::Ok });
									if (button == DialogButton::Ok) {
										return;
									}
								}

							}
						}
						else {
							error_display(0, "Cannot use inherited data without changing the main texture name. Ignoring %s map change.", type.c_str());
						}
					}
					else {
						if (!currentTextures[index][type].empty()) {
							fullName = build_subtype_texture_name(currentTextures[index][type], type);
							if (testTexture(fullName)) {
								SCP_vector<texture_replace>::iterator ii, end;
								end = Fred_texture_replacements.end();
								if (!m_multi) {
									end = Fred_texture_replacements.end();
									for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
									{
										if (!stricmp(ii->ship_name, Ships[_editor->cur_ship].ship_name))
										{
											do {
												end--;
											} while (end != ii && !stricmp(end->ship_name, Ships[_editor->cur_ship].ship_name));
											if (end == ii)
												break;
											texture_set(&(*ii), &(*end));
										}
									}

									if (end != Fred_texture_replacements.end())
										Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

									// now put the new entries on the end of the list
									texture_replace tr;
									strcpy_s(tr.old_texture, defaultTextures[index].c_str());
									strcpy_s(tr.new_texture, fullName.c_str());
									strcpy_s(tr.ship_name, Ships[_editor->cur_ship].ship_name);
									tr.new_texture_id = -1;
									tr.from_table = false;

									// assign to global FRED array
									Fred_texture_replacements.push_back(tr);
									_editor->missionChanged();
								}
								else {
									object* objp = nullptr;
									objp = GET_FIRST(&obj_used_list);
									while (objp != END_OF_LIST(&obj_used_list)) {
										if (((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) &&
											(objp->flags[Object::Object_Flags::Marked])) {
											Assert((objp->type == OBJ_SHIP) || (objp->type == OBJ_START));
											auto shipp = &Ships[get_ship_from_obj(objp)];
											end = Fred_texture_replacements.end();
											for (ii = Fred_texture_replacements.begin(); ii != end; ++ii)
											{
												if (!stricmp(ii->ship_name, shipp->ship_name))
												{
													do {
														end--;
													} while (end != ii && !stricmp(end->ship_name, shipp->ship_name));
													if (end == ii)
														break;
													texture_set(&(*ii), &(*end));
												}
											}

											if (end != Fred_texture_replacements.end())
												Fred_texture_replacements.erase(end, Fred_texture_replacements.end());

											// now put the new entries on the end of the list
											texture_replace tr;
											strcpy_s(tr.old_texture, defaultTextures[index].c_str());
											strcpy_s(tr.new_texture, fullName.c_str());
											strcpy_s(tr.ship_name, shipp->ship_name);
											tr.new_texture_id = -1;
											tr.from_table = false;

											// assign to global FRED array
											Fred_texture_replacements.push_back(tr);
											_editor->missionChanged();
										}

										objp = GET_NEXT(objp);
									}
								}
							}
							else {
								auto button = _viewport->dialogProvider->showButtonDialog(DialogType::Error, "Missing Texture", "FRED was unable to find %s \n Skipping",
									{ DialogButton::Ok });
								if (button == DialogButton::Ok) {
									return;
								}
							}

						}
					}
				}
			}

			bool ShipTextureReplacementDialogModel::testTexture(const SCP_string& fullName)
			{
				int temp_bmp, temp_frames, temp_fps;
				if (fullName == "invisible") {
					return true;
				}
				else {
					// try loading the texture (bmpman should take care of eventually unloading it)
					temp_bmp = bm_load(fullName);
					if (temp_bmp < 0)
					{
						temp_bmp = bm_load_animation(fullName.c_str(), &temp_frames, &temp_fps, nullptr, nullptr, false, true);
					}
					return temp_bmp >= 0;
				}
			}

			size_t ShipTextureReplacementDialogModel::getSize() const
			{
				return defaultTextures.size();
			}
			SCP_string ShipTextureReplacementDialogModel::getDefaultName(const size_t index) const
			{
				Assert(index <= defaultTextures.size());
				return defaultTextures[index];
			}
			void ShipTextureReplacementDialogModel::setMap(const size_t index, const SCP_string& type, const SCP_string& newName)
			{
				Assert(index < currentTextures.size());
				auto pos = currentTextures[index].find(type);
				if (pos == currentTextures[index].end()) {
					//handle the error
					error_display(1, "Tried to set non existant map type %s. Get a programmer", type.c_str());
				}
				else {
					modify(currentTextures[index][type], newName);
				}

			}
			SCP_string ShipTextureReplacementDialogModel::getMap(const size_t index, const SCP_string& type) const {
				Assert(index < currentTextures.size());
				auto pos = currentTextures[index].find(type);
				if (pos == currentTextures[index].end()) {
					error_display(1, "Asked for non existant map type %s. Get a programmer", type.c_str());
					return nullptr;
				}
				else {
					return pos->second;
				}
			}
			SCP_map<SCP_string, bool> ShipTextureReplacementDialogModel::getSubtypesForMap(const size_t index) const
			{
				Assert(index < currentTextures.size());
				return subTypesAvailable[index];
			}
			SCP_map<SCP_string, bool> ShipTextureReplacementDialogModel::getReplace(const size_t index) const
			{
				Assert(index < currentTextures.size());
				return replaceMap[index];
			}
			SCP_map<SCP_string, bool> ShipTextureReplacementDialogModel::getInherit(const size_t index) const
			{
				Assert(index < currentTextures.size());
				return inheritMap[index];
			}

			void ShipTextureReplacementDialogModel::setReplace(const size_t index, const SCP_string& type, const bool state)
			{
				Assert(index < currentTextures.size());
				modify(replaceMap[index][type], state);
			}
			void ShipTextureReplacementDialogModel::setInherit(const size_t index, const SCP_string& type, const bool state)
			{
				Assert(index < currentTextures.size());
				modify(inheritMap[index][type], state);
			}
		}
	}
}
