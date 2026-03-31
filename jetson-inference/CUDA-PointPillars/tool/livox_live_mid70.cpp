// tool/livox_live.cpp  (MID-70 / SDK v1 전용)
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

#include "cuda_runtime.h"
#include "pointpillar.h"

using namespace std::chrono_literals;

// ===== Livox SDK v1 =====
extern "C" {
  #include "livox_sdk.h"  // 예: /livox_ws/Livox-SDK/sdk_core/include/livox_sdk.h
}

// ===== OpenCV (옵션) =====
#ifdef WITH_OPENCV
  #include <opencv2/core.hpp>
  #include <opencv2/highgui.hpp>
  #include <opencv2/imgproc.hpp>
#endif

// ===== 디버그/유틸 =====
#define STEP(tag) do{ std::fprintf(stderr,"[STEP] %s:%d %s\n", __FILE__, __LINE__, tag); std::fflush(stderr);}while(0)
#define CUDA_CHK(x) do{ auto _e=(x); if(_e!=cudaSuccess){ \
  std::fprintf(stderr,"[CUDA_ERR] %s:%d %s -> %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(_e)); \
  return; \
}}while(0)

static inline bool has_bad(const float* p, size_t n){
  for(size_t i=0;i<n;i++) if(!std::isfinite(p[i])) return true;
  return false;
}

// ===== 범위/버퍼 =====
static std::atomic<bool> g_running{true};
static void SigIntHandler(int) { g_running = false; }

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

// ===== 통계 =====
static std::atomic<uint64_t> g_pts_total{0};
static std::atomic<uint64_t> g_pkt_total{0};
static std::atomic<uint64_t> g_last_log_ms{0};

// ===== BEV =====
#ifdef WITH_OPENCV
static constexpr float kBEV_RES = 0.10f; // 10cm/px
static const int kBEV_W = int(std::round((kMaxY - kMinY)/kBEV_RES));
static const int kBEV_H = int(std::round((kMaxX - kMinX)/kBEV_RES));
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

// ===== SDK1 콜백 =====
static void OnCommonCmd(livox_status st, uint8_t handle, uint8_t resp, void*) {
  std::printf("[Livox] CommonCmd: st=%d handle=%u resp=%u\n", st, handle, resp);
}

static void OnData(uint8_t handle, LivoxEthPacket* pkt, uint32_t data_num, void*) {
  if (!pkt || data_num == 0) return;
  g_pkt_total += 1; g_pts_total += data_num;

  // 데이터 타입별 파싱 (단위 mm → m)
  switch (pkt->data_type) {
    case kCartesian: {
      const LivoxRawPoint* p = reinterpret_cast<const LivoxRawPoint*>(pkt->data);
      for (uint32_t i=0;i<data_num;++i) {
        const float x=p[i].x*0.001f, y=p[i].y*0.001f, z=p[i].z*0.001f;
        const float intensity = static_cast<unsigned>(p[i].reflectivity)/255.0f;
        PushPoint(x,y,z,intensity);
      } break;
    }
    case kExtendCartesian: {
      const LivoxExtendRawPoint* p = reinterpret_cast<const LivoxExtendRawPoint*>(pkt->data);
      for (uint32_t i=0;i<data_num;++i) {
        const float x=p[i].x*0.001f, y=p[i].y*0.001f, z=p[i].z*0.001f;
        const float intensity = static_cast<unsigned>(p[i].reflectivity)/255.0f;
        PushPoint(x,y,z,intensity);
      } break;
    }
    default: break;
  }

  const uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
  uint64_t last = g_last_log_ms.load();
  if (now_ms - last > 1000 && g_last_log_ms.compare_exchange_strong(last, now_ms)) {
    const auto pts = g_pts_total.exchange(0);
    const auto pkts = g_pkt_total.exchange(0);
    std::printf("[Livox] last 1s: points=%llu packets=%llu avg=%.1f pts/pkt\n",
      (unsigned long long)pts, (unsigned long long)pkts, pkts ? (double)pts/pkts : 0.0);
  }
}

static const char* LidarStateName(int s){
  switch (s) { case 0: return "Init"; case 1: return "Normal"; case 2: return "Standby"; case 3: return "Error"; default: return "Unknown"; }
}

static void OnDeviceChange(const DeviceInfo* device, DeviceEvent type) {
  if (!device) return;
  if (type == kEventConnect) {
    std::printf("[Livox] Connected: handle=%u\n", device->handle);

    // 좌표를 카테시안으로
    SetCartesianCoordinate(device->handle, OnCommonCmd, nullptr);

    // 리턴 모드 로그
    LidarGetPointCloudReturnMode(
      device->handle,
      [](livox_status st, uint8_t h, LidarGetPointCloudReturnModeResponse* resp, void*) {
        if (st == kStatusSuccess && resp)
          std::printf("[Livox] handle=%u return_mode=%u\n", h, (unsigned)resp->mode);
      }, nullptr);

    // 데이터 콜백 및 샘플링 시작
    SetDataCallback(device->handle, OnData, nullptr);
    LidarStartSampling(device->handle, OnCommonCmd, nullptr);

  } else if (type == kEventDisconnect) {
    std::printf("[Livox] Disconnected: handle=%u\n", device->handle);

  } else if (type == kEventStateChange) {
    std::printf("[Livox] StateChange: handle=%u state=%d (%s)\n",
                device->handle, device->state, LidarStateName(device->state));
  }
}

static void OnDeviceBroadcast(const BroadcastDeviceInfo* info) {
  if (!info) return;
  uint8_t handle = 0;
  const livox_status st = AddLidarToConnect(info->broadcast_code, &handle);
  if (st != kStatusSuccess) std::printf("[Livox] AddLidarToConnect failed: st=%d\n", st);
  else                      std::printf("[Livox] AddLidarToConnect OK: handle=%u\n", handle);
}

// ===== 추론 루프 =====
static void InferLoop(cudaStream_t stream) {
  STEP("create PointPillar");
  PointPillar net("../model/pointpillar.onnx", stream);

  std::vector<float> snap;
  std::vector<Bndbox> dets;
  dets.reserve(256);

  std::puts("[LIVE] TRT ready. start inference loop.");
  size_t frame_id = 0;

  while (g_running) {
    std::this_thread::sleep_for(100ms);

    // 스냅샷
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (g_frame.empty()) continue;
      snap.swap(g_frame);
    }

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

    // GPU 복사
    float* d_points = nullptr;
    if (cudaMallocManaged((void**)&d_points, bytes) != cudaSuccess) { snap.clear(); continue; }
    if (cudaMemcpy(d_points, snap.data(), bytes, cudaMemcpyDefault) != cudaSuccess) {
      cudaFree(d_points); snap.clear(); continue;
    }
    CUDA_CHK(cudaDeviceSynchronize());

    // 추론
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
    // BEV 시각화 — headless(SSH 등)에서는 DISPLAY 없으면 스킵, imshow 실패 시 이후 비활성화
    {
      static bool s_warned_headless = false;
      static bool s_gui_failed = false;
      bool try_show = true;
#if defined(__linux__)
      const char* disp = std::getenv("DISPLAY");
      const char* wl = std::getenv("WAYLAND_DISPLAY");
      try_show = (disp && disp[0]) || (wl && wl[0]);
      if (!try_show && !s_warned_headless) {
        s_warned_headless = true;
        std::fprintf(stderr, "[GUI] No DISPLAY/WAYLAND_DISPLAY — BEV disabled (inference only).\n");
      }
#endif
      if (try_show && !s_gui_failed) {
        try {
          cv::Mat bev(kBEV_H, kBEV_W, CV_8UC3, cv::Scalar(30,30,30));
          for (size_t i=0;i+3<snap.size(); i+=4) {
            const float x=snap[i+0], y=snap[i+1], inten=snap[i+3];
            const cv::Point p = bevPix(x,y);
            if ((unsigned)p.y < (unsigned)bev.rows && (unsigned)p.x < (unsigned)bev.cols) {
              const int r = 1;
              const int v = (int)std::min(255.f, 40.f + 215.f*std::max(0.f,std::min(1.f,inten)));
              cv::circle(bev, p, r, cv::Scalar(v,v,v), -1, cv::LINE_AA);
            }
          }
          for (const auto& b : dets) {
            cv::Point2f pts[4];
            boxCornersXY(b.x,b.y,b.w,b.l,b.rt,pts);
            const cv::Scalar col = (b.id==0)?cv::Scalar(40,180,255): (b.id==1)?cv::Scalar(255,60,60)
                                  : (b.id==2)?cv::Scalar(60,240,60): cv::Scalar(200,200,200);
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
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, col, 1, cv::LINE_AA);
          }
          cv::putText(bev, "BEV x:up y:right (res=0.1m)", {10, 20},
                      cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200,200,200), 1, cv::LINE_AA);
          cv::imshow("BEV", bev);
          cv::waitKey(1);
        } catch (const cv::Exception& e) {
          s_gui_failed = true;
          std::fprintf(stderr, "[GUI] OpenCV window failed, BEV off: %s\n", e.what());
        }
      }
    }
