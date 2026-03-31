# Orin PointPillars + Livox

NVIDIA Jetson(Orin 등)에서 **TensorRT PointPillars**와 **Livox 라이다** 실시간 파이프라인을 빌드하기 위한 프로젝트임.

소스 트리는 `jetson-inference/CUDA-PointPillars` 아래 CMake 소스코드가 핵심임.

**Livox SDK:** `livox_ws/`에는 **Livox-SDK(v1, MID-70 등)** 와 **Livox-SDK2(v2, MID-360 등)** 만 둔다. 중복이었던 `lsdk2`와 로컬 데모(`pointcloud_read`, `vis3d*`, `vis_realtime`, `visualizer` 등)는 제거함. CMake 빌드에는 각 SDK의 `sdk_core`를 정적 라이브러리로 빌드한 뒤, 아래 예시의 `LIVOX_SDK_*` / `LIVOX2_*` 경로를 그 결과물로 지정하면 됨 (클론 위치에 맞게 `/path/to/...` 만 조정).

---

## Livox MID-70 vs MID-360 (CMake 옵션)

MID-70과 MID-360은 **서로 다른 SDK**를 씀. 이 CMake에서는 두 SDK를 동시에 켤 수 없음.

| 라이다 | Livox SDK | CMake 플래그 | 생성되는 실행 파일(예) |
|--------|-------------|--------------|-------------------------|
| **MID-70** 등 (SDK v1) | Livox-SDK (v1) | `WITH_LIVOX=ON` | `livox_live_mid70` |
| **MID-360** 등 (SDK v2) | Livox-SDK2 | `WITH_LIVOX2=ON` | `livox_live_mid360` |

### 반드시 지킬 규칙

- **`WITH_LIVOX`와 `WITH_LIVOX2`는 둘 중 하나만 `ON`** 이어야 함.
- 둘 다 `ON`이면 CMake가 다음 오류로 중단:  
  `Enable either WITH_LIVOX (SDK v1) OR WITH_LIVOX2 (SDK v2), not both.`

### MID-360을 쓰려면

1. **`WITH_LIVOX=OFF`** (또는 해당 옵션을 빼서 기본값 `OFF` 유지)
2. **`WITH_LIVOX2=ON`**
3. SDK v2 경로 지정:
   - `LIVOX2_INCLUDE` → `livox_lidar_api.h`가 있는 디렉터리 (보통 `.../Livox-SDK2/include`)
   - `LIVOX2_LIB_FILE` → 빌드된 정적 라이브러리 (보통 `.../Livox-SDK2/build/sdk_core/liblivox_lidar_sdk_static.a`)

예시:

```bash
cd jetson-inference/CUDA-PointPillars/build
cmake .. \
  -DWITH_LIVOX=OFF \
  -DWITH_LIVOX2=ON \
  -DLIVOX2_INCLUDE=/path/to/Livox-SDK2/include \
  -DLIVOX2_LIB_FILE=/path/to/Livox-SDK2/build/sdk_core/liblivox_lidar_sdk_static.a \
  -DWITH_OPENCV=ON
make livox_live_mid360 -j$(nproc)
```

### MID-70을 쓰려면

1. **`WITH_LIVOX2=OFF`** (기본값)
2. **`WITH_LIVOX=ON`**
3. SDK v1 경로 지정:
   - `LIVOX_SDK_INCLUDE` → `livox_sdk.h`가 있는 디렉터리 (보통 `.../Livox-SDK/sdk_core/include`)
   - `LIVOX_SDK_LIB_FILE` → 빌드된 정적 라이브러리 (보통 `.../Livox-SDK/build/sdk_core/liblivox_sdk_static.a`)

예시:

```bash
cd jetson-inference/CUDA-PointPillars/build
cmake .. \
  -DWITH_LIVOX=ON \
  -DWITH_LIVOX2=OFF \
  -DLIVOX_SDK_INCLUDE=/path/to/Livox-SDK/sdk_core/include \
  -DLIVOX_SDK_LIB_FILE=/path/to/Livox-SDK/build/sdk_core/liblivox_sdk_static.a \
  -DWITH_OPENCV=ON
make livox_live_mid70 -j$(nproc)
```

### SDK 전환 후

