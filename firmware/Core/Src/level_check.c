/*
 * level_check.c
 *
 *  Created on: Aug 8, 2026
 *      Author: graytongiesman
 */
#include "level_check.h"
#include "stm32f4xx_hal.h"

#define TANK_HIGH_PORT    GPIOA
#define TANK_HIGH_PIN     GPIO_PIN_15
#define TANK_REFILL_PORT  GPIOB
#define TANK_REFILL_PIN   GPIO_PIN_7
#define TANK_LOW_PORT     GPIOB
#define TANK_LOW_PIN      GPIO_PIN_0


#define BOT1_LEVEL_PORT   GPIOC
#define BOT1_LEVEL_PIN    GPIO_PIN_10

#define BOT2_LEVEL_PORT   GPIOC
#define BOT2_LEVEL_PIN    GPIO_PIN_11

#define BOT3_LEVEL_PORT   GPIOC
#define BOT3_LEVEL_PIN    GPIO_PIN_12

#define BOT4_LEVEL_PORT   GPIOD
#define BOT4_LEVEL_PIN    GPIO_PIN_2


// bottle switches are wired normally-open with a pull-up: the pin reads SET
// (HIGH) when the float switch is open, i.e. no liquid contact at that
// sensor's height, and RESET (LOW) when liquid is present there.
//
// the tank floats are the opposite polarity (vertical vs. horizontal switch
// bodies - normally-closed instead of normally-open), same pull-up wiring:
// RESET (LOW) when open/no liquid contact, SET (HIGH) when liquid present.
//
// `asserted` below is the pin level that means the condition the getter
// reports is true, so the polarity difference lives in this table rather
// than being repeated at each read site.

typedef enum {
    SW_TANK_LOW = 0,    // asserted: water at/below the LOW mark
    SW_TANK_REFILL,     // asserted: water below the REFILL mark
    SW_TANK_HIGH,       // asserted: water at/above the FULL mark
    SW_BOT1,            // asserted: bottle empty
    SW_BOT2,
    SW_BOT3,
    SW_BOT4,
    SW_COUNT
} switch_id_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState asserted;
} switch_cfg_t;

static const switch_cfg_t switches[SW_COUNT] = {
    [SW_TANK_LOW]    = { TANK_LOW_PORT,    TANK_LOW_PIN,    GPIO_PIN_RESET },
    [SW_TANK_REFILL] = { TANK_REFILL_PORT, TANK_REFILL_PIN, GPIO_PIN_RESET },
    [SW_TANK_HIGH]   = { TANK_HIGH_PORT,   TANK_HIGH_PIN,   GPIO_PIN_SET   },
    [SW_BOT1]        = { BOT1_LEVEL_PORT,  BOT1_LEVEL_PIN,  GPIO_PIN_SET   },
    [SW_BOT2]        = { BOT2_LEVEL_PORT,  BOT2_LEVEL_PIN,  GPIO_PIN_SET   },
    [SW_BOT3]        = { BOT3_LEVEL_PORT,  BOT3_LEVEL_PIN,  GPIO_PIN_SET   },
    [SW_BOT4]        = { BOT4_LEVEL_PORT,  BOT4_LEVEL_PIN,  GPIO_PIN_SET   },
};

// debounce is sampled once per main-loop pass (50 ms), so this needs no
// timer of its own. the count is advanced by level_check_update() only, so
// it does not matter how many times - or how few - the getters below are
// called in a given pass.
#define DEBOUNCE_SAMPLES 3  // ~150 ms at the 50 ms main loop poll rate

typedef struct {
    bool stable;
    bool candidate;
    uint8_t count;
} debounce_t;

static debounce_t debounce[SW_COUNT];

static bool switch_read_raw(switch_id_t id)
{
    return HAL_GPIO_ReadPin(switches[id].port, switches[id].pin) == switches[id].asserted;
}

void level_check_init(void)
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    init.Mode  = GPIO_MODE_INPUT;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    init.Pin   = TANK_HIGH_PIN;
    HAL_GPIO_Init(TANK_HIGH_PORT, &init);

    init.Pin   = TANK_REFILL_PIN | TANK_LOW_PIN;
    HAL_GPIO_Init(TANK_REFILL_PORT, &init);

    init.Pin   = BOT1_LEVEL_PIN | BOT2_LEVEL_PIN | BOT3_LEVEL_PIN;
    HAL_GPIO_Init(BOT1_LEVEL_PORT, &init);

    init.Pin   = BOT4_LEVEL_PIN;
    HAL_GPIO_Init(BOT4_LEVEL_PORT, &init);

    // seed from the actual pin state so the getters report the real level on
    // the first pass, instead of reading as de-asserted for the first
    // DEBOUNCE_SAMPLES passes. a switch still settling at power-on gets
    // corrected within the usual ~150 ms.
    for (switch_id_t i = 0; i < SW_COUNT; i++) {
        bool raw = switch_read_raw(i);
        debounce[i].stable    = raw;
        debounce[i].candidate = raw;
        debounce[i].count     = DEBOUNCE_SAMPLES;
    }
}

// samples every switch once and advances its debounce counter. call exactly
// once per main-loop pass, before the getters are read.
void level_check_update(void)
{
    for (switch_id_t i = 0; i < SW_COUNT; i++) {
        debounce_t *d = &debounce[i];
        bool raw = switch_read_raw(i);

        if (raw == d->candidate) {
            if (d->count < DEBOUNCE_SAMPLES) {
                d->count++;
            }
        } else {
            d->candidate = raw;
            d->count = 1;
        }

        if (d->count >= DEBOUNCE_SAMPLES) {
            d->stable = d->candidate;
        }
    }
}

bool tank_is_low(void)       { return debounce[SW_TANK_LOW].stable; }
bool tank_below_refill(void) { return debounce[SW_TANK_REFILL].stable; }
bool tank_is_full(void)      { return debounce[SW_TANK_HIGH].stable; }

bool bot1_is_empty(void) { return debounce[SW_BOT1].stable; }
bool bot2_is_empty(void) { return debounce[SW_BOT2].stable; }
bool bot3_is_empty(void) { return debounce[SW_BOT3].stable; }
bool bot4_is_empty(void) { return debounce[SW_BOT4].stable; }
