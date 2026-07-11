#include "core/video_thumbnail.h"
#include "app/data_dir.h"
#include "util/fnv.h"
#include "util/string_util.h"
#include "util/file_util.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <strmif.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace meplayer {

using Microsoft::WRL::ComPtr;

VideoThumbnail& VideoThumbnail::instance() {
    static VideoThumbnail inst;
    return inst;
}

VideoThumbnail::VideoThumbnail() {}

// 将 RGBA 字节写入 JPEG 文件（用 WIC）
static bool saveJpeg(const uint8_t* rgba, int w, int h,
                     const std::wstring& outPath, int quality = 85) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    ComPtr<IWICImagingFactory> wic;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic.GetAddressOf()));
    if (FAILED(hr)) return false;

    ComPtr<IWICStream> stream;
    wic->CreateStream(stream.GetAddressOf());
    stream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.GetAddressOf());
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf());
    PROPBAG2 bag = {};
    bag.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT v; VariantInit(&v);
    v.vt = VT_R4; v.fltVal = quality / 100.0f;
    props->Write(1, &bag, &v);
    frame->Initialize(props.Get());
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    frame->SetPixelFormat(&fmt);

    // RGBA 转 BGRA（WIC 期望的内存顺序）
    std::vector<uint8_t> bgra(rgba, rgba + (size_t)w * h * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i+2]);

    UINT stride = w * 4;
    frame->WritePixels(h, stride, stride * h, bgra.data());
    frame->Commit();
    encoder->Commit();
    return true;
}

std::string VideoThumbnail::generate(const std::string& videoPath, int size) {
    std::wstring wpath = util::utf8_to_wide(videoPath);

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return "";

    // 创建 SourceReader
    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, reader.GetAddressOf());
    if (FAILED(hr)) { MFShutdown(); return ""; }

    // 启用缩略图生成模式（解码器跳过 B/P 帧，加速）
    ComPtr<IUnknown> unk;
    reader->GetServiceForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                IID_PPV_ARGS(unk.GetAddressOf()));
    if (unk) {
        ComPtr<ICodecAPI> codec;
        unk.As(&codec);
        if (codec) {
            VARIANT v; VariantInit(&v);
            v.vt = VT_BOOL; v.boolVal = VARIANT_TRUE;
            codec->SetValue(&CODECAPI_AVDecVideoThumbnailGenerationMode, &v);
        }
    }

    // 设置输出格式为 RGBA32
    ComPtr<IMFMediaType> mt;
    MFCreateMediaType(mt.GetAddressOf());
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt.Get());

    // 读第一帧
    ComPtr<IMFSample> sample;
    DWORD flags = 0;
    int tries = 0;
    while (tries < 50) {
        reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                           nullptr, sample.GetAddressOf());
        if (sample) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { MFShutdown(); return ""; }
        tries++;
    }
    if (!sample) { MFShutdown(); return ""; }

    // 获取缓冲区
    ComPtr<IMFMediaBuffer> buf;
    sample->ConvertToContiguousBuffer(buf.GetAddressOf());
    if (!buf) { MFShutdown(); return ""; }

    BYTE* data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    buf->Lock(&data, &maxLen, &curLen);

    // 获取帧尺寸
    ComPtr<IMFMediaType> actual;
    reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, actual.GetAddressOf());
    UINT32 fw = 0, fh = 0;
    MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &fw, &fh);
    if (fw == 0 || fh == 0) { buf->Unlock(); MFShutdown(); return ""; }

    // RGB32 是 BGRA，居中裁剪为正方形后缩放到 size×size
    int srcSize = std::min(fw, fh);
    int offsetX = (fw - srcSize) / 2;
    int offsetY = (fh - srcSize) / 2;

    std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; y++) {
        int sy = offsetY + y * srcSize / size;
        if (sy >= (int)fh) sy = fh - 1;
        for (int x = 0; x < size; x++) {
            int sx = offsetX + x * srcSize / size;
            if (sx >= (int)fw) sx = fw - 1;
            size_t srcIdx = (size_t(sy) * fw + sx) * 4;
            size_t dstIdx = (size_t(y) * size + x) * 4;
            out[dstIdx]   = data[srcIdx];     // B
            out[dstIdx+1] = data[srcIdx+1];   // G
            out[dstIdx+2] = data[srcIdx+2];   // R
            out[dstIdx+3] = 255;              // A
        }
    }
    buf->Unlock();

    // 写文件
    std::string hash = util::fnv1a_hex32(videoPath);
    std::wstring relPath = L"thumbnails/" + util::utf8_to_wide(hash) + L".jpg";
    std::wstring absPath = AppDataDir::instance().toAbsolute(relPath);

    // 确保目录存在
    util::create_dirs(AppDataDir::instance().thumbsDir());

    bool ok = saveJpeg(out.data(), size, size, absPath);
    MFShutdown();
    return ok ? util::wide_to_utf8(relPath) : "";
}

void VideoThumbnail::enqueue(const std::string& videoPath, DoneCb cb) {
    if (!running_.load()) {
        running_ = true;
        int workers = 2;
        for (int i = 0; i < workers; i++) {
            workers_.emplace_back([this]() { worker(); });
        }
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.push({videoPath, cb});
    }
    cv_.notify_one();
}

void VideoThumbnail::worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        Task t;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() { return !queue_.empty() || !running_.load(); });
            if (!running_.load() && queue_.empty()) break;
            t = queue_.front();
            queue_.pop();
            active_.fetch_add(1);
        }
        std::string thumb = generate(t.path);
        if (t.cb) t.cb(t.path, thumb);
        active_.fetch_sub(1);
        cv_.notify_all();
    }
    CoUninitialize();
}

void VideoThumbnail::waitAll() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this]() { return queue_.empty() && active_.load() == 0; });
}

void VideoThumbnail::shutdown() {
    running_ = false;
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

}  // namespace meplayer
