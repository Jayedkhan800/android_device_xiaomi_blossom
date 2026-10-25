/*
 * SPDX-FileCopyrightText: 2026 The Xiaomi Blossom Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "sensor_shim"

#include "sensor_shim_ops.h"

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

#include <log/log.h>
#include <mutex>

namespace {

std::mutex g_shim_lock;
void* g_provider_handle = nullptr;

/*
 * Binds a symbol provider: every mandatory call is looked up by its legacy
 * name, optional calls may be absent. The legacy surface uses an explicit
 * cookie as first argument, while the exported C calls carry their device
 * pointer instead, so the two shapes are resolved to the same table type.
 */
int BindBySymbol(void* handle, char const* path, sensor_shim_ops_t* ops) {
    struct Entry {
        char const* symbol;
        size_t offset;
        bool mandatory;
    };

    static constexpr Entry kEntries[] = {
            {"legacy_sensors_get_sensors_list",
             offsetof(sensor_shim_ops_t, get_sensors_list), true},
            {"legacy_sensors_open_device", offsetof(sensor_shim_ops_t, open_device), true},
            {"legacy_sensors_close_device", offsetof(sensor_shim_ops_t, close_device), true},
            {"legacy_sensors_activate", offsetof(sensor_shim_ops_t, activate), true},
            {"legacy_sensors_set_delay", offsetof(sensor_shim_ops_t, set_delay), true},
            {"legacy_sensors_poll", offsetof(sensor_shim_ops_t, poll), true},
            {"legacy_sensors_batch", offsetof(sensor_shim_ops_t, batch), false},
            {"legacy_sensors_flush", offsetof(sensor_shim_ops_t, flush), false},
            {"legacy_sensors_inject_sensor_data", offsetof(sensor_shim_ops_t, inject_sensor_data),
             false},
            {"legacy_sensors_register_direct_channel",
             offsetof(sensor_shim_ops_t, register_direct_channel), false},
            {"legacy_sensors_config_direct_report",
             offsetof(sensor_shim_ops_t, config_direct_report), false},
    };

    for (const Entry& entry : kEntries) {
        void* sym = dlsym(handle, entry.symbol);
        if (sym == nullptr) {
            if (entry.mandatory) {
                ALOGE("%s: mandatory symbol %s is missing", path, entry.symbol);
                return -ENOLINK;
            }
            ALOGV("%s: optional symbol %s is absent", path, entry.symbol);
            continue;
        }
        *reinterpret_cast<void**>(reinterpret_cast<char*>(ops) + entry.offset) = sym;
    }

    return 0;
}

} // namespace

int sensor_shim_open(char const* provider_path, char const* entry_symbol,
                       sensor_shim_ops_t* out_ops) {
    if (provider_path == nullptr || out_ops == nullptr) {
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(g_shim_lock);

    if (g_provider_handle != nullptr) {
        ALOGE("a provider is already bound, refusing to load %s", provider_path);
        return -EBUSY;
    }

    void* handle = dlopen(provider_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        ALOGE("cannot load provider %s: %s", provider_path, dlerror());
        return -ENOENT;
    }

    memset(out_ops, 0, sizeof(*out_ops));
    out_ops->cookie = handle;

    int err = 0;
    if (entry_symbol != nullptr) {
        auto init = reinterpret_cast<sensor_shim_provider_init_t>(dlsym(handle, entry_symbol));
        if (init == nullptr) {
            ALOGE("%s: table init %s not exported: %s", provider_path, entry_symbol, dlerror());
            err = -ENOLINK;
        } else {
            err = init(out_ops);
            if (err != 0) {
                ALOGE("%s: table init %s rejected the request: %d", provider_path, entry_symbol,
                      err);
            }
        }
    } else {
        err = BindBySymbol(handle, provider_path, out_ops);
    }

    if (err != 0) {
        dlclose(handle);
        memset(out_ops, 0, sizeof(*out_ops));
        return err;
    }

    g_provider_handle = handle;
    ALOGI("provider %s bound (%s mode)", provider_path,
          entry_symbol != nullptr ? "table" : "symbol");
    return 0;
}

void sensor_shim_close(void) {
    std::lock_guard<std::mutex> lock(g_shim_lock);

    if (g_provider_handle == nullptr) {
        return;
    }

    dlclose(g_provider_handle);
    g_provider_handle = nullptr;
}
