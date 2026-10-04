# zxing-cpp v2.3.0 — vendored static, readers-only.
# Source: https://github.com/zxing-cpp/zxing-cpp tag v2.3.0
# (commit d6068bcebeb8fd9f0d35a99b00d202be86a14dbe, Apache-2.0, see LICENSE;
#  libzueci is BSD-3-Clause, SPDX tag in each file).
# File list parsed from core/CMakeLists.txt with ZXING_READERS=ON and all
# ZXING_WRITERS*=OFF: every set(*_FILES) block whose guard is true,
# .cpp/.c only. WriteBarcode.cpp is in upstream's unconditional COMMON set
# (its body is #ifdef ZXING_WRITERS-guarded, so it compiles to ~empty here).
# libzint/ (NEW-writers backend) is not vendored. Regenerate on bump with
# the same rule; include/Version.h is rendered from core/Version.h.in
# (PROJECT_VERSION 2.3.0, readers only).

ZXING_ROOT = $$PWD

INCLUDEPATH += $$ZXING_ROOT/src $$ZXING_ROOT/include

# ZXING_READERS comes from include/Version.h (rendered from
# core/Version.h.in). Upstream ZXING_PRIVATE_FLAGS for a readers-only
# build: ZXING_INTERNAL gates internal row()/col() access;
# ZUECI_EMBED_NO_TO_ECI drops zueci's ECI writer tables (upstream sets
# it per-file on zueci.c when writers are off — global here, only
# zueci.c reads it).
DEFINES += ZXING_INTERNAL ZUECI_EMBED_NO_TO_ECI

