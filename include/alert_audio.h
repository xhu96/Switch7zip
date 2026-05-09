/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#include <stdbool.h>

void alert_audio_init(void);
void alert_audio_quit(void);
void alert_audio_play(bool success);
