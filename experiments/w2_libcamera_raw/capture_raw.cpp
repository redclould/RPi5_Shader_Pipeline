// capture_raw.cpp
//
// W2 — 用 libcamera 抓 IMX708 的 RAW10 Bayer，把一張 frame 存成 .raw 檔。
//
// 流程（這也是 Month 3 zero-copy pipeline 的前半段）：
//   CameraManager 啟動 → 取得 camera → 設定一個 Raw stream →
//   FrameBufferAllocator 配 dma-buf → 建 Request → start → queueRequest →
//   requestCompleted signal 回來 → 把 buffer 內容 mmap 出來寫到檔案。
//
// 參考：libcamera 官方 "simple-cam" tutorial，這裡改成 StreamRole::Raw 並存檔。

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

#include <sys/mman.h>

#include <libcamera/libcamera.h>

using namespace libcamera;
using namespace std::chrono_literals;

// 要抓幾張。前幾張 sensor + AE/AGC 還在收斂，多抓幾張、存最後一張比較好。
// 30 張 @ ~30fps ≈ 1 秒，足夠讓自動曝光穩定下來。
static constexpr unsigned int kCaptureFrames = 30;

static std::shared_ptr<Camera> camera;
static Stream *rawStream = nullptr;
static const StreamConfiguration *rawConfig = nullptr;

// 每個 FrameBuffer 的 plane0 先 mmap 好，requestCompleted 時直接讀。
static std::map<FrameBuffer *, std::pair<void *, size_t>> mapped;

static std::atomic<unsigned int> doneCount{0};
static std::string outPrefix = "imx708";

// 把一張 frame 的 raw bytes 寫到檔案
static void saveFrame(FrameBuffer *buffer, unsigned int index)
{
    auto it = mapped.find(buffer);
    if (it == mapped.end())
        return;

    void *mem = it->second.first;
    size_t len = it->second.second;

    (void)index;
    std::string name = outPrefix + ".raw";   // 固定檔名，免得對 index
    std::ofstream out(name, std::ios::binary);
    out.write(static_cast<const char *>(mem), len);
    out.close();

    std::cout << "  已存檔: " << name << " (" << len << " bytes)\n";
}

// 每抓完一張 frame，libcamera 會在自己的 thread 呼叫這個 callback
static void requestComplete(Request *request)
{
    if (request->status() == Request::RequestCancelled)
        return;

    unsigned int idx = doneCount.fetch_add(1);

    const Request::BufferMap &buffers = request->buffers();
    for (auto &[stream, buffer] : buffers) {
        const FrameMetadata &meta = buffer->metadata();
        std::cout << "frame #" << idx
              << "  seq=" << meta.sequence
              << "  bytesused=" << meta.planes()[0].bytesused
              << "\n";

        // 只存最後一張
        if (idx == kCaptureFrames - 1)
            saveFrame(buffer, idx);
    }

    // 還沒抓夠 → 把同一個 request 重新排入 queue
    if (idx + 1 < kCaptureFrames) {
        request->reuse(Request::ReuseBuffers);
        camera->queueRequest(request);
    }
}

