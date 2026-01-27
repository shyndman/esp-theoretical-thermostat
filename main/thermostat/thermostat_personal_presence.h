#pragma once

#include <stdbool.h>

void thermostat_personal_presence_init(void);
void thermostat_personal_presence_process_face(const char *payload, bool retained);
void thermostat_personal_presence_process_person_count(const char *payload);
void thermostat_personal_presence_on_led_complete(void);
