// tool/livox_live_mid360.cpp  (MID-360 전용 / Livox SDK2 v1.2.5)
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <csignal>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
#ifdef __linux__
#include <unistd.h>   // access()
#endif

// ===== CUDA / PointPillars =====
#include "cuda_runtime.h"
#include "pointpillar.h"

using namespace std::chrono_literals;

// ===== Livox SDK2 (v1.2.5) =====
extern "C" {
  #include "livox_lidar_def.h"
  #include "livox_lidar_api.h"
}

// ===== OpenCV (옵션) =====
#ifdef WITH_OPENCV
  #include <opencv2/core.hpp>
  #include <opencv2/highgui.hpp>
  #include <opencv2/imgproc.hpp>
#endif

// ===== 유틸 =====
#define STEP(tag) do{ std::fprintf(stderr,"[STEP] %s:%d %s\n", __FILE__, __LINE__, tag); std::fflush(stderr);}while(0)
#define CUDA_CHK(x) do{ auto _e=(x); if(_e!=cudaSuccess){ \
  std::fprintf(stderr,"[CUDA_ERR] %s:%d %s -> %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(_e)); \
  return; \
}}while(0)

static inline bool has_bad(const float* p, size_t n){
  for(size_t i=0;i<n;i++) if(!std::isfinite(p[i])) return true;
  return false;
}

// ===== 전역 런타임 =====
static std::atomic<bool> g_running{true};
static void SigIntHandler(int) { g_running = false; }

// ===== 포인트 버퍼 / 범위 =====
static std::mutex g_mtx;
static std::vector<float> g_frame;               // [x,y,z,i] 반복
static constexpr size_t kMaxFramePoints = 300000;

static const float kMinX = -50.f, kMaxX =  50.f;
static const float kMinY = -50.f, kMaxY =  50.f;
static const float kMinZ =  -3.f, kMaxZ =   2.f;

static inline bool in_range(float x, float y, float z) {
  return (x >= kMinX && x <= kMaxX) &&
         (y >= kMinY && y <= kMaxY) &&
         (z >= kMinZ && z <= kMaxZ);
}

static inline void PushPoint(float x, float y, float z, float i) {
  if (!in_range(x,y,z)) return;
  std::lock_guard<std::mutex> lk(g_mtx);
  if (g_frame.size()/4 >= kMaxFramePoints) return;
  g_frame.push_back(x); g_frame.push_back(y); g_frame.push_back(z); g_frame.push_back(i);
}

// ===== 통계 로그 =====
static std::atomic<uint64_t> g_pts_total{0};
static std::atomic<uint64_t> g_pkt_total{0};
static std::atomic<uint64_t> g_last_log_ms{0};
static std::atomic<int> g_first_pkts{0};
static constexpr int kShowFirstPkts = 5;

// ===== BEV 시각화 옵션 =====
#ifdef WITH_OPENCV
static constexpr float kBEV_RES = 0.10f;     // 10cm/px
static const int kBEV_W = int(std::round((kMaxY - kMinY)/kBEV_RES));
static const int kBEV_H = int(std::round((kMaxX - kMinX)/kBEV_RES));
static constexpr int   kBEV_POINT_SIZE = 2;  // 픽셀 반지름(굵게 보이도록). 0이면 1픽셀.
static constexpr int   kBEV_DRAW_DECIMATE = 1; // 그리기 샘플링(1=모두, 2=두 점에 하나)

static inline cv::Point bevPix(float x, float y) {
  const int u = int(std::round((y - kMinY)/kBEV_RES));   // 오른쪽(+y)
  const int v = int(std::round((kMaxX - x)/kBEV_RES));   // 위쪽(+x)
  return {u, v};
}
static inline void boxCornersXY(float x, float y, float w, float l, float yaw,
                                cv::Point2f out[4]) {
  const float c = std::cos(yaw), s = std::sin(yaw);
  const float hx = l*0.5f, hy = w*0.5f;
  const float px[4] = { +hx, +hx, -hx, -hx };
  const float py[4] = { +hy, -hy, -hy, +hy };
  for (int i=0;i<4;++i) {
    const float gx = x + ( c*px[i] - s*py[i] );
    const float gy = y + ( s*px[i] + c*py[i] );
    const cv::Point p = bevPix(gx, gy);
    out[i] = cv::Point2f((float)p.x, (float)p.y);
  }
}
static const cv::Scalar kColorCar (40,180,255);
static const cv::Scalar kColorPed (60,240,60);
static const cv::Scalar kColorCyc (255,60,60);
static inline cv::Scalar colorForId(int id) {
  switch(id){ case 0: return kColorCar; case 1: return kColorCyc; case 2: return kColorPed; default: return {200,200,200}; }
}
#endif

// ======================================================
//                 SDK2 콜백 (v1.2.5)
//   - 시그니처는 Livox 샘플(livox_lidar_quick_start)과 동일
// ======================================================

// 포인트 데이터
static void PointCloudCallback(uint32_t handle, const uint8_t dev_type,
                               LivoxLidarEthernetPacket* data, void* client_data) {
  (void)dev_type; (void)client_data;
  if (!data) return;

  g_pkt_total += 1;
  g_pts_total += data->dot_num;

  int seen = g_first_pkts.load();
  if (seen < kShowFirstPkts && g_first_pkts.compare_exchange_strong(seen, seen+1)) {
    std::printf("[SDK2] pkt#%d: handle=%u type=%u dots=%u len=%u frame=%u\n",
                seen, handle, (unsigned)data->data_type, (unsigned)data->dot_num,
                (unsigned)data->length, (unsigned)data->frame_cnt);
  }

  // High/Low raw 카테시안 파싱 (mm->m)
  if (data->data_type == kLivoxLidarCartesianCoordinateHighData) {
    auto* p = (LivoxLidarCartesianHighRawPoint*)data->data;
    const float scale = 0.001f;
    for (uint32_t i=0;i<data->dot_num;++i) {
      PushPoint(p[i].x*scale, p[i].y*scale, p[i].z*scale, p[i].reflectivity/255.0f);
    }
  } else if (data->data_type == kLivoxLidarCartesianCoordinateLowData) {
    auto* p = (LivoxLidarCartesianLowRawPoint*)data->data;
    const float scale = 0.001f;
    for (uint32_t i=0;i<data->dot_num;++i) {
      PushPoint(p[i].x*scale, p[i].y*scale, p[i].z*scale, p[i].reflectivity/255.0f);
    }
  } else {
    // 필요하면 Spherical 등 추가 처리
  }

  // 1초 통계 로그
  const uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
  uint64_t last = g_last_log_ms.load();
  if (now_ms - last > 1000 && g_last_log_ms.compare_exchange_strong(last, now_ms)) {
    const auto pts = g_pts_total.exchange(0);
    const auto pkts = g_pkt_total.exchange(0);
    std::printf("[SDK2] last 1s: points=%llu packets=%llu avg=%.1f pts/pkt\n",
      (unsigned long long)pts, (unsigned long long)pkts, pkts ? (double)pts/pkts : 0.0);
  }
}

// IMU (옵션)
static void ImuDataCallback(uint32_t handle, const uint8_t dev_type,
                            LivoxLidarEthernetPacket* data, void* client_data) {
  (void)handle; (void)dev_type; (void)client_data;
  if (!data) return;
  // 필요시 처리
}

// 문자열 push (옵션)
static void LivoxPushMsgCallback(uint32_t handle, const uint8_t dev_type,
                                 const char* info, void* client_data) {
  (void)dev_type; (void)client_data;
  std::printf("[SDK2] push: handle=%u info=%s\n", handle, info?info:"");
}

// WorkMode 설정 결과
static void WorkModeCallback(livox_status status, uint32_t handle,
                             LivoxLidarAsyncControlResponse* resp, void*) {
  std::printf("[SDK2] WorkMode ret status=%u handle=%u rc=%u errkey=%u\n",
              status, handle, resp?resp->ret_code:999, resp?resp->error_key:999);
}

// 장치 정보 변경 → NORMAL 모드로 가동
static void LidarInfoChangeCallback(uint32_t handle, const LivoxLidarInfo* info, void*) {
  std::printf("[SDK2] InfoChange: handle=%u (info=%p)\n", handle, (const void*)info);
  SetLivoxLidarWorkMode(handle, kLivoxLidarNormal, WorkModeCallback, nullptr);
}

// ======================================================
//                    추론 루프
// ======================================================
static void InferLoop(cudaStream_t stream) {
  STEP("create PointPillar");
  PointPillar net("../model/pointpillar.onnx", stream);

  std::vector<float> snap;
  std::vector<Bndbox> dets;
  dets.reserve(256);

#ifdef WITH_OPENCV
  cv::namedWindow("BEV", cv::WINDOW_NORMAL);
  cv::resizeWindow("BEV", 960, 960);  // 크게 보기
#endif

  std::puts("[LIVE] TRT ready. start inference loop.");
  size_t frame_id = 0;

  while (g_running) {
    std::this_thread::sleep_for(100ms);

    // A) 스냅샷
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (g_frame.empty()) continue;
      snap.swap(g_frame);
    }

    // B) 검증/클램프
    const size_t elems = snap.size();
    if (elems % 4 != 0) { snap.clear(); continue; }
    size_t points_size = elems/4;
    size_t bytes = elems*sizeof(float);

    static const size_t kMaxPtsForInfer = 200000;
    if (points_size > kMaxPtsForInfer) {
      snap.resize(kMaxPtsForInfer*4);
      points_size = kMaxPtsForInfer;
      bytes = snap.size()*sizeof(float);
    }
    if (has_bad(snap.data(), snap.size())) { snap.clear(); continue; }

    // C) GPU 복사
    float* d_points = nullptr;
    if (cudaMallocManaged((void**)&d_points, bytes) != cudaSuccess) { snap.clear(); continue; }
    if (cudaMemcpy(d_points, snap.data(), bytes, cudaMemcpyDefault) != cudaSuccess) {
      cudaFree(d_points); snap.clear(); continue;
    }
    CUDA_CHK(cudaDeviceSynchronize());

    // D) 추론
    cudaEvent_t s,e; cudaEventCreate(&s); cudaEventCreate(&e);
    cudaEventRecord(s, stream);
    dets.clear();
    int ret = 0;
    try { ret = net.doinfer(d_points, static_cast<unsigned int>(points_size), dets); }
    catch (...) { std::fprintf(stderr, "[EXC] doinfer\n"); }
    cudaEventRecord(e, stream);
    cudaEventSynchronize(e);
    float ms=0.f; cudaEventElapsedTime(&ms, s, e);
    cudaEventDestroy(s); cudaEventDestroy(e);
    cudaFree(d_points);

    std::printf("[DETECT] #%zu points=%zu det=%zu time=%.2f ms (ret=%d)\n",
                ++frame_id, points_size, dets.size(), ms, ret);

#ifdef WITH_OPENCV
    // E) 간단 BEV 시각화 (점 굵게)
    cv::Mat bev(kBEV_H, kBEV_W, CV_8UC3, cv::Scalar(30,30,30));
    if (kBEV_POINT_SIZE <= 0) {
      // 단일 픽셀
      for (size_t i=0;i+3<snap.size(); i+=4*kBEV_DRAW_DECIMATE) {
        const float x=snap[i+0], y=snap[i+1], inten=snap[i+3];
        const cv::Point p = bevPix(x,y);
        if ((unsigned)p.y < (unsigned)bev.rows && (unsigned)p.x < (unsigned)bev.cols) {
          const uint8_t v = (uint8_t)std::min(255.f, 40.f + 215.f*std::max(0.f,std::min(1.f,inten)));
          bev.at<cv::Vec3b>(p.y, p.x) = cv::Vec3b(v,v,v);
        }
      }
    } else {
      // 굵은 점(원)
      for (size_t i=0;i+3<snap.size(); i+=4*kBEV_DRAW_DECIMATE) {
        const float x=snap[i+0], y=snap[i+1], inten=snap[i+3];
        const cv::Point p = bevPix(x,y);
        if ((unsigned)p.y < (unsigned)bev.rows && (unsigned)p.x < (unsigned)bev.cols) {
          const int v = (int)std::min(255.f, 40.f + 215.f*std::max(0.f,std::min(1.f,inten)));
          cv::circle(bev, p, kBEV_POINT_SIZE, cv::Scalar(v,v,v), -1, cv::LINE_AA);
        }
      }
    }

    // (검출 박스가 있으면 표시 — b.x/y/l/w/rt 사용)
    for (const auto& b : dets) {
      cv::Point2f pts[4];
      boxCornersXY(b.x,b.y,b.w,b.l,b.rt,pts);
      const cv::Scalar col = colorForId(b.id);
      for (int k=0;k<4;++k) {
        const cv::Point2f a=pts[k], c=pts[(k+1)&3];
        if (a.x<0||a.y<0||a.x>=bev.cols||a.y>=bev.rows) continue;
        if (c.x<0||c.y<0||c.x>=bev.cols||c.y>=bev.rows) continue;
        cv::line(bev,a,c,col,2,cv::LINE_AA);
      }
      const float c=std::cos(b.rt), s=std::sin(b.rt), hx=b.l*0.5f;
      cv::arrowedLine(bev, bevPix(b.x,b.y), bevPix(b.x + c*hx, b.y + s*hx),
                      col, 2, cv::LINE_AA, 0, 0.25);
      char text[64]; std::snprintf(text,sizeof(text),"id=%d %.2f", b.id, b.score);
      cv::putText(bev, text, bevPix(b.x + c*hx, b.y + s*hx) + cv::Point(6,-6),
                  cv::FONT_HERSHEY_SIMPLEX, 0.45, col, 1, cv::LINE_AA);
    }

    cv::putText(bev, "BEV x:up y:right  res=0.1m", {10, 22},
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(200,200,200), 1, cv::LINE_AA);
    cv::imshow("BEV", bev);
    cv::waitKey(1);
#endif

    snap.clear();
  }
}

