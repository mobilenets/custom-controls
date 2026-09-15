#include "fcn_status.h"

#include "esp_err.h"

/*
 * Board-specific implementation.
 *
 * Common FCN code deals only with semantic status values.
 * The selected board driver decides how those states
 * are physically displayed.
 */

extern esp_err_t fcn_board_status_init(void);
extern esp_err_t fcn_board_status_set(fcn_status_t status);


esp_err_t fcn_status_init(void)
{
    return fcn_board_status_init();
}


esp_err_t fcn_status_set(fcn_status_t status)
{
    return fcn_board_status_set(status);
}
