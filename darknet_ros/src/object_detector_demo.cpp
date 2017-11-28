/*
 * object_detector_demo.cpp
 *
 *  Created on: Dec 19, 2016
 *      Author: Marko Bjelonic
 *   Institute: ETH Zurich, Robotic Systems Lab
 */

#ifdef GPU
#include "cuda_runtime.h"
#include "curand.h"
#include "cublas_v2.h"
#endif

#include "darknet_ros/YoloObjectDetector.h"

extern "C" {
#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "darknet_ros/image_interface.h"
#include <sys/time.h>
}

#define DEMO 1

#ifdef OPENCV

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

extern "C" void ipl_into_image(IplImage* src, image im);
extern "C" image ipl_to_image(IplImage* src);
extern "C" void convert_yolo_detections(float *predictions, int classes, int num, int square,
                                        int side, int w, int h, float thresh, float **probs,
                                        box *boxes, int only_objectness);
extern "C" void draw_yolo(image im, int num, float thresh, box *boxes, float **probs);
extern "C" void show_image_cv(image p, const char *name, IplImage *disp);

static char **demo_names;
static image **demo_alphabet;
static int demo_classes;

static float **probs;
static box *boxes;
static network net;
static image buff[3];
static image buff_letter[3];
static int buff_index = 0;
static IplImage * ipl;
static float fps = 0;
static float demo_thresh = 0;
static float demo_hier = .5;
static int running = 0;

static int demo_delay = 0;
static int demo_frame = 3;
static int demo_detections = 0;
static float **predictions;
static int demo_index = 0;
static int demo_done = 0;
static float *last_avg2;
static float *last_avg;
static float *avg;
double demo_time;

static darknet_ros::RosBox_ *ROI_boxes;
static bool view_image;
static bool enable_console_output;
static int wait_key_delay;
static int full_screen;
char *demo_prefix;

double get_wall_time()
{
  struct timeval time;
  if (gettimeofday(&time, NULL)) {
    return 0;
  }
  return (double) time.tv_sec + (double) time.tv_usec * .000001;
}

void *fetch_in_thread(void *ptr)
{
  IplImage* ROS_img = darknet_ros::get_ipl_image();
  ipl_into_image(ROS_img, buff[buff_index]);
  rgbgr_image(buff[buff_index]);
//  delete ROS_img;
//  ROS_img = NULL;
  letterbox_image_into(buff[buff_index], net.w, net.h, buff_letter[buff_index]);
  return 0;
}

