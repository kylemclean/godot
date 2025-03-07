/*************************************************************************/
/*  project_settings.cpp                                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "project_settings.h"

#include "core/core_bind.h" // For Compression enum.
#include "core/core_string_names.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_network.h"
#include "core/io/file_access_pack.h"
#include "core/io/marshalls.h"
#include "core/os/keyboard.h"
#include "core/variant/variant_parser.h"
#include "core/version.h"

#ifdef TOOLS_ENABLED
#include "modules/modules_enabled.gen.h" // For mono.
#endif // TOOLS_ENABLED

const String ProjectSettings::PROJECT_DATA_DIR_NAME_SUFFIX = "godot";

ProjectSettings *ProjectSettings::singleton = nullptr;

ProjectSettings *ProjectSettings::get_singleton() {
	return singleton;
}

String ProjectSettings::get_project_data_dir_name() const {
	return project_data_dir_name;
}

String ProjectSettings::get_project_data_path() const {
	return "res://" + get_project_data_dir_name();
}

String ProjectSettings::get_resource_path() const {
	return resource_path;
}

String ProjectSettings::get_safe_project_name() const {
	String safe_name = OS::get_singleton()->get_safe_dir_name(get("application/config/name"));
	if (safe_name.is_empty()) {
		safe_name = "UnnamedProject";
	}
	return safe_name;
}

String ProjectSettings::get_imported_files_path() const {
	return get_project_data_path().path_join("imported");
}

#ifdef TOOLS_ENABLED
// Returns the features that a project must have when opened with this build of Godot.
// This is used by the project manager to provide the initial_settings for config/features.
const PackedStringArray ProjectSettings::get_required_features() {
	PackedStringArray features = PackedStringArray();
	features.append(VERSION_BRANCH);
#ifdef REAL_T_IS_DOUBLE
	features.append("Double Precision");
#endif
	return features;
}

// Returns the features supported by this build of Godot. Includes all required features.
const PackedStringArray ProjectSettings::_get_supported_features() {
	PackedStringArray features = get_required_features();
#ifdef MODULE_MONO_ENABLED
	features.append("C#");
#endif
	// Allow pinning to a specific patch number or build type by marking
	// them as supported. They're only used if the user adds them manually.
	features.append(VERSION_BRANCH "." _MKSTR(VERSION_PATCH));
	features.append(VERSION_FULL_CONFIG);
	features.append(VERSION_FULL_BUILD);

#ifdef VULKAN_ENABLED
	features.append("Forward Plus");
	features.append("Mobile");
#endif

#ifdef GLES3_ENABLED
	features.append("GL Compatibility");
#endif
	return features;
}

// Returns the features that this project needs but this build of Godot lacks.
const PackedStringArray ProjectSettings::get_unsupported_features(const PackedStringArray &p_project_features) {
	PackedStringArray unsupported_features = PackedStringArray();
	PackedStringArray supported_features = singleton->_get_supported_features();
	for (int i = 0; i < p_project_features.size(); i++) {
		if (!supported_features.has(p_project_features[i])) {
			// Temporary compatibility code to ease upgrade to 4.0 beta 2+.
			if (p_project_features[i].begins_with("Vulkan")) {
				continue;
			}
			unsupported_features.append(p_project_features[i]);
		}
	}
	unsupported_features.sort();
	return unsupported_features;
}

// Returns the features that both this project has and this build of Godot has, ensuring required features exist.
const PackedStringArray ProjectSettings::_trim_to_supported_features(const PackedStringArray &p_project_features) {
	// Remove unsupported features if present.
	PackedStringArray features = PackedStringArray(p_project_features);
	PackedStringArray supported_features = _get_supported_features();
	for (int i = p_project_features.size() - 1; i > -1; i--) {
		if (!supported_features.has(p_project_features[i])) {
			features.remove_at(i);
		}
	}
	// Add required features if not present.
	PackedStringArray required_features = get_required_features();
	for (int i = 0; i < required_features.size(); i++) {
		if (!features.has(required_features[i])) {
			features.append(required_features[i]);
		}
	}
	features.sort();
	return features;
}
#endif // TOOLS_ENABLED

String ProjectSettings::localize_path(const String &p_path) const {
	if (resource_path.is_empty() || p_path.begins_with("res://") || p_path.begins_with("user://") ||
			(p_path.is_absolute_path() && !p_path.begins_with(resource_path))) {
		return p_path.simplify_path();
	}

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);

	String path = p_path.replace("\\", "/").simplify_path();

	if (dir->change_dir(path) == OK) {
		String cwd = dir->get_current_dir();
		cwd = cwd.replace("\\", "/");

		// Ensure that we end with a '/'.
		// This is important to ensure that we do not wrongly localize the resource path
		// in an absolute path that just happens to contain this string but points to a
		// different folder (e.g. "/my/project" as resource_path would be contained in
		// "/my/project_data", even though the latter is not part of res://.
		// `path_join("")` is an easy way to ensure we have a trailing '/'.
		const String res_path = resource_path.path_join("");

		// DirAccess::get_current_dir() is not guaranteed to return a path that with a trailing '/',
		// so we must make sure we have it as well in order to compare with 'res_path'.
		cwd = cwd.path_join("");

		if (!cwd.begins_with(res_path)) {
			return p_path;
		}

		return cwd.replace_first(res_path, "res://");
	} else {
		int sep = path.rfind("/");
		if (sep == -1) {
			return "res://" + path;
		}

		String parent = path.substr(0, sep);

		String plocal = localize_path(parent);
		if (plocal.is_empty()) {
			return "";
		}
		// Only strip the starting '/' from 'path' if its parent ('plocal') ends with '/'
		if (plocal[plocal.length() - 1] == '/') {
			sep += 1;
		}
		return plocal + path.substr(sep, path.size() - sep);
	}
}

void ProjectSettings::set_initial_value(const String &p_name, const Variant &p_value) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props[p_name].initial = p_value;
}

void ProjectSettings::set_restart_if_changed(const String &p_name, bool p_restart) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props[p_name].restart_if_changed = p_restart;
}

void ProjectSettings::set_as_basic(const String &p_name, bool p_basic) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props[p_name].basic = p_basic;
}

void ProjectSettings::set_as_internal(const String &p_name, bool p_internal) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props[p_name].internal = p_internal;
}

void ProjectSettings::set_ignore_value_in_docs(const String &p_name, bool p_ignore) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
#ifdef DEBUG_METHODS_ENABLED
	props[p_name].ignore_value_in_docs = p_ignore;
#endif
}

bool ProjectSettings::get_ignore_value_in_docs(const String &p_name) const {
	ERR_FAIL_COND_V_MSG(!props.has(p_name), false, "Request for nonexistent project setting: " + p_name + ".");
#ifdef DEBUG_METHODS_ENABLED
	return props[p_name].ignore_value_in_docs;
#else
	return false;
#endif
}

String ProjectSettings::globalize_path(const String &p_path) const {
	if (p_path.begins_with("res://")) {
		if (!resource_path.is_empty()) {
			return p_path.replace("res:/", resource_path);
		}
		return p_path.replace("res://", "");
	} else if (p_path.begins_with("user://")) {
		String data_dir = OS::get_singleton()->get_user_data_dir();
		if (!data_dir.is_empty()) {
			return p_path.replace("user:/", data_dir);
		}
		return p_path.replace("user://", "");
	}

	return p_path;
}

bool ProjectSettings::_set(const StringName &p_name, const Variant &p_value) {
	_THREAD_SAFE_METHOD_

	if (p_value.get_type() == Variant::NIL) {
		props.erase(p_name);
		if (p_name.operator String().begins_with("autoload/")) {
			String node_name = p_name.operator String().split("/")[1];
			if (autoloads.has(node_name)) {
				remove_autoload(node_name);
			}
		}
	} else {
		if (p_name == CoreStringNames::get_singleton()->_custom_features) {
			Vector<String> custom_feature_array = String(p_value).split(",");
			for (int i = 0; i < custom_feature_array.size(); i++) {
				custom_features.insert(custom_feature_array[i]);
			}
			return true;
		}

		if (!disable_feature_overrides) {
			int dot = p_name.operator String().find(".");
			if (dot != -1) {
				Vector<String> s = p_name.operator String().split(".");

				bool override_valid = false;
				for (int i = 1; i < s.size(); i++) {
					String feature = s[i].strip_edges();
					if (OS::get_singleton()->has_feature(feature) || custom_features.has(feature)) {
						override_valid = true;
						break;
					}
				}

				if (override_valid) {
					feature_overrides[s[0]] = p_name;
				}
			}
		}

		if (props.has(p_name)) {
			if (!props[p_name].overridden) {
				props[p_name].variant = p_value;
			}

		} else {
			props[p_name] = VariantContainer(p_value, last_order++);
		}
		if (p_name.operator String().begins_with("autoload/")) {
			String node_name = p_name.operator String().split("/")[1];
			AutoloadInfo autoload;
			autoload.name = node_name;
			String path = p_value;
			if (path.begins_with("*")) {
				autoload.is_singleton = true;
				autoload.path = path.substr(1);
			} else {
				autoload.path = path;
			}
			add_autoload(autoload);
		}
	}

	return true;
}

bool ProjectSettings::_get(const StringName &p_name, Variant &r_ret) const {
	_THREAD_SAFE_METHOD_

	StringName name = p_name;
	if (!disable_feature_overrides && feature_overrides.has(name)) {
		name = feature_overrides[name];
	}
	if (!props.has(name)) {
		WARN_PRINT("Property not found: " + String(name));
		return false;
	}
	r_ret = props[name].variant;
	return true;
}

struct _VCSort {
	String name;
	Variant::Type type = Variant::VARIANT_MAX;
	int order = 0;
	uint32_t flags = 0;

	bool operator<(const _VCSort &p_vcs) const { return order == p_vcs.order ? name < p_vcs.name : order < p_vcs.order; }
};

void ProjectSettings::_get_property_list(List<PropertyInfo> *p_list) const {
	_THREAD_SAFE_METHOD_

	RBSet<_VCSort> vclist;

	for (const KeyValue<StringName, VariantContainer> &E : props) {
		const VariantContainer *v = &E.value;

		if (v->hide_from_editor) {
			continue;
		}

		_VCSort vc;
		vc.name = E.key;
		vc.order = v->order;
		vc.type = v->variant.get_type();
		if (v->internal || vc.name.begins_with("input/") || vc.name.begins_with("importer_defaults/") || vc.name.begins_with("import/") || vc.name.begins_with("autoload/") || vc.name.begins_with("editor_plugins/") || vc.name.begins_with("shader_globals/")) {
			vc.flags = PROPERTY_USAGE_STORAGE;
		} else {
			vc.flags = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
		}

		if (v->basic) {
			vc.flags |= PROPERTY_USAGE_EDITOR_BASIC_SETTING;
		}

		if (v->restart_if_changed) {
			vc.flags |= PROPERTY_USAGE_RESTART_IF_CHANGED;
		}
		vclist.insert(vc);
	}

	for (const _VCSort &E : vclist) {
		String prop_info_name = E.name;
		int dot = prop_info_name.find(".");
		if (dot != -1 && !custom_prop_info.has(prop_info_name)) {
			prop_info_name = prop_info_name.substr(0, dot);
		}

		if (custom_prop_info.has(prop_info_name)) {
			PropertyInfo pi = custom_prop_info[prop_info_name];
			pi.name = E.name;
			pi.usage = E.flags;
			p_list->push_back(pi);
		} else {
			p_list->push_back(PropertyInfo(E.type, E.name, PROPERTY_HINT_NONE, "", E.flags));
		}
	}
}

bool ProjectSettings::_load_resource_pack(const String &p_pack, bool p_replace_files, int p_offset) {
	if (PackedData::get_singleton()->is_disabled()) {
		return false;
	}

	bool ok = PackedData::get_singleton()->add_pack(p_pack, p_replace_files, p_offset) == OK;

	if (!ok) {
		return false;
	}

	//if data.pck is found, all directory access will be from here
	DirAccess::make_default<DirAccessPack>(DirAccess::ACCESS_RESOURCES);
	using_datapack = true;

	return true;
}

void ProjectSettings::_convert_to_last_version(int p_from_version) {
	if (p_from_version <= 3) {
		// Converts the actions from array to dictionary (array of events to dictionary with deadzone + events)
		for (KeyValue<StringName, ProjectSettings::VariantContainer> &E : props) {
			Variant value = E.value.variant;
			if (String(E.key).begins_with("input/") && value.get_type() == Variant::ARRAY) {
				Array array = value;
				Dictionary action;
				action["deadzone"] = Variant(0.5f);
				action["events"] = array;
				E.value.variant = action;
			}
		}
	}
}

/*
 * This method is responsible for loading a project.godot file and/or data file
 * using the following merit order:
 *  - If using NetworkClient, try to lookup project file or fail.
 *  - If --main-pack was passed by the user (`p_main_pack`), load it or fail.
 *  - Search for project PCKs automatically. For each step we try loading a potential
 *    PCK, and if it doesn't work, we proceed to the next step. If any step succeeds,
 *    we try loading the project settings, and abort if it fails. Steps:
 *    o Bundled PCK in the executable.
 *    o [macOS only] PCK with same basename as the binary in the .app resource dir.
 *    o PCK with same basename as the binary in the binary's directory. We handle both
 *      changing the extension to '.pck' (e.g. 'win_game.exe' -> 'win_game.pck') and
 *      appending '.pck' to the binary name (e.g. 'linux_game' -> 'linux_game.pck').
 *    o PCK with the same basename as the binary in the current working directory.
 *      Same as above for the two possible PCK file names.
 *  - On relevant platforms (Android/iOS), lookup project file in OS resource path.
 *    If found, load it or fail.
 *  - Lookup project file in passed `p_path` (--path passed by the user), i.e. we
 *    are running from source code.
 *    If not found and `p_upwards` is true (--upwards passed by the user), look for
 *    project files in parent folders up to the system root (used to run a game
 *    from command line while in a subfolder).
 *    If a project file is found, load it or fail.
 *    If nothing was found, error out.
 */
