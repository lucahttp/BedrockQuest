# Pico Native OpenXR SDK/Samples
## Introduction
This is a project which contains header files for both experimental and pre-release OpenXR APIs and features and samples aimed to help you understand and utilize these features. These samples serve as practical demonstrations, showing you how to integrate and use the APIs effectively.
## Structure of project
### OpenXR
This directory contains experimental OpenXR features from Pico. If you want to be an early bird and try out new features, check it out now!
### Samples
There are practical demonstrations showing you how to intergrate and use the OpenXR APIs list on head files in the OpenXR directory mentioned above.
#### 3rdParty
This directory contains some open source library codes from three parties. Thanks a lot!
#### framework
All framework codes are concentrated here, and all XrApp demonstrations in the [samples](https://bits.bytedance.net/code/pico-xr-runtime/OpenXR_Native_SDK/tree/master/Samples/samples) directory are built on this part of the code. They greatly simplify the difficulty and complexity of developing a XR application. Developers no longer need to spend a lot of energy on openxr, platform, and graphics API as before, but only need to focus on the rendering and interaction logic of the application. Of course, this part is still under continuous iteration and improvement. If you want to contribute code to it, just contact us or submit MR directly, and they will be merged into it after being carefully reviewed and processed. Join this work, come on!
#### samples
All XrApp demonstrations are concentrated in this directory. You can quickly create and develop your own app module based on the existing templates. Welcome to contribute your own various applications, which can be used to test a new feature or a new openxr extension, can be used to run unit tests, or even just for fun. Compared with the framework part, its code review rules are more lenient and flexible.
### More
If you want to learn more about creating app modules efficiently using templates, go [here](https://bits.bytedance.net/code/pico-xr-runtime/OpenXR_Native_SDK/tree/master/Samples/templates/README.md).