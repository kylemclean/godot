/*************************************************************************/
/*  os.cpp                                                               */
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

#include "os.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/midi_driver.h"
#include "core/version_generated.gen.h"

#include <stdarg.h>

OS *OS::singleton = nullptr;
uint64_t OS::target_ticks = 0;

OS *OS::get_singleton() {
	return singleton;
}

uint64_t OS::get_ticks_msec() const {
	return get_ticks_usec() / 1000ULL;
}

double OS::get_unix_time() const {
	return 0;
}

void OS::_set_logger(CompositeLogger *p_logger) {
	if (_logger) {
		memdelete(_logger);
	}
	_logger = p_logger;
}

void OS::add_logger(Logger *p_logger) {
	if (!_logger) {
		Vector<Logger *> loggers;
		loggers.push_back(p_logger);
		_logger = memnew(CompositeLogger(loggers));
	} else {
		_logger->add_logger(p_logger);
	}
}

void OS::print_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, Logger::ErrorType p_type) {
	if (!_stderr_enabled) {
		return;
	}

	if (_logger) {
		_logger->log_error(p_function, p_file, p_line, p_code, p_rationale, p_editor_notify, p_type);
	}
}

void OS::print(const char *p_format, ...) {
	if (!_stdout_enabled) {
		return;
	}

	va_list argp;
	va_start(argp, p_format);

	if (_logger) {
		_logger->logv(p_format, argp, false);
	}

	va_end(argp);
}

void OS::print_rich(const char *p_format, ...) {
	if (!_stdout_enabled) {
		return;
	}

	va_list argp;
	va_start(argp, p_format);

	if (_logger) {
		_logger->logv(p_format, argp, false);
	}

	va_end(argp);
}

void OS::printerr(const char *p_format, ...) {
	if (!_stderr_enabled) {
		return;
	}

	va_list argp;
	va_start(argp, p_format);

	if (_logger) {
		_logger->logv(p_format, argp, true);
	}

	va_end(argp);
}

void OS::alert(const String &p_alert, const String &p_title) {
	fprintf(stderr, "%s: %s\n", p_title.utf8().get_data(), p_alert.utf8().get_data());
}

void OS::set_low_processor_usage_mode(bool p_enabled) {
	low_processor_usage_mode = p_enabled;
}

bool OS::is_in_low_processor_usage_mode() const {
	return low_processor_usage_mode;
}

void OS::set_low_processor_usage_mode_sleep_usec(int p_usec) {
	low_processor_usage_mode_sleep_usec = p_usec;
}

int OS::get_low_processor_usage_mode_sleep_usec() const {
	return low_processor_usage_mode_sleep_usec;
}

String OS::get_executable_path() const {
	return _execpath;
}

int OS::get_process_id() const {
	return -1;
}

void OS::vibrate_handheld(int p_duration_ms) {
	WARN_PRINT("vibrate_handheld() only works with Android, iOS and Web");
}

bool OS::is_stdout_verbose() const {
	return _verbose_stdout;
}

bool OS::is_stdout_debug_enabled() const {
	return _debug_stdout;
}

bool OS::is_stdout_enabled() const {
	return _stdout_enabled;
}

bool OS::is_stderr_enabled() const {
	return _stderr_enabled;
}

void OS::set_stdout_enabled(bool p_enabled) {
	_stdout_enabled = p_enabled;
}

void OS::set_stderr_enabled(bool p_enabled) {
	_stderr_enabled = p_enabled;
}

int OS::get_exit_code() const {
	return _exit_code;
}

void OS::set_exit_code(int p_code) {
	_exit_code = p_code;
}

String OS::get_locale() const {
	return "en";
}

// Non-virtual helper to extract the 2 or 3-letter language code from
// `get_locale()` in a way that's consistent for all platforms.
String OS::get_locale_language() const {
	return get_locale().left(3).replace("_", "");
}

// Embedded PCK offset.
uint64_t OS::get_embedded_pck_offset() const {
	return 0;
}

// Helper function to ensure that a dir name/path will be valid on the OS
String OS::get_safe_dir_name(const String &p_dir_name, bool p_allow_dir_separator) const {
	Vector<String> invalid_chars = String(": * ? \" < > |").split(" ");
	if (p_allow_dir_separator) {
		// Dir separators are allowed, but disallow ".." to avoid going up the filesystem
		invalid_chars.push_back("..");
	} else {
		invalid_chars.push_back("/");
	}

	String safe_dir_name = p_dir_name.replace("\\", "/").strip_edges();
	for (int i = 0; i < invalid_chars.size(); i++) {
		safe_dir_name = safe_dir_name.replace(invalid_chars[i], "-");
	}
	return safe_dir_name;
}

