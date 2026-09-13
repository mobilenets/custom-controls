#include "fcn_config.h"

#include <string.h>


void fcn_config_init(fcn_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->agent_port = 8888;
}


bool fcn_config_is_valid(const fcn_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->agent_ip[0] == '\0') {
        return false;
    }

    if (config->agent_port == 0) {
        return false;
    }

    return true;
}