Error ProjectSettings::_setup(const String &p_path, const String &p_main_pack, bool p_upwards, bool p_ignore_override) {
	if (!OS::get_singleton()->get_resource_dir().is_empty()) {
		// OS will call ProjectSettings->get_resource_path which will be empty if not overridden!
		// If the OS would rather use a specific location, then it will not be empty.
		resource_path = OS::get_singleton()->get_resource_dir().replace("\\", "/");
		if (!resource_path.is_empty() && resource_path[resource_path.length() - 1] == '/') {
			resource_path = resource_path.substr(0, resource_path.length() - 1); // Chop end.
		}
	}

	// If looking for files in a network client, use it directly

	if (FileAccessNetworkClient::get_singleton()) {
		Error err = _load_settings_text_or_binary("res://project.godot", "res://project.binary");
		if (err == OK && !p_ignore_override) {
			// Optional, we don't mind if it fails
			_load_settings_text("res://override.cfg");
		}
		return err;
	}

	// Attempt with a user-defined main pack first

	if (!p_main_pack.is_empty()) {
		bool ok = _load_resource_pack(p_main_pack);
		ERR_FAIL_COND_V_MSG(!ok, ERR_CANT_OPEN, "Cannot open resource pack '" + p_main_pack + "'.");

		Error err = _load_settings_text_or_binary("res://project.godot", "res://project.binary");
		if (err == OK && !p_ignore_override) {
			// Load override from location of the main pack
			// Optional, we don't mind if it fails
			_load_settings_text(p_main_pack.get_base_dir().path_join("override.cfg"));
		}
		return err;
	}

	String exec_path = OS::get_singleton()->get_executable_path();

	if (!exec_path.is_empty()) {
		// We do several tests sequentially until one succeeds to find a PCK,
		// and if so, we attempt loading it at the end.

		// Attempt with PCK bundled into executable.
		bool found = _load_resource_pack(exec_path);

		// Attempt with exec_name.pck.
		// (This is the usual case when distributing a Godot game.)
		String exec_dir = exec_path.get_base_dir();
		String exec_filename = exec_path.get_file();
		String exec_basename = exec_filename.get_basename();

		// Based on the OS, it can be the exec path + '.pck' (Linux w/o extension, macOS in .app bundle)
		// or the exec path's basename + '.pck' (Windows).
		// We need to test both possibilities as extensions for Linux binaries are optional
		// (so both 'mygame.bin' and 'mygame' should be able to find 'mygame.pck').

#ifdef MACOS_ENABLED
		if (!found) {
			// Attempt to load PCK from macOS .app bundle resources.
			found = _load_resource_pack(OS::get_singleton()->get_bundle_resource_dir().path_join(exec_basename + ".pck")) || _load_resource_pack(OS::get_singleton()->get_bundle_resource_dir().path_join(exec_filename + ".pck"));
		}
#endif

		if (!found) {
			// Try to load data pack at the location of the executable.
			// As mentioned above, we have two potential names to attempt.
			found = _load_resource_pack(exec_dir.path_join(exec_basename + ".pck")) || _load_resource_pack(exec_dir.path_join(exec_filename + ".pck"));
		}

		if (!found) {
			// If we couldn't find them next to the executable, we attempt
			// the current working directory. Same story, two tests.
			found = _load_resource_pack(exec_basename + ".pck") || _load_resource_pack(exec_filename + ".pck");
		}

		// If we opened our package, try and load our project.
		if (found) {
			Error err = _load_settings_text_or_binary("res://project.godot", "res://project.binary");
			if (err == OK && !p_ignore_override) {
				// Load overrides from the PCK and the executable location.
				// Optional, we don't mind if either fails.
				_load_settings_text("res://override.cfg");
				_load_settings_text(exec_path.get_base_dir().path_join("override.cfg"));
			}
			return err;
		}
	}

	// Try to use the filesystem for files, according to OS.
	// (Only Android -when reading from pck- and iOS use this.)

	if (!OS::get_singleton()->get_resource_dir().is_empty()) {
		Error err = _load_settings_text_or_binary("res://project.godot", "res://project.binary");
		if (err == OK && !p_ignore_override) {
			// Optional, we don't mind if it fails.
			_load_settings_text("res://override.cfg");
		}
		return err;
	}

	// Nothing was found, try to find a project file in provided path (`p_path`)
	// or, if requested (`p_upwards`) in parent directories.

	Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V_MSG(d.is_null(), ERR_CANT_CREATE, "Cannot create DirAccess for path '" + p_path + "'.");
	d->change_dir(p_path);

	String current_dir = d->get_current_dir();
	bool found = false;
	Error err;

	while (true) {
		// Set the resource path early so things can be resolved when loading.
		resource_path = current_dir;
		resource_path = resource_path.replace("\\", "/"); // Windows path to Unix path just in case.
		err = _load_settings_text_or_binary(current_dir.path_join("project.godot"), current_dir.path_join("project.binary"));
		if (err == OK && !p_ignore_override) {
			// Optional, we don't mind if it fails.
			_load_settings_text(current_dir.path_join("override.cfg"));
			found = true;
			break;
		}

		if (p_upwards) {
			// Try to load settings ascending through parent directories
			d->change_dir("..");
			if (d->get_current_dir() == current_dir) {
				break; // not doing anything useful
			}
			current_dir = d->get_current_dir();
		} else {
			break;
		}
	}

	if (!found) {
		return err;
	}

	if (resource_path.length() && resource_path[resource_path.length() - 1] == '/') {
		resource_path = resource_path.substr(0, resource_path.length() - 1); // Chop end.
	}

	return OK;
}

