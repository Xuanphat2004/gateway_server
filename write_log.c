#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include "write_log.h"

void write_log(const char *write_log, const char *level, const char *format, ...)
{
    FILE *fp = fopen(write_log, "a"); // Mở file ở chế độ append
    if (!fp)
        return;

    // Lấy thời gian hiện tại
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
            t->tm_year + 1900, t->tm_mon + 1,
            t->tm_mday, t->tm_hour,
            t->tm_min,
            t->tm_sec, level);

    // Ghi nội dung log
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}
