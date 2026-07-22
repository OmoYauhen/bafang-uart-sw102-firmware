#include "screen_cfg_utils.h"
#include "state.h"
#include "eeprom.h"
#include "ui.h"

extern const struct screen screen_main;

static void do_reset_trip_a(const struct configtree_t *ign);
static void do_reset_trip_b(const struct configtree_t *ign);
static void do_reset_ble(const struct configtree_t *ign);
static void do_reset_all(const struct configtree_t *ign);
void cfg_push_assist_screen(const struct configtree_t *ign);
void cfg_push_walk_assist_screen(const struct configtree_t *ign);

static bool do_set_wh(const struct configtree_t *ign, int wh);
static bool do_set_odometer(const struct configtree_t *ign, int wh);

static const char *disable_enable[] = { "disable", "enable", 0 };
static const char *off_on[] = { "off", "on", 0 };

static const struct configtree_t cfgroot[] = {
	{ "Trip memory", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 18, 0, 128, (const struct configtree_t[]) {
		{ "Reset trip A", F_BUTTON, .action = do_reset_trip_a },
		{ "Reset trip B", F_BUTTON, .action = do_reset_trip_b },
		{},
	}}},
	{ "Wheel", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Max speed", F_NUMERIC, .numeric = &(const struct cfgnumeric_t){ PTRSIZE(ui_vars.wheel_max_speed_x10), 1, "km/h", 10, 990, 10 }},
		{ "Circumference", F_NUMERIC, .numeric = &(const struct cfgnumeric_t){ PTRSIZE(ui_vars.ui16_wheel_perimeter), 0, "mm", 750, 3000, 10 }},
		{},
	}}},
	{ "Battery", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Max current", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_battery_max_current), 0, "A", 1, 30 }},
 		{ "Cut-off voltage", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_low_voltage_cut_off_x10), 1, "V", 160, 630 }},
		{ "Resistance", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_pack_resistance_x1000), 0, "mohm", 0, 1000 }},
		{ "Voltage", F_NUMERIC|F_RO, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_voltage_soc_x10), 1, "V" }},
		{ "Est. resistance", F_NUMERIC|F_RO, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_pack_resistance_estimated_x1000), 0, "mohm" }},
		{ "Power loss", F_NUMERIC|F_RO, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_power_loss), 0, "W" }},
		{},
	}}},
	{ "Charge", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Display", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_battery_soc_enable), (const char*[]){ "none", "charge %", "voltage", 0 }}},
		{ "Reset voltage", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_battery_voltage_reset_wh_counter_x10), 1, "V", 160, 630 }},
		{ "Total capacity", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui32_wh_x10_100_percent), 1, "Wh", 0, 9990, 100 }},
		{ "Used Wh", F_NUMERIC|F_CALLBACK,  .numeric_cb = &(const struct cfgnumeric_cb_t) { { PTRSIZE(ui_vars.ui32_wh_x10), 1, "Wh", 0, 9990, 100 }, do_set_wh }},
		{},
	}}},
	{ "Motor", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Max current", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_motor_max_current), 0, "A", 1, 30 }},
		{ "Current ramp", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_ramp_up_amps_per_second_x10), 1, "A", 4, 100 }},
		{},
	}}},
	// (BBSHD has no torque sensor — the whole Torque submenu, its calibration
	//  screen, and Motor's TSDZ2 tuning knobs — Motor voltage, Control mode,
	//  Min current, Field weakening — were removed when this display was
	//  ported to the Bafang protocol.)
	{ "Assist", F_BUTTON, .action = cfg_push_assist_screen },
	{ "Walk assist", F_BUTTON, .action = cfg_push_walk_assist_screen },
	{ "Temperature", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Temp. sensor", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_temperature_limit_feature_enabled), disable_enable } },
		{ "Min limit", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_motor_temperature_min_value_to_limit), 0, "C", 30, 100 }},
		{ "Max limit", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_motor_temperature_max_value_to_limit), 0, "C", 30, 100 }},
		{},
	}}},
	{ "Street mode", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Feature", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_street_mode_function_enabled), disable_enable } },
		{ "Current status", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_street_mode_enabled), off_on } },
		{ "At startup", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_street_mode_enabled), (const char*[]){ "no change", "activate", 0 } }},
		{ "Hotkey", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_street_mode_hotkey_enabled), disable_enable } },
		{ "Speed limit", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_street_mode_speed_limit), 0, "km/h", 1, 99 }},
		{ "Power limit", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui16_street_mode_power_limit), 0, "W", 25, 1000, 25 }},
		{ "Throttle", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_street_mode_throttle_enabled), disable_enable } },
		{},
	}}},
	{ "Various", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		{ "Odometer", F_NUMERIC|F_CALLBACK, .numeric_cb = &(const struct cfgnumeric_cb_t) { { PTRSIZE(ui_vars.ui32_odometer_x10), 1, "km", 0, INT32_MAX-100 }, do_set_odometer }},
		{ "Auto power off", F_NUMERIC, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_lcd_power_off_time_minutes), 0, "min", 0, 255 }},
		{ "Reset BLE peers", F_BUTTON, .action = do_reset_ble },
		{ "Reset all settings", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 18, 0, 128, (const struct configtree_t[]) {
			{ "Confirm reset all", F_BUTTON, .action = do_reset_all },
			{}
		}}},
		{}
	}}},
	{ "Technical", F_SUBMENU, .submenu = &(const struct scroller_config){ 20, 58, 36, 0, 128, (const struct configtree_t[]) {
		// BBSHD reports no pedal cadence over the display protocol; this
		// currently reads a hard-coded stub (99). Will be revisited when we
		// decide whether to synthesise from PAS state or hide the field.
		{ "Cadence (stub)", F_NUMERIC|F_RO, .numeric = &(const struct cfgnumeric_t) { PTRSIZE(ui_vars.ui8_pedal_cadence), 0, "rpm" }},
		{},
	}}},
	{}
};