Error ProjectSettings::setup(const String &p_path, const String &p_main_pack, bool p_upwards, bool p_ignore_override) {
	Error err = _setup(p_path, p_main_pack, p_upwards, p_ignore_override);
	if (err == OK) {
		String custom_settings = GLOBAL_DEF("application/config/project_settings_override", "");
		if (!custom_settings.is_empty()) {
			_load_settings_text(custom_settings);
		}
	}

	// Updating the default value after the project settings have loaded.
	bool use_hidden_directory = GLOBAL_GET("application/config/use_hidden_project_data_directory");
	project_data_dir_name = (use_hidden_directory ? "." : "") + PROJECT_DATA_DIR_NAME_SUFFIX;

	// Using GLOBAL_GET on every block for compressing can be slow, so assigning here.
	Compression::zstd_long_distance_matching = GLOBAL_GET("compression/formats/zstd/long_distance_matching");
	Compression::zstd_level = GLOBAL_GET("compression/formats/zstd/compression_level");
	Compression::zstd_window_log_size = GLOBAL_GET("compression/formats/zstd/window_log_size");

	Compression::zlib_level = GLOBAL_GET("compression/formats/zlib/compression_level");

	Compression::gzip_level = GLOBAL_GET("compression/formats/gzip/compression_level");

	return err;
}