#endif
  }
}

// ===== main =====
int main() {
  std::signal(SIGINT, SigIntHandler);

  // SDK1 초기화
  if (!Init()) {
    std::fprintf(stderr, "[Livox] Init() failed\n");
    return 1;
  }
  SetBroadcastCallback(OnDeviceBroadcast);
  SetDeviceStateUpdateCallback(OnDeviceChange);
  if (!Start()) {
    std::fprintf(stderr, "[Livox] Start() failed\n");
    Uninit();
    return 1;
  }

  // CUDA 스트림 & 추론 스레드
  cudaStream_t stream = nullptr;
  cudaStreamCreate(&stream);
  std::thread worker(InferLoop, stream);

  std::printf("[LIVE] Running... Press Ctrl+C to stop.\n");
  while (g_running) std::this_thread::sleep_for(200ms);

  // 종료 처리
  worker.join();
  cudaStreamDestroy(stream);

  DeviceInfo list[kMaxLidarCount];
  uint8_t cnt = kMaxLidarCount;
  if (GetConnectedDevices(list, &cnt) == kStatusSuccess) {
    for (uint8_t i = 0; i < cnt; ++i) {
      LidarStopSampling(list[i].handle, OnCommonCmd, nullptr);
      DisconnectDevice(list[i].handle, OnCommonCmd, nullptr);
    }
  }
  Uninit();

  std::puts("[LIVE] Bye.");
  return 0;
}
