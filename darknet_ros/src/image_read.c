#include "darknet_ros/image_read.h"

//#define DIRNAME "/home/avees/Downloads/KODAS/"
#define DIRNAME "/home/avees/Downloads/FrontLeft/"

//#define DEBUG

/* get timestamp in ms */
double get_time_in_ms(){
	struct timespec time_after_boot;
	clock_gettime(CLOCK_MONOTONIC,&time_after_boot);

	/* return ms */
	return (time_after_boot.tv_sec * 1000 + time_after_boot.tv_nsec * 0.000001);
}

/* filter jpg */
int file_filter(const struct dirent *info){
	char *ext;

	ext = strchr(info->d_name, '.');

	if(ext == NULL){
		return 1;
	}

	if(strcmp(ext, ".jpg") == 0){
		return 1;
	} else {
		return 0;
	}
}

/* Read image from disk , -1: Error, 0: Finish reading image, 1: Success */
int read_image_from_disk(image *img, int buff_index, char *path, int *frame_count){
	char buff[256];
	char temp[256];
	char *input = buff;
	static struct dirent **namelist;
	static int img_count;
	static int idx = 0;

	/* init */
	if(idx == 0){
		if(opendir(path) == NULL){
			fprintf(stderr, "OPEN ERROR %s\n", path);
			return -1;
		}
		img_count = scandir(path, &namelist, file_filter, alphasort);
		if(img_count == -1){
			fprintf(stderr, "SCAN ERROR %s\n", path);
			return -1;
		}
		//printf("Number of image: %d\n", img_count);
	}
	strncpy(input, path, 256);
	strcat(input, namelist[idx]->d_name);
	frame_count = idx + 1;

#ifdef DEBUG
	printf("input file: %s\n", buff);
	printf("image index: %d\n", idx);
#endif

	*(img + buff_index) = load_image_color(input, 0, 0);

	if(idx >= (img_count - 1)){
		printf("Finish reading image\n");
		
		/* Finish reading image */
		return 0;
	}
	else idx++;

	return 1;
}