bool ProjectSettings::has_setting(String p_var) const {
	_THREAD_SAFE_METHOD_

	return props.has(p_var);
}

Error ProjectSettings::_load_settings_binary(const String &p_path) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (err != OK) {
		return err;
	}

	uint8_t hdr[4];
	f->get_buffer(hdr, 4);
	ERR_FAIL_COND_V_MSG((hdr[0] != 'E' || hdr[1] != 'C' || hdr[2] != 'F' || hdr[3] != 'G'), ERR_FILE_CORRUPT, "Corrupted header in binary project.binary (not ECFG).");

	uint32_t count = f->get_32();

	for (uint32_t i = 0; i < count; i++) {
		uint32_t slen = f->get_32();
		CharString cs;
		cs.resize(slen + 1);
		cs[slen] = 0;
		f->get_buffer((uint8_t *)cs.ptr(), slen);
		String key;
		key.parse_utf8(cs.ptr());

		uint32_t vlen = f->get_32();
		Vector<uint8_t> d;
		d.resize(vlen);
		f->get_buffer(d.ptrw(), vlen);
		Variant value;
		err = decode_variant(value, d.ptr(), d.size(), nullptr, true);
		ERR_CONTINUE_MSG(err != OK, "Error decoding property: " + key + ".");
		set(key, value);
	}

	return OK;
}

Error ProjectSettings::_load_settings_text(const String &p_path) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);

	if (f.is_null()) {
		// FIXME: Above 'err' error code is ERR_FILE_CANT_OPEN if the file is missing
		// This needs to be streamlined if we want decent error reporting
		return ERR_FILE_NOT_FOUND;
	}

	VariantParser::StreamFile stream;
	stream.f = f;

	String assign;
	Variant value;
	VariantParser::Tag next_tag;

	int lines = 0;
	String error_text;
	String section;
	int config_version = 0;

	while (true) {
		assign = Variant();
		next_tag.fields.clear();
		next_tag.name = String();

		err = VariantParser::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, nullptr, true);
		if (err == ERR_FILE_EOF) {
			// If we're loading a project.godot from source code, we can operate some
			// ProjectSettings conversions if need be.
			_convert_to_last_version(config_version);
			last_save_time = FileAccess::get_modified_time(get_resource_path().path_join("project.godot"));
			return OK;
		}
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error parsing " + p_path + " at line " + itos(lines) + ": " + error_text + " File might be corrupted.");

		if (!assign.is_empty()) {
			if (section.is_empty() && assign == "config_version") {
				config_version = value;
				ERR_FAIL_COND_V_MSG(config_version > CONFIG_VERSION, ERR_FILE_CANT_OPEN, vformat("Can't open project at '%s', its `config_version` (%d) is from a more recent and incompatible version of the engine. Expected config version: %d.", p_path, config_version, CONFIG_VERSION));
			} else {
				if (section.is_empty()) {
					set(assign, value);
				} else {
					set(section + "/" + assign, value);
				}
			}
		} else if (!next_tag.name.is_empty()) {
			section = next_tag.name;
		}
	}
}

Error ProjectSettings::_load_settings_text_or_binary(const String &p_text_path, const String &p_bin_path) {
	// Attempt first to load the binary project.godot file.
	Error err = _load_settings_binary(p_bin_path);
	if (err == OK) {
		return OK;
	} else if (err != ERR_FILE_NOT_FOUND) {
		// If the file exists but can't be loaded, we want to know it.
		ERR_PRINT("Couldn't load file '" + p_bin_path + "', error code " + itos(err) + ".");
	}

	// Fallback to text-based project.godot file if binary was not found.
	err = _load_settings_text(p_text_path);
	if (err == OK) {
		return OK;
	} else if (err != ERR_FILE_NOT_FOUND) {
		ERR_PRINT("Couldn't load file '" + p_text_path + "', error code " + itos(err) + ".");
	}

	return err;
}