const struct scroller_config cfg_root = { 20, 58, 18, 0, 128,  cfgroot };

static int tmp_rescale = 100;
bool enumerate_assist_levels(const struct scroller_config *cfg, int index, const struct scroller_item_t **it);
bool enumerate_walk_assist_levels(const struct scroller_config *cfg, int index, const struct scroller_item_t **it);

bool do_change_assist_levels(const struct configtree_t *ign, int newv);
bool rescale_update(const struct configtree_t *it, int value);
void rescale_preview(const struct configtree_t *it, int value);
void rescale_revert(const struct configtree_t *it);
void do_resize_assist_levels(const struct configtree_t *ign);
void do_interpolate_assist_levels(const struct configtree_t *ign);

const struct assist_scroller_config cfg_assist = { { 20, 26, 36, 0, 76, (const struct configtree_t[]) {
	{ "Assist levels", F_NUMERIC | F_CALLBACK, .numeric_cb = &(const struct cfgnumeric_cb_t) { { PTRSIZE(ui_vars.ui8_number_of_assist_levels), 0, "", 1, 9 }, do_change_assist_levels }},
	{ "Rescale all", F_NUMERIC | F_CALLBACK, .numeric_cb = &(const struct cfgnumeric_cb_t) { { PTRSIZE(tmp_rescale), 0, "%", 25, 400, 5 }, rescale_update, rescale_preview, rescale_revert }},
	// this is a template
	{ (char[10]){}, F_NUMERIC | F_CALLBACK, .numeric_cb = &(struct cfgnumeric_cb_t) { { { 0, 0 }, 0, "%", 1, 3200 /* we could go up to about 300x assist, but even 30x is absurd */ } }}
}, enumerate_assist_levels }, 2 };

const struct scroller_config cfg_levels_extend = { 20, 26, 18, 0, 76, (const struct configtree_t[]) {
	{ "Interpolate", F_BUTTON, .action = do_interpolate_assist_levels },
	{ "Add higher", F_BUTTON, .action = do_resize_assist_levels },
	{}
}};

const struct scroller_config cfg_levels_truncate = { 20, 26, 18, 0, 76, (const struct configtree_t[]) {
	{ "Interpolate", F_BUTTON, .action = do_interpolate_assist_levels },
	{ "Keep lowest", F_BUTTON, .action = do_resize_assist_levels },
	{}
}};

const struct assist_scroller_config cfg_walk_assist = { { 20, 26, 36, 0, 76, (const struct configtree_t[]) {
	{ "Feature", F_OPTIONS, .options = &(const struct cfgoptions_t) { PTRSIZE(ui_vars.ui8_walk_assist_feature_enabled), disable_enable } },
	// this is a template
	{ (char[10]){}, F_NUMERIC | F_CALLBACK, .numeric_cb = &(struct cfgnumeric_cb_t) { { { 0, 0 }, 0, "%", 1, 100 } }}
}, enumerate_assist_levels }, 1};

static void do_reset_trip_a(const struct configtree_t *ign)
{
	// FIXME is accessing rt_vars safe here?
	rt_vars.ui32_trip_a_distance_x1000 = 0;
	rt_vars.ui32_trip_a_time = 0;
	rt_vars.ui16_trip_a_avg_speed_x10 = 0;
	rt_vars.ui16_trip_a_max_speed_x10 = 0;
	sstack_pop();
}

static void do_reset_trip_b(const struct configtree_t *ign)
{
	rt_vars.ui32_trip_b_distance_x1000 = 0;
	rt_vars.ui32_trip_b_time = 0;
	rt_vars.ui16_trip_b_avg_speed_x10 = 0;
	rt_vars.ui16_trip_b_max_speed_x10 = 0;
	sstack_pop();
}

#if defined(NRF51)
#include "peer_manager.h"
#endif

static void do_reset_ble(const struct configtree_t *ign)
{
#if defined(NRF51)
	// TODO: fist disable any connection
	// Warning: Use this (pm_peers_delete) function only when not connected or connectable. If a peer is or becomes connected
	// or a PM_PEER_DATA_FUNCTIONS function is used during this procedure (until the success or failure event happens),
	// the behavior is undefined.
	pm_peers_delete();
#endif
	sstack_pop();
}

static void do_reset_all(const struct configtree_t *ign)
{
	eeprom_init_defaults();
	showScreen(&screen_main);
}

static bool do_set_wh(const struct configtree_t *ign, int wh)
{
	reset_wh();
	ui_vars.ui32_wh_x10_offset = wh;
	return true;
}

static bool do_set_odometer(const struct configtree_t *ign, int v)
{
	// FIXME rt_vars?
	rt_vars.ui32_odometer_x10 = v;
	return true;
}


