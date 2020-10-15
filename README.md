# YOLO ROS: Real-Time Object Detection for ROS

### More details
* https://github.com/leggedrobotics/darknet_ros

### Environment
* Jetson AGX Xavier 
* CUDA 10.2
* CUDNN 8.0
* OpenCV 3.3.1
* ROS melodic

### Subscribed topic
* You can set camera topic in config/ros.yaml (default = /usb_cam/image_raw)

### Param
* In darknet_ros.launch, set "result_path" to save detection results. 
* In darknet_ros.launch, set "gt_path" to read ground truth file.