Error ProjectSettings::load_custom(const String &p_path) {
	if (p_path.ends_with(".binary")) {
		return _load_settings_binary(p_path);
	}
	return _load_settings_text(p_path);
}

int ProjectSettings::get_order(const String &p_name) const {
	ERR_FAIL_COND_V_MSG(!props.has(p_name), -1, "Request for nonexistent project setting: " + p_name + ".");
	return props[p_name].order;
}

void ProjectSettings::set_order(const String &p_name, int p_order) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props[p_name].order = p_order;
}

void ProjectSettings::set_builtin_order(const String &p_name) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	if (props[p_name].order >= NO_BUILTIN_ORDER_BASE) {
		props[p_name].order = last_builtin_order++;
	}
}

bool ProjectSettings::is_builtin_setting(const String &p_name) const {
	// Return true because a false negative is worse than a false positive.
	ERR_FAIL_COND_V_MSG(!props.has(p_name), true, "Request for nonexistent project setting: " + p_name + ".");
	return props[p_name].order < NO_BUILTIN_ORDER_BASE;
}

void ProjectSettings::clear(const String &p_name) {
	ERR_FAIL_COND_MSG(!props.has(p_name), "Request for nonexistent project setting: " + p_name + ".");
	props.erase(p_name);
}

Error ProjectSettings::save() {
	Error error = save_custom(get_resource_path().path_join("project.godot"));
	if (error == OK) {
		last_save_time = FileAccess::get_modified_time(get_resource_path().path_join("project.godot"));
	}
	return error;
}

Error ProjectSettings::_save_settings_binary(const String &p_file, const RBMap<String, List<String>> &props, const CustomMap &p_custom, const String &p_custom_features) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Couldn't save project.binary at " + p_file + ".");

	uint8_t hdr[4] = { 'E', 'C', 'F', 'G' };
	file->store_buffer(hdr, 4);

	int count = 0;

	for (const KeyValue<String, List<String>> &E : props) {
		count += E.value.size();
	}

	if (!p_custom_features.is_empty()) {
		file->store_32(count + 1);
		//store how many properties are saved, add one for custom featuers, which must always go first
		String key = CoreStringNames::get_singleton()->_custom_features;
		file->store_pascal_string(key);

		int len;
		err = encode_variant(p_custom_features, nullptr, len, false);
		ERR_FAIL_COND_V(err != OK, err);

		Vector<uint8_t> buff;
		buff.resize(len);

		err = encode_variant(p_custom_features, buff.ptrw(), len, false);
		ERR_FAIL_COND_V(err != OK, err);
		file->store_32(len);
		file->store_buffer(buff.ptr(), buff.size());

	} else {
		file->store_32(count); //store how many properties are saved
	}

	for (const KeyValue<String, List<String>> &E : props) {
		for (const String &key : E.value) {
			String k = key;
			if (!E.key.is_empty()) {
				k = E.key + "/" + k;
			}
			Variant value;
			if (p_custom.has(k)) {
				value = p_custom[k];
			} else {
				value = get(k);
			}

			file->store_pascal_string(k);

			int len;
			err = encode_variant(value, nullptr, len, true);
			ERR_FAIL_COND_V_MSG(err != OK, ERR_INVALID_DATA, "Error when trying to encode Variant.");

			Vector<uint8_t> buff;
			buff.resize(len);

			err = encode_variant(value, buff.ptrw(), len, true);
			ERR_FAIL_COND_V_MSG(err != OK, ERR_INVALID_DATA, "Error when trying to encode Variant.");
			file->store_32(len);
			file->store_buffer(buff.ptr(), buff.size());
		}
	}

	return OK;
}

Error ProjectSettings::_save_settings_text(const String &p_file, const RBMap<String, List<String>> &props, const CustomMap &p_custom, const String &p_custom_features) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);

	ERR_FAIL_COND_V_MSG(err != OK, err, "Couldn't save project.godot - " + p_file + ".");

	file->store_line("; Engine configuration file.");
	file->store_line("; It's best edited using the editor UI and not directly,");
	file->store_line("; since the parameters that go here are not all obvious.");
	file->store_line(";");
	file->store_line("; Format:");
	file->store_line(";   [section] ; section goes between []");
	file->store_line(";   param=value ; assign values to parameters");
	file->store_line("");

	file->store_string("config_version=" + itos(CONFIG_VERSION) + "\n");
	if (!p_custom_features.is_empty()) {
		file->store_string("custom_features=\"" + p_custom_features + "\"\n");
	}
	file->store_string("\n");

	for (const KeyValue<String, List<String>> &E : props) {
		if (E.key != props.begin()->key) {
			file->store_string("\n");
		}

		if (!E.key.is_empty()) {
			file->store_string("[" + E.key + "]\n\n");
		}
		for (const String &F : E.value) {
			String key = F;
			if (!E.key.is_empty()) {
				key = E.key + "/" + key;
			}
			Variant value;
			if (p_custom.has(key)) {
				value = p_custom[key];
			} else {
				value = get(key);
			}

			String vstr;
			VariantWriter::write_to_string(value, vstr);
			file->store_string(F.property_name_encode() + "=" + vstr + "\n");
		}
	}

	return OK;
}

Error ProjectSettings::_save_custom_bnd(const String &p_file) { // add other params as dictionary and array?

	return save_custom(p_file);
}

Error ProjectSettings::save_custom(const String &p_path, const CustomMap &p_custom, const Vector<String> &p_custom_features, bool p_merge_with_current) {
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), ERR_INVALID_PARAMETER, "Project settings save path cannot be empty.");

