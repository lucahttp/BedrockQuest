/*
 * Copyright 2025 - 2025 PICO. All rights reserved.
 *
 * NOTICE: All information contained herein is, and remains the property of PICO.
 * The intellectual and technical concepts contained herein are proprietary to PICO.
 * and may be covered by patents, patents in process, and are protected by trade
 * secret or copyright law. Dissemination of this information or reproduction of
 * this material is strictly forbidden unless prior written permission is obtained
 * from PICO.
 */

#pragma once
#include "pch.h"
#include <sstream>
#include "util/LogUtils.h"
#include "AndroidOpenXrProgram.h"

// NOLINTBEGIN
namespace SimpleSecureMr {

#define TO_STRING(x) #x

    std::string toHexString(unsigned long long value) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << value;  // show in lower
        return oss.str();
    }

    //////////////////////////////////////
    ////////////// Pipeline //////////////
    //////////////////////////////////////

#define CreatePipe(framework, pipeline)                                                                    \
    {                                                                                                      \
        XrSecureMrPipelineCreateInfoPICO createInfo{XR_TYPE_SECURE_MR_PIPELINE_CREATE_INFO_PICO, nullptr}; \
        CHECK_XRCMD(xrCreateSecureMrPipelinePICO(framework, &createInfo, &pipeline));                      \
        std::string logPrefix = std::string("[Pipeline] ") + TO_STRING(pipeline) + std::string(", ID = "); \
        std::string msg = logPrefix + toHexString(reinterpret_cast<unsigned long long>(pipeline));         \
        PLOGI(msg.c_str());                                                                                \
    }

    //////////////////////////////////////
    ////////////// Operator //////////////
    //////////////////////////////////////

    void PrintOperatorInfo() {
        PLOGI("SecureMR operator info: ");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO = 1");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO = 4");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO = 5");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO = 6");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO = 7");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO = 8");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO = 9");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO = 10");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO = 11");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO = 12");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO = 13");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO = 14");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO = 15");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO = 16");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO = 17");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO = 18");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO = 19");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO = 21");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO = 22");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO = 23");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO = 24");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO = 25");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO = 26");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO = 27");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO = 28");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO = 29");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO = 30");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO = 31");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO = 32");
        PLOGI("  XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO = 33");
    }

#define CreateBasicOperator(pipeline, type, op)                                                                    \
    {                                                                                                              \
        XrSecureMrOperatorBaseHeaderPICO header{XR_TYPE_SECURE_MR_OPERATOR_BASE_HEADER_PICO, nullptr};             \
        XrSecureMrOperatorCreateInfoPICO createInfo{XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, &header, \
                                                    type};                                                         \
        CHECK_XRCMD(xrCreateSecureMrOperatorPICO(pipeline, &createInfo, &op));                                     \
        std::string logPrefix = std::string("[Operator] ") + TO_STRING(op) + std::string(", ID = ");               \
        std::string msg = logPrefix + toHexString(reinterpret_cast<unsigned long long>(op));                       \
        PLOGI(msg.c_str());                                                                                        \
    }

#define CreateConfigOperator(pipeline, type, op, header)                                                          \
    {                                                                                                             \
        XrSecureMrOperatorCreateInfoPICO createInfo{XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr,         \
                                                    reinterpret_cast<XrSecureMrOperatorBaseHeaderPICO*>(&header), \
                                                    type};                                                        \
        CHECK_XRCMD(xrCreateSecureMrOperatorPICO(pipeline, &createInfo, &op));                                    \
        std::string logPrefix = std::string("[Operator] ") + TO_STRING(op) + std::string(", ID = ");              \
        std::string msg = logPrefix + toHexString(reinterpret_cast<unsigned long long>(op));                      \
        PLOGI(msg.c_str());                                                                                       \
    }

#define CreateVstAccessOp(pipeline, op) \
    CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO, op)

#define CreateLoadTextureOp(pipeline, op) \
    CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO, op)

#define CreateUpdateGltfOp_MaterialBaseColorTexture(pipeline, op)                                \
    {                                                                                            \
        XrSecureMrOperatorUpdateGltfPICO header{                                                 \
                XR_TYPE_SECURE_MR_OPERATOR_UPDATE_GLTF_PICO, nullptr,                            \
                XR_SECURE_MR_GLTF_OPERATOR_ATTRIBUTE_MATERIAL_BASE_COLOR_TEXTURE_PICO};          \
        CreateConfigOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO, op, header); \
    }

#define CreateRenderGltfOp(pipeline, op) \
    CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO, op)

#define CreateGetAffineOp(pipeline, op) CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO, op)

#define CreateApplyAffineOp(pipeline, op) \
    CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO, op)

#define CreateConvertColorOp(pipeline, op, cvtColorParam)                                                 \
    {                                                                                                     \
        XrSecureMrOperatorColorConvertPICO header{XR_TYPE_SECURE_MR_OPERATOR_COLOR_CONVERT_PICO, nullptr, \
                                                  cvtColorParam};                                         \
        CreateConfigOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO, op, header);        \
    }