int main()
{
    // 1. 啟動 CameraManager，列出 camera
    auto cm = std::make_unique<CameraManager>();
    cm->start();

    if (cm->cameras().empty()) {
        std::cerr << "找不到任何 camera。檢查 config.txt 的 dtoverlay=imx708,cam0、"
                 "排線方向、rpicam-hello --list-cameras\n";
        cm->stop();
        return 1;
    }

    std::string camId = cm->cameras()[0]->id();
    std::cout << "使用 camera: " << camId << "\n";

    camera = cm->get(camId);
    camera->acquire();

    // 2. 產生一個 Raw role 的設定（這會選到 sensor 原生 Bayer 格式，例如 SBGGR10 / SRGGB10）
    std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration({ StreamRole::Raw });
    if (!config || config->empty()) {
        std::cerr << "generateConfiguration(Raw) 失敗\n";
        return 1;
    }

    StreamConfiguration &cfg = config->at(0);
    std::cout << "預設 Raw 設定: " << cfg.toString() << "\n";

    // RPi5 的 PiSP 預設會把 raw 串流壓成 PISP_COMP1（私有壓縮）。
    // 我們想要乾淨未壓縮的 Bayer，方便自己 unpack / 餵 GPU demosaic。
    // 試 unpacked 16-bit；validate() 後印出實際格式，看 PiSP 給不給。
    PixelFormat requested = formats::SBGGR16;
    cfg.pixelFormat = requested;

    // validate() 會把不合法的設定夾到最接近的合法值
    if (config->validate() == CameraConfiguration::Invalid) {
        std::cerr << "設定無法驗證\n";
        return 1;
    }
    std::cout << "驗證後 Raw 設定: " << cfg.toString() << "\n";
    if (cfg.pixelFormat != requested)
        std::cout << "** PiSP 不接受 " << requested.toString()
                  << "，強制改成 " << cfg.pixelFormat.toString() << " **\n";

    if (camera->configure(config.get()) < 0) {
        std::cerr << "camera->configure 失敗\n";
        return 1;
    }

    rawStream = cfg.stream();
    rawConfig = &cfg;
    std::cout << "格式: " << cfg.pixelFormat.toString()
          << "  尺寸: " << cfg.size.toString()
          << "  stride: " << cfg.stride << " bytes/row\n";

    // 3. 配 buffer（libcamera 會配 dma-buf），並把每個 buffer 的 plane0 mmap 起來
    auto allocator = std::make_unique<FrameBufferAllocator>(camera);
    if (allocator->allocate(rawStream) < 0) {
        std::cerr << "buffer 配置失敗\n";
        return 1;
    }

    const auto &buffers = allocator->buffers(rawStream);
    std::cout << "配到 " << buffers.size() << " 個 buffer\n";

    std::vector<std::unique_ptr<Request>> requests;
    for (const std::unique_ptr<FrameBuffer> &buffer : buffers) {
        // RAW10 是單一 plane；mmap 出來方便寫檔
        const FrameBuffer::Plane &plane = buffer->planes()[0];
        void *mem = mmap(nullptr, plane.length, PROT_READ,
                 MAP_SHARED, plane.fd.get(), plane.offset);
        if (mem == MAP_FAILED) {
            std::cerr << "mmap 失敗\n";
            return 1;
        }
        mapped[buffer.get()] = { mem, plane.length };

        std::unique_ptr<Request> request = camera->createRequest();
        if (!request) {
            std::cerr << "createRequest 失敗\n";
            return 1;
        }
        if (request->addBuffer(rawStream, buffer.get()) < 0) {
            std::cerr << "addBuffer 失敗\n";
            return 1;
        }
        requests.push_back(std::move(request));
    }

    // 4. 接 callback、start、把所有 request 排入 queue
    camera->requestCompleted.connect(requestComplete);

    if (camera->start() < 0) {
        std::cerr << "camera->start 失敗\n";
        return 1;
    }
    for (auto &request : requests)
        camera->queueRequest(request.get());

    // 5. 等抓夠張數（callback 在別的 thread 跑）
    while (doneCount.load() < kCaptureFrames)
        std::this_thread::sleep_for(20ms);

    // 6. 收尾（順序很重要：先放掉所有「還抓著 camera」的東西，再 release）
    camera->stop();
    for (auto &[buf, m] : mapped)
        munmap(m.first, m.second);
    requests.clear();            // Request 物件持有 camera 參照，先清掉
    allocator->free(rawStream);
    allocator.reset();           // 釋放 buffer / dma-buf fd
    camera->release();
    camera.reset();
    cm->stop();

    std::cout << "完成。記下上面的「格式 / 尺寸 / stride」，看檔用得到。\n";
    return 0;
}