#ifdef TOOLS_ENABLED
	PackedStringArray project_features = get_setting("application/config/features");
	// If there is no feature list currently present, force one to generate.
	if (project_features.is_empty()) {
		project_features = ProjectSettings::get_required_features();
	}
	// Check the rendering API.
	const String rendering_api = has_setting("rendering/renderer/rendering_method") ? (String)get_setting("rendering/renderer/rendering_method") : String();
	if (!rendering_api.is_empty()) {
		// Add the rendering API as a project feature if it doesn't already exist.
		if (!project_features.has(rendering_api)) {
			project_features.append(rendering_api);
		}
	}
	// Check for the existence of a csproj file.
	if (FileAccess::exists(get_resource_path().path_join(get_safe_project_name() + ".csproj"))) {
		// If there is a csproj file, add the C# feature if it doesn't already exist.
		if (!project_features.has("C#")) {
			project_features.append("C#");
		}
	} else {
		// If there isn't a csproj file, remove the C# feature if it exists.
		if (project_features.has("C#")) {
			project_features.remove_at(project_features.find("C#"));
		}
	}
	project_features = _trim_to_supported_features(project_features);
	set_setting("application/config/features", project_features);
#endif // TOOLS_ENABLED

	RBSet<_VCSort> vclist;

	if (p_merge_with_current) {
		for (const KeyValue<StringName, VariantContainer> &G : props) {
			const VariantContainer *v = &G.value;

			if (v->hide_from_editor) {
				continue;
			}

			if (p_custom.has(G.key)) {
				continue;
			}

			_VCSort vc;
			vc.name = G.key; //*k;
			vc.order = v->order;
			vc.type = v->variant.get_type();
			vc.flags = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
			if (v->variant == v->initial) {
				continue;
			}

			vclist.insert(vc);
		}
	}

	for (const KeyValue<String, Variant> &E : p_custom) {
		// Lookup global prop to store in the same order
		RBMap<StringName, VariantContainer>::Iterator global_prop = props.find(E.key);

		_VCSort vc;
		vc.name = E.key;
		vc.order = global_prop ? global_prop->value.order : 0xFFFFFFF;
		vc.type = E.value.get_type();
		vc.flags = PROPERTY_USAGE_STORAGE;
		vclist.insert(vc);
	}

	RBMap<String, List<String>> props;

	for (const _VCSort &E : vclist) {
		String category = E.name;
		String name = E.name;

		int div = category.find("/");

		if (div < 0) {
			category = "";
		} else {
			category = category.substr(0, div);
			name = name.substr(div + 1, name.size());
		}
		props[category].push_back(name);
	}

	String custom_features;

	for (int i = 0; i < p_custom_features.size(); i++) {
		if (i > 0) {
			custom_features += ",";
		}

		String f = p_custom_features[i].strip_edges().replace("\"", "");
		custom_features += f;
	}

	if (p_path.ends_with(".godot") || p_path.ends_with("override.cfg")) {
		return _save_settings_text(p_path, props, p_custom, custom_features);
	} else if (p_path.ends_with(".binary")) {
		return _save_settings_binary(p_path, props, p_custom, custom_features);
	} else {
		ERR_FAIL_V_MSG(ERR_FILE_UNRECOGNIZED, "Unknown config file format: " + p_path + ".");
	}
}

Variant _GLOBAL_DEF(const String &p_var, const Variant &p_default, bool p_restart_if_changed, bool p_ignore_value_in_docs, bool p_basic, bool p_internal) {
	Variant ret;
	if (!ProjectSettings::get_singleton()->has_setting(p_var)) {
		ProjectSettings::get_singleton()->set(p_var, p_default);
	}
	ret = ProjectSettings::get_singleton()->get(p_var);

	ProjectSettings::get_singleton()->set_initial_value(p_var, p_default);
	ProjectSettings::get_singleton()->set_builtin_order(p_var);
	ProjectSettings::get_singleton()->set_as_basic(p_var, p_basic);
	ProjectSettings::get_singleton()->set_restart_if_changed(p_var, p_restart_if_changed);
	ProjectSettings::get_singleton()->set_ignore_value_in_docs(p_var, p_ignore_value_in_docs);
	ProjectSettings::get_singleton()->set_as_internal(p_var, p_internal);
	return ret;
}

Vector<String> ProjectSettings::get_optimizer_presets() const {
	List<PropertyInfo> pi;
	ProjectSettings::get_singleton()->get_property_list(&pi);
	Vector<String> names;

	for (const PropertyInfo &E : pi) {
		if (!E.name.begins_with("optimizer_presets/")) {
			continue;
		}
		names.push_back(E.name.get_slicec('/', 1));
	}

	names.sort();

	return names;
}

void ProjectSettings::_add_property_info_bind(const Dictionary &p_info) {
	ERR_FAIL_COND(!p_info.has("name"));
	ERR_FAIL_COND(!p_info.has("type"));

	PropertyInfo pinfo;
	pinfo.name = p_info["name"];
	ERR_FAIL_COND(!props.has(pinfo.name));
	pinfo.type = Variant::Type(p_info["type"].operator int());
	ERR_FAIL_INDEX(pinfo.type, Variant::VARIANT_MAX);

	if (p_info.has("hint")) {
		pinfo.hint = PropertyHint(p_info["hint"].operator int());
	}
	if (p_info.has("hint_string")) {
		pinfo.hint_string = p_info["hint_string"];
	}

	set_custom_property_info(pinfo.name, pinfo);
}

void ProjectSettings::set_custom_property_info(const String &p_prop, const PropertyInfo &p_info) {
	ERR_FAIL_COND(!props.has(p_prop));
	custom_prop_info[p_prop] = p_info;
	custom_prop_info[p_prop].name = p_prop;
}

const HashMap<StringName, PropertyInfo> &ProjectSettings::get_custom_property_info() const {
	return custom_prop_info;
}

void ProjectSettings::set_disable_feature_overrides(bool p_disable) {
	disable_feature_overrides = p_disable;
}

bool ProjectSettings::is_using_datapack() const {
	return using_datapack;
}

bool ProjectSettings::_property_can_revert(const StringName &p_name) const {
	if (!props.has(p_name)) {
		return false;
	}

	return props[p_name].initial != props[p_name].variant;
}

bool ProjectSettings::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	if (!props.has(p_name)) {
		return false;
	}

	r_property = props[p_name].initial;
	return true;
}

void ProjectSettings::set_setting(const String &p_setting, const Variant &p_value) {
	set(p_setting, p_value);
}

Variant ProjectSettings::get_setting(const String &p_setting) const {
	return get(p_setting);
}

