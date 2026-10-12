#include "screen_share.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <wels/codec_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace yapper
{
using Microsoft::WRL::ComPtr;

constexpr int  kMaxWidth = 1280;
constexpr int  kMaxHeight = 720;
constexpr int  kFps = 30;
constexpr int  kBitrate = 2'500'000;
constexpr auto kFrameTime = std::chrono::microseconds(1'000'000 / kFps);
constexpr auto kKeyframeInterval = std::chrono::seconds(2); // so lost packets heal by themselves
constexpr auto kMinKeyframeGap = std::chrono::milliseconds(500);
constexpr auto kRetryDelay = std::chrono::milliseconds(500);

static void Check(HRESULT     result,
                  const char *what)
{
  if (FAILED(result))
  {
    throw std::runtime_error(std::string(what) + " failed");
  }
}

// Calls visit(adapter, output) for every monitor of every graphics card, until it returns true
template <typename Visit>
static void ForEachMonitor(Visit visit)
{
  ComPtr<IDXGIFactory1> factory;
  Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++)
  {
    ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++)
    {
      if (visit(adapter.Get(), output.Get()))
      {
        return;
      }
    }
  }
}

std::vector<std::string> ListMonitors()
{
  std::vector<std::string> monitors;

  try
  {
    ForEachMonitor(
        [&](IDXGIAdapter1 *, IDXGIOutput *output)
        {
          DXGI_OUTPUT_DESC desc{};
          output->GetDesc(&desc);
          const RECT &rect = desc.DesktopCoordinates;
          monitors.push_back("Monitor " + std::to_string(monitors.size() + 1) + " (" +
                             std::to_string(rect.right - rect.left) + "x" + std::to_string(rect.bottom - rect.top) +
                             ")");
          return false;
        });
  }
  catch (const std::exception &)
  {
    // No monitors listed means nothing to share
  }

  return monitors;
}

struct Duplication
{
  ComPtr<ID3D11Device>           device;
  ComPtr<ID3D11DeviceContext>    context;
  ComPtr<IDXGIOutputDuplication> duplication;
  ComPtr<ID3D11Texture2D>        staging; // a CPU readable copy of the screen
  int                            width = 0;
  int                            height = 0;
};

// shortcut: rotated monitors come through sideways, rotate the image if anyone uses portrait mode
static Duplication OpenMonitor(int monitor)
{
  Duplication duplication{};
  int         index = 0;
  bool        found = false;

  ForEachMonitor(
      [&](IDXGIAdapter1 *adapter, IDXGIOutput *output)
      {
        if (index++ != monitor)
        {
          return false;
        }

        found = true;
        Check(D3D11CreateDevice(adapter,
                                D3D_DRIVER_TYPE_UNKNOWN,
                                nullptr,
                                0,
                                nullptr,
                                0,
                                D3D11_SDK_VERSION,
                                &duplication.device,
                                nullptr,
                                &duplication.context),
              "D3D11CreateDevice");

        ComPtr<IDXGIOutput1> output1;
        Check(output->QueryInterface(IID_PPV_ARGS(&output1)), "IDXGIOutput1");
        Check(output1->DuplicateOutput(duplication.device.Get(), &duplication.duplication), "DuplicateOutput");
        return true;
      });

  if (!found)
  {
    throw std::runtime_error("that monitor is gone");
  }

  DXGI_OUTDUPL_DESC desc{};
  duplication.duplication->GetDesc(&desc);
  duplication.width = static_cast<int>(desc.ModeDesc.Width);
  duplication.height = static_cast<int>(desc.ModeDesc.Height);

  D3D11_TEXTURE2D_DESC staging{};
  staging.Width = desc.ModeDesc.Width;
  staging.Height = desc.ModeDesc.Height;
  staging.MipLevels = 1;
  staging.ArraySize = 1;
  staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  staging.SampleDesc.Count = 1;
  staging.Usage = D3D11_USAGE_STAGING;
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Check(duplication.device->CreateTexture2D(&staging, nullptr, &duplication.staging), "CreateTexture2D");

  return duplication;
}

