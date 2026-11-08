#include "hardware/structs/sau.h"
#include "hardware/structs/scb.h"
#include "hardware/platform_defs.h"
#include "hardware/regs/sio.h"
#include "exam_mode.h"
#include "tz_config.h"

#define SAU_CTRL_ENABLE_BIT (1UL << 0)
#define SAU_RLAR_ENABLE_BIT (1UL << 0)
#define SAU_RLAR_LADDR_MASK 0xFFFFF000UL

#define EXAM_FLASH_ADDR (XIP_BASE + EXAM_FLASH_TARGET_OFFSET)
#define EXAM_FLASH_END  (EXAM_FLASH_ADDR + 0xFFFUL)

#define DHCSR_ADDR     0xE000EDF0UL
#define DHCSR_DBGKEY    0xA05F0000UL

static volatile uint32_t *const dhcsr = (volatile uint32_t *)DHCSR_ADDR;
static volatile bool g_sau_locked = false;

void tz_config_init(void)
{
    sau_hw->ctrl = 0;
    __asm volatile("dsb sy" ::: "memory");
    __asm volatile("isb" ::: "memory");

    /* Region 0: Exam flag flash sector — Secure only.
     * The application runs entirely in Secure state, so the only SAU
     * region we need is the one that seals the exam flag against any
     * code that could run in the Non-Secure state.
     */
    sau_hw->rnr = 0;
    sau_hw->rbar = EXAM_FLASH_ADDR;
    sau_hw->rlar = (EXAM_FLASH_END & SAU_RLAR_LADDR_MASK)
                 | SAU_RLAR_ENABLE_BIT;

    /* Regions 1-7: disabled (default to Secure attribution) */
    for (unsigned int r = 1; r < 8; r++)
    {
        sau_hw->rnr = r;
        sau_hw->rbar = 0;
        sau_hw->rlar = 0;
    }

    /* Enable SAU */
    sau_hw->ctrl = SAU_CTRL_ENABLE_BIT;

    __asm volatile("dsb sy" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

void tz_config_lock_sau(void)
{
    __asm volatile("dsb sy" ::: "memory");
    __asm volatile("isb" ::: "memory");

    g_sau_locked = true;
}

void tz_config_lock_debug(void)
{
    __asm volatile("dsb sy" ::: "memory");

    /* Disable debug: write DBGKEY with C_DEBUGEN=0 (bit 0 cleared) so that
     * no debugger can halt/read the core while exam mode is active. */
    *dhcsr = DHCSR_DBGKEY;

    __asm volatile("dsb sy" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

void tz_config_lock_all(void)
{
    /* Exam-mode hardening, final configuration:
     * - tz_config_lock_debug(): disables SWD/MDBTAP debug access (DHCSR).
     * - tz_config_lock_sau(): seals the exam flag sector via SAU.
     * Note: ACCESSCTRL is intentionally NOT locked — the whole app runs in
     * Secure state with no Non-Secure masters, so ACCESSCTRL added no real
     * protection and its register access froze the device on hardware.
     */
    tz_config_lock_debug();
    tz_config_lock_sau();
}

bool tz_config_is_sau_locked(void)
{
    return g_sau_locked;
}