bool ProjectSettings::has_custom_feature(const String &p_feature) const {
	return custom_features.has(p_feature);
}

const HashMap<StringName, ProjectSettings::AutoloadInfo> &ProjectSettings::get_autoload_list() const {
	return autoloads;
}

void ProjectSettings::add_autoload(const AutoloadInfo &p_autoload) {
	ERR_FAIL_COND_MSG(p_autoload.name == StringName(), "Trying to add autoload with no name.");
	autoloads[p_autoload.name] = p_autoload;
}

void ProjectSettings::remove_autoload(const StringName &p_autoload) {
	ERR_FAIL_COND_MSG(!autoloads.has(p_autoload), "Trying to remove non-existent autoload.");
	autoloads.erase(p_autoload);
}

bool ProjectSettings::has_autoload(const StringName &p_autoload) const {
	return autoloads.has(p_autoload);
}

ProjectSettings::AutoloadInfo ProjectSettings::get_autoload(const StringName &p_name) const {
	ERR_FAIL_COND_V_MSG(!autoloads.has(p_name), AutoloadInfo(), "Trying to get non-existent autoload.");
	return autoloads[p_name];
}

void ProjectSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("has_setting", "name"), &ProjectSettings::has_setting);
	ClassDB::bind_method(D_METHOD("set_setting", "name", "value"), &ProjectSettings::set_setting);
	ClassDB::bind_method(D_METHOD("get_setting", "name"), &ProjectSettings::get_setting);
	ClassDB::bind_method(D_METHOD("set_order", "name", "position"), &ProjectSettings::set_order);
	ClassDB::bind_method(D_METHOD("get_order", "name"), &ProjectSettings::get_order);
	ClassDB::bind_method(D_METHOD("set_initial_value", "name", "value"), &ProjectSettings::set_initial_value);
	ClassDB::bind_method(D_METHOD("add_property_info", "hint"), &ProjectSettings::_add_property_info_bind);
	ClassDB::bind_method(D_METHOD("set_restart_if_changed", "name", "restart"), &ProjectSettings::set_restart_if_changed);
	ClassDB::bind_method(D_METHOD("clear", "name"), &ProjectSettings::clear);
	ClassDB::bind_method(D_METHOD("localize_path", "path"), &ProjectSettings::localize_path);
	ClassDB::bind_method(D_METHOD("globalize_path", "path"), &ProjectSettings::globalize_path);
	ClassDB::bind_method(D_METHOD("save"), &ProjectSettings::save);
	ClassDB::bind_method(D_METHOD("load_resource_pack", "pack", "replace_files", "offset"), &ProjectSettings::_load_resource_pack, DEFVAL(true), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("save_custom", "file"), &ProjectSettings::_save_custom_bnd);
}

void ProjectSettings::_add_builtin_input_map() {
	if (InputMap::get_singleton()) {
		HashMap<String, List<Ref<InputEvent>>> builtins = InputMap::get_singleton()->get_builtins();

		for (KeyValue<String, List<Ref<InputEvent>>> &E : builtins) {
			Array events;

			// Convert list of input events into array
			for (List<Ref<InputEvent>>::Element *I = E.value.front(); I; I = I->next()) {
				events.push_back(I->get());
			}

			Dictionary action;
			action["deadzone"] = Variant(0.5f);
			action["events"] = events;

			String action_name = "input/" + E.key;
			GLOBAL_DEF_INTERNAL(action_name, action);
			input_presets.push_back(action_name);
		}
	}
}