SOURCES += \
    $$ZXING_ROOT/src/Barcode.cpp \
    $$ZXING_ROOT/src/BarcodeFormat.cpp \
    $$ZXING_ROOT/src/BinaryBitmap.cpp \
    $$ZXING_ROOT/src/BitArray.cpp \
    $$ZXING_ROOT/src/BitMatrix.cpp \
    $$ZXING_ROOT/src/BitMatrixIO.cpp \
    $$ZXING_ROOT/src/BitSource.cpp \
    $$ZXING_ROOT/src/CharacterSet.cpp \
    $$ZXING_ROOT/src/ConcentricFinder.cpp \
    $$ZXING_ROOT/src/Content.cpp \
    $$ZXING_ROOT/src/DecodeHints.cpp \
    $$ZXING_ROOT/src/ECI.cpp \
    $$ZXING_ROOT/src/Error.cpp \
    $$ZXING_ROOT/src/GTIN.cpp \
    $$ZXING_ROOT/src/GenericGF.cpp \
    $$ZXING_ROOT/src/GenericGFPoly.cpp \
    $$ZXING_ROOT/src/GlobalHistogramBinarizer.cpp \
    $$ZXING_ROOT/src/GridSampler.cpp \
    $$ZXING_ROOT/src/HRI.cpp \
    $$ZXING_ROOT/src/HybridBinarizer.cpp \
    $$ZXING_ROOT/src/MultiFormatReader.cpp \
    $$ZXING_ROOT/src/PerspectiveTransform.cpp \
    $$ZXING_ROOT/src/ReadBarcode.cpp \
    $$ZXING_ROOT/src/ReedSolomonDecoder.cpp \
    $$ZXING_ROOT/src/ResultPoint.cpp \
    $$ZXING_ROOT/src/TextDecoder.cpp \
    $$ZXING_ROOT/src/TextUtfEncoding.cpp \
    $$ZXING_ROOT/src/Utf.cpp \
    $$ZXING_ROOT/src/WhiteRectDetector.cpp \
    $$ZXING_ROOT/src/WriteBarcode.cpp \
    $$ZXING_ROOT/src/ZXingC.cpp \
    $$ZXING_ROOT/src/ZXingCpp.cpp \
    $$ZXING_ROOT/src/aztec/AZDecoder.cpp \
    $$ZXING_ROOT/src/aztec/AZDetector.cpp \
    $$ZXING_ROOT/src/aztec/AZReader.cpp \
    $$ZXING_ROOT/src/datamatrix/DMBitLayout.cpp \
    $$ZXING_ROOT/src/datamatrix/DMDataBlock.cpp \
    $$ZXING_ROOT/src/datamatrix/DMDecoder.cpp \
    $$ZXING_ROOT/src/datamatrix/DMDetector.cpp \
    $$ZXING_ROOT/src/datamatrix/DMReader.cpp \
    $$ZXING_ROOT/src/datamatrix/DMVersion.cpp \
    $$ZXING_ROOT/src/libzueci/zueci.c \
    $$ZXING_ROOT/src/maxicode/MCBitMatrixParser.cpp \
    $$ZXING_ROOT/src/maxicode/MCDecoder.cpp \
    $$ZXING_ROOT/src/maxicode/MCReader.cpp \
    $$ZXING_ROOT/src/oned/ODCodabarReader.cpp \
    $$ZXING_ROOT/src/oned/ODCode128Patterns.cpp \
    $$ZXING_ROOT/src/oned/ODCode128Reader.cpp \
    $$ZXING_ROOT/src/oned/ODCode39Reader.cpp \
    $$ZXING_ROOT/src/oned/ODCode93Reader.cpp \
    $$ZXING_ROOT/src/oned/ODDXFilmEdgeReader.cpp \
    $$ZXING_ROOT/src/oned/ODDataBarCommon.cpp \
    $$ZXING_ROOT/src/oned/ODDataBarExpandedBitDecoder.cpp \
    $$ZXING_ROOT/src/oned/ODDataBarExpandedReader.cpp \
    $$ZXING_ROOT/src/oned/ODDataBarLimitedReader.cpp \
    $$ZXING_ROOT/src/oned/ODDataBarReader.cpp \
    $$ZXING_ROOT/src/oned/ODITFReader.cpp \
    $$ZXING_ROOT/src/oned/ODMultiUPCEANReader.cpp \
    $$ZXING_ROOT/src/oned/ODReader.cpp \
    $$ZXING_ROOT/src/oned/ODUPCEANCommon.cpp \
    $$ZXING_ROOT/src/pdf417/PDFBarcodeValue.cpp \
    $$ZXING_ROOT/src/pdf417/PDFBoundingBox.cpp \
    $$ZXING_ROOT/src/pdf417/PDFCodewordDecoder.cpp \
    $$ZXING_ROOT/src/pdf417/PDFDecoder.cpp \
    $$ZXING_ROOT/src/pdf417/PDFDetectionResult.cpp \
    $$ZXING_ROOT/src/pdf417/PDFDetectionResultColumn.cpp \
    $$ZXING_ROOT/src/pdf417/PDFDetector.cpp \
    $$ZXING_ROOT/src/pdf417/PDFModulusGF.cpp \
    $$ZXING_ROOT/src/pdf417/PDFModulusPoly.cpp \
    $$ZXING_ROOT/src/pdf417/PDFReader.cpp \
    $$ZXING_ROOT/src/pdf417/PDFScanningDecoder.cpp \
    $$ZXING_ROOT/src/pdf417/ZXBigInteger.cpp \
    $$ZXING_ROOT/src/qrcode/QRBitMatrixParser.cpp \
    $$ZXING_ROOT/src/qrcode/QRCodecMode.cpp \
    $$ZXING_ROOT/src/qrcode/QRDataBlock.cpp \
    $$ZXING_ROOT/src/qrcode/QRDecoder.cpp \
    $$ZXING_ROOT/src/qrcode/QRDetector.cpp \
    $$ZXING_ROOT/src/qrcode/QRErrorCorrectionLevel.cpp \
    $$ZXING_ROOT/src/qrcode/QRFormatInformation.cpp \
    $$ZXING_ROOT/src/qrcode/QRReader.cpp \
    $$ZXING_ROOT/src/qrcode/QRVersion.cpp
