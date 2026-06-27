#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define EXT_ID_HIGH(id)   (uint16_t)(((id) >> 13) & 0xFFFFU)
#define EXT_ID_LOW(id)    (uint16_t)((((id) << 3) | 4U) & 0xFFFFU)

/* STM32 Filter Simulator */
typedef struct {
    uint32_t FI_High;
    uint32_t FI_Low;
    uint32_t FM_High;
    uint32_t FM_Low;
    int mode; // 0=16-bit List, 1=32-bit List, 2=32-bit Mask
} FilterBank;

bool check_16bit_list(FilterBank *b, uint32_t std_id, uint32_t ext_id, bool is_ext) {
    if (is_ext) return false; // 16-bit list in this config only matches STD (IDE=0)
    uint32_t match_id = std_id << 5;
    if (b->FI_High == match_id) return true;
    if (b->FM_High == match_id) return true;
    if (b->FI_Low == match_id) return true;
    if (b->FM_Low == match_id) return true;
    return false;
}

bool check_32bit_list(FilterBank *b, uint32_t std_id, uint32_t ext_id, bool is_ext) {
    uint32_t input_id = is_ext ? ((ext_id << 3) | 4U) : (std_id << 21);
    uint32_t f1 = (b->FI_High << 16) | b->FI_Low;
    uint32_t f2 = (b->FM_High << 16) | b->FM_Low;
    if (f1 == input_id || f2 == input_id) return true;
    return false;
}

bool check_32bit_mask(FilterBank *b, uint32_t std_id, uint32_t ext_id, bool is_ext) {
    uint32_t input_id = is_ext ? ((ext_id << 3) | 4U) : (std_id << 21);
    uint32_t f_id = (b->FI_High << 16) | b->FI_Low;
    uint32_t f_mask = (b->FM_High << 16) | b->FM_Low;
    return ((input_id ^ f_id) & f_mask) == 0;
}

bool run_filters(FilterBank banks[], int num_banks, uint32_t std_id, uint32_t ext_id, bool is_ext) {
    for (int i = 0; i < num_banks; i++) {
        FilterBank *b = &banks[i];
        if (b->mode == 0 && check_16bit_list(b, std_id, ext_id, is_ext)) return true;
        if (b->mode == 1 && check_32bit_list(b, std_id, ext_id, is_ext)) return true;
        if (b->mode == 2 && check_32bit_mask(b, std_id, ext_id, is_ext)) return true;
    }
    return false;
}

int main() {
    FilterBank banks[4];

    // Bank 14: 16-bit List Mode
    banks[0].FI_High = 0x02F4U << 5;
    banks[0].FM_High = 0x04F4U << 5;
    banks[0].FI_Low  = 0x05F4U << 5;
    banks[0].FM_Low  = 0x07F4U << 5;
    banks[0].mode = 0;

    // Bank 15: 32-bit List Mode
    banks[1].FI_High = EXT_ID_HIGH(0x18F128F4UL);
    banks[1].FI_Low  = EXT_ID_LOW(0x18F128F4UL);
    banks[1].FM_High = EXT_ID_HIGH(0x18F0F472UL);
    banks[1].FM_Low  = EXT_ID_LOW(0x18F0F472UL);
    banks[1].mode = 1;

    // Bank 16: 32-bit List Mode
    banks[2].FI_High = EXT_ID_HIGH(0x18F528F4UL);
    banks[2].FI_Low  = EXT_ID_LOW(0x18F528F4UL);
    banks[2].FM_High = EXT_ID_HIGH(0x18F228F4UL);
    banks[2].FM_Low  = EXT_ID_LOW(0x18F228F4UL);
    banks[2].mode = 1;

    // Bank 17: 32-bit Mask Mode
    banks[3].FI_High = EXT_ID_HIGH(0x18E000F4UL);
    banks[3].FI_Low  = EXT_ID_LOW(0x18E000F4UL);
    banks[3].FM_High = EXT_ID_HIGH(0xFFFFF8FFUL);
    banks[3].FM_Low  = (uint16_t)((((0xFFFFF8FFUL) << 3) | 6U) & 0xFFFFU);
    banks[3].mode = 2;

    printf("==========================================\n");
    printf(" STM32 CAN FILTER SIMULATOR TEST\n");
    printf("==========================================\n\n");

    struct {
        const char *name;
        uint32_t std_id;
        uint32_t ext_id;
        bool is_ext;
    } tests[] = {
        {"BATT_ST1",       0x02F4, 0,             false},
        {"CELL_VOLT",      0x04F4, 0,             false},
        {"CELL_TEMP",      0x05F4, 0,             false},
        {"ALM_INFO",       0x07F4, 0,             false},
        {"BATT_ST2",       0,      0x18F128F4,    true},
        {"CHG_REQUEST",    0,      0x18F0F472,    true},
        {"BMS_SW_STA",     0,      0x18F528F4,    true},
        {"CELL_TEMP_FULL", 0,      0x18F228F4,    true},
        // Range test for mask
        {"CELL_VOLT_0",    0,      0x18E000F4,    true},
        {"CELL_VOLT_3",    0,      0x18E003F4,    true},
        {"CELL_VOLT_7",    0,      0x18E007F4,    true},
    };

    bool all_ok = true;

    printf("--- VALID IDs (Must be ACCEPTED) ---\n");
    for (int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        bool accepted = run_filters(banks, 4, tests[i].std_id, tests[i].ext_id, tests[i].is_ext);
        const char *t = tests[i].is_ext ? "EXT" : "STD";
        uint32_t id = tests[i].is_ext ? tests[i].ext_id : tests[i].std_id;
        
        printf("[%-4s] 0x%08X %-15s -> %s\n", t, id, tests[i].name, accepted ? "PASS (Accepted)" : "FAIL (Rejected)");
        if (!accepted) all_ok = false;
    }

    printf("\n--- GARBAGE IDs (Must be REJECTED) ---\n");
    struct {
        const char *name;
        uint32_t std_id;
        uint32_t ext_id;
        bool is_ext;
    } garbage_tests[] = {
        {"Random STD",     0x123,  0,             false},
        {"Another STD",    0x7FF,  0,             false},
        {"Random EXT",     0,      0x18EEEEEE,    true},
        {"CELL_VOLT_8",    0,      0x18E008F4,    true}, // Out of bounds of mask!
    };

    for (int i = 0; i < sizeof(garbage_tests)/sizeof(garbage_tests[0]); i++) {
        bool accepted = run_filters(banks, 4, garbage_tests[i].std_id, garbage_tests[i].ext_id, garbage_tests[i].is_ext);
        const char *t = garbage_tests[i].is_ext ? "EXT" : "STD";
        uint32_t id = garbage_tests[i].is_ext ? garbage_tests[i].ext_id : garbage_tests[i].std_id;
        
        printf("[%-4s] 0x%08X %-15s -> %s\n", t, id, garbage_tests[i].name, !accepted ? "PASS (Rejected)" : "FAIL (Accepted)");
        if (accepted) all_ok = false;
    }

    printf("\nRESULT: %s\n", all_ok ? "ALL TESTS PASSED!" : "SOME TESTS FAILED!");
    return all_ok ? 0 : 1;
}