ProjectSettings::ProjectSettings() {
	// Initialization of engine variables should be done in the setup() method,
	// so that the values can be overridden from project.godot or project.binary.

	singleton = this;

	GLOBAL_DEF_BASIC("application/config/name", "");
	GLOBAL_DEF_BASIC("application/config/name_localized", Dictionary());
	custom_prop_info["application/config/name_localized"] = PropertyInfo(Variant::DICTIONARY, "application/config/name_localized", PROPERTY_HINT_LOCALIZABLE_STRING);
	GLOBAL_DEF_BASIC("application/config/description", "");
	custom_prop_info["application/config/description"] = PropertyInfo(Variant::STRING, "application/config/description", PROPERTY_HINT_MULTILINE_TEXT);
	GLOBAL_DEF_BASIC("application/run/main_scene", "");
	custom_prop_info["application/run/main_scene"] = PropertyInfo(Variant::STRING, "application/run/main_scene", PROPERTY_HINT_FILE, "*.tscn,*.scn,*.res");
	GLOBAL_DEF("application/run/disable_stdout", false);
	GLOBAL_DEF("application/run/disable_stderr", false);
	GLOBAL_DEF_RST("application/config/use_hidden_project_data_directory", true);
	GLOBAL_DEF("application/config/use_custom_user_dir", false);
	GLOBAL_DEF("application/config/custom_user_dir_name", "");
	GLOBAL_DEF("application/config/project_settings_override", "");

	// The default window size is tuned to:
	// - Have a 16:9 aspect ratio,
	// - Have both dimensions divisible by 8 to better play along with video recording,
	// - Be displayable correctly in windowed mode on a 1366×768 display (tested on Windows 10 with default settings).
	GLOBAL_DEF_BASIC("display/window/size/viewport_width", 1152);
	custom_prop_info["display/window/size/viewport_width"] = PropertyInfo(Variant::INT, "display/window/size/viewport_width", PROPERTY_HINT_RANGE, "0,7680,1,or_greater"); // 8K resolution

	GLOBAL_DEF_BASIC("display/window/size/viewport_height", 648);
	custom_prop_info["display/window/size/viewport_height"] = PropertyInfo(Variant::INT, "display/window/size/viewport_height", PROPERTY_HINT_RANGE, "0,4320,1,or_greater"); // 8K resolution

	GLOBAL_DEF_BASIC("display/window/size/mode", 0);
	custom_prop_info["display/window/size/mode"] = PropertyInfo(Variant::INT, "display/window/size/mode", PROPERTY_HINT_ENUM, "Windowed,Minimized,Maximized,Fullscreen,Exclusive Fullscreen");

	GLOBAL_DEF_BASIC("display/window/size/resizable", true);
	GLOBAL_DEF_BASIC("display/window/size/borderless", false);
	GLOBAL_DEF("display/window/size/always_on_top", false);
	GLOBAL_DEF("display/window/size/transparent", false);
	GLOBAL_DEF("display/window/size/extend_to_title", false);
	GLOBAL_DEF("display/window/size/no_focus", false);

	GLOBAL_DEF("display/window/size/window_width_override", 0);
	custom_prop_info["display/window/size/window_width_override"] = PropertyInfo(Variant::INT, "display/window/size/window_width_override", PROPERTY_HINT_RANGE, "0,7680,1,or_greater"); // 8K resolution
	GLOBAL_DEF("display/window/size/window_height_override", 0);
	custom_prop_info["display/window/size/window_height_override"] = PropertyInfo(Variant::INT, "display/window/size/window_height_override", PROPERTY_HINT_RANGE, "0,4320,1,or_greater"); // 8K resolution

	GLOBAL_DEF("display/window/energy_saving/keep_screen_on", true);
	GLOBAL_DEF("display/window/energy_saving/keep_screen_on.editor", false);

	GLOBAL_DEF_BASIC("audio/buses/default_bus_layout", "res://default_bus_layout.tres");
	custom_prop_info["audio/buses/default_bus_layout"] = PropertyInfo(Variant::STRING, "audio/buses/default_bus_layout", PROPERTY_HINT_FILE, "*.tres");
	GLOBAL_DEF_RST("audio/general/2d_panning_strength", 1.0f);
	custom_prop_info["audio/general/2d_panning_strength"] = PropertyInfo(Variant::FLOAT, "audio/general/2d_panning_strength", PROPERTY_HINT_RANGE, "0,4,0.01");
	GLOBAL_DEF_RST("audio/general/3d_panning_strength", 1.0f);
	custom_prop_info["audio/general/3d_panning_strength"] = PropertyInfo(Variant::FLOAT, "audio/general/3d_panning_strength", PROPERTY_HINT_RANGE, "0,4,0.01");

	PackedStringArray extensions = PackedStringArray();
	extensions.push_back("gd");
	if (Engine::get_singleton()->has_singleton("GodotSharp")) {
		extensions.push_back("cs");
	}
	extensions.push_back("gdshader");

	GLOBAL_DEF("editor/run/main_run_args", "");

	GLOBAL_DEF("editor/script/search_in_file_extensions", extensions);
	custom_prop_info["editor/script/search_in_file_extensions"] = PropertyInfo(Variant::PACKED_STRING_ARRAY, "editor/script/search_in_file_extensions");

	GLOBAL_DEF("editor/script/templates_search_path", "res://script_templates");
	custom_prop_info["editor/script/templates_search_path"] = PropertyInfo(Variant::STRING, "editor/script/templates_search_path", PROPERTY_HINT_DIR);

	_add_builtin_input_map();

	// Keep the enum values in sync with the `DisplayServer::ScreenOrientation` enum.
	custom_prop_info["display/window/handheld/orientation"] = PropertyInfo(Variant::INT, "display/window/handheld/orientation", PROPERTY_HINT_ENUM, "Landscape,Portrait,Reverse Landscape,Reverse Portrait,Sensor Landscape,Sensor Portrait,Sensor");
	// Keep the enum values in sync with the `DisplayServer::VSyncMode` enum.
	custom_prop_info["display/window/vsync/vsync_mode"] = PropertyInfo(Variant::INT, "display/window/vsync/vsync_mode", PROPERTY_HINT_ENUM, "Disabled,Enabled,Adaptive,Mailbox");
	custom_prop_info["rendering/driver/threads/thread_model"] = PropertyInfo(Variant::INT, "rendering/driver/threads/thread_model", PROPERTY_HINT_ENUM, "Single-Unsafe,Single-Safe,Multi-Threaded");
	GLOBAL_DEF("physics/2d/run_on_separate_thread", false);
	GLOBAL_DEF("physics/3d/run_on_separate_thread", false);

	GLOBAL_DEF("debug/settings/profiler/max_functions", 16384);
	custom_prop_info["debug/settings/profiler/max_functions"] = PropertyInfo(Variant::INT, "debug/settings/profiler/max_functions", PROPERTY_HINT_RANGE, "128,65535,1");

	GLOBAL_DEF("compression/formats/zstd/long_distance_matching", Compression::zstd_long_distance_matching);
	custom_prop_info["compression/formats/zstd/long_distance_matching"] = PropertyInfo(Variant::BOOL, "compression/formats/zstd/long_distance_matching");
	GLOBAL_DEF("compression/formats/zstd/compression_level", Compression::zstd_level);
	custom_prop_info["compression/formats/zstd/compression_level"] = PropertyInfo(Variant::INT, "compression/formats/zstd/compression_level", PROPERTY_HINT_RANGE, "1,22,1");
	GLOBAL_DEF("compression/formats/zstd/window_log_size", Compression::zstd_window_log_size);
	custom_prop_info["compression/formats/zstd/window_log_size"] = PropertyInfo(Variant::INT, "compression/formats/zstd/window_log_size", PROPERTY_HINT_RANGE, "10,30,1");

	GLOBAL_DEF("compression/formats/zlib/compression_level", Compression::zlib_level);
	custom_prop_info["compression/formats/zlib/compression_level"] = PropertyInfo(Variant::INT, "compression/formats/zlib/compression_level", PROPERTY_HINT_RANGE, "-1,9,1");

	GLOBAL_DEF("compression/formats/gzip/compression_level", Compression::gzip_level);
	custom_prop_info["compression/formats/gzip/compression_level"] = PropertyInfo(Variant::INT, "compression/formats/gzip/compression_level", PROPERTY_HINT_RANGE, "-1,9,1");

	// These properties will not show up in the dialog nor in the documentation. If you want to exclude whole groups, see _get_property_list() method.
	GLOBAL_DEF_INTERNAL("application/config/features", PackedStringArray());
	GLOBAL_DEF_INTERNAL("internationalization/locale/translation_remaps", PackedStringArray());
	GLOBAL_DEF_INTERNAL("internationalization/locale/translations", PackedStringArray());
}

ProjectSettings::~ProjectSettings() {
	singleton = nullptr;
}