// ======================================================
//                      main
//  사용법: ./livox_live_mid360 <mid360_config.json>
// ======================================================
int main(int argc, char** argv) {
  std::signal(SIGINT, SigIntHandler);

  // --- config 경로 결정: argv[1] > LIVOX_SDK2_CFG > 기본 경로(있으면)
  const char* cfg = nullptr;
  if (argc >= 2) cfg = argv[1];
  if (!cfg || !*cfg) cfg = std::getenv("LIVOX_SDK2_CFG");
#ifdef __linux__
  if ((!cfg || access(cfg, R_OK) != 0) &&
      access("/livox_ws/Livox-SDK2/samples/multi_lidars_upgrade/mid360_config.json", R_OK) == 0)
    cfg = "/livox_ws/Livox-SDK2/samples/multi_lidars_upgrade/mid360_config.json";
#endif
  if (!cfg || !*cfg) {
    std::fprintf(stderr,
      "[SDK2] config 경로가 없습니다.\n"
      "  사용법: ./livox_live_mid360 <mid360_config.json>\n"
      "  또는   LIVOX_SDK2_CFG=/path/to/mid360_config.json ./livox_live_mid360\n");
    return 1;
  }
  std::printf("[SDK2] using config: %s\n", cfg);

  // SDK2 init (퀵스타트 스타일, 단일 인자)
  if (!LivoxLidarSdkInit(cfg)) {
    std::fprintf(stderr, "[SDK2] LivoxLidarSdkInit() failed\n");
    return 1;
  }

  // 콜백 등록 (퀵스타트와 동일)
  SetLivoxLidarPointCloudCallBack(PointCloudCallback, nullptr);
  SetLivoxLidarImuDataCallback(ImuDataCallback, nullptr);       // 선택
  SetLivoxLidarInfoCallback(LivoxPushMsgCallback, nullptr);     // 선택 (문자열 push)
  SetLivoxLidarInfoChangeCallback(LidarInfoChangeCallback, nullptr); // 필수 (NORMAL 진입)

  // CUDA 스트림 & 추론 스레드
  cudaStream_t stream = nullptr;
  cudaStreamCreate(&stream);
  std::thread worker(InferLoop, stream);

  std::printf("[LIVE] Running... Press Ctrl+C to stop.\n");
  while (g_running) std::this_thread::sleep_for(200ms);

  worker.join();
  cudaStreamDestroy(stream);

  LivoxLidarSdkUninit();
  std::puts("[LIVE] Bye.");
  return 0;
}