#define CreateArithmeticOp(pipeline, op, config)                                                                    \
    {                                                                                                               \
        XrSecureMrOperatorArithmeticComposePICO header{XR_TYPE_SECURE_MR_OPERATOR_ARITHMETIC_COMPOSE_PICO, nullptr, \
                                                       config};                                                     \
        CreateConfigOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO, op, header);             \
    }

#define CreateAssignmentOp(pipeline, op) CreateBasicOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO, op)

#define CreateRenderTextOp(pipeline, op)                                                         \
    {                                                                                            \
        XrSecureMrOperatorRenderTextPICO header{XR_TYPE_SECURE_MR_OPERATOR_RENDER_TEXT_PICO,     \
                                                nullptr,                                         \
                                                XR_SECURE_MR_FONT_TYPEFACE_SANS_SERIF_PICO,      \
                                                "en-US",                                         \
                                                1440,                                            \
                                                960};                                            \
        CreateConfigOperator(pipeline, XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO, op, header); \
    }

    ////////////////////////////////////
    ////////////// Tensor //////////////
    ////////////////////////////////////

#define CreateTensor(pipeline, tensor, createInfo)                                                             \
    {                                                                                                          \
        CHECK_XRCMD(xrCreateSecureMrPipelineTensorPICO(                                                        \
                pipeline, reinterpret_cast<XrSecureMrTensorCreateInfoBaseHeaderPICO*>(&createInfo), &tensor)); \
        std::string logPrefix = std::string("[Tensor] ") + TO_STRING(tensor) + std::string(", ID = ");         \
        std::string msg = logPrefix + toHexString(reinterpret_cast<unsigned long long>(tensor));               \
        PLOGI(msg.c_str());                                                                                    \
    }

#define CreateTensor_mat_uint8_ch3(pipeline, tensor, width, height)                                        \
    {                                                                                                      \
        int32_t shape[2] = {width, height};                                                                \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 3,               \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                        \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 2, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_mat_uint8_ch3_placeholder(pipeline, tensor, width, height)                           \
    {                                                                                                     \
        int32_t shape[2] = {width, height};                                                               \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 3,              \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                       \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                   \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, true, 2, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                       \
    }

#define CreateTensor_mat_uint8_ch1(pipeline, tensor, width, height)                                        \
    {                                                                                                      \
        int32_t shape[2] = {width, height};                                                                \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                        \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 2, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_mat_float32_ch3(pipeline, tensor, width, height)                                      \
    {                                                                                                      \
        int32_t shape[2] = {width, height};                                                                \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 3,             \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                        \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 2, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_mat_float32_ch1(pipeline, tensor, width, height)                                      \
    {                                                                                                      \
        int32_t shape[2] = {width, height};                                                                \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 1,             \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                        \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 2, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_int8_ch1(pipeline, tensor, length)                                             \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_INT8_PICO, 1,                \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_uint8_ch1(pipeline, tensor, length)                                            \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_uint16_ch1(pipeline, tensor, length)                                           \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT16_PICO, 1,              \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_int16_ch1(pipeline, tensor, length)                                            \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_INT16_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_int32_ch1(pipeline, tensor, length)                                            \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_float32_ch1(pipeline, tensor, length)                                          \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 1,             \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_float64_ch1(pipeline, tensor, length)                                          \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT64_PICO, 1,             \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_scalar_float32_ch1_placeholder(pipeline, tensor, length)                             \
    {                                                                                                     \
        int32_t shape[1] = {length};                                                                      \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 1,            \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                    \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                   \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, true, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                       \
    }

#define CreateTensor_scalar_int32_ch1_placeholder(pipeline, tensor, length)                               \
    {                                                                                                     \
        int32_t shape[1] = {length};                                                                      \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO, 1,              \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                    \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                   \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, true, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                       \
    }

#define CreateTensor_scalar_uint8_ch1_placeholder(pipeline, tensor, length)                               \
    {                                                                                                     \
        int32_t shape[1] = {length};                                                                      \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 1,              \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                    \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                   \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, true, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                       \
    }

#define CreateTensor_gltf(pipeline, tensor, as_placeholder)                                                    \
    {                                                                                                          \
        XrSecureMrTensorCreateInfoGltfPICO createInfo{XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_GLTF_PICO, nullptr, \
                                                      as_placeholder, 0, nullptr};                             \
        CreateTensor(pipeline, tensor, createInfo);                                                            \
    }

#define CreateTensor_point_float32_ch2(pipeline, tensor, numPoints)                                        \
    {                                                                                                      \
        int32_t shape[1] = {numPoints};                                                                    \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 2,             \
                                                XR_SECURE_MR_TENSOR_TYPE_POINT_PICO};                      \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

#define CreateTensor_color_uint8_ch4(pipeline, tensor, numColors)                                          \
    {                                                                                                      \
        int32_t shape[1] = {numColors};                                                                    \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 4,               \
                                                XR_SECURE_MR_TENSOR_TYPE_COLOR_PICO};                      \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateTensor(pipeline, tensor, createInfo);                                                        \
    }

    ///////////////////////////////////////////
    ////////////// Global Tensor //////////////
    ///////////////////////////////////////////

#define CreateGlobalTensor(framework, tensor, createInfo)                                                       \
    {                                                                                                           \
        CHECK_XRCMD(xrCreateSecureMrTensorPICO(                                                                 \
                framework, reinterpret_cast<XrSecureMrTensorCreateInfoBaseHeaderPICO*>(&createInfo), &tensor)); \
        std::string logPrefix = std::string("[Tensor] ") + TO_STRING(tensor) + std::string(", ID = ");          \
        std::string msg = logPrefix + toHexString(reinterpret_cast<unsigned long long>(tensor));                \
        PLOGI(msg.c_str());                                                                                     \
    }

#define CreateGlobalTensor_scalar_float32_ch1(framework, tensor, length)                                   \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO, 1,             \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateGlobalTensor(framework, tensor, createInfo);                                                 \
    }

#define CreateGlobalTensor_scalar_int32_ch1(framework, tensor, length)                                     \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateGlobalTensor(framework, tensor, createInfo);                                                 \
    }

#define CreateGlobalTensor_scalar_uint8_ch1(framework, tensor, length)                                     \
    {                                                                                                      \
        int32_t shape[1] = {length};                                                                       \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 1,               \
                                                XR_SECURE_MR_TENSOR_TYPE_SCALAR_PICO};                     \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 1, shape, &tensorFormat}; \
        CreateGlobalTensor(framework, tensor, createInfo);                                                 \
    }

