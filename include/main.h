#ifndef MAIN_H
#define MAIN_H

//是否启用DEBUG调试输出
#define DEBUG
#ifdef DEBUG
#define CONFIG_ARDUHAL_LOG_COLORS 1
#define FTP_SERVER_DEBUG
#include <esp_log.h>
#endif

#include <esp_task_wdt.h>
#include <Wire.h>

#define I2C_SCL 22
// #define I2C_SDA 19
#define I2C_SDA 21
#endif
