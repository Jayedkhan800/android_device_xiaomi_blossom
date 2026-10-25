/*
 * SPDX-FileCopyrightText: 2026 The Xiaomi Blossom Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/sensors.h>
#include <stdint.h>

/*
 * Runtime shim for legacy sensors HAL providers.
 *
 * Prebuilt vendor sensor stacks on this platform were built against older
 * revisions of <hardware/sensors.h> and some of them only expose a plain C
 * entry surface instead of a sensors_module_t. Instead of linking such a
 * provider at build time, describe the interface as a table of function
 * pointers and bind it to the provider at runtime.
 *
 * Two binding styles are supported:
 *
 *   1. Table providers, which export a single init function matching
 *      sensor_shim_provider_init_t. It fills the whole table and returns
 *      0 on success.
 *
 *   2. Symbol providers, which export the legacy C calls one by one. The
 *      shim resolves every entry point with dlsym(3); entries that are
 *      not exported stay NULL and callers must treat them as unsupported.
 *
 * The shim never owns the provider's hw_device_t instances, it only
 * forwards calls into them.
 */

typedef struct sensor_shim_ops {
    /* Provider cookie, opaque to callers, passed as first argument. */
    void* cookie;

    int (*get_sensors_list)(void* cookie, struct sensor_t const** list);
    int (*open_device)(void* cookie, char const* name, struct hw_device_t** device);
    int (*close_device)(void* cookie, struct hw_device_t* device);

    int (*activate)(void* cookie, int handle, int enabled);
    int (*set_delay)(void* cookie, int handle, int64_t sampling_period_ns);
    int (*poll)(void* cookie, sensors_event_t* events, int count);

    /* Optional, NULL on providers older than sensors HAL 1.0. */
    int (*batch)(void* cookie, int handle, int flags, int64_t sampling_period_ns,
                 int64_t max_report_latency_ns);
    int (*flush)(void* cookie, int handle);
    int (*inject_sensor_data)(void* cookie, sensors_event_t const* event);

    /* Optional, NULL on providers older than sensors HAL 1.3. */
    int (*register_direct_channel)(void* cookie, struct sensors_direct_mem_t const* mem,
                                   int channel);
    int (*config_direct_report)(void* cookie, int handle, int channel,
                                struct sensors_direct_cfg_t* config);
} sensor_shim_ops_t;

/* Signature of the init function exported by table providers. */
typedef int (*sensor_shim_provider_init_t)(sensor_shim_ops_t* out_ops);

/*
 * Loads the provider library and binds out_ops to it.
 *
 * @param provider_path absolute path of the provider shared object
 * @param entry_symbol  name of the table init function, or NULL to bind
 *                      each legacy symbol separately
 * @param out_ops       table to fill, cleared before binding
 * @return 0 on success, -ENOENT when the provider cannot be loaded,
 *         -ENOLINK when a mandatory entry point is missing
 */
int sensor_shim_open(char const* provider_path, char const* entry_symbol,
                       sensor_shim_ops_t* out_ops);

/* Releases the provider again. Safe to call on an unbound shim. */
void sensor_shim_close(void);
