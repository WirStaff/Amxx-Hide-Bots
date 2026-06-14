#if defined _hide_bots_included
    #endinput
#endif
#define _hide_bots_included

#pragma reqlib hide_bots
#if !defined AMXMODX_NOAUTOLOAD
    #pragma loadlib hide_bots
#endif

/**
 * Sets the number of bots
 *
 * @param count Number of bots
 *
 * @noreturn
 */
native hb_set_bot_count(const count = 0);
