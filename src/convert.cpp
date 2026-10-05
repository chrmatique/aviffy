#include "convert.hpp"

#include <avif/avif.h>
#include <jpeglib.h>

#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace aviffy {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// libjpeg error handling (libjpeg uses longjmp on fatal errors)
// ---------------------------------------------------------------------------

struct JpegErrorManager {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void jpegErrorExit(j_common_ptr cinfo) {
    auto* mgr = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    longjmp(mgr->jump, 1);
}

// ---------------------------------------------------------------------------
// Orientation transforms
//
// libavif 1.4.x stores the irot/imir boxes but does NOT apply them to the
// decoded pixels; the comment on avifDecoder::image is explicit about this
// ("images won't be pre-cropped or mirrored upon decode"). We must apply them
// ourselves, in the order required by ISO/IEC 23008-12: clap, then irot, then
// imir. (clap is a rare fractional crop and is not handled; see README.)
// ---------------------------------------------------------------------------

// Rotate a tightly packed RGB buffer anti-clockwise by angle*90 degrees.
std::vector<uint8_t> rotateCcw(const std::vector<uint8_t>& src, int w, int h,
                               int angle, int& ow, int& oh) {
    angle = ((angle % 4) + 4) % 4;
    if (angle == 0) {
        ow = w;
        oh = h;
        return src;
    }
    if (angle == 1 || angle == 3) {
        ow = h;
        oh = w;
    } else { // 180
        ow = w;
        oh = h;
    }

    std::vector<uint8_t> dst(static_cast<size_t>(ow) * oh * 3);
    for (int sy = 0; sy < h; ++sy) {
        for (int sx = 0; sx < w; ++sx) {
            int dx;
            int dy;
            if (angle == 1) {
                dx = sy;
                dy = w - 1 - sx;
            } else if (angle == 2) {
                dx = w - 1 - sx;
                dy = h - 1 - sy;
            } else { // 3 (== 90 clockwise)
                dx = h - 1 - sy;
                dy = sx;
            }
            const uint8_t* s = &src[(static_cast<size_t>(sy) * w + sx) * 3];
            uint8_t* d = &dst[(static_cast<size_t>(dy) * ow + dx) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
    return dst;
}

// Mirror a tightly packed RGB buffer. axis 0 swaps top/bottom, axis 1 swaps
// left/right.
std::vector<uint8_t> mirror(const std::vector<uint8_t>& src, int w, int h, int axis) {
    std::vector<uint8_t> dst(src.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int dx = (axis == 1) ? (w - 1 - x) : x;
            const int dy = (axis == 0) ? (h - 1 - y) : y;
            const uint8_t* s = &src[(static_cast<size_t>(y) * w + x) * 3];
            uint8_t* d = &dst[(static_cast<size_t>(dy) * w + dx) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// JPEG encoding
// ---------------------------------------------------------------------------

bool writeJpeg(const std::string& path, const uint8_t* rgb, uint32_t w, uint32_t h,
               int quality, const uint8_t* icc, size_t iccSize, std::string& err) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        err = "cannot open output file for writing";
        return false;
    }

    jpeg_compress_struct cinfo;
    std::memset(&cinfo, 0, sizeof(cinfo)); // safe to destroy if create() fails
    JpegErrorManager jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

    if (setjmp(jerr.jump)) {
        jpeg_destroy_compress(&cinfo);
        std::fclose(fp);
        std::remove(path.c_str()); // do not leave a half-written file behind
        err = "libjpeg failed while encoding";
        return false;
    }

    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    if (iccSize > 0 && icc != nullptr) {
        jpeg_write_icc_profile(&cinfo, reinterpret_cast<const JOCTET*>(icc),
                               static_cast<unsigned int>(iccSize));
    }

    const size_t stride = static_cast<size_t>(w) * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPLE* row =
            const_cast<JSAMPLE*>(rgb + static_cast<size_t>(cinfo.next_scanline) * stride);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::fclose(fp);
    return true;
}

// RAII for raw pointers coming out of libavif.
template <typename T, void (*Destroy)(T*)>
struct Guard {
    T* p;
    explicit Guard(T* ptr) : p(ptr) {}
    ~Guard() {
        if (p) {
            Destroy(p);
        }
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

void destroyDecoder(avifDecoder* d) { avifDecoderDestroy(d); }
void freeRgbPixels(avifRGBImage* r) { avifRGBImageFreePixels(r); }

} // namespace

ConvertResult convertFile(const std::string& srcPath, const std::string& dstPath,
                          const ConvertOptions& opt) {
    if (!opt.force) {
        std::error_code ec;
        if (fs::exists(dstPath, ec) && !ec) {
            return {ConvertStatus::Skipped, "output already exists"};
        }
    }

    avifDecoder* rawDecoder = avifDecoderCreate();
    if (!rawDecoder) {
        return {ConvertStatus::Failed, "out of memory creating AVIF decoder"};
    }
    Guard<avifDecoder, destroyDecoder> decoder(rawDecoder);

    avifResult res = avifDecoderSetIOFile(decoder.p, srcPath.c_str());
    if (res != AVIF_RESULT_OK) {
        return {ConvertStatus::Failed, std::string("cannot open input: ") + avifResultToString(res)};
    }
    res = avifDecoderParse(decoder.p);
    if (res != AVIF_RESULT_OK) {
        return {ConvertStatus::Failed, std::string("not a readable AVIF: ") + avifResultToString(res)};
    }
    res = avifDecoderNextImage(decoder.p);
    if (res != AVIF_RESULT_OK) {
        return {ConvertStatus::Failed, std::string("decode failed: ") + avifResultToString(res)};
    }

    const avifImage* image = decoder.p->image;
    if (!image || image->width == 0 || image->height == 0) {
        return {ConvertStatus::Failed, "decoder produced no image data"};
    }

    const int iw = static_cast<int>(image->width);
    const int ih = static_cast<int>(image->height);
    const bool hasAlpha = (image->alphaPlane != nullptr);

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.depth = 8; // down-rescale 10/12/16-bit input to 8-bit JPEG samples
    rgb.format = hasAlpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;

    if (avifRGBImageAllocatePixels(&rgb) != AVIF_RESULT_OK) {
        return {ConvertStatus::Failed, "out of memory allocating RGB buffer"};
    }
    Guard<avifRGBImage, freeRgbPixels> rgbGuard(&rgb);

    res = avifImageYUVToRGB(image, &rgb);
    if (res != AVIF_RESULT_OK) {
        return {ConvertStatus::Failed, std::string("YUV->RGB conversion failed: ") + avifResultToString(res)};
    }

    // Flatten to a tightly packed 3-channel RGB buffer. JPEG has no alpha, so
    // any alpha is composited over white.
    std::vector<uint8_t> buf(static_cast<size_t>(iw) * ih * 3);
    if (hasAlpha) {
        for (int y = 0; y < ih; ++y) {
            const uint8_t* s = rgb.pixels + static_cast<size_t>(y) * rgb.rowBytes;
            uint8_t* d = buf.data() + static_cast<size_t>(y) * iw * 3;
            for (int x = 0; x < iw; ++x) {
                const int a = s[3];
                const int inv = 255 - a;
                d[0] = static_cast<uint8_t>((s[0] * a + 255 * inv + 127) / 255);
                d[1] = static_cast<uint8_t>((s[1] * a + 255 * inv + 127) / 255);
                d[2] = static_cast<uint8_t>((s[2] * a + 255 * inv + 127) / 255);
                s += 4;
                d += 3;
            }
        }
    } else {
        const size_t rowBytes = static_cast<size_t>(iw) * 3;
        for (int y = 0; y < ih; ++y) {
            std::memcpy(buf.data() + static_cast<size_t>(y) * rowBytes,
                        rgb.pixels + static_cast<size_t>(y) * rgb.rowBytes, rowBytes);
        }
    }

    // Apply irot (rotation) then imir (mirroring), per the HEIF transform order.
    int ow = iw;
    int oh = ih;
    if (image->transformFlags & AVIF_TRANSFORM_IROT) {
        buf = rotateCcw(buf, ow, oh, image->irot.angle, ow, oh);
    }
    if (image->transformFlags & AVIF_TRANSFORM_IMIR) {
        buf = mirror(buf, ow, oh, image->imir.axis);
    }

    // Preserve an embedded ICC profile so wide-gamut images keep their colour.
    std::vector<uint8_t> icc;
    if (image->icc.size > 0 && image->icc.data != nullptr) {
        icc.assign(image->icc.data, image->icc.data + image->icc.size);
    }

    std::error_code ec;
    const fs::path parent = fs::path(dstPath).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec); // ignore "already exists"
    }

    std::string werr;
    if (!writeJpeg(dstPath, buf.data(), static_cast<uint32_t>(ow), static_cast<uint32_t>(oh),
                   opt.quality, icc.empty() ? nullptr : icc.data(), icc.size(), werr)) {
        return {ConvertStatus::Failed, werr};
    }
    return {ConvertStatus::Converted, ""};
}

} // namespace aviffy
