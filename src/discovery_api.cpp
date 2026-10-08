// SPDX-License-Identifier: Apache-2.0
// C boundary for send_airplay2/receivers.h: argument checks, the owning list and
// exception containment. Selection logic lives in receiver_list.cpp.
#include "send_airplay2/receivers.h"
#include "send_airplay2/discovery.h"
#include "receiver_list.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

struct sap2_receiver_list {
    std::vector<send_airplay2::detail::ReceiverEntry> entries;
};

extern "C" {

int32_t sap2_discover(uint32_t duration_ms, sap2_receiver_list** list) {
    if (!list) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    *list = nullptr;
    if (duration_ms == 0 || duration_ms > SAP2_MAX_DISCOVERY_MS) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        send_airplay2::DiscoveryOptions options;
        options.duration_ms = duration_ms;
        const auto result = send_airplay2::discover(options);
        auto owned = std::make_unique<sap2_receiver_list>();
        owned->entries = send_airplay2::detail::castable_receivers(result);
        *list = owned.release();
        return SAP2_OK;
    } catch (const std::invalid_argument&) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return SAP2_ERROR_OUT_OF_MEMORY;
    } catch (const std::runtime_error&) {
        return SAP2_ERROR_CONNECTION; // Native transport setup or I/O failed.
    } catch (...) {
        return SAP2_ERROR_INTERNAL;
    }
}

size_t sap2_receiver_list_count(const sap2_receiver_list* list) {
    return list ? list->entries.size() : 0;
}

int32_t sap2_receiver_list_get(const sap2_receiver_list* list, size_t index,
                               sap2_receiver_info* info) {
    if (!list || !info || info->struct_size < sizeof(sap2_receiver_info) ||
        index >= list->entries.size()) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto& entry = list->entries[index];
    info->flags = (entry.password_required ? SAP2_RECEIVER_PASSWORD_REQUIRED : 0u) |
                  (entry.features ? SAP2_RECEIVER_HAS_FEATURES : 0u);
    info->id = entry.id.c_str();
    info->name = entry.name.c_str();
    info->name_length = entry.name.size();
    info->model = entry.model.c_str();
    info->address = entry.address.c_str();
    info->port = entry.port;
    info->features = entry.features.value_or(0);
    return SAP2_OK;
}

void sap2_receiver_list_free(sap2_receiver_list* list) {
    delete list;
}
} // extern "C"
