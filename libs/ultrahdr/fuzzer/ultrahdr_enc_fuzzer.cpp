/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// System include files
#include <fuzzer/FuzzedDataProvider.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

// User include files
#include "ultrahdr/gainmapmath.h"
#include "ultrahdr/jpegencoderhelper.h"
#include "utils/Log.h"

using namespace android::ultrahdr;

// constants
const int kMinWidth = 8;
const int kMaxWidth = 7680;

const int kMinHeight = 8;
const int kMaxHeight = 4320;

const int kScaleFactor = 4;

const int kJpegBlock = 16;

// Color gamuts for image data, sync with ultrahdr.h
const int kCgMin = ULTRAHDR_COLORGAMUT_UNSPECIFIED + 1;
const int kCgMax = ULTRAHDR_COLORGAMUT_MAX;

// Transfer functions for image data, sync with ultrahdr.h
const int kTfMin = ULTRAHDR_TF_UNSPECIFIED + 1;
const int kTfMax = ULTRAHDR_TF_MAX;

// Transfer functions for image data, sync with ultrahdr.h
const int kOfMin = ULTRAHDR_OUTPUT_UNSPECIFIED + 1;
const int kOfMax = ULTRAHDR_OUTPUT_MAX;

// quality factor
const int kQfMin = 0;
const int kQfMax = 100;

// seed
const unsigned kSeed = 0x7ab7;

class JpegHDRFuzzer {
public:
    JpegHDRFuzzer(const uint8_t* data, size_t size) : mFdp(data, size){};
    void process();
    void fillP010Buffer(uint16_t* data, int width, int height, int stride);
    void fill420Buffer(uint8_t* data, int size);

private:
    FuzzedDataProvider mFdp;
};

void JpegHDRFuzzer::fillP010Buffer(uint16_t* data, int width, int height, int stride) {
    uint16_t* tmp = data;
    std::vector<uint16_t> buffer(16);
    for (int i = 0; i < buffer.size(); i++) {
        buffer[i] = mFdp.ConsumeIntegralInRange<int>(0, (1 << 10) - 1);
    }
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i += buffer.size()) {
            memcpy(data + i, buffer.data(), std::min((int)buffer.size(), (width - i)));
            std::shuffle(buffer.begin(), buffer.end(), std::default_random_engine(kSeed));
        }
        tmp += stride;
    }
}

void JpegHDRFuzzer::fill420Buffer(uint8_t* data, int size) {
    std::vector<uint8_t> buffer(16);
    mFdp.ConsumeData(buffer.data(), buffer.size());
    for (int i = 0; i < size; i += buffer.size()) {
        memcpy(data + i, buffer.data(), std::min((int)buffer.size(), (size - i)));
        std::shuffle(buffer.begin(), buffer.end(), std::default_random_engine(kSeed));
    }
}

