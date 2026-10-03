# zxing-cpp v3.0.2 — vendored static, readers-only, all symbologies.
# Source: https://github.com/zxing-cpp/zxing-cpp (Apache-2.0, see LICENSE).
# File list parsed from core/CMakeLists.txt with ZXING_READERS=ON,
# ZXING_WRITERS=OFF; symbology flags come from include/Version.h.
# Regenerate when bumping the version (drop every file whose CMake block
# mentions WRITERS without READERS).

ZXING_ROOT = $$PWD

INCLUDEPATH += $$ZXING_ROOT/include $$ZXING_ROOT/src

# Upstream's ZXING_PRIVATE_FLAGS (core/CMakeLists.txt): ZXING_INTERNAL gates
# Range-based row()/col() access; ZUECI_EMBED_NO_TO_ECI drops zueci's ECI
# writer tables since ZXING_WRITERS is off.
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
    $$ZXING_ROOT/src/CreateBarcode.cpp \
    $$ZXING_ROOT/src/ECI.cpp \
    $$ZXING_ROOT/src/Error.cpp \
    $$ZXING_ROOT/src/GTIN.cpp \
    $$ZXING_ROOT/src/GenericGF.cpp \
    $$ZXING_ROOT/src/GenericGFPoly.cpp \
    $$ZXING_ROOT/src/GlobalHistogramBinarizer.cpp \
    $$ZXING_ROOT/src/GridSampler.cpp \
    $$ZXING_ROOT/src/HRI.cpp \
    $$ZXING_ROOT/src/HybridBinarizer.cpp \
    $$ZXING_ROOT/src/JSON.cpp \
    $$ZXING_ROOT/src/MultiFormatReader.cpp \
    $$ZXING_ROOT/src/PerspectiveTransform.cpp \
    $$ZXING_ROOT/src/ReadBarcode.cpp \
    $$ZXING_ROOT/src/ReedSolomonDecoder.cpp \
    $$ZXING_ROOT/src/ResultPoint.cpp \
    $$ZXING_ROOT/src/TextDecoder.cpp \
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
    $$ZXING_ROOT/src/qrcode/QRVersion.cpp \

HEADERS += \
    $$ZXING_ROOT/include/Version.h
