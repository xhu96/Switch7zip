/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

const char *switch7zip_log_path(void);
void switch7zip_log_reset(void);
void switch7zip_log_line(const char *fmt, ...);
