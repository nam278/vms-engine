# plugins/CMakeLists.txt — Custom DeepStream inference parser plugins
#
# Each target builds a SHARED (.so) library that can be loaded at runtime
# by nvinfer via the `custom-lib-path` field in the model config.
#
# Variables used (all defined by root CMakeLists.txt):
#   DEEPSTREAM_INCLUDE_DIRS   — DeepStream SDK headers
#   CUDAToolkit_INCLUDE_DIRS  — CUDA headers
#   GLIB2_INCLUDE_DIRS        — GLib-2.0 headers (g_strdup etc.)

# ── Sanity check ────────────────────────────────────────────────────────────
if(NOT DEEPSTREAM_INCLUDE_DIRS)
    message(FATAL_ERROR
        "DEEPSTREAM_INCLUDE_DIRS is not set. "
        "Ensure -DDEEPSTREAM_DIR=... was passed to CMake.")
endif()

# ── Target definitions ───────────────────────────────────────────────────────

# 1. DeepMAR person attribute parser (44 labels)
add_library(lantana_person_attr_parser SHARED
    src/nvdsinfer_person_attrib_deepmar.cpp)
set_target_properties(lantana_person_attr_parser PROPERTIES
    OUTPUT_NAME              lantana_person_attr_parser
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 2. DeepMAR person attribute parser (24 labels)
add_library(lantana_person_attr_24label SHARED
    src/nvdsinfer_person_attrib_deepmar_24label.cpp)
set_target_properties(lantana_person_attr_24label PROPERTIES
    OUTPUT_NAME              lantana_person_attr_24label
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 3. Fast Plate OCR (multi-head classification)
add_library(ocr_fast_plate_parser SHARED
    src/ocr_fast_plate_parser.cpp)
set_target_properties(ocr_fast_plate_parser PROPERTIES
    OUTPUT_NAME              ocr_fast_plate_parser
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 4. YOLO bounding-box parser
add_library(nvdsinfer_custom_impl_Yolo SHARED
    src/nvdsinfer_custom_impl_Yolo.cpp)
set_target_properties(nvdsinfer_custom_impl_Yolo PROPERTIES
    OUTPUT_NAME              nvdsinfer_custom_impl_Yolo
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 5. YOLO padded bounding-box parser
add_library(nvdsinfer_custom_impl_Yolo_padded SHARED
    src/nvdsinfer_custom_impl_Yolo_padded.cpp)
set_target_properties(nvdsinfer_custom_impl_Yolo_padded PROPERTIES
    OUTPUT_NAME              nvdsinfer_custom_impl_Yolo_padded
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 6. Fast Plate OCR — Vietnamese plates
add_library(ocr_fast_plate_parser_vn SHARED
    src/ocr_fast_plate_parser_vn.cpp)
set_target_properties(ocr_fast_plate_parser_vn PROPERTIES
    OUTPUT_NAME              ocr_fast_plate_parser_vn
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# 7. YOLO face detection with landmarks
add_library(nvdsinfer_custom_impl_Yolo_face SHARED
    src/nvdsinfer_custom_impl_Yolo_face.cpp)
set_target_properties(nvdsinfer_custom_impl_Yolo_face PROPERTIES
    OUTPUT_NAME              nvdsinfer_custom_impl_Yolo_face
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# ── Shared include directories & C++17 for every plugin target ──────────────
foreach(_target
    lantana_person_attr_parser
    lantana_person_attr_24label
    ocr_fast_plate_parser
    nvdsinfer_custom_impl_Yolo
    nvdsinfer_custom_impl_Yolo_padded
    ocr_fast_plate_parser_vn
    nvdsinfer_custom_impl_Yolo_face
)
    target_include_directories(${_target} PRIVATE
        ${DEEPSTREAM_INCLUDE_DIRS}    # nvdsinfer_custom_impl.h, nvdsinfer.h
        ${CUDAToolkit_INCLUDE_DIRS}   # cuda_runtime.h
        ${GLIB2_INCLUDE_DIRS}         # glib.h
    )
    target_compile_features(${_target} PUBLIC cxx_std_17)
endforeach()

# ── Install rules ────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS
    lantana_person_attr_parser
    lantana_person_attr_24label
    ocr_fast_plate_parser
    nvdsinfer_custom_impl_Yolo
    nvdsinfer_custom_impl_Yolo_padded
    ocr_fast_plate_parser_vn
    nvdsinfer_custom_impl_Yolo_face
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/vms-engine/plugins
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "✔ plugins: 7 custom DeepStream parser targets defined")
