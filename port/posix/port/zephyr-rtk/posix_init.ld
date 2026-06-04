/*
 * Linker script fragment for the POSIX auto-init mechanism.
 *
 * The posix_init machinery places function-pointer sentinels in
 * .posix$initS / .posix$initE and driver init pointers in
 * .posix$init0 ~ .posix$init2 (priority 0=board, 1=device, 2=app).
 * Without explicit placement rules the linker emits "orphan section"
 * warnings.  This fragment silences them and fixes the ordering so
 * that posix_auto_init() can iterate correctly.
 */

SECTION_PROLOGUE(posix_init,,)
{
    KEEP(*(".posix$initS"))   /* start sentinel */
    KEEP(*(".posix$init0"))   /* priority 0 – board */
    KEEP(*(".posix$init1"))   /* priority 1 – device */
    KEEP(*(".posix$init2"))   /* priority 2 – app */
    KEEP(*(".posix$initE"))   /* end sentinel */
} GROUP_LINK_IN(ROMABLE_REGION)