// Path to data, config, cache, etc. OS-specific folders

// Get properly capitalized engine name for system paths
String OS::get_godot_dir_name() const {
	// Default to lowercase, so only override when different case is needed
	return String(VERSION_SHORT_NAME).to_lower();
}

// OS equivalent of XDG_DATA_HOME
String OS::get_data_path() const {
	return ".";
}

// OS equivalent of XDG_CONFIG_HOME
String OS::get_config_path() const {
	return ".";
}

// OS equivalent of XDG_CACHE_HOME
String OS::get_cache_path() const {
	return ".";
}

// Path to macOS .app bundle resources
String OS::get_bundle_resource_dir() const {
	return ".";
}

// Path to macOS .app bundle embedded icon
String OS::get_bundle_icon_path() const {
	return String();
}

// OS specific path for user://
String OS::get_user_data_dir() const {
	return ".";
}

// Absolute path to res://
String OS::get_resource_dir() const {
	return ProjectSettings::get_singleton()->get_resource_path();
}

// Access system-specific dirs like Documents, Downloads, etc.
String OS::get_system_dir(SystemDir p_dir, bool p_shared_storage) const {
	return ".";
}

Error OS::shell_open(String p_uri) {
	return ERR_UNAVAILABLE;
}

// implement these with the canvas?

uint64_t OS::get_static_memory_usage() const {
	return Memory::get_mem_usage();
}

uint64_t OS::get_static_memory_peak_usage() const {
	return Memory::get_mem_max_usage();
}

Error OS::set_cwd(const String &p_cwd) {
	return ERR_CANT_OPEN;
}

uint64_t OS::get_free_static_memory() const {
	return Memory::get_mem_available();
}

void OS::yield() {
}

void OS::ensure_user_data_dir() {
	String dd = get_user_data_dir();
	if (DirAccess::exists(dd)) {
		return;
	}

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	Error err = da->make_dir_recursive(dd);
	ERR_FAIL_COND_MSG(err != OK, "Error attempting to create data dir: " + dd + ".");
}

String OS::get_model_name() const {
	return "GenericDevice";
}

void OS::set_cmdline(const char *p_execpath, const List<String> &p_args, const List<String> &p_user_args) {
	_execpath = String::utf8(p_execpath);
	_cmdline = p_args;
	_user_args = p_user_args;
}

String OS::get_unique_id() const {
	ERR_FAIL_V("");
}

int OS::get_processor_count() const {
	return 1;
}

String OS::get_processor_name() const {
	return "";
}

void OS::set_has_server_feature_callback(HasServerFeatureCallback p_callback) {
	has_server_feature_callback = p_callback;
}

bool OS::has_feature(const String &p_feature) {
	// Feature tags are always lowercase for consistency.
	if (p_feature == get_name().to_lower()) {
		return true;
	}

	// Catch-all `linuxbsd` feature tag that matches on both Linux and BSD.
	// This is the one exposed in the project settings dialog.
	if (p_feature == "linuxbsd" && (get_name() == "Linux" || get_name() == "FreeBSD" || get_name() == "NetBSD" || get_name() == "OpenBSD" || get_name() == "BSD")) {
		return true;
	}

	if (p_feature == "movie") {
		return _writing_movie;
	}

#ifdef DEBUG_ENABLED
	if (p_feature == "debug") {
		return true;
	}
#else
	if (p_feature == "release") {
		return true;
	}
#endif
#ifdef TOOLS_ENABLED
	if (p_feature == "editor") {
		return true;
	}
#else
	if (p_feature == "standalone") {
		return true;
	}
#endif

	if (sizeof(void *) == 8 && p_feature == "64") {
		return true;
	}
	if (sizeof(void *) == 4 && p_feature == "32") {
		return true;
	}
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64__) || defined(__i386) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
	if (p_feature == "x86_64") {
		return true;
	}
#elif defined(__i386) || defined(__i386__) || defined(_M_IX86)
	if (p_feature == "x86_32") {
		return true;
	}
#endif
	if (p_feature == "x86") {
		return true;
	}
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
#if defined(__aarch64__) || defined(_M_ARM64)
	if (p_feature == "arm64") {
		return true;
	}
#elif defined(__arm__) || defined(_M_ARM)
	if (p_feature == "arm32") {
		return true;
	}
