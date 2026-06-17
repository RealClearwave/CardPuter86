#include "guest_memory.h"
#include "gbConfig.h"
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <wear_levelling.h>

static const char *SWAP_PARTITION_LABEL = "swap";
static const uint32_t PAGE_SIZE = 4096;
static const uint16_t GUEST_PAGE_COUNT = gb_max_ram / PAGE_SIZE;
static const uint8_t CACHE_PAGE_COUNT = 32;
static const int8_t NO_SLOT = -1;
static const uint8_t NO_PAGE = 0xFF;

static const esp_partition_t *swap_partition = nullptr;
static wl_handle_t swap_wl = WL_INVALID_HANDLE;
static uint8_t *cache_data = nullptr;
static int8_t page_to_slot[GUEST_PAGE_COUNT];
static uint8_t slot_page[CACHE_PAGE_COUNT];
static uint64_t slot_age[CACHE_PAGE_COUNT];
static bool slot_dirty[CACHE_PAGE_COUNT];
static uint8_t swap_valid[(GUEST_PAGE_COUNT + 7) / 8];
static uint64_t access_clock = 0;
static bool mode_512k = false;

static bool load_512k_setting(void) {
    if (nvs_flash_init() != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t enabled = 0;
    nvs_get_u8(handle, "ram512", &enabled);
    nvs_close(handle);
    return enabled != 0;
}

static void save_512k_setting(bool enabled) {
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, "ram512", enabled ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool mount_swap(void) {
    if (swap_wl != WL_INVALID_HANDLE) return true;
    swap_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
        SWAP_PARTITION_LABEL);
    if (!swap_partition || wl_mount(swap_partition, &swap_wl) != ESP_OK ||
        wl_sector_size(swap_wl) != PAGE_SIZE || wl_size(swap_wl) < gb_max_ram) {
        if (swap_wl != WL_INVALID_HANDLE) wl_unmount(swap_wl);
        swap_wl = WL_INVALID_HANDLE;
        return false;
    }
    return true;
}

static bool swap_page_valid(uint8_t page) {
    return swap_valid[page >> 3] & (1U << (page & 7));
}

static void set_swap_page_valid(uint8_t page) {
    swap_valid[page >> 3] |= 1U << (page & 7);
}

static bool write_back(uint8_t slot) {
    if (!slot_dirty[slot] || slot_page[slot] == NO_PAGE) return true;
    const size_t offset = (size_t)slot_page[slot] * PAGE_SIZE;
    if (wl_erase_range(swap_wl, offset, PAGE_SIZE) != ESP_OK ||
        wl_write(swap_wl, offset,
                 cache_data + slot * PAGE_SIZE, PAGE_SIZE) != ESP_OK) {
        return false;
    }
    set_swap_page_valid(slot_page[slot]);
    slot_dirty[slot] = false;
    return true;
}

static int8_t load_page(uint8_t page) {
    int8_t slot = page_to_slot[page];
    if (slot != NO_SLOT) {
        slot_age[slot] = ++access_clock;
        return slot;
    }

    uint8_t victim = 0;
    for (uint8_t i = 0; i < CACHE_PAGE_COUNT; i++) {
        if (slot_page[i] == NO_PAGE) {
            victim = i;
            break;
        }
        if (slot_age[i] < slot_age[victim]) victim = i;
    }

    if (!write_back(victim)) return NO_SLOT;
    if (slot_page[victim] != NO_PAGE) page_to_slot[slot_page[victim]] = NO_SLOT;

    uint8_t *destination = cache_data + victim * PAGE_SIZE;
    if (swap_page_valid(page)) {
        if (wl_read(swap_wl, (size_t)page * PAGE_SIZE,
                    destination, PAGE_SIZE) != ESP_OK) {
            return NO_SLOT;
        }
    } else {
        memset(destination, 0, PAGE_SIZE);
    }

    slot_page[victim] = page;
    slot_age[victim] = ++access_clock;
    slot_dirty[victim] = false;
    page_to_slot[page] = victim;
    return victim;
}

bool guest_memory_init(void) {
    static_assert(gb_max_ram == 512 * 1024,
                  "Swap layout currently expects 512 KB guest RAM");
    mode_512k = load_512k_setting();
    cache_data = static_cast<uint8_t *>(malloc(CACHE_PAGE_COUNT * PAGE_SIZE));
    if (!cache_data) return false;
    if (mode_512k && !mount_swap()) mode_512k = false;
    guest_memory_clear();
    return true;
}

bool guest_memory_set_512k_enabled(bool enabled) {
    if (enabled && !mount_swap()) return false;
    guest_memory_clear();
    if (!enabled && swap_wl != WL_INVALID_HANDLE) {
        wl_unmount(swap_wl);
        swap_wl = WL_INVALID_HANDLE;
    }
    mode_512k = enabled;
    save_512k_setting(enabled);
    return true;
}

bool guest_memory_512k_enabled(void) {
    return mode_512k;
}

uint32_t guest_memory_size(void) {
    return mode_512k ? gb_max_ram : 128 * 1024;
}

void guest_memory_clear(void) {
    memset(page_to_slot, NO_SLOT, sizeof(page_to_slot));
    memset(slot_page, NO_PAGE, sizeof(slot_page));
    memset(slot_age, 0, sizeof(slot_age));
    memset(slot_dirty, 0, sizeof(slot_dirty));
    memset(swap_valid, 0, sizeof(swap_valid));
    if (cache_data) memset(cache_data, 0, CACHE_PAGE_COUNT * PAGE_SIZE);
    access_clock = 0;
}

uint8_t guest_memory_read(uint32_t address) {
    if (!cache_data || address >= guest_memory_size()) return 0;
    const uint8_t page = address / PAGE_SIZE;
    const int8_t slot = load_page(page);
    if (slot == NO_SLOT) return 0;
    return cache_data[(uint32_t)slot * PAGE_SIZE + (address & (PAGE_SIZE - 1))];
}

void guest_memory_write(uint32_t address, uint8_t value) {
    if (!cache_data || address >= guest_memory_size()) return;
    const uint8_t page = address / PAGE_SIZE;
    const int8_t slot = load_page(page);
    if (slot == NO_SLOT) return;
    cache_data[(uint32_t)slot * PAGE_SIZE + (address & (PAGE_SIZE - 1))] = value;
    slot_dirty[slot] = true;
}
