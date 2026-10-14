# The pinned SDK uses IJG libjpeg 9, whereas current upstream assumes turbo's
# JCS_EXT_BGRX. Use the standard RGB decoder on BOTH platforms, converting a
# scanline backwards in place to engine RGB888 (inverted alpha = 0).
file(READ "${CRE_ROOT}/crengine/src/lvimg.cpp" CRE_IMAGE_CODE)
string(REPLACE "cinfo.out_color_space = JCS_EXT_BGRX;" "cinfo.out_color_space = JCS_RGB;" CRE_IMAGE_CODE "${CRE_IMAGE_CODE}")
set(CRE_JPEG_BEFORE [=[                    // CRe wants inverted alpha, but libjpeg-turbo doesn't guarantee that the alpha byte will be zero...
                    const unsigned char* const end = buffer + (cinfo.output_width << 2U);
                    // Start at the first alpha byte, then loop over each subsequent alpha byte, pixel-by-pixel
                    for (unsigned char* p = buffer + 3U; p <= end; p += 4U) {
                        *p = 0x00;
                    }]=])
set(CRE_JPEG_AFTER [=[                    // EPUB reader: portable RGB -> RGB888, without a turbo dependency.
                    for (int x = static_cast<int>(cinfo.output_width) - 1; x >= 0; --x) {
                        const lUInt32 r = buffer[x * 3];
                        const lUInt32 g = buffer[x * 3 + 1];
                        const lUInt32 b = buffer[x * 3 + 2];
                        reinterpret_cast<lUInt32 *>(buffer)[x] = (r << 16) | (g << 8) | b;
                    }]=])
string(FIND "${CRE_IMAGE_CODE}" "${CRE_JPEG_BEFORE}" CRE_JPEG_HOOK_INDEX)
if(CRE_JPEG_HOOK_INDEX EQUAL -1)
    message(FATAL_ERROR "Pinned CRengine JPEG hook changed")
endif()
string(REPLACE "${CRE_JPEG_BEFORE}" "${CRE_JPEG_AFTER}" CRE_IMAGE_CODE "${CRE_IMAGE_CODE}")
string(REPLACE "// NOTE: We know that cinfo.output_components is 4, given the out_color_space we chose."
    "// Four bytes/pixel reserve room for in-place expansion from standard RGB." CRE_IMAGE_CODE "${CRE_IMAGE_CODE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/crengine-config/lvimg.cpp" "${CRE_IMAGE_CODE}")
list(APPEND CRE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/crengine-config/lvimg.cpp")