void *detect_in_thread(void *ptr)
{
  running = 1;
  float nms = .4;

  layer l = net.layers[net.n - 1];
  float *X = buff_letter[(buff_index + 2) % 3].data;
  float *prediction = network_predict(net, X);

  memcpy(predictions[demo_index], prediction, l.outputs * sizeof(float));
  mean_arrays(predictions, demo_frame, l.outputs, avg);
  l.output = last_avg2;
  if (demo_delay == 0)
    l.output = avg;
  if (l.type == DETECTION) {
    get_detection_boxes(l, 1, 1, demo_thresh, probs, boxes, 0);
  } else if (l.type == REGION) {
    get_region_boxes(l, buff[0].w, buff[0].h, net.w, net.h, demo_thresh, probs, boxes, 0, 0,
                     demo_hier, 1);
  } else {
    error("Last layer must produce detections\n");
  }
  if (nms > 0)
    do_nms_obj(boxes, probs, l.w * l.h * l.n, l.classes, nms);

  if (view_image) {
    if (enable_console_output) {
      printf("\nFPS:%.1f\n", fps);
      printf("Objects:\n\n");
    }
    image display = buff[(buff_index + 2) % 3];
    draw_detections(display, demo_detections, demo_thresh, boxes, probs, demo_names, demo_alphabet,
                    demo_classes);
  }

  // extract the bounding boxes and send them to ROS
  int total = l.w * l.h * l.n;
  int i, j;
  int count = 0;
  for (i = 0; i < total; ++i) {
    float xmin = boxes[i].x - boxes[i].w / 2.;
    float xmax = boxes[i].x + boxes[i].w / 2.;
    float ymin = boxes[i].y - boxes[i].h / 2.;
    float ymax = boxes[i].y + boxes[i].h / 2.;

    if (xmin < 0)
      xmin = 0;
    if (ymin < 0)
      ymin = 0;
    if (xmax > 1)
      xmax = 1;
    if (ymax > 1)
      ymax = 1;

    // iterate through possible boxes and collect the bounding boxes
    for (j = 0; j < l.classes; ++j) {
      if (probs[i][j]) {
        float x_center = (xmin + xmax) / 2;
        float y_center = (ymin + ymax) / 2;
        float BoundingBox_width = xmax - xmin;
        float BoundingBox_height = ymax - ymin;

        // define bounding box
        // BoundingBox must be 1% size of frame (3.2x2.4 pixels)
        if (BoundingBox_width > 0.01 && BoundingBox_height > 0.01) {
          ROI_boxes[count].x = x_center;
          ROI_boxes[count].y = y_center;
          ROI_boxes[count].w = BoundingBox_width;
          ROI_boxes[count].h = BoundingBox_height;
          ROI_boxes[count].Class = j;
          ROI_boxes[count].prob = probs[i][j];
          count++;
        }
      }
    }
  }

  // create array to store found bounding boxes
  // if no object detected, make sure that ROS knows that num = 0
  if (count == 0) {
    ROI_boxes[0].num = 0;
  } else {
    ROI_boxes[0].num = count;
  }

  demo_index = (demo_index + 1) % demo_frame;
  running = 0;
  return 0;
}

void *display_in_thread(void *ptr)
{
  show_image_cv(buff[(buff_index + 1) % 3], "Demo", ipl);
  int c = cvWaitKey(wait_key_delay);
  if (c != -1)
    c = c % 256;
  if (c == 10) {
    if (demo_delay == 0)
      demo_delay = 60;
    else if (demo_delay == 5)
      demo_delay = 0;
    else if (demo_delay == 60)
      demo_delay = 5;
    else
      demo_delay = 0;
  } else if (c == 27) {
    demo_done = 1;
    return 0;
  } else if (c == 82) {
    demo_thresh += .02;
  } else if (c == 84) {
    demo_thresh -= .02;
    if (demo_thresh <= .02)
      demo_thresh = .02;
  } else if (c == 83) {
    demo_hier += .02;
  } else if (c == 81) {
    demo_hier -= .02;
    if (demo_hier <= .0)
      demo_hier = .0;
  }
  return 0;
}

void *display_loop(void *ptr)
{
  while (1) {
    display_in_thread(0);
  }
}

void *detect_loop(void *ptr)
{
  while (1) {
    detect_in_thread(0);
  }
}

extern "C" void setup_network(char *cfgfile, char *weightfile, char *datafile, float thresh,
                              char **names, int classes, bool viewimage, int waitkeydelay,
                              int delay, char *prefix, int avg_frames, float hier, int w, int h,
                              int frames, int fullscreen, bool enableConsoleOutput)
{
  demo_prefix = prefix;
  demo_delay = delay;
  demo_frame = avg_frames;
  predictions = (float **) calloc(demo_frame, sizeof(float*));
  image **alphabet = load_alphabet_with_file(datafile);
  demo_names = names;
  demo_alphabet = alphabet;
  demo_classes = classes;
  demo_thresh = thresh;
  demo_hier = hier;
  view_image = viewimage;
  wait_key_delay = waitkeydelay;
  full_screen = fullscreen;
  enable_console_output = enableConsoleOutput;
  printf("YOLO_V2\n");
  net = parse_network_cfg(cfgfile);
  if (weightfile) {
    load_weights(&net, weightfile);
  }
  set_batch_network(&net, 1);
}

