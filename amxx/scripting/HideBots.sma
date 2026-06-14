#include <amxmodx>
#include <hide_bots>

#pragma semicolon 1
#pragma tabsize 4

public plugin_init()
{
    register_plugin("Hide Bots", "1.0.0", "Wirstaff", "https://t.me/wirstaff_inc", "Hides bots from A2S and Steam APIs");

    hb_set_bot_count(1);
}