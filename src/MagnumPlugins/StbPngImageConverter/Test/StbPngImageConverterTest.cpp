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

#include <sstream>
#include <tuple>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/TestSuite/Tester.h>
#include <Corrade/Utility/Directory.h>

#include "Magnum/ColorFormat.h"
#include "Magnum/Image.h"
#include "Magnum/Trade/ImageData.h"
#include "MagnumPlugins/StbPngImageConverter/StbPngImageConverter.h"
#include "MagnumPlugins/StbImageImporter/StbImageImporter.h"

#include "configure.h"

namespace Magnum { namespace Trade { namespace Test {

struct StbPngImageConverterTest: TestSuite::Tester {
    explicit StbPngImageConverterTest();

    void wrongFormat();
    void wrongType();

    void data();
};

namespace {
    #ifndef CORRADE_GCC46_COMPATIBILITY
    constexpr
    #endif
    const char originalData[] = {
        1, 2, 3, 2, 3, 4,
        3, 4, 5, 4, 5, 6,
        5, 6, 7, 6, 7, 8
    };

    const ImageReference2D original(ColorFormat::RGB, ColorType::UnsignedByte, {2, 3}, originalData);
}

StbPngImageConverterTest::StbPngImageConverterTest() {
    addTests<StbPngImageConverterTest>({&StbPngImageConverterTest::wrongFormat,
              &StbPngImageConverterTest::wrongType,

              &StbPngImageConverterTest::data});
}

void StbPngImageConverterTest::wrongFormat() {
    ImageReference2D image(ColorFormat::DepthComponent, ColorType::UnsignedByte, {}, nullptr);

    std::ostringstream out;
    Error::setOutput(&out);

    const auto data = StbPngImageConverter{}.exportToData(image);
    CORRADE_VERIFY(!data);
    CORRADE_COMPARE(out.str(), "Trade::StbPngImageConverter::exportToData(): unsupported color format ColorFormat::DepthComponent\n");
}

void StbPngImageConverterTest::wrongType() {
    ImageReference2D image(ColorFormat::Red, ColorType::Float, {}, nullptr);

    std::ostringstream out;
    Error::setOutput(&out);

    const auto data = StbPngImageConverter{}.exportToData(image);
    CORRADE_VERIFY(!data);
    CORRADE_COMPARE(out.str(), "Trade::StbPngImageConverter::exportToData(): unsupported color type ColorType::Float\n");
}

void StbPngImageConverterTest::data() {
    const auto data = StbPngImageConverter{}.exportToData(original);

    StbImageImporter importer;
    CORRADE_VERIFY(importer.openData(data));
    std::optional<Trade::ImageData2D> converted = importer.image2D(0);
    CORRADE_VERIFY(converted);

    CORRADE_COMPARE(converted->size(), Vector2i(2, 3));
    CORRADE_COMPARE(converted->format(), ColorFormat::RGB);
    CORRADE_COMPARE(converted->type(), ColorType::UnsignedByte);
    /* GCC 4.5 doesn't like {} here */
    CORRADE_COMPARE(std::string(converted->data(), 2*3*3),
                    std::string(original.data(), 2*3*3));
}

}}}

CORRADE_TEST_MAIN(Magnum::Trade::Test::StbPngImageConverterTest)
