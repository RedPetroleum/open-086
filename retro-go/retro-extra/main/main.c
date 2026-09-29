#include "shared.h"

void app_main(void)
{
    rg_app_t *app = rg_system_init(AUDIO_SAMPLE_RATE, NULL, NULL);
    if (strcmp(app->configNs, "a26") == 0)
        a26_main();
    RG_PANIC("Unknown app for retro-extra");
}
