/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include "PngImporter.h"

#include <fstream>
#include <sstream>
#include <png.h>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Utility/Debug.h>
#include <Magnum/ColorFormat.h>
#include <Magnum/Trade/ImageData.h>

#ifdef MAGNUM_TARGET_GLES2
#include <Magnum/Context.h>
#include <Magnum/Extensions.h>
#endif

namespace Magnum { namespace Trade {

PngImporter::PngImporter(): _in(nullptr) {}

PngImporter::PngImporter(PluginManager::AbstractManager& manager, std::string plugin): AbstractImporter(manager, std::move(plugin)), _in(nullptr) {}

PngImporter::~PngImporter() { close(); }

auto PngImporter::doFeatures() const -> Features { return Feature::OpenData; }

bool PngImporter::doIsOpened() const { return _in; }

void PngImporter::doClose() {
    delete _in;
    _in = nullptr;
}

void PngImporter::doOpenData(const Containers::ArrayView<const char> data) {
    /* GCC 4.5 doesn't like {} here */
    _in = new std::istringstream{std::string(data, data.size())};
}

void PngImporter::doOpenFile(const std::string& filename) {
    _in = new std::ifstream(filename);
    if(_in->good()) return;

    Error() << "Trade::PngImporter::openFile(): cannot open file" << filename;
    close();
}

UnsignedInt PngImporter::doImage2DCount() const { return 1; }

#ifdef CORRADE_GCC44_COMPATIBILITY
namespace {
    void reader(png_structp file, png_bytep data, png_size_t length) {
        reinterpret_cast<std::istream*>(png_get_io_ptr(file))->read(reinterpret_cast<char*>(data), length);
    }
}
#endif

std::optional<ImageData2D> PngImporter::doImage2D(UnsignedInt) {
    /* Verify file signature */
    png_byte signature[8];
    _in->read(reinterpret_cast<char*>(signature), 8);
    if(png_sig_cmp(signature, 0, 8) != 0) {
        Error() << "Trade::PngImporter::image2D(): wrong file signature";
        return std::nullopt;
    }

    /* Structures for reading the file */
    png_structp file = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    CORRADE_INTERNAL_ASSERT(file);
    png_infop info = png_create_info_struct(file);
    CORRADE_INTERNAL_ASSERT(info);
    png_bytep* rows = nullptr;
    unsigned char* data = nullptr;

    /* Error handling routine */
    /** @todo Get rid of setjmp (won't work everywhere) */
    if(setjmp(png_jmpbuf(file))) {
        Error() << "Trade::PngImporter::image2D(): error while reading PNG file";

        png_destroy_read_struct(&file, &info, nullptr);
        delete[] rows;
        delete[] data;
        return std::nullopt;
    }

    /* Set function for reading from std::istream */
    #ifndef CORRADE_GCC44_COMPATIBILITY
    png_set_read_fn(file, _in, [](png_structp file, png_bytep data, png_size_t length) {
        reinterpret_cast<std::istream*>(png_get_io_ptr(file))->read(reinterpret_cast<char*>(data), length);
    });
    #else
    png_set_read_fn(file, _in, reader);
    #endif

    /* The signature is already read */
    png_set_sig_bytes(file, 8);

    /* Read file information */
    png_read_info(file, info);

    /* Image size */
    const Vector2i size(png_get_image_width(file, info), png_get_image_height(file, info));

    /* Image channels and bit depth */
    png_uint_32 bits = png_get_bit_depth(file, info);
    png_uint_32 channels = png_get_channels(file, info);
    const png_uint_32 colorType = png_get_color_type(file, info);

    /* Image format */
    ColorFormat format;
    switch(colorType) {
        /* Types that can be used without conversion */
        case PNG_COLOR_TYPE_GRAY:
            #ifndef MAGNUM_TARGET_GLES2
            format = ColorFormat::Red;
            #else
            format = Context::current() && Context::current()->isExtensionSupported<Extensions::GL::EXT::texture_rg>() ?
                ColorFormat::Red : ColorFormat::Luminance;
            #endif
            CORRADE_INTERNAL_ASSERT(channels == 1);

            /* Convert to 8-bit */
            if(bits < 8) {
                png_set_expand_gray_1_2_4_to_8(file);
                bits = 8;
            }

            break;

        case PNG_COLOR_TYPE_RGB:
            format = ColorFormat::RGB;
            CORRADE_INTERNAL_ASSERT(channels == 3);
            break;

        case PNG_COLOR_TYPE_RGBA:
            format = ColorFormat::RGBA;
            CORRADE_INTERNAL_ASSERT(channels == 4);
            break;

        /* Palette needs to be converted */
        case PNG_COLOR_TYPE_PALETTE:
            /** @todo test case for this */
            png_set_palette_to_rgb(file);
            format = ColorFormat::RGB;
            channels = 3;
            break;

        default:
            Error() << "Trade::PngImporter::image2D(): unsupported color type" << colorType;
            png_destroy_read_struct(&file, &info, nullptr);
            return std::nullopt;
    }

    /* Convert transparency mask to alpha */
    if(png_get_valid(file, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(file);
        channels += 1;
        CORRADE_INTERNAL_ASSERT(channels == 4);
    }

    /* Image type */
    ColorType type;
    switch(bits) {
        case 8:  type = ColorType::UnsignedByte;  break;
        case 16: type = ColorType::UnsignedShort; break;

        default:
            Error() << "Trade::PngImporter::image2D(): unsupported bit depth" << bits;
            png_destroy_read_struct(&file, &info, nullptr);
            return std::nullopt;
    }

    /* Initialize data array */
    data = new unsigned char[size.product()*channels*bits/8];

    /* Read image row by row */
    rows = new png_bytep[size.y()];
    const Int stride = size.x()*channels*bits/8;
    for(Int i = 0; i != size.y(); ++i)
        rows[i] = data + (size.y() - i - 1)*stride;
    png_read_image(file, rows);
    delete[] rows;

    /* Cleanup */
    png_destroy_read_struct(&file, &info, nullptr);

    return Trade::ImageData2D(format, type, size, data);
}

}}
