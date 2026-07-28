#include "competition_storage.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ti_msp_dl_config.h"

#define COMPETITION_STORAGE_ADDRESS 0x0001FC00UL
#define COMPETITION_STORAGE_MAGIC   0x43464745UL

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size_bytes;
    uint32_t generation;
    competition_settings_t settings;
    uint32_t crc32;
    uint32_t reserved;
} competition_storage_record_t;

_Static_assert((sizeof(competition_storage_record_t) % 8U) == 0U,
    "competition record must use complete flash words");
_Static_assert((COMPETITION_STORAGE_ADDRESS % 1024U) == 0U,
    "competition storage must start on a sector boundary");

volatile competition_storage_diagnostics_t g_competition_storage_diag;

static uint32_t CompetitionStorage_RecordCrc(
    const competition_storage_record_t *record)
{
    return CompetitionStorage_Crc32(record,
        (uint32_t) offsetof(competition_storage_record_t, crc32));
}

uint32_t CompetitionStorage_Crc32(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++) {
            uint32_t mask = 0U - (crc & 1U);

            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool CompetitionStorage_Load(competition_settings_t *settings)
{
    const competition_storage_record_t *record =
        (const competition_storage_record_t *) COMPETITION_STORAGE_ADDRESS;

    g_competition_storage_diag.load_count++;
    if (settings == NULL ||
        record->magic != COMPETITION_STORAGE_MAGIC ||
        record->version != COMPETITION_SETTINGS_VERSION ||
        record->size_bytes != sizeof(competition_storage_record_t) ||
        record->crc32 != CompetitionStorage_RecordCrc(record)) {
        g_competition_storage_diag.loaded_valid = 0U;
        return false;
    }
    *settings = record->settings;
    g_competition_storage_diag.generation = record->generation;
    g_competition_storage_diag.loaded_valid = 1U;
    return true;
}

bool CompetitionStorage_Save(const competition_settings_t *settings)
{
    competition_storage_record_t record;
    const uint32_t *words;
    uint32_t offset;
    bool ok = true;

    if (settings == NULL) {
        return false;
    }
    memset(&record, 0xFF, sizeof(record));
    record.magic = COMPETITION_STORAGE_MAGIC;
    record.version = COMPETITION_SETTINGS_VERSION;
    record.size_bytes = sizeof(record);
    record.generation = g_competition_storage_diag.generation + 1U;
    record.settings = *settings;
    record.crc32 = CompetitionStorage_RecordCrc(&record);
    words = (const uint32_t *) &record;

    taskENTER_CRITICAL();
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, COMPETITION_STORAGE_ADDRESS,
        DL_FLASHCTL_REGION_SELECT_MAIN);
    ok = DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL,
        COMPETITION_STORAGE_ADDRESS, DL_FLASHCTL_COMMAND_SIZE_SECTOR) ==
        DL_FLASHCTL_COMMAND_STATUS_PASSED;
    for (offset = 0U; ok && offset < sizeof(record); offset += 8U) {
        DL_FlashCTL_executeClearStatus(FLASHCTL);
        DL_FlashCTL_unprotectSector(FLASHCTL,
            COMPETITION_STORAGE_ADDRESS, DL_FLASHCTL_REGION_SELECT_MAIN);
        ok = DL_FlashCTL_programMemoryFromRAM64WithECCGenerated(FLASHCTL,
            COMPETITION_STORAGE_ADDRESS + offset, &words[offset / 4U]) ==
            DL_FLASHCTL_COMMAND_STATUS_PASSED;
    }
    DL_FlashCTL_protectSector(FLASHCTL, COMPETITION_STORAGE_ADDRESS,
        DL_FLASHCTL_REGION_SELECT_MAIN);
    taskEXIT_CRITICAL();

    if (ok) {
        const competition_storage_record_t *stored =
            (const competition_storage_record_t *)
                COMPETITION_STORAGE_ADDRESS;

        ok = memcmp(stored, &record, sizeof(record)) == 0;
    }
    g_competition_storage_diag.last_save_ok = ok ? 1U : 0U;
    if (ok) {
        g_competition_storage_diag.save_count++;
        g_competition_storage_diag.generation = record.generation;
    } else {
        g_competition_storage_diag.save_failure_count++;
    }
    return ok;
}