#endif
#if defined(__ARM_ARCH_7A__)
	if (p_feature == "armv7a" || p_feature == "armv7") {
		return true;
	}
#endif
#if defined(__ARM_ARCH_7S__)
	if (p_feature == "armv7s" || p_feature == "armv7") {
		return true;
	}
#endif
	if (p_feature == "arm") {
		return true;
	}
#elif defined(__riscv)
#if __riscv_xlen == 8
	if (p_feature == "rv64") {
		return true;
	}
#endif
	if (p_feature == "riscv") {
		return true;
	}
#elif defined(__powerpc__)
#if defined(__powerpc64__)
	if (p_feature == "ppc64") {
		return true;
	}
#endif
	if (p_feature == "ppc") {
		return true;
	}
#elif defined(__wasm__)
#if defined(__wasm64__)
	if (p_feature == "wasm64") {
		return true;
	}
#elif defined(__wasm32__)
	if (p_feature == "wasm32") {
		return true;
	}
#endif
	if (p_feature == "wasm") {
		return true;
	}
#endif

	if (_check_internal_feature_support(p_feature)) {
		return true;
	}

	if (has_server_feature_callback && has_server_feature_callback(p_feature)) {
		return true;
	}

	if (ProjectSettings::get_singleton()->has_custom_feature(p_feature)) {
		return true;
	}

	return false;
}

void OS::set_restart_on_exit(bool p_restart, const List<String> &p_restart_arguments) {
	restart_on_exit = p_restart;
	restart_commandline = p_restart_arguments;
}

bool OS::is_restart_on_exit_set() const {
	return restart_on_exit;
}

List<String> OS::get_restart_on_exit_arguments() const {
	return restart_commandline;
}

PackedStringArray OS::get_connected_midi_inputs() {
	if (MIDIDriver::get_singleton()) {
		return MIDIDriver::get_singleton()->get_connected_inputs();
	}

	PackedStringArray list;
	ERR_FAIL_V_MSG(list, vformat("MIDI input isn't supported on %s.", OS::get_singleton()->get_name()));
}

void OS::open_midi_inputs() {
	if (MIDIDriver::get_singleton()) {
		MIDIDriver::get_singleton()->open();
	} else {
		ERR_PRINT(vformat("MIDI input isn't supported on %s.", OS::get_singleton()->get_name()));
	}
}

void OS::close_midi_inputs() {
	if (MIDIDriver::get_singleton()) {
		MIDIDriver::get_singleton()->close();
	} else {
		ERR_PRINT(vformat("MIDI input isn't supported on %s.", OS::get_singleton()->get_name()));
	}
}

void OS::add_frame_delay(bool p_can_draw) {
	const uint32_t frame_delay = Engine::get_singleton()->get_frame_delay();
	if (frame_delay) {
		// Add fixed frame delay to decrease CPU/GPU usage. This doesn't take
		// the actual frame time into account.
		// Due to the high fluctuation of the actual sleep duration, it's not recommended
		// to use this as a FPS limiter.
		delay_usec(frame_delay * 1000);
	}

	// Add a dynamic frame delay to decrease CPU/GPU usage. This takes the
	// previous frame time into account for a smoother result.
	uint64_t dynamic_delay = 0;
	if (is_in_low_processor_usage_mode() || !p_can_draw) {
		dynamic_delay = get_low_processor_usage_mode_sleep_usec();
	}
	const int max_fps = Engine::get_singleton()->get_max_fps();
	if (max_fps > 0 && !Engine::get_singleton()->is_editor_hint()) {
		// Override the low processor usage mode sleep delay if the target FPS is lower.
		dynamic_delay = MAX(dynamic_delay, (uint64_t)(1000000 / max_fps));
	}

	if (dynamic_delay > 0) {
		target_ticks += dynamic_delay;
		uint64_t current_ticks = get_ticks_usec();

		if (current_ticks < target_ticks) {
			delay_usec(target_ticks - current_ticks);
		}

		current_ticks = get_ticks_usec();
		target_ticks = MIN(MAX(target_ticks, current_ticks - dynamic_delay), current_ticks + dynamic_delay);
	}
}

OS::OS() {
	singleton = this;

	Vector<Logger *> loggers;
	loggers.push_back(memnew(StdLogger));
	_set_logger(memnew(CompositeLogger(loggers)));
}

OS::~OS() {
	if (_logger) {
		memdelete(_logger);
	}
	singleton = nullptr;
}