#define CreateGlobalTensor_mat_uint8_ch3(pipeline, tensor, width, height)                                  \
    {                                                                                                      \
        int32_t shape[2] = {width, height};                                                                \
        XrSecureMrTensorFormatPICO tensorFormat{XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO, 3,               \
                                                XR_SECURE_MR_TENSOR_TYPE_MAT_PICO};                        \
        XrSecureMrTensorCreateInfoShapePICO createInfo{                                                    \
                XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO, nullptr, false, 2, shape, &tensorFormat}; \
        CreateGlobalTensor(pipeline, tensor, createInfo);                                                  \
    }

    ///////////////////////////////////
    ////////////// Graph //////////////
    ///////////////////////////////////

#define SetInput(pipeline, op, name, tensor) \
    CHECK_XRCMD(xrSetSecureMrOperatorOperandByNamePICO(pipeline, op, tensor, name));

#define SetOutput(pipeline, op, name, tensor) \
    CHECK_XRCMD(xrSetSecureMrOperatorResultByNamePICO(pipeline, op, tensor, name));

#define SetInputById(pipeline, op, id, tensor) \
    CHECK_XRCMD(xrSetSecureMrOperatorOperandByIndexPICO(pipeline, op, tensor, id));

#define SetOutputById(pipeline, op, id, tensor) \
    CHECK_XRCMD(xrSetSecureMrOperatorResultByIndexPICO(pipeline, op, tensor, id));

    /////////////////////////////////////////
    ////////////// Set Content //////////////
    /////////////////////////////////////////

    template <typename T>
    uint32_t GetMemorySize(const T& obj) {
        return static_cast<uint32_t>(sizeof(obj));
    }

    template <>
    uint32_t GetMemorySize<char*>(char* const& obj) {
        return static_cast<uint32_t>(strlen(obj));
    }
    template <>
    uint32_t GetMemorySize<const char*>(const char* const& obj) {
        return static_cast<uint32_t>(strlen(obj));
    }

    template <typename T>
    uint32_t GetMemorySize(const T& obj, size_t length) {
        return static_cast<uint32_t>(sizeof(T) * length);
    }

#define SetTensor(pipeline, op, obj)                                                                               \
    {                                                                                                              \
        XrSecureMrTensorBufferPICO buffer{XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, GetMemorySize(obj), obj}; \
        CHECK_XRCMD(xrResetSecureMrPipelineTensorPICO(pipeline, op, &buffer));                                     \
    }

#define SetTensorWithLength(pipeline, op, obj, length)                                                               \
    {                                                                                                                \
        XrSecureMrTensorBufferPICO buffer{XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, GetMemorySize(obj, length), \
                                          obj};                                                                      \
        CHECK_XRCMD(xrResetSecureMrPipelineTensorPICO(pipeline, op, &buffer));                                       \
    }

#define SetIOPair(pair, local, global)                       \
    {                                                        \
        pair.type = XR_TYPE_SECURE_MR_PIPELINE_IO_PAIR_PICO; \
        pair.localPlaceHolderTensor = local;                 \
        pair.globalTensor = global;                          \
    }

}  // namespace SimpleSecureMr
// NOLINTEND