한쪽 SDK로 이미 `cmake`를 돌린 뒤 다른 라이다로 바꿀 때는, **`WITH_LIVOX` / `WITH_LIVOX2` 조합이 이전과 다르면** `build`에서 다시 `cmake ..`로 재구성하는 것을 추천.
캐시에 남은 플래그 때문에 CMAKE가 꼬일 수 있음.
LIVOX-SDK1과 LIVOX-SDK2를 기기에 따라서 분리해서 사용해야 했는데, 기기에 맞게 자동으로 올바른 상위 레포지토리에서 헤더파일을 가져오도록 구현하지는 못하였음.

---

## 클론 직후 전체 빌드 순서

GitHub에서 **처음 클론**한 뒤에는 아래 순서를 지키면 됨. (실행 파일은 **항상** `CUDA-PointPillars/build` 기준.)

1. **Livox SDK 정적 라이브러리** — 사용하는 쪽만 빌드하면 됨.
   - MID-70: `livox_ws/Livox-SDK` 에서 `mkdir build && cd build && cmake .. && make -j$(nproc)`  
     → `build/sdk_core/liblivox_sdk_static.a` 생성.
   - MID-360: `livox_ws/Livox-SDK2` 도 동일하게 빌드한 뒤, 위 CMake 예시의 `LIVOX2_*` 경로에 그 결과물을 맞출 것.
2. **CUDA-PointPillars** — 위에서 만든 `.a` 경로를 넣고 `cmake` / `make` (아래 **MID-70 / MID-360** 절 참고).
3. **`build` 디렉터리를 비우는 경우** — 다른 PC에서 가져온 `CMakeCache.txt`가 남아 있으면 소스 경로 불일치로 `cmake`가 실패할 수 있음. 그때는 `CUDA-PointPillars/build` 를 삭제 후 다시 `mkdir build && cmake ..` 할 것.

---

## 실행 방법

실행 파일은 **`CUDA-PointPillars/build`** 디렉터리에서 돌리는 것을 전제로 함 (코드 안 모델 경로가 `../model/pointpillar.onnx`).

```bash
cd /path/to/Orin_PointPillars_Livox/jetson-inference/CUDA-PointPillars/build
./livox_live_mid70      # MID-70 / Livox SDK v1 로 빌드했을 때
./livox_live_mid360     # MID-360 / Livox SDK v2 로 빌드했을 때 (아래 설정 파일 필요)
```

예시 경로: `cd /root/Orin_PointPillars_Livox/jetson-inference/CUDA-PointPillars/build`  
(클론 위치에 맞게 `/path/to/...` 만 바꾸면 됨.)

### 최초 실행 시 (TensorRT 엔진)

- **첫 실행**이거나 **`jetson-inference/CUDA-PointPillars/model/pointpillar.onnx.cache` 가 없을 때**  
  콘솔에 `Building TRT engine.` 과 `Enable fp16!` 이 나오고, ONNX로부터 TensorRT 엔진을 **컴파일**함. Jetson에서는 **수 분** 걸릴 수 있으니 그대로 두면 됨.
- 같은 ONNX·동일 GPU 환경에서는 완료 후 **`pointpillar.onnx.cache`** 가 모델 옆에 생김. 이후 실행은 보통 **`load TRT cache.`** 로 빨리 올라감.
- ONNX나 TensorRT 버전을 바꾼 뒤 이상하면 **캐시 파일을 지우고** 다시 첫 빌드처럼 기다리면 됨.

### 헤드리스 / SSH (BEV 창)

- `WITH_OPENCV=ON` 이더라도 **Linux에서 `DISPLAY`·`WAYLAND_DISPLAY` 가 없으면** BEV 창은 띄우지 않고 **`[GUI] No DISPLAY/WAYLAND_DISPLAY — BEV disabled (inference only).`** 한 줄만 출력한 뒤 **추론·`[DETECT]` 로그는 계속** 나감.
- 로컬 모니터나 원격 데스크톱에서 BEV를 보려면 해당 세션에서 실행하거나, X11 포워딩 등으로 `DISPLAY` 를 잡을 것.

### MID-360 — 설정 파일 (`mid360_config.json`)

SDK v2 실행 파일은 **JSON 설정**을 받음. 소스: `tool/livox_live_mid360.cpp`.

| 우선순위 | 방법 |
|----------|------|
| 1 | 인자: `./livox_live_mid360 /path/to/mid360_config.json` |
| 2 | 환경 변수: `LIVOX_SDK2_CFG=/path/to/mid360_config.json ./livox_live_mid360` |
| 3 | (Linux) 아래 파일이 읽기 가능하면 기본값으로 사용: `.../Livox-SDK2/samples/multi_lidars_upgrade/mid360_config.json` |

