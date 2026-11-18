#ifndef DISTANCE_H_
#define DISTANCE_H_

#include "radar_svc.h"

void distance_process(const struct radar_iq_report *local, const struct radar_iq_report *peer);

#endif
