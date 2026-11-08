/*
 * level_check.h
 *
 *  Created on: Aug 8, 2026
 *      Author: graytongiesman
 */

#ifndef INC_LEVEL_CHECK_H_
#define INC_LEVEL_CHECK_H_

#include <stdbool.h>

void level_check_init(void);

// samples all 7 float switches once and advances their debounce state.
// call exactly once per main-loop pass, before reading the getters below.
// the getters are plain reads of the debounced state, so they can be called
// in any order, any number of times - including zero - within a pass.
void level_check_update(void);

// main tank float switches
bool tank_is_low(void);       // true: water at/below the LOW mark
bool tank_below_refill(void); // true: water below the REFILL mark
bool tank_is_full(void);      // true: water at/above the FULL (high) mark

// bottle float switches
bool bot1_is_empty(void);
bool bot2_is_empty(void);
bool bot3_is_empty(void);
bool bot4_is_empty(void);

#endif /* INC_LEVEL_CHECK_H_ */
