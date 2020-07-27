#include "video_player.hpp"

#include <QBoxLayout>
#include <QDebug>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

#include "config.hpp"

using namespace suanzi;

VideoPlayer::VideoPlayer(FaceDatabasePtr db, FaceDetectorPtr detector,
                         FacePoseEstimatorPtr pose_estimator,
                         FaceExtractorPtr extractor,
                         FaceAntiSpoofingPtr anti_spoofing,
                         PersonService::ptr person_service, QWidget *parent)
    : QWidget(parent) {
  // Initialize QT widget
  QPalette pal = palette();
  pal.setColor(QPalette::Background, Qt::transparent);
  pal.setColor(QPalette::Foreground, Qt::green);
  setPalette(pal);

  auto app = Config::get_app();
  auto quface = Config::get_quface();

  camera_reader_ = new CameraReader(this);

  int screen_width, screen_height;
  if (!camera_reader_->get_screen_size(screen_width, screen_height)) {
    SZ_LOG_ERROR("Get screen size error");
  } else {
    SZ_LOG_INFO("Get screen w={}, h={}", screen_width, screen_height);
  }

  detect_tip_widget_bgr_ =
      new DetectTipWidget(0, 0, screen_width, screen_height, this);
  detect_tip_widget_bgr_->hide();

  detect_tip_widget_nir_ = new DetectTipWidget(
      screen_width - screen_width * 25 / 100, 0, screen_width * 25 / 100,
      screen_height * 25 / 100, this);
  detect_tip_widget_nir_->hide();

  recognize_tip_widget_ = new RecognizeTipWidget(nullptr);
  recognize_tip_widget_->hide();

  screen_saver_ = new ScreenSaverWidget(screen_width, screen_height, nullptr);
  screen_saver_->hide();

  // Initialize QThreads
  camera_reader_ = new CameraReader(this);

  detect_task_ = new DetectTask(detector, pose_estimator, nullptr, this);
  recognize_task_ =
      new RecognizeTask(db, extractor, anti_spoofing, nullptr, this);
  record_task_ = new RecordTask(person_service, nullptr, this);
  face_timer_ = new FaceTimer(nullptr, this);

  // IO Tasks
  upload_task_ = new UploadTask(person_service, nullptr, this);

  // Connect camera_reader to detect_task
  connect((const QObject *)camera_reader_,
          SIGNAL(tx_frame(PingPangBuffer<ImagePackage> *)),
          (const QObject *)detect_task_,
          SLOT(rx_frame(PingPangBuffer<ImagePackage> *)));
  connect((const QObject *)detect_task_, SIGNAL(tx_finish()),
          (const QObject *)camera_reader_, SLOT(rx_finish()));

  // Connect detect_task to recognize_task
  connect((const QObject *)detect_task_,
          SIGNAL(tx_frame(PingPangBuffer<DetectionData> *)),
          (const QObject *)recognize_task_,
          SLOT(rx_frame(PingPangBuffer<DetectionData> *)));
  connect((const QObject *)recognize_task_, SIGNAL(tx_finish()),
          (const QObject *)detect_task_, SLOT(rx_finish()));

  // Connect recognize_task to record_task
  connect((const QObject *)recognize_task_,
          SIGNAL(tx_frame(PingPangBuffer<RecognizeData> *)),
          (const QObject *)record_task_,
          SLOT(rx_frame(PingPangBuffer<RecognizeData> *)));
  connect((const QObject *)record_task_, SIGNAL(tx_finish()),
          (const QObject *)recognize_task_, SLOT(rx_finish()));

  // Connect detect_task to face_timer
  connect((const QObject *)detect_task_,
          SIGNAL(tx_frame(PingPangBuffer<DetectionData> *)),
          (const QObject *)face_timer_,
          SLOT(rx_frame(PingPangBuffer<DetectionData> *)));

  // Connect face_timer to record_task
  connect((const QObject *)face_timer_, SIGNAL(tx_face_disappear(int)),
          (const QObject *)record_task_, SLOT(rx_reset()));

  // Connect detect_task to detect_tip_widget
  connect((const QObject *)detect_task_,
          SIGNAL(tx_bgr_display(DetectionRatio, bool)),
          (const QObject *)detect_tip_widget_bgr_,
          SLOT(rx_display(DetectionRatio, bool)));
  if (Config::enable_anti_spoofing()) {
    connect((const QObject *)detect_task_,
            SIGNAL(tx_nir_display(DetectionRatio, bool)),
            (const QObject *)detect_tip_widget_nir_,
            SLOT(rx_display(DetectionRatio, bool)));
  }

  // Connect record_task to recognize_tip_widget
  connect((const QObject *)record_task_, SIGNAL(tx_display(PersonData, bool)),
          (const QObject *)recognize_tip_widget_,
          SLOT(rx_display(PersonData, bool)));

  // Connect record_task to upload_task_
  connect((const QObject *)record_task_, SIGNAL(tx_display(PersonData, bool)),
          (const QObject *)upload_task_, SLOT(rx_upload(PersonData, bool)));

  // Connect face_timer to recognize_tip_widget
  connect((const QObject *)face_timer_, SIGNAL(tx_face_disappear(int)),
          (const QObject *)recognize_tip_widget_, SLOT(rx_reset()));

  // Connect face_timer to screen_saver
  connect((const QObject *)face_timer_, SIGNAL(tx_face_disappear(int)),
          (const QObject *)screen_saver_, SLOT(rx_display(int)));
  connect((const QObject *)face_timer_, SIGNAL(tx_face_appear(int)),
          (const QObject *)screen_saver_, SLOT(rx_hide()));

  camera_reader_->start_sample();

  QTimer::singleShot(1, this, SLOT(init_widgets()));
}

VideoPlayer::~VideoPlayer() {}

void VideoPlayer::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  detect_tip_widget_bgr_->paint(&painter);
  detect_tip_widget_nir_->paint(&painter);
}

void VideoPlayer::init_widgets() {
  auto app = Config::get_app();

  int screen_width, screen_height;
  camera_reader_->get_screen_size(screen_width, screen_height);

  int x = 0;
  int y = screen_height * app.recognize_tip_top_percent / 100;
  int w = screen_width;
  int h = screen_height - y;

  recognize_tip_widget_->setFixedSize(w, h);
  recognize_tip_widget_->move(x, y);
}
