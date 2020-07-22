#include "recognize_task.hpp"

#include <QThread>
#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

#include "logger.hpp"

using namespace suanzi;

RecognizeTask::RecognizeTask(
    FaceDatabasePtr db, FaceExtractorPtr extractor,
    PersonService::ptr person_service,
    MemoryPool<ImageBuffer, sizeof(ImageBuffer) * 5> *mem_pool, QThread *thread,
    QObject *parent)
    : face_database_(db),
      face_extractor_(extractor),
      person_service_(person_service),
      mem_pool_(mem_pool) {
  if (thread == nullptr) {
    static QThread new_thread;
    moveToThread(&new_thread);
    new_thread.start();
  } else {
    moveToThread(thread);
    thread->start();
  }
}

RecognizeTask::~RecognizeTask() {}

void RecognizeTask::rx_frame(PingPangBuffer<RecognizeData> *buffer) {
  auto liveness_enable = Config::enable_anti_spoofing();
  RecognizeData *pang = buffer->get_pang();
  if (pang->bgr_detection.b_valid && (pang->is_alive || !liveness_enable)) {
    // crop in large image
    int width = pang->img_bgr_large->width;
    int height = pang->img_bgr_large->height;
    suanzi::FaceDetection face_detection =
        pang->bgr_detection.to_detection(width, height);

    // extract: 25ms
    suanzi::FaceFeature feature;
    face_extractor_->extract(
        (const SVP_IMAGE_S *)pang->img_bgr_large->pImplData, face_detection,
        feature);

    // push query result to history
    std::vector<suanzi::QueryResult> results;
    SZ_RETCODE ret = face_database_->query(feature, 1, results);
    if (SZ_RETCODE_OK == ret) {
      query_success(results[0], pang);
    } else if (SZ_RETCODE_EMPTY_DATABASE == ret) {
      query_empty_database(pang);
    }
  }
  buffer->switch_buffer();
  emit tx_finish();
}

void RecognizeTask::rx_no_frame() { query_no_face(); }

void RecognizeTask::query_success(const suanzi::QueryResult &person_info,
                                  RecognizeData *img) {
  auto cfg = Config::get_extract();
  auto user_cfg = Config::get_user();
  history_.push_back(person_info);
  if (history_.size() >= cfg.history_size) {
    SZ_UINT32 face_id = 0;

    suanzi::PersonData person;
    if (sequence_query(history_, face_id)) {
      SZ_RETCODE ret = person_service_->get_person(face_id, person);
      if (ret == SZ_RETCODE_OK) {
        SZ_LOG_INFO("recognized: id = {}, name = {}", person.id, person.name);
        if (person.status == "blacklist") {
          if (user_cfg.blacklist_policy == "alert") {
            // TODO: Alert sound
            person.number = "";
            person.name = "异常";
            person.face_path = ":asserts/avatar_unknown.jpg";
          } else if (user_cfg.blacklist_policy == "stranger") {
            person.number = "";
            person.name = "访客";
            person.face_path = ":asserts/avatar_unknown.jpg";
          }
        }

        tx_display(person);

        if (!if_duplicated(person.id))
          report(person.id, img);
        else
          SZ_LOG_INFO("duplicated: id = {}", person.id);
      }
    } else {
      SZ_LOG_INFO("recognized: unknown");
      person.number = "";
      person.name = "访客";
      person.face_path = ":asserts/avatar_unknown.jpg";
      tx_display(person);

      report(0, img);
    }

    history_.clear();
  }
}

void RecognizeTask::query_empty_database(RecognizeData *img) {
  auto cfg = Config::get_extract();
  static int empty_age = 0;
  if (++empty_age >= cfg.history_size) {
    suanzi::PersonData person;
    person.number = "";
    person.name = "访客";
    person.face_path = ":asserts/avatar_unknown.jpg";
    tx_display(person);

    report(0, img);

    history_.clear();
    empty_age = 0;
  }
}

void RecognizeTask::query_no_face() {
  auto cfg = Config::get_extract();
  static int lost_age = 0;
  if (++lost_age >= cfg.max_lost_age) {
    suanzi::PersonData person;
    person.status = "clear";
    tx_display(person);

    history_.clear();
    lost_age = 0;
  }
}

bool RecognizeTask::sequence_query(std::vector<suanzi::QueryResult> history,
                                   SZ_UINT32 &face_id) {
  auto cfg = Config::get_extract();
  std::map<SZ_UINT32, int> person_counts;
  std::map<SZ_UINT32, float> person_accumulate_score;
  std::map<SZ_UINT32, float> person_max_score;

  // initialize map
  for (auto &person : history) {
    person_counts[person.face_id] = 0;
    person_accumulate_score[person.face_id] = 0.f;
    person_max_score[person.face_id] = 0.f;
  }

  // accumulate id and score
  for (auto &person : history) {
    person_counts[person.face_id] += 1;
    person_accumulate_score[person.face_id] += person.score;
    person_max_score[person.face_id] =
        std::max(person.score, person_max_score[person.face_id]);
  }

  // person id with max counts
  auto max_person = std::max_element(
      person_counts.begin(), person_counts.end(),
      [](const std::pair<int, int> &i, const std::pair<int, int> &j) {
        return i.second < j.second;
      });

  SZ_UINT32 max_person_id = max_person->first;
  int max_count = max_person->second;
  float max_person_accumulate_score = person_accumulate_score[max_person_id];
  float max_person_score = person_max_score[max_person_id];

  SZ_LOG_DEBUG("id={}, count={}, max_score={}, accumulate_score={}",
               max_person_id, max_count, max_person_score,
               person_accumulate_score[max_person_id]);
  if (max_count >= cfg.min_recognize_count &&
      max_person_accumulate_score >= cfg.min_accumulate_score &&
      max_person_score >= cfg.min_recognize_score) {
    face_id = max_person_id;
    return true;
  } else {
    return false;
  }
}

bool RecognizeTask::if_duplicated(SZ_UINT32 face_id) {
  auto cfg = Config::get_user();
  bool ret = false;

  auto current_query_clock = std::chrono::steady_clock::now();
  if (query_clock_.find(face_id) != query_clock_.end()) {
    auto last_query_clock = query_clock_[face_id];
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                        current_query_clock - last_query_clock)
                        .count();

    if (duration > cfg.duplication_interval) {
      query_clock_[face_id] = current_query_clock;
      return false;
    } else
      return true;
  } else {
    query_clock_[face_id] = current_query_clock;
    return false;
  }
}

void RecognizeTask::report(SZ_UINT32 face_id, RecognizeData *img) {
  int width = img->img_bgr_small->width;
  int height = img->img_bgr_small->height;

  // TODO: imagebuffer size as global config;
  assert(width == 224);
  assert(height == 320);

  // TODO: move image convert to record_task
  MmzImage *dest_img = new MmzImage(width, height, SZ_IMAGETYPE_BGR_PACKAGE);

  if (Ive::getInstance()->yuv2RgbPacked(dest_img, img->img_bgr_small, true)) {
    ImageBuffer *buffer = mem_pool_->allocate(1);
    memcpy(buffer->data, dest_img->pData, sizeof(buffer->data));

    emit tx_record(face_id, buffer);
  } else
    SZ_LOG_ERROR("IVE yuv2RgbPacked failed");

  delete dest_img;
}
