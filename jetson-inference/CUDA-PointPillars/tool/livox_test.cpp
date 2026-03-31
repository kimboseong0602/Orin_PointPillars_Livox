// livox_test.cpp
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" {
#include "livox_sdk.h"  // e.g. /livox_ws/Livox-SDK/sdk_core/include/livox_sdk.h
}

using namespace std::chrono_literals;

// -----------------------------
// Helpers & globals
// -----------------------------

static const char* ReturnModeName(uint8_t mode) {
  // livox_def.h 의 enum PointCloudReturnMode 값에 맞춰 필요시 수정
  // (대부분: 0=Strongest/First, 1=Last, 2=Dual)
  switch (mode) {
    case 0: return "Strongest/First (Single)";
    case 1: return "Last (Single)";
    case 2: return "Dual";
    default: return "Unknown";
  }
}

// 1초 누적/출력, 5초 이동평균용
static std::atomic<uint64_t> g_pts_total{0};
static std::atomic<uint64_t> g_pkt_total{0};
static std::atomic<uint64_t> g_last_print_ms{0};
static uint64_t g_pts_hist[5] = {0};
static int g_hist_idx = 0;

static std::atomic<bool> g_running{true};

// -----------------------------
// Callbacks
// -----------------------------

static void OnCommonCmd(livox_status status, uint8_t handle, uint8_t resp, void* /*client_data*/) {
  std::printf("[Livox] CommonCmd: status=%d handle=%u resp=%u\n", status, handle, resp);
}

static void OnData(uint8_t handle, LivoxEthPacket* pkt, uint32_t data_num, void* /*client_data*/) {
  if (!pkt || data_num == 0) return;

  g_pts_total += data_num;
  g_pkt_total += 1;

  // 필요하면 true로: 패킷 단위 로그 켜기
  constexpr bool kPrintEachPacket = false;
  if (kPrintEachPacket) {
    std::printf("[Livox] handle=%u pkt_points=%u data_type=%u\n",
                handle, data_num, (unsigned)pkt->data_type);
    if (pkt->data_type == kCartesian) {
      auto* pts = reinterpret_cast<LivoxRawPoint*>(pkt->data);
      float x0 = pts[0].x * 0.001f, y0 = pts[0].y * 0.001f, z0 = pts[0].z * 0.001f;
      std::printf("         first point: (%.3f, %.3f, %.3f)\n", x0, y0, z0);
    }
  }

  // 1초마다 PPS/패킷수/5초 이동평균 출력
  const uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
  uint64_t last = g_last_print_ms.load();
  if (now_ms - last > 1000) {
    if (g_last_print_ms.compare_exchange_strong(last, now_ms)) {
      const auto pts  = g_pts_total.exchange(0);
      const auto pkts = g_pkt_total.exchange(0);

      g_pts_hist[g_hist_idx] = pts;
      g_hist_idx = (g_hist_idx + 1) % 5;
      uint64_t sum5 = 0;
      for (int i = 0; i < 5; ++i) sum5 += g_pts_hist[i];
      const double avg5 = sum5 / 5.0;

      std::printf("[Livox] last 1s: points=%llu, packets=%llu, avg=%.1f pts/pkt | 5s avg≈%.0f pts/s\n",
                  (unsigned long long)pts,
                  (unsigned long long)pkts,
                  pkts ? (double)pts / (double)pkts : 0.0,
                  avg5);
      // 싱글 리턴이면 ≈100k, 듀얼이면 ≈200k 근처가 정상
    }
  }
}

static void OnDeviceChange(const DeviceInfo* device, DeviceEvent type) {
  if (!device) return;

  if (type == kEventConnect) {
    std::printf("[Livox] Connected: handle=%u\n", device->handle);

    // 좌표계를 Cartesian으로 강제(안전한 기본값)
    SetCartesianCoordinate(device->handle, OnCommonCmd, nullptr);

    // 현재 리턴 모드 로깅
    LidarGetPointCloudReturnMode(
      device->handle,
      [](livox_status st, uint8_t h, LidarGetPointCloudReturnModeResponse* resp, void*) {
        if (st == kStatusSuccess && resp) {
          std::printf("[Livox] handle=%u return_mode=%u (%s)\n",
                      h, (unsigned)resp->mode, ReturnModeName(resp->mode));
        } else {
          std::printf("[Livox] handle=%u return_mode query failed (st=%d)\n", h, st);
        }
      },
      nullptr);

    // 필요 시 듀얼/싱글 강제 설정 (livox_def.h 의 enum 값 확인 후 사용)
    // LidarSetPointCloudReturnMode(device->handle, (PointCloudReturnMode)2, OnCommonCmd, nullptr); // Dual 예시

    SetDataCallback(device->handle, OnData, nullptr);
    LidarStartSampling(device->handle, OnCommonCmd, nullptr);

  } else if (type == kEventDisconnect) {
    std::printf("[Livox] Disconnected: handle=%u\n", device->handle);

  } else if (type == kEventStateChange) {
    std::printf("[Livox] StateChange: handle=%u state=%d\n", device->handle, device->state);
  }
}

static void OnDeviceBroadcast(const BroadcastDeviceInfo* info) {
  if (!info) return;

  uint8_t handle = 0;
  livox_status st = AddLidarToConnect(info->broadcast_code, &handle);
  if (st != kStatusSuccess) {
    std::printf("[Livox] AddLidarToConnect failed: status=%d\n", st);
  } else {
    std::printf("[Livox] AddLidarToConnect OK: handle=%u (connecting...)\n", handle);
  }
}

// -----------------------------
// Main
// -----------------------------

int main() {
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

  std::printf("[Livox] Discovering/receiving for 10s...\n");
  std::this_thread::sleep_for(10s);

  // 종료 처리: 연결된 장치들 샘플링 정지 및 해제
  DeviceInfo list[kMaxLidarCount];
  uint8_t cnt = kMaxLidarCount;  // 헤더는 uint8_t* 요구
  if (GetConnectedDevices(list, &cnt) == kStatusSuccess) {
    for (uint8_t i = 0; i < cnt; ++i) {
      LidarStopSampling(list[i].handle, OnCommonCmd, nullptr);
      DisconnectDevice(list[i].handle, OnCommonCmd, nullptr);
    }
  }

  Uninit();
  std::printf("[Livox] Uninit. Bye.\n");
  return 0;
}
