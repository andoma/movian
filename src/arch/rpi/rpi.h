#pragma once


#define DISPLAY_STATUS_OFF             0
#define DISPLAY_STATUS_ON              1

#define RUNMODE_EXIT                   0
#define RUNMODE_RUNNING                1
#define RUNMODE_STANDBY                2

extern int display_status;
extern int cec_we_are_not_active;

void rpi_cec_init(void);