void JpegHDRFuzzer::process() {
    while (mFdp.remaining_bytes()) {
        struct jpegr_uncompressed_struct p010Img {};
        struct jpegr_uncompressed_struct yuv420Img {};
        struct jpegr_uncompressed_struct grayImg {};
        struct jpegr_compressed_struct jpegImgR {};
        struct jpegr_compressed_struct jpegImg {};
        struct jpegr_compressed_struct jpegGainMap {};

        // which encode api to select
        int muxSwitch = mFdp.ConsumeIntegralInRange<int>(0, 4);

        // quality factor
        int quality = mFdp.ConsumeIntegralInRange<int>(kQfMin, kQfMax);

        // hdr_tf
        auto tf = static_cast<ultrahdr_transfer_function>(
                mFdp.ConsumeIntegralInRange<int>(kTfMin, kTfMax));

        // p010 Cg
        auto p010Cg =
                static_cast<ultrahdr_color_gamut>(mFdp.ConsumeIntegralInRange<int>(kCgMin, kCgMax));

        // 420 Cg
        auto yuv420Cg =
                static_cast<ultrahdr_color_gamut>(mFdp.ConsumeIntegralInRange<int>(kCgMin, kCgMax));

        // hdr_of
        auto of = static_cast<ultrahdr_output_format>(
                mFdp.ConsumeIntegralInRange<int>(kOfMin, kOfMax));

        int width = mFdp.ConsumeIntegralInRange<int>(kMinWidth, kMaxWidth);
        width = (width >> 1) << 1;

        int height = mFdp.ConsumeIntegralInRange<int>(kMinHeight, kMaxHeight);
        height = (height >> 1) << 1;

        std::unique_ptr<uint16_t[]> bufferY = nullptr;
        std::unique_ptr<uint16_t[]> bufferUV = nullptr;
        std::unique_ptr<uint8_t[]> yuv420ImgRaw = nullptr;
        std::unique_ptr<uint8_t[]> grayImgRaw = nullptr;
        if (muxSwitch != 4) {
            // init p010 image
            bool isUVContiguous = mFdp.ConsumeBool();
            bool hasYStride = mFdp.ConsumeBool();
            int yStride = hasYStride ? mFdp.ConsumeIntegralInRange<int>(width, width + 128) : width;
            p010Img.width = width;
            p010Img.height = height;
            p010Img.colorGamut = p010Cg;
            p010Img.luma_stride = hasYStride ? yStride : 0;
            int bppP010 = 2;
            if (isUVContiguous) {
                size_t p010Size = yStride * height * 3 / 2;
                bufferY = std::make_unique<uint16_t[]>(p010Size);
                p010Img.data = bufferY.get();
                p010Img.chroma_data = nullptr;
                p010Img.chroma_stride = 0;
                fillP010Buffer(bufferY.get(), width, height, yStride);
                fillP010Buffer(bufferY.get() + yStride * height, width, height / 2, yStride);
            } else {
                int uvStride = mFdp.ConsumeIntegralInRange<int>(width, width + 128);
                size_t p010YSize = yStride * height;
                bufferY = std::make_unique<uint16_t[]>(p010YSize);
                p010Img.data = bufferY.get();
                fillP010Buffer(bufferY.get(), width, height, yStride);
                size_t p010UVSize = uvStride * p010Img.height / 2;
                bufferUV = std::make_unique<uint16_t[]>(p010UVSize);
                p010Img.chroma_data = bufferUV.get();
                p010Img.chroma_stride = uvStride;
                fillP010Buffer(bufferUV.get(), width, height / 2, uvStride);
            }
        } else {
            int map_width = width / kScaleFactor;
            int map_height = height / kScaleFactor;
            map_width = static_cast<size_t>(floor((map_width + kJpegBlock - 1) / kJpegBlock)) *
                    kJpegBlock;
            map_height = ((map_height + 1) >> 1) << 1;
            // init 400 image
            grayImg.width = map_width;
            grayImg.height = map_height;
            grayImg.colorGamut = ULTRAHDR_COLORGAMUT_UNSPECIFIED;

            const size_t graySize = map_width * map_height;
            grayImgRaw = std::make_unique<uint8_t[]>(graySize);
            grayImg.data = grayImgRaw.get();
            fill420Buffer(grayImgRaw.get(), graySize);
            grayImg.chroma_data = nullptr;
            grayImg.luma_stride = 0;
            grayImg.chroma_stride = 0;
        }

        if (muxSwitch > 0) {
            // init 420 image
            yuv420Img.width = width;
            yuv420Img.height = height;
            yuv420Img.colorGamut = yuv420Cg;

            const size_t yuv420Size = (yuv420Img.width * yuv420Img.height * 3) / 2;
            yuv420ImgRaw = std::make_unique<uint8_t[]>(yuv420Size);
            yuv420Img.data = yuv420ImgRaw.get();
            fill420Buffer(yuv420ImgRaw.get(), yuv420Size);
            yuv420Img.chroma_data = nullptr;
            yuv420Img.luma_stride = 0;
            yuv420Img.chroma_stride = 0;
        }

        // dest
        // 2 * p010 size as input data is random, DCT compression might not behave as expected
        jpegImgR.maxLength = std::max(8 * 1024 /* min size 8kb */, width * height * 3 * 2);
        auto jpegImgRaw = std::make_unique<uint8_t[]>(jpegImgR.maxLength);
        jpegImgR.data = jpegImgRaw.get();

//#define DUMP_PARAM
#ifdef DUMP_PARAM
        std::cout << "Api Select " << muxSwitch << std::endl;
        std::cout << "image dimensions " << width << " x " << height << std::endl;
        std::cout << "p010 color gamut " << p010Img.colorGamut << std::endl;
        std::cout << "p010 luma stride " << p010Img.luma_stride << std::endl;
        std::cout << "p010 chroma stride " << p010Img.chroma_stride << std::endl;
        std::cout << "420 color gamut " << yuv420Img.colorGamut << std::endl;
        std::cout << "quality factor " << quality << std::endl;
#endif

        JpegR jpegHdr;
        android::status_t status = android::UNKNOWN_ERROR;
        if (muxSwitch == 0) { // api 0
            jpegImgR.length = 0;
            status = jpegHdr.encodeJPEGR(&p010Img, tf, &jpegImgR, quality, nullptr);
        } else if (muxSwitch == 1) { // api 1
            jpegImgR.length = 0;
            status = jpegHdr.encodeJPEGR(&p010Img, &yuv420Img, tf, &jpegImgR, quality, nullptr);
        } else {
            // compressed img
            JpegEncoderHelper encoder;
            if (encoder.compressImage(yuv420Img.data, yuv420Img.width, yuv420Img.height, quality,
                                      nullptr, 0)) {
                jpegImg.length = encoder.getCompressedImageSize();
                jpegImg.maxLength = jpegImg.length;
                jpegImg.data = encoder.getCompressedImagePtr();
                jpegImg.colorGamut = yuv420Cg;

                if (muxSwitch == 2) { // api 2
                    jpegImgR.length = 0;
                    status = jpegHdr.encodeJPEGR(&p010Img, &yuv420Img, &jpegImg, tf, &jpegImgR);
                } else if (muxSwitch == 3) { // api 3
                    jpegImgR.length = 0;
                    status = jpegHdr.encodeJPEGR(&p010Img, &jpegImg, tf, &jpegImgR);
                } else if (muxSwitch == 4) { // api 4
                    jpegImgR.length = 0;
                    JpegEncoderHelper gainMapEncoder;
                    if (gainMapEncoder.compressImage(grayImg.data, grayImg.width, grayImg.height,
                                                     quality, nullptr, 0, true)) {
                        jpegGainMap.length = gainMapEncoder.getCompressedImageSize();
                        jpegGainMap.maxLength = jpegImg.length;
                        jpegGainMap.data = gainMapEncoder.getCompressedImagePtr();
                        jpegGainMap.colorGamut = ULTRAHDR_COLORGAMUT_UNSPECIFIED;
                        ultrahdr_metadata_struct metadata;
                        metadata.version = "1.3.1";
                        if (tf == ULTRAHDR_TF_HLG) {
                            metadata.maxContentBoost = kHlgMaxNits / kSdrWhiteNits;
                        } else if (tf == ULTRAHDR_TF_PQ) {
                            metadata.maxContentBoost = kPqMaxNits / kSdrWhiteNits;
                        } else {
                            metadata.maxContentBoost = 0;
                        }
                        metadata.minContentBoost = 1.0f;
                        status = jpegHdr.encodeJPEGR(&jpegImg, &jpegGainMap, &metadata, &jpegImgR);
                    }
                }
            }
        }
        if (status == android::OK) {
            jpegr_uncompressed_struct decodedJpegR;
            auto decodedRaw = std::make_unique<uint8_t[]>(width * height * 8);
            decodedJpegR.data = decodedRaw.get();
            jpegHdr.decodeJPEGR(&jpegImgR, &decodedJpegR,
                                mFdp.ConsumeFloatingPointInRange<float>(1.0, FLT_MAX), nullptr, of,
                                nullptr, nullptr);
            std::vector<uint8_t> iccData(0);
            std::vector<uint8_t> exifData(0);
            jpegr_info_struct info{0, 0, &iccData, &exifData};
            jpegHdr.getJPEGRInfo(&jpegImgR, &info);
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    JpegHDRFuzzer fuzzHandle(data, size);
    fuzzHandle.process();
    return 0;
}