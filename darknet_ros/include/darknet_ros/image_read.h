#ifndef IMREAD_H
#define IMREAD_H

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include "image.h"

double get_time_in_ms();
int file_filter(const struct dirent *info);
int read_image_from_disk(image *img, int buff_index);

#endif
