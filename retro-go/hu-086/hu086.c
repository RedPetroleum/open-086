// HU-086 power handling. Copied into components/retro-go/ by build_retro_go.sh.
#include "rg_system.h"

#if defined(RG_TARGET_HU_086) && defined(ESP_PLATFORM)
#include <driver/gpio.h>

#define LONG_PRESS_MS 2000

static void power_off(void)
{
    RG_LOGW("Power button held, switching off");
    gpio_hold_dis(HU086_POWER_CTRL);
    gpio_set_level(HU086_POWER_CTRL, 0);
    // Running on battery the latch drops now. On USB power we keep running, so halt.
    rg_system_shutdown();
}

static void power_task(void *arg)
{
    int held_ms = 0;
    // The button is usually still held from switching on: wait for the release first.
    while (gpio_get_level(HU086_POWER_SW) == 0)
        rg_task_delay(50);
    while (1)
    {
        held_ms = gpio_get_level(HU086_POWER_SW) == 0 ? held_ms + 50 : 0;
        if (held_ms >= LONG_PRESS_MS)
            power_off();
        rg_task_delay(50);
    }
}

void hu086_platform_init(void)
{
    // GPIO41 (LCD reset) is muxed to JTAG (MTDI) at boot, and the display driver only calls
    // gpio_set_direction(), which does not switch the pad to GPIO: the panel stays in reset
    // and the screen black. gpio_reset_pin() selects the GPIO function.
    gpio_reset_pin(RG_GPIO_LCD_RST);
    gpio_reset_pin(RG_GPIO_LCD_DC);

    // Latch the power on. The hold keeps GPIO1 high through esp_restart(), which retro-go
    // uses to switch between launcher and emulators; otherwise the console would switch off.
    gpio_set_level(HU086_POWER_CTRL, 1);
    gpio_set_direction(HU086_POWER_CTRL, GPIO_MODE_OUTPUT);
    gpio_hold_en(HU086_POWER_CTRL);

    gpio_set_direction(HU086_POWER_SW, GPIO_MODE_INPUT);
    gpio_set_pull_mode(HU086_POWER_SW, GPIO_PULLUP_ONLY);
    rg_task_create("hu086_power", &power_task, NULL, 2048, RG_TASK_PRIORITY_1, -1);
}
#endif