extern "C" void yolo()
{
  const auto wait_duration = std::chrono::milliseconds(2000);
  while (!darknet_ros::get_image_status()) {
    printf("Waiting for image.\n");
    if (!darknet_ros::is_node_running()) {
      return;
    }
    std::this_thread::sleep_for(wait_duration);
  }

  pthread_t detect_thread;
  pthread_t fetch_thread;

  srand(2222222);

  layer l = net.layers[net.n - 1];
  demo_detections = l.n * l.w * l.h;
  int j;

  avg = (float *) calloc(l.outputs, sizeof(float));
  last_avg = (float *) calloc(l.outputs, sizeof(float));
  last_avg2 = (float *) calloc(l.outputs, sizeof(float));
  for (j = 0; j < demo_frame; ++j)
    predictions[j] = (float *) calloc(l.outputs, sizeof(float));

  boxes = (box *) calloc(l.w * l.h * l.n, sizeof(box));
  ROI_boxes = (darknet_ros::RosBox_ *) calloc(l.w * l.h * l.n, sizeof(darknet_ros::RosBox_));
  probs = (float **) calloc(l.w * l.h * l.n, sizeof(float *));
  for (j = 0; j < l.w * l.h * l.n; ++j)
    probs[j] = (float *) calloc(l.classes + 1, sizeof(float));

  IplImage* ROS_img = darknet_ros::get_ipl_image();
  buff[0] = ipl_to_image(ROS_img);
  buff[1] = copy_image(buff[0]);
  buff[2] = copy_image(buff[0]);
  buff_letter[0] = letterbox_image(buff[0], net.w, net.h);
  buff_letter[1] = letterbox_image(buff[0], net.w, net.h);
  buff_letter[2] = letterbox_image(buff[0], net.w, net.h);
  ipl = cvCreateImage(cvSize(buff[0].w, buff[0].h), IPL_DEPTH_8U, buff[0].c);

  int count = 0;

  if (!demo_prefix) {
    cvNamedWindow("Demo", CV_WINDOW_NORMAL);
    if (full_screen) {
      cvSetWindowProperty("Demo", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
    } else {
      cvMoveWindow("Demo", 0, 0);
      cvResizeWindow("Demo", 1352, 1013);
    }
  }

  demo_time = get_wall_time();

  while (!demo_done) {
    buff_index = (buff_index + 1) % 3;
    if (pthread_create(&fetch_thread, 0, fetch_in_thread, 0))
      error("Thread creation failed");
    if (pthread_create(&detect_thread, 0, detect_in_thread, 0))
      error("Thread creation failed");
    if (!demo_prefix) {
      if (count % (demo_delay + 1) == 0) {
        fps = 1. / (get_wall_time() - demo_time);
        demo_time = get_wall_time();
        float *swap = last_avg;
        last_avg = last_avg2;
        last_avg2 = swap;
        memcpy(last_avg, avg, l.outputs * sizeof(float));
      }
      display_in_thread(0);
    } else {
      char name[256];
      sprintf(name, "%s_%08d", demo_prefix, count);
      save_image(buff[(buff_index + 1) % 3], name);
    }
    pthread_join(fetch_thread, 0);
    pthread_join(detect_thread, 0);
    ++count;
    if (!darknet_ros::is_node_running()) {
      demo_done = true;
    }
  }

}

#else
extern "C" void setup_network(char *cfgfile, char *weightfile, char *datafile,
    float thresh,
    char **names, int classes,
    bool viewimage, int waitkeydelay,
    int delay, char *prefix, int avg_frames,
    float hier,
    int w, int h, int frames, int fullscreen,
    bool enableConsoleOutput) {
  {
    fprintf(stderr, "YOLO demo needs OpenCV for webcam images.\n");
  }

  extern "C" void yolo() {
    {
      fprintf(stderr, "YOLO demo needs OpenCV for webcam images.\n");
    }
#endif