라이다 IP, 호스트 IP, 포트 등은 **Livox-SDK2 샘플의 `mid360_config.json`** 을 복사해 환경에 맞게 수정하면 됨.

### MID-70 — 별도 JSON 없음 (소스에서 조정)

SDK v1용 `livox_live_mid70`은 **설정 JSON을 읽지 않음.**  
시작 시 `SetBroadcastCallback` → 브로드캐스트를 받으면 `OnDeviceBroadcast`에서 `AddLidarToConnect(info->broadcast_code, ...)` 로 **자동 연결**함.

#### MID-70 + Orin 랜 포트 연결 시 네트워크 설정

MID-70을 **Jetson Orin의 랜(Ethernet) 포트에 연결**해 사용할 때는, 호스트(Orin) 측 네트워크를 아래와 같이 맞춘 뒤 사용해야 함.

| 항목 | 값 |
|------|-----|
| IP 주소 | `192.168.1.5` |
| Netmask (서브넷 마스크) | `255.255.255.0` |
| Gateway | `192.168.1.1` |


바꾸고 싶을 때는 **`jetson-inference/CUDA-PointPillars/tool/livox_live_mid70.cpp`** 를 수정함.ㅎㅎ

| 바꾸고 싶은 내용 | 대략적인 위치 (같은 파일) |
|------------------|---------------------------|
| 특정 라이다만 연결 (broadcast code 화이트리스트) | `OnDeviceBroadcast` — `info->broadcast_code` 비교 후 `AddLidarToConnect` 호출 여부 결정 |
| 연결 직후 모드 (카테시안, 콜백, 샘플링 시작) | `OnDeviceChange` (`kEventConnect` 분기) — `SetCartesianCoordinate`, `SetDataCallback`, `LidarStartSampling` 등 |
| 포인트 사용 범위 (ROI, m) | 파일 상단 근처 `kMinX` ~ `kMaxZ` |
| 프레임 버퍼·추론 입력 상한 | `kMaxFramePoints`, `InferLoop` 안 `kMaxPtsForInfer` |
| 추론 주기 | `InferLoop` 의 `sleep_for(100ms)` |
| ONNX 경로 | `InferLoop` 의 `PointPillar net("../model/pointpillar.onnx", ...)` |

Hub 전용 연결이나 Livox 문서의 **hub 샘플**은 SDK 안 `sample_cc/hub` 등 다른 예제를 참고하면 됨. **이 레포의 `livox_live_mid70`은 Hub용 JSON을 로드하지 않음.**

---

## OpenCV (`WITH_OPENCV`)

BEV 등 **화면 시각화(`imshow`)** 를 쓰려면 **`WITH_OPENCV=ON`** 으로 빌드해야 함. `OFF`이면 추론·로그만 동작하고 창 API는 링크되지 않음. `ON`인데 SSH 등으로 디스플레이가 없을 때의 동작은 위 **「헤드리스 / SSH (BEV 창)」** 절 참고.

---

## 모델 파일

저장소에 **PointPillars ONNX 가중치**가 포함되어 있음.

| 경로 |
|------|
| `jetson-inference/CUDA-PointPillars/model/pointpillar.onnx` |

실행 시 상대 경로는 보통 `../model/pointpillar.onnx` 기준이므로, **`build/`에서 실행할 때** `CUDA-PointPillars/model/`에 파일이 있어야 함.

### 출처 (Pretrained)

이 저장소에 포함된 `pointpillar.onnx`는 NVIDIA 공식 **CUDA-PointPillars** 레포에서 제공하는 사전 학습 가중치를 기준으로 함.

- [NVIDIA-AI-IOT/CUDA-PointPillars @ `092affc`](https://github.com/NVIDIA-AI-IOT/CUDA-PointPillars/tree/092affc36c72d7b8f7530685d4c0f538d987a94b)

재 학습 후 교체 시 위와 같은 경로의 `pointpillar.onnx`를 덮어쓰면 됨.

`pointpillar.onnx.cache`(TensorRT 엔진 캐시)는 **첫 실행** 시 같은 디렉터리에 자동 생성됨. 소요 시간·삭제 시 동작은 위 **「최초 실행 시 (TensorRT 엔진)」** 절 참고.

---

## 요약

| 목적 | 설정 |
|------|------|
| **MID-360** | `WITH_LIVOX=OFF`, `WITH_LIVOX2=ON` + `LIVOX2_*` 경로 |
| **MID-70** | `WITH_LIVOX=ON`, `WITH_LIVOX2=OFF` + `LIVOX_SDK_*` 경로 |