struct Cursor
{
  bool                              visible = false;
  int                               x = 0;
  int                               y = 0;
  std::vector<std::uint8_t>         shape;
  DXGI_OUTDUPL_POINTER_SHAPE_INFO   info{};
};

// Copies the latest screen image and cursor state, if anything changed.
// Returns false if the duplication was lost and has to be reopened.
static bool Grab(Duplication               &duplication,
                 std::vector<std::uint8_t> &bgra,
                 Cursor                    &cursor,
                 bool                      &changed)
{
  DXGI_OUTDUPL_FRAME_INFO info{};
  ComPtr<IDXGIResource>   resource;

  const HRESULT result = duplication.duplication->AcquireNextFrame(0, &info, &resource);
  if (result == DXGI_ERROR_WAIT_TIMEOUT)
  {
    return true;
  }
  if (result == DXGI_ERROR_ACCESS_LOST)
  {
    return false;
  }
  Check(result, "AcquireNextFrame");

  if (info.LastPresentTime.QuadPart != 0)
  {
    ComPtr<ID3D11Texture2D> texture;
    Check(resource->QueryInterface(IID_PPV_ARGS(&texture)), "screen texture");
    duplication.context->CopyResource(duplication.staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(duplication.context->Map(duplication.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");

    const std::size_t rowBytes = static_cast<std::size_t>(duplication.width) * 4;
    bgra.resize(rowBytes * duplication.height);
    for (int row = 0; row < duplication.height; row++)
    {
      std::memcpy(bgra.data() + row * rowBytes, static_cast<std::uint8_t *>(mapped.pData) + row * mapped.RowPitch,
                  rowBytes);
    }

    duplication.context->Unmap(duplication.staging.Get(), 0);
    changed = true;
  }

  if (info.LastMouseUpdateTime.QuadPart != 0)
  {
    cursor.visible = info.PointerPosition.Visible;
    cursor.x = info.PointerPosition.Position.x;
    cursor.y = info.PointerPosition.Position.y;
    changed = true;
  }

  if (info.PointerShapeBufferSize > 0)
  {
    cursor.shape.resize(info.PointerShapeBufferSize);
    UINT needed = 0;
    Check(duplication.duplication->GetFramePointerShape(
              info.PointerShapeBufferSize, cursor.shape.data(), &needed, &cursor.info),
          "GetFramePointerShape");
    changed = true;
  }

  duplication.duplication->ReleaseFrame();
  return true;
}

// The screen image doesn't include the mouse pointer, Windows hands it over separately.
// Drawn onto the already scaled image, so the cursor shape is scaled too (nearest pixel).
static void DrawCursor(std::vector<std::uint8_t> &image,
                       int                        width,
                       int                        height,
                       double                     scale,
                       const Cursor              &cursor)
{
  if (!cursor.visible || cursor.shape.empty())
  {
    return;
  }

  const DXGI_OUTDUPL_POINTER_SHAPE_INFO &info = cursor.info;

  // Monochrome cursors are two 1 bit masks stacked on top of each other: AND, then XOR
  const bool monochrome = info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
  const int  shapeHeight = monochrome ? static_cast<int>(info.Height) / 2 : static_cast<int>(info.Height);

  const int left = static_cast<int>(cursor.x * scale);
  const int top = static_cast<int>(cursor.y * scale);
  const int right = static_cast<int>((cursor.x + static_cast<int>(info.Width)) * scale);
  const int bottom = static_cast<int>((cursor.y + shapeHeight) * scale);

  for (int y = std::max(top, 0); y < std::min(bottom, height); y++)
  {
    for (int x = std::max(left, 0); x < std::min(right, width); x++)
    {
      const int sx = std::min(static_cast<int>((x - left) / scale), static_cast<int>(info.Width) - 1);
      const int sy = std::min(static_cast<int>((y - top) / scale), shapeHeight - 1);

      std::uint8_t *pixel = &image[(static_cast<std::size_t>(y) * width + x) * 4];

      if (monochrome)
      {
        const std::uint8_t bit = static_cast<std::uint8_t>(0x80 >> (sx % 8));
        const bool         andBit = cursor.shape[sy * info.Pitch + sx / 8] & bit;
        const bool         xorBit = cursor.shape[(sy + shapeHeight) * info.Pitch + sx / 8] & bit;
        for (int c = 0; c < 3; c++)
        {
          pixel[c] = static_cast<std::uint8_t>((andBit ? pixel[c] : 0) ^ (xorBit ? 0xFF : 0));
        }
        continue;
      }

      const std::uint8_t *source = &cursor.shape[sy * info.Pitch + sx * 4];
      if (info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR)
      {
        const int alpha = source[3];
        for (int c = 0; c < 3; c++)
        {
          pixel[c] = static_cast<std::uint8_t>((source[c] * alpha + pixel[c] * (255 - alpha)) / 255);
        }
      }
      else // masked color: alpha 0 means replace, anything else means XOR
      {
        for (int c = 0; c < 3; c++)
        {
          pixel[c] = source[3] == 0 ? source[c] : static_cast<std::uint8_t>(pixel[c] ^ source[c]);
        }
      }
    }
  }
}

struct Sample
{
  int offset0; // byte offset of the first source pixel (column or row)
  int offset1; // and the next one
  int weight;  // how much of the next one, 0-256
};

// Where each target column or row samples from. Worked out once per image instead of per pixel.
static std::vector<Sample> Samples(int sourceSize,
                                   int targetSize,
                                   int step)
{
  std::vector<Sample> samples(targetSize);
  for (int i = 0; i < targetSize; i++)
  {
    const double position = std::max(0.0, (i + 0.5) * sourceSize / targetSize - 0.5);
    const int    first = std::min(static_cast<int>(position), sourceSize - 1);
    const int    next = std::min(first + 1, sourceSize - 1);
    samples[i] = {first * step, next * step, static_cast<int>((position - first) * 256)};
  }
  return samples;
}

// Bilinear resize, so text stays readable when shrinking 1080p or 1440p down to 720p.
// Fixed point (weights out of 256) because this runs on every changed frame.
static void Resize(const std::vector<std::uint8_t> &source,
                   int                              sourceWidth,
                   int                              sourceHeight,
                   std::vector<std::uint8_t>       &target,
                   int                              width,
                   int                              height)
{
  target.resize(static_cast<std::size_t>(width) * height * 4);
  const std::vector<Sample> columns = Samples(sourceWidth, width, 4);
  const std::vector<Sample> rows = Samples(sourceHeight, height, sourceWidth * 4);

  for (int y = 0; y < height; y++)
  {
    const std::uint8_t *row0 = source.data() + rows[y].offset0;
    const std::uint8_t *row1 = source.data() + rows[y].offset1;
    const int           wy = rows[y].weight;
    std::uint8_t       *out = &target[static_cast<std::size_t>(y) * width * 4];

    for (int x = 0; x < width; x++, out += 4)
    {
      const Sample &column = columns[x];
      const int     wx = column.weight;
      for (int i = 0; i < 3; i++)
      {
        const int top = row0[column.offset0 + i] * (256 - wx) + row0[column.offset1 + i] * wx;
        const int bottom = row1[column.offset0 + i] * (256 - wx) + row1[column.offset1 + i] * wx;
        out[i] = static_cast<std::uint8_t>((top * (256 - wy) + bottom * wy + 32768) >> 16);
      }
    }
  }
}

// BGRA to I420 (BT.601, limited range), the input format OpenH264 takes
static void ToI420(const std::vector<std::uint8_t> &bgra,
                   int                              width,
                   int                              height,
                   std::vector<std::uint8_t>       &yuv)
{
  yuv.resize(static_cast<std::size_t>(width) * height * 3 / 2);
  std::uint8_t *yPlane = yuv.data();
  std::uint8_t *uPlane = yPlane + width * height;
  std::uint8_t *vPlane = uPlane + (width / 2) * (height / 2);

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::uint8_t *p = &bgra[(static_cast<std::size_t>(y) * width + x) * 4];
      yPlane[y * width + x] = static_cast<std::uint8_t>(((66 * p[2] + 129 * p[1] + 25 * p[0] + 128) >> 8) + 16);
    }
  }

  // One U and V for every 2x2 block of pixels
  for (int y = 0; y < height / 2; y++)
  {
    for (int x = 0; x < width / 2; x++)
    {
      int r = 0;
      int g = 0;
      int b = 0;
      for (int dy = 0; dy < 2; dy++)
      {
        for (int dx = 0; dx < 2; dx++)
        {
          const std::uint8_t *p = &bgra[(static_cast<std::size_t>(y * 2 + dy) * width + x * 2 + dx) * 4];
          b += p[0];
          g += p[1];
          r += p[2];
        }
      }
      r /= 4;
      g /= 4;
      b /= 4;
      uPlane[y * (width / 2) + x] = static_cast<std::uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
      vPlane[y * (width / 2) + x] = static_cast<std::uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
    }
  }
}

ScreenShare::ScreenShare(Connection &connection,
                         int         monitor)
    : connection_(connection), monitor_(monitor), thread_([this] { Run(); })
{
}

ScreenShare::~ScreenShare()
{
  running_ = false;
  thread_.join();
}

std::string ScreenShare::Error() const
{
  std::lock_guard lock(mutex_);
  return error_;
}

void ScreenShare::Run()
{
  // Exceptions can't leave a thread, so they become the error the UI shows
  try
  {
    Capture();
  }
  catch (const std::exception &e)
  {
    std::lock_guard lock(mutex_);
    error_ = e.what();
  }
}

void ScreenShare::Capture()
{
  Duplication duplication = OpenMonitor(monitor_);

  // Fit inside 1280x720 keeping the shape. H.264 needs even sizes.
  const double scale = std::min({1.0,
                                 static_cast<double>(kMaxWidth) / duplication.width,
                                 static_cast<double>(kMaxHeight) / duplication.height});
  const int    width = static_cast<int>(duplication.width * scale) & ~1;
  const int    height = static_cast<int>(duplication.height * scale) & ~1;

  ISVCEncoder *rawEncoder = nullptr;
  if (WelsCreateSVCEncoder(&rawEncoder) != 0 || rawEncoder == nullptr)
  {
    throw std::runtime_error("couldn't create the H.264 encoder");
  }
  std::unique_ptr<ISVCEncoder, decltype(&WelsDestroySVCEncoder)> encoder(rawEncoder, WelsDestroySVCEncoder);

  SEncParamExt params{};
  encoder->GetDefaultParams(&params);
  // Camera mode, not screen content mode: screen content took 20 ms per 720p frame, camera mode 5 ms
  params.iUsageType = CAMERA_VIDEO_REAL_TIME;
  params.iPicWidth = width;
  params.iPicHeight = height;
  params.iTargetBitrate = kBitrate;
  params.iMaxBitrate = kBitrate * 2;
  params.iRCMode = RC_BITRATE_MODE;
  params.fMaxFrameRate = kFps;
  params.uiIntraPeriod = 0; // keyframes are forced by time below, since unchanged frames aren't encoded
  params.bEnableFrameSkip = false;
  params.iComplexityMode = LOW_COMPLEXITY;
  params.iSpatialLayerNum = 1;
  params.sSpatialLayers[0].iVideoWidth = width;
  params.sSpatialLayers[0].iVideoHeight = height;
  params.sSpatialLayers[0].fFrameRate = kFps;
  params.sSpatialLayers[0].iSpatialBitrate = kBitrate;
  params.sSpatialLayers[0].iMaxSpatialBitrate = kBitrate * 2;
  if (encoder->InitializeExt(&params) != cmResultSuccess)
  {
    throw std::runtime_error("couldn't start the H.264 encoder");
  }

  std::vector<std::uint8_t> screen;
  std::vector<std::uint8_t> scaled;
  std::vector<std::uint8_t> yuv;
  Cursor                    cursor;

  const auto start = std::chrono::steady_clock::now();
  auto       nextFrame = start;
  auto       lastKeyframe = std::chrono::steady_clock::time_point{};

  while (running_)
  {
    bool changed = false;
    if (!Grab(duplication, screen, cursor, changed))
    {
      // Lost on resolution changes, fullscreen games and the UAC prompt. Keep trying until it's back.
      duplication = Duplication{};
      while (running_ && !duplication.duplication)
      {
        try
        {
          duplication = OpenMonitor(monitor_);
        }
        catch (const std::exception &)
        {
          std::this_thread::sleep_for(kRetryDelay);
        }
      }
      continue;
    }

    // Only redo the expensive scaling when the screen or cursor actually changed
    if (changed && !screen.empty())
    {
      Resize(screen, duplication.width, duplication.height, scaled, width, height);
      DrawCursor(scaled, width, height, scale, cursor);
      ToI420(scaled, width, height, yuv);
    }

    // Encoding is the most expensive part, so a still screen only sends the regular keyframes
    const auto time = std::chrono::steady_clock::now();
    const bool keyframeDue = time - lastKeyframe > kKeyframeInterval;
    const bool keyframeAsked = time - lastKeyframe > kMinKeyframeGap && connection_.TakeKeyframeRequest();

    if (!yuv.empty() && (changed || keyframeDue || keyframeAsked))
    {
      if (keyframeDue || keyframeAsked)
      {
        encoder->ForceIntraFrame(true);
        lastKeyframe = time;
      }

      SSourcePicture picture{};
      picture.iColorFormat = videoFormatI420;
      picture.iPicWidth = width;
      picture.iPicHeight = height;
      picture.iStride[0] = width;
      picture.iStride[1] = width / 2;
      picture.iStride[2] = width / 2;
      picture.pData[0] = yuv.data();
      picture.pData[1] = yuv.data() + width * height;
      picture.pData[2] = picture.pData[1] + (width / 2) * (height / 2);
      picture.uiTimeStamp = std::chrono::duration_cast<std::chrono::milliseconds>(time - start).count();

      SFrameBSInfo encoded{};
      if (encoder->EncodeFrame(&picture, &encoded) != cmResultSuccess)
      {
        throw std::runtime_error("H.264 encoding failed");
      }

      if (encoded.eFrameType != videoFrameTypeSkip)
      {
        std::vector<std::uint8_t> frame;
        for (int layer = 0; layer < encoded.iLayerNum; layer++)
        {
          const SLayerBSInfo &info = encoded.sLayerInfo[layer];
          int                 size = 0;
          for (int nal = 0; nal < info.iNalCount; nal++)
          {
            size += info.pNalLengthInByte[nal];
          }
          frame.insert(frame.end(), info.pBsBuf, info.pBsBuf + size);
        }

        connection_.SendVideoFrame(std::move(frame), encoded.eFrameType == videoFrameTypeIDR);
      }
    }

    // Steady 30 fps. If we fell behind, start counting from now instead of rushing to catch up.
    nextFrame += kFrameTime;
    const auto now = std::chrono::steady_clock::now();
    if (now > nextFrame + kFrameTime)
    {
      nextFrame = now;
    }
    std::this_thread::sleep_until(nextFrame);
  }
}
} // namespace yapper
