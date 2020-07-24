#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QWidget>

#include "config.hpp"
#include "image_package.hpp"
#include "pingpang_buffer.hpp"
#include "recognize_data.hpp"

#include "camera_reader.hpp"
#include "detect_task.hpp"
#include "face_timer.hpp"
#include "recognize_task.hpp"
#include "record_task.hpp"
#include "upload_task.hpp"

#include "detect_tip_widget.hpp"
#include "recognize_tip_widget.hpp"
#include "screen_saver_widget.hpp"

namespace suanzi {

class VideoPlayer : public QWidget {
  Q_OBJECT

 public:
  VideoPlayer(FaceDatabasePtr db, FaceDetectorPtr detector,
              FaceExtractorPtr extractor, FaceAntiSpoofingPtr anti_spoofing,
              PersonService::ptr person_service, QWidget *parent = nullptr);
  ~VideoPlayer();

 protected:
  void paintEvent(QPaintEvent *event) override;

 private slots:
  void init_widgets();

 private:
  DetectTipWidget *detect_tip_widget_bgr_;
  DetectTipWidget *detect_tip_widget_nir_;
  RecognizeTipWidget *recognize_tip_widget_;
  ScreenSaverWidget *screen_saver_;

  CameraReader *camera_reader_;

  DetectTask *detect_task_;
  RecognizeTask *recognize_task_;
  RecordTask *record_task_;
  FaceTimer *face_timer_;

  UploadTask *upload_task_;
};

}  // namespace suanzi

#endif
