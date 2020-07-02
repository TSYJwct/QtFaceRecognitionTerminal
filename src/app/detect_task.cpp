#include "detect_task.h"

#include <QRect>
#include <QThread>

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

#include "quface/common.hpp"
#include "quface/face.hpp"

using namespace suanzi;

DetectTask::DetectTask(QThread *pThread, QObject *parent) {
  face_detector_ = new suanzi::FaceDetector("facemodel.bin");

  if (pThread == nullptr) {
    static QThread thread;
    moveToThread(&thread);
    thread.start();
  } else {
    moveToThread(pThread);
    pThread->start();
  }
}

DetectTask::~DetectTask() {
  if (face_detector_) delete face_detector_;
}

void DetectTask::rxFrame(PingPangBuffer<ImagePackage> *buffer) {
  ImagePackage *pPang = buffer->getPang();
  // printf("DetectTask threadId=%x  %x %d\n", QThread::currentThreadId(),
  // pPang,
  //  pPang->frame_idx);
  // QThread::msleep(100);

  std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

  std::vector<suanzi::FaceDetection> detections;
  face_detector_->detect((const SVP_IMAGE_S *)pPang->img_bgr_small->pImplData,
                         detections);  // 256*256  7ms
  std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
  std::chrono::duration<double> time_span =
      std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
  //   std::cout << "det: "
  //             << ": \t" << time_span.count() << "\tseconds." << std::endl;
  //   printf("det size: %d \n", detections.size());
  for (int i = 0; i < detections.size(); i++) {
    auto rect = detections[i].bbox;
    printf("%d: %0.2f %0.2f %0.2f %0.2f \n", i, rect.x, rect.y, rect.width,
           rect.height);
    break;
  }

  buffer->switchToPing();

  // TODO
  // bgr and nir face detection
  // 2 detection results combine
  // send msgs for next steps

  DetectionFloat detection_bgr;
  DetectionFloat detection_nir;
  if (detections.size() > 0) {
    int w = pPang->img_bgr_small->width;
    int h = pPang->img_bgr_small->height;
    auto rect = detections[0].bbox;
    detection_bgr.x = rect.x * 1.0 / w;
    detection_bgr.y = rect.y * 1.0 / h;
    detection_bgr.width = rect.width * 1.0 / w;
    detection_bgr.height = rect.height * 1.0 / h;
    detection_bgr.b_valid = true;
    for (int i = 0; i < 5; i++) {
      detection_bgr.landmark[i][0] = detections[0].landmarks.point[i].x / w;
      detection_bgr.landmark[i][1] = detections[0].landmarks.point[i].y / h;
    }
  } else {
    detection_bgr.b_valid = false;
  }
  emit tx_detection_bgr(buffer, detection_bgr);
  emit tx_detection_nir(buffer, detection_nir);

  // printf("tx1 threadId=%x   %x\n", QThread::currentThreadId(), pPing);
}