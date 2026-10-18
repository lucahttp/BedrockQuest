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
#include "AndroidOpenXrProgram.h"
#include "SimpleSecureMr.h"
#include "util/LogUtils.h"

using namespace SimpleSecureMr;

#define MODEL_INPUT 28
#define MODEL_FILE "model.serialized.bin"

namespace {

#define GET_INTSANCE_PROC_ADDR(name, ptr) \
    CHECK_XRCMD(xrGetInstanceProcAddr(xr_instance_, name, reinterpret_cast<PFN_xrVoidFunction*>(&ptr)));

    class SecureMrProgram {
    public:
        SecureMrProgram(const XrInstance& instance, const XrSession& session,
                        PVRSampleFW::AndroidOpenXrProgram* program)
            : xr_instance_(instance), xr_session_(session), xr_program_(program) {
            getInstanceProcAddr();
        }

        ~SecureMrProgram() {
            // NOTE: If you find your code crash here, please comment out the following code
            // to find out the root cause.
            PLOGI("Release SecureMrFramework ...");
            if (pipeline_1_ != XR_NULL_HANDLE) {
                CHECK_XRCMD(xrDestroySecureMrPipelinePICO(pipeline_1_));
            }
            CHECK_XRCMD(xrDestroySecureMrFrameworkPICO(framework_));
            PLOGI("Release SecureMrFramework done.");
        }

        void CreateFramework() {
            PLOGI("CreateFramework ...");
            XrSecureMrFrameworkCreateInfoPICO createInfo{XR_TYPE_SECURE_MR_FRAMEWORK_CREATE_INFO_PICO, nullptr,
                                                         image_width_, image_height_};
            CHECK_XRCMD(xrCreateSecureMrFrameworkPICO(xr_session_, &createInfo, &framework_));
            PrintOperatorInfo();
            PLOGI("CreateFramework done");
        }

        // NOLINTBEGIN
        void CreateMnistModel(XrSecureMrPipelinePICO& pipeline, XrSecureMrPipelineTensorPICO& inputTensor,
                              XrSecureMrPipelineTensorPICO& predClass, XrSecureMrPipelineTensorPICO& predScore,
                              bool withDummyInput) {
            XrSecureMrOperatorPICO modelOp;
            XrSecureMrOperatorModelPICO modelOpInfo;
            {
                XrSecureMrOperatorIOMapPICO modelInput;
                modelInput.type = XR_TYPE_SECURE_MR_OPERATOR_IO_MAP_PICO;
                modelInput.next = nullptr;
                modelInput.encodingType = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
                strncpy(modelInput.nodeName, "input", XR_MAX_OPERATOR_NODE_NAME_PICO);
                strncpy(modelInput.operatorIOName, "input", XR_MAX_OPERATOR_NODE_NAME_PICO);

                XrSecureMrOperatorIOMapPICO modelOutput[2];
                modelOutput[0].type = XR_TYPE_SECURE_MR_OPERATOR_IO_MAP_PICO;
                modelOutput[0].next = nullptr;
                modelOutput[0].encodingType = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
                strncpy(modelOutput[0].nodeName, "index", XR_MAX_OPERATOR_NODE_NAME_PICO);
                strncpy(modelOutput[0].operatorIOName, "index", XR_MAX_OPERATOR_NODE_NAME_PICO);

                modelOutput[1].type = XR_TYPE_SECURE_MR_OPERATOR_IO_MAP_PICO;
                modelOutput[1].next = nullptr;
                modelOutput[1].encodingType = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
                strncpy(modelOutput[1].nodeName, "score", XR_MAX_OPERATOR_NODE_NAME_PICO);
                strncpy(modelOutput[1].operatorIOName, "score", XR_MAX_OPERATOR_NODE_NAME_PICO);

                std::vector<uint8_t> uint8_vector = xr_program_->LoadFileFromAsset(MODEL_FILE);
                std::vector<char> modelBuffer(uint8_vector.size());
                std::transform(uint8_vector.begin(), uint8_vector.end(), modelBuffer.begin(),
                               [](uint8_t val) { return static_cast<char>(val); });
                if (modelBuffer.size() > 0) {
                    modelOpInfo.type = XR_TYPE_SECURE_MR_OPERATOR_MODEL_PICO;
                    modelOpInfo.next = nullptr;
                    modelOpInfo.modelInputs = &modelInput;
                    modelOpInfo.modelInputCount = 1;
                    modelOpInfo.modelOutputs = modelOutput;
                    modelOpInfo.modelOutputCount = 2;
                    modelOpInfo.bufferSize = static_cast<int32_t>(modelBuffer.size());
                    modelOpInfo.buffer = modelBuffer.data();
                    modelOpInfo.modelType = XR_SECURE_MR_MODEL_TYPE_QNN_CONTEXT_BINARY_PICO;
                    modelOpInfo.modelName = "model";
                    PLOGI("Load model data from assets.");
                } else {
                    PLOGE("Failed to load model data from asset.");
                }
            }
            XrSecureMrOperatorCreateInfoPICO modelCreateInfo;
            modelCreateInfo.type = XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO;
            modelCreateInfo.next = nullptr;
            modelCreateInfo.operatorInfo = reinterpret_cast<XrSecureMrOperatorBaseHeaderPICO*>(&modelOpInfo);
            modelCreateInfo.operatorType = XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO;
            CHECK_XRCMD(xrCreateSecureMrOperatorPICO(pipeline, &modelCreateInfo, &modelOp));

            CreateTensor_mat_float32_ch1(pipeline, inputTensor, MODEL_INPUT, MODEL_INPUT)
            CreateTensor_scalar_float32_ch1_placeholder(pipeline, predScore, 1)
            CreateTensor_scalar_float32_ch1_placeholder(pipeline, predClass, 1)

            if (withDummyInput) {
                int totalSize = MODEL_INPUT * MODEL_INPUT * 1;
                std::vector<float> dummyData(totalSize);
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dis(0.0f, 1.0f);
                for (int i = 0; i < totalSize; ++i) {
                    dummyData[i] = dis(gen);
                }
                XrSecureMrTensorBufferPICO dummyBuffer = {XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr,
                                                          static_cast<uint32_t>(dummyData.size()), dummyData.data()};
                CHECK_XRCMD(xrResetSecureMrPipelineTensorPICO(pipeline, inputTensor, &dummyBuffer));
            }
            SetInput(pipeline, modelOp, "input", inputTensor);
            SetOutput(pipeline, modelOp, "score", predScore);
            SetOutput(pipeline, modelOp, "index", predClass);
        }

        void CropRegionToPoint(float x1, float y1, float x2, float y2, float height, float width,
                               XrSecureMrPipelineTensorPICO& src, XrSecureMrPipelineTensorPICO& dst,
                               XrSecureMrPipelinePICO& pipeline) {
            CreateTensor_point_float32_ch2(pipeline, src, 3);
            CreateTensor_point_float32_ch2(pipeline, dst, 3);
            float srcData[] = {x1, y1, x2, y1, x2, y2};
            float dstData[] = {0, 0, width, 0, width, height};
            XrSecureMrTensorBufferPICO srcBuffer = {XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, sizeof(srcData),
                                                    srcData};
            XrSecureMrTensorBufferPICO dstBuffer = {XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, sizeof(dstData),
                                                    dstData};
            CHECK_XRCMD(xrResetSecureMrPipelineTensorPICO(pipeline, src, &srcBuffer));
            CHECK_XRCMD(xrResetSecureMrPipelineTensorPICO(pipeline, dst, &dstBuffer));
        }
        // NOLINTEND

        void CreatePipeline() {
            PLOGI("CreatePipeline ...");
            CreatePipe(framework_, pipeline_1_);

            ///////// Operators /////////
            XrSecureMrOperatorPICO vstOp, getAffineOp, applyAffineOp, rgbToGrayOp, uint8ToFloat32Op, normalizeOp;
            CreateVstAccessOp(pipeline_1_, vstOp);
            CreateGetAffineOp(pipeline_1_, getAffineOp);
            CreateApplyAffineOp(pipeline_1_, applyAffineOp);
            CreateConvertColorOp(pipeline_1_, rgbToGrayOp, 7);
            CreateAssignmentOp(pipeline_1_, uint8ToFloat32Op);
            CreateArithmeticOp(pipeline_1_, normalizeOp, "{0} / 255.0");

            ///////// Tensors  /////////
            XrSecureMrPipelineTensorPICO rawRgb, cropGray, cropGrayFloat;
            CreateTensor_mat_uint8_ch3(pipeline_1_, rawRgb, image_height_, image_width_);
            CreateTensor_mat_uint8_ch1(pipeline_1_, cropGray, crop_width_, crop_height_);
            CreateTensor_mat_float32_ch1(pipeline_1_, cropGrayFloat, crop_width_, crop_height_);
            CreateTensor_mat_uint8_ch3_placeholder(pipeline_1_, crop_rgb_write_, crop_width_, crop_height_);

            XrSecureMrPipelineTensorPICO srcPoints, dstPoints, affineMat;
            CropRegionToPoint(crop_x1_, crop_y1_, crop_x2_, crop_y2_, crop_width_, crop_height_, srcPoints, dstPoints,
                              pipeline_1_);
            CreateTensor_mat_float32_ch1(pipeline_1_, affineMat, 2, 3);

            XrSecureMrPipelineTensorPICO inputTensor;
            CreateMnistModel(pipeline_1_, inputTensor, pred_class_write_, pred_score_write_, false);

            CreateGlobalTensor_scalar_float32_ch1(framework_, pred_class_global_, 1);
            CreateGlobalTensor_scalar_float32_ch1(framework_, pred_score_global_, 1);
            CreateGlobalTensor_mat_uint8_ch3(framework_, crop_rgb_global_, crop_width_, crop_height_);

            ///////// Graph /////////
            SetOutput(pipeline_1_, vstOp, "left image", rawRgb);

            SetInput(pipeline_1_, getAffineOp, "src", srcPoints);
            SetInput(pipeline_1_, getAffineOp, "dst", dstPoints);
            SetOutput(pipeline_1_, getAffineOp, "result", affineMat);

            SetInput(pipeline_1_, applyAffineOp, "affine", affineMat);
            SetInput(pipeline_1_, applyAffineOp, "src image", rawRgb);
            SetOutput(pipeline_1_, applyAffineOp, "dst image", crop_rgb_write_);

            SetInput(pipeline_1_, rgbToGrayOp, "src", crop_rgb_write_);
            SetOutput(pipeline_1_, rgbToGrayOp, "dst", cropGray);

            SetInput(pipeline_1_, uint8ToFloat32Op, "src", cropGray);
            SetOutput(pipeline_1_, uint8ToFloat32Op, "dst", cropGrayFloat);

            SetInput(pipeline_1_, normalizeOp, "{0}", cropGrayFloat);
            SetOutputById(pipeline_1_, normalizeOp, 0, inputTensor);

            PLOGI("CreatePipeline done.");
        }

        /**
        * Run Pipeline for each call time.
        */
        void RunPipeline() {
            PLOGI("RunPipeline");

            XrSecureMrPipelineExecuteParameterPICO executeParams;
            XrSecureMrPipelineIOPairPICO pipelineIOPair[3];
            SetIOPair(pipelineIOPair[0], pred_class_write_, pred_class_global_);
            SetIOPair(pipelineIOPair[1], pred_score_write_, pred_score_global_);
            SetIOPair(pipelineIOPair[2], crop_rgb_write_, crop_rgb_global_);
            executeParams = XrSecureMrPipelineExecuteParameterPICO{XR_TYPE_SECURE_MR_PIPELINE_EXECUTE_PARAMETER_PICO,
                                                                   nullptr,
                                                                   XR_NULL_HANDLE,
                                                                   XR_NULL_HANDLE,
                                                                   3,
                                                                   pipelineIOPair};
            XrSecureMrPipelineRunPICO pipelineRun;
            CHECK_XRCMD(xrExecuteSecureMrPipelinePICO(pipeline_1_, &executeParams, &pipelineRun));
        }

        /**
        * Create Renderer pipeline
        */
        void CreateRenderer() {
            PLOGI("CreateRenderer");
            CreatePipe(framework_, pipeline_3_);

            ///////// Operators /////////
            XrSecureMrOperatorPICO renderTextOp, renderGltfOp, renderTextOp2, renderGltfOp2, renderGltfOp3;
            CreateRenderTextOp(pipeline_3_, renderTextOp);
            CreateRenderGltfOp(pipeline_3_, renderGltfOp);
            CreateRenderTextOp(pipeline_3_, renderTextOp2);
            CreateRenderGltfOp(pipeline_3_, renderGltfOp2);
            CreateRenderGltfOp(pipeline_3_, renderGltfOp3);
            XrSecureMrOperatorPICO loadTextureOp, updateGltfOp;
            CreateLoadTextureOp(pipeline_3_, loadTextureOp);
            CreateUpdateGltfOp_MaterialBaseColorTexture(pipeline_3_, updateGltfOp);

            ///////// Tensors  /////////
            XrSecureMrPipelineTensorPICO text, startPosition, colors, textureId, fontSize, poseMat;
            XrSecureMrPipelineTensorPICO text2, startPosition2, colors2, textureId2, fontSize2, poseMat2;
            CreateTensor_scalar_int8_ch1(pipeline_3_, text, 30);
            CreateTensor_point_float32_ch2(pipeline_3_, startPosition, 1);
            CreateTensor_color_uint8_ch4(pipeline_3_, colors, 2);
            CreateTensor_scalar_uint16_ch1(pipeline_3_, textureId, 1);
            CreateTensor_scalar_float32_ch1(pipeline_3_, fontSize, 1);
            CreateTensor_mat_float32_ch1(pipeline_3_, poseMat, 4, 4);
            CreateTensor_scalar_int8_ch1(pipeline_3_, text2, 30);
            CreateTensor_point_float32_ch2(pipeline_3_, startPosition2, 1);
            CreateTensor_color_uint8_ch4(pipeline_3_, colors2, 2);
            CreateTensor_scalar_uint16_ch1(pipeline_3_, textureId2, 1);
            CreateTensor_scalar_float32_ch1(pipeline_3_, fontSize2, 1);
            CreateTensor_mat_float32_ch1(pipeline_3_, poseMat2, 4, 4);
            CreateTensor_gltf(pipeline_3_, debug_gltf_placeholder_, true);
            CreateTensor_gltf(pipeline_3_, debug_gltf_placeholder_2_, true);
            CreateTensor_scalar_float32_ch1_placeholder(pipeline_3_, pred_class_read_, 1);
            CreateTensor_scalar_float32_ch1_placeholder(pipeline_3_, pred_score_read_, 1);
            CreateTensor_mat_uint8_ch3_placeholder(pipeline_3_, crop_rgb_read_, crop_width_, crop_height_);

            XrSecureMrPipelineTensorPICO gltfMaterialIndex, gltfTextureIndex, poseMat3;
            CreateTensor_scalar_uint16_ch1(pipeline_3_, gltfMaterialIndex, 1);
            CreateTensor_scalar_uint16_ch1(pipeline_3_, gltfTextureIndex, 1);
            CreateTensor_mat_float32_ch1(pipeline_3_, poseMat3, 4, 4);
            CreateTensor_gltf(pipeline_3_, debug_gltf_placeholder_3_, true);

            ///////// Graph /////////
            SetInput(pipeline_3_, renderTextOp, "text", pred_class_read_);
            SetInput(pipeline_3_, renderTextOp, "start", startPosition);
            SetInput(pipeline_3_, renderTextOp, "colors", colors);
            SetInput(pipeline_3_, renderTextOp, "texture ID", textureId);
            SetInput(pipeline_3_, renderTextOp, "font size", fontSize);
            SetInput(pipeline_3_, renderTextOp, "gltf", debug_gltf_placeholder_);

            SetInput(pipeline_3_, renderGltfOp, "gltf", debug_gltf_placeholder_);
            SetInput(pipeline_3_, renderGltfOp, "world pose", poseMat);

            SetInput(pipeline_3_, renderTextOp2, "text", pred_score_read_);
            SetInput(pipeline_3_, renderTextOp2, "start", startPosition2);
            SetInput(pipeline_3_, renderTextOp2, "colors", colors2);
            SetInput(pipeline_3_, renderTextOp2, "texture ID", textureId2);
            SetInput(pipeline_3_, renderTextOp2, "font size", fontSize2);
            SetInput(pipeline_3_, renderTextOp2, "gltf", debug_gltf_placeholder_2_);

            SetInput(pipeline_3_, renderGltfOp2, "gltf", debug_gltf_placeholder_2_);
            SetInput(pipeline_3_, renderGltfOp2, "world pose", poseMat2);

            SetInput(pipeline_3_, loadTextureOp, "rgb image", crop_rgb_read_);
            SetInput(pipeline_3_, loadTextureOp, "gltf", debug_gltf_placeholder_3_);
            SetOutput(pipeline_3_, loadTextureOp, "texture ID", gltfTextureIndex);

            SetInput(pipeline_3_, updateGltfOp, "gltf", debug_gltf_placeholder_3_);
            SetInput(pipeline_3_, updateGltfOp, "material ID", gltfMaterialIndex);
            SetInput(pipeline_3_, updateGltfOp, "value", gltfTextureIndex);

            SetInput(pipeline_3_, renderGltfOp3, "gltf", debug_gltf_placeholder_3_);
            SetInput(pipeline_3_, renderGltfOp3, "world pose", poseMat3);

            ///////// Set Content /////////
            char textValue[] = "Hello world!";
            SetTensor(pipeline_3_, text, textValue);

            float startValue[2] = {0.1f, 0.3f};
            SetTensorWithLength(pipeline_3_, startPosition, startValue, 2);

            uint8_t colorsValue[8] = {255, 255, 255, 255, 0, 0, 0, 255};
            SetTensorWithLength(pipeline_3_, colors, colorsValue, 8);

            int textureId_value = 0;
            SetTensor(pipeline_3_, textureId, &textureId_value);

            float fontSize_value = 144.0f;
            SetTensor(pipeline_3_, fontSize, &fontSize_value);

            float poseMatValue[16] = {0.5f, 0.0f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 0.5f, -1.5f, 0.0f, 0.0f, 0.0f, 1.0f};
            SetTensorWithLength(pipeline_3_, poseMat, poseMatValue, 16);

            SetTensor(pipeline_3_, text2, textValue);
            SetTensorWithLength(pipeline_3_, startPosition2, startValue, 2);
            SetTensorWithLength(pipeline_3_, colors2, colorsValue, 8);
            SetTensor(pipeline_3_, textureId2, &textureId_value);
            SetTensor(pipeline_3_, fontSize2, &fontSize_value);
            float poseMatValue2[16] = {0.5f, 0.0f, 0.0f, 0.5f,  0.0f, 0.5f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 0.5f, -1.5f, 0.0f, 0.0f, 0.0f, 1.0f};
            SetTensorWithLength(pipeline_3_, poseMat2, poseMatValue2, 16);

            float poseMatValue3[16] = {0.5f, 0.0f, 0.0f, 0.0f,  0.0f, 0.5f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.5f, -1.5f, 0.0f, 0.0f, 0.0f, 1.0f};
            SetTensorWithLength(pipeline_3_, poseMat3, poseMatValue3, 16);

            {
                XrSecureMrTensorCreateInfoGltfPICO createInfo{XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_GLTF_PICO, nullptr,
                                                              false, 0, nullptr};
                std::vector<uint8_t> uint8_vector = xr_program_->LoadFileFromAsset(debug_gltf_path_);
                std::vector<char> gltfData(uint8_vector.size());
                std::transform(uint8_vector.begin(), uint8_vector.end(), gltfData.begin(),
                               [](uint8_t val) { return static_cast<char>(val); });
                if (gltfData.size() > 0) {
                    createInfo.bufferSize = static_cast<uint32_t>(gltfData.size());
                    createInfo.buffer = gltfData.data();
                    PLOGI("Loaded GLTF data from file %s", debug_gltf_path_);
                } else {
                    PLOGE("Failed to load GLTF data from file.");
                }
                CHECK_XRCMD(xrCreateSecureMrTensorPICO(
                        framework_, reinterpret_cast<XrSecureMrTensorCreateInfoBaseHeaderPICO*>(&createInfo),
                        &debug_gltf_tensor_));
                CHECK_XRCMD(xrCreateSecureMrTensorPICO(
                        framework_, reinterpret_cast<XrSecureMrTensorCreateInfoBaseHeaderPICO*>(&createInfo),
                        &debug_gltf_tensor_2_));
                CHECK_XRCMD(xrCreateSecureMrTensorPICO(
                        framework_, reinterpret_cast<XrSecureMrTensorCreateInfoBaseHeaderPICO*>(&createInfo),
                        &debug_gltf_tensor_3_));
            }
        }

        /**
        * Render frame, called by OpenXR program RenderFrame.
        */
        void RenderFrame() {
            PLOGV("RenderFrame ...");

            {
                XrSecureMrPipelineExecuteParameterPICO executeParams;
                XrSecureMrPipelineIOPairPICO pipelineIOPair[6];
                SetIOPair(pipelineIOPair[0], debug_gltf_placeholder_, debug_gltf_tensor_);
                SetIOPair(pipelineIOPair[1], debug_gltf_placeholder_2_, debug_gltf_tensor_2_);
                SetIOPair(pipelineIOPair[2], debug_gltf_placeholder_3_, debug_gltf_tensor_3_);
                SetIOPair(pipelineIOPair[3], pred_class_read_, pred_class_global_);
                SetIOPair(pipelineIOPair[4], pred_score_read_, pred_score_global_);
                SetIOPair(pipelineIOPair[5], crop_rgb_read_, crop_rgb_global_);

                executeParams =
                        XrSecureMrPipelineExecuteParameterPICO{XR_TYPE_SECURE_MR_PIPELINE_EXECUTE_PARAMETER_PICO,
                                                               nullptr,
                                                               XR_NULL_HANDLE,
                                                               XR_NULL_HANDLE,
                                                               6,
                                                               pipelineIOPair};
                XrSecureMrPipelineRunPICO pipelineRun;
                CHECK_XRCMD(xrExecuteSecureMrPipelinePICO(pipeline_3_, &executeParams, &pipelineRun));
            }
        }

    protected:
        void getInstanceProcAddr() {
            PLOGI("getInstanceProcAddr start.");
            GET_INTSANCE_PROC_ADDR("xrCreateSecureMrFrameworkPICO", xrCreateSecureMrFrameworkPICO);
            GET_INTSANCE_PROC_ADDR("xrDestroySecureMrFrameworkPICO", xrDestroySecureMrFrameworkPICO);
            GET_INTSANCE_PROC_ADDR("xrCreateSecureMrPipelinePICO", xrCreateSecureMrPipelinePICO);
            GET_INTSANCE_PROC_ADDR("xrDestroySecureMrPipelinePICO", xrDestroySecureMrPipelinePICO);
            GET_INTSANCE_PROC_ADDR("xrCreateSecureMrOperatorPICO", xrCreateSecureMrOperatorPICO);
            GET_INTSANCE_PROC_ADDR("xrCreateSecureMrTensorPICO", xrCreateSecureMrTensorPICO);
            GET_INTSANCE_PROC_ADDR("xrCreateSecureMrPipelineTensorPICO", xrCreateSecureMrPipelineTensorPICO);
            GET_INTSANCE_PROC_ADDR("xrResetSecureMrTensorPICO", xrResetSecureMrTensorPICO);
            GET_INTSANCE_PROC_ADDR("xrResetSecureMrPipelineTensorPICO", xrResetSecureMrPipelineTensorPICO);
            GET_INTSANCE_PROC_ADDR("xrSetSecureMrOperatorOperandByNamePICO", xrSetSecureMrOperatorOperandByNamePICO);
            GET_INTSANCE_PROC_ADDR("xrSetSecureMrOperatorOperandByIndexPICO", xrSetSecureMrOperatorOperandByIndexPICO);
            GET_INTSANCE_PROC_ADDR("xrSetSecureMrOperatorResultByNamePICO", xrSetSecureMrOperatorResultByNamePICO);
            GET_INTSANCE_PROC_ADDR("xrSetSecureMrOperatorResultByIndexPICO", xrSetSecureMrOperatorResultByIndexPICO);
            GET_INTSANCE_PROC_ADDR("xrExecuteSecureMrPipelinePICO", xrExecuteSecureMrPipelinePICO);
            PLOGI("getInstanceProcAddr end.");
        }

    private:
        XrInstance xr_instance_;
        XrSession xr_session_;
        PVRSampleFW::AndroidOpenXrProgram* xr_program_;

        PFN_xrCreateSecureMrFrameworkPICO xrCreateSecureMrFrameworkPICO = nullptr;
        PFN_xrDestroySecureMrFrameworkPICO xrDestroySecureMrFrameworkPICO = nullptr;
        PFN_xrCreateSecureMrPipelinePICO xrCreateSecureMrPipelinePICO = nullptr;
        PFN_xrDestroySecureMrPipelinePICO xrDestroySecureMrPipelinePICO = nullptr;
        PFN_xrCreateSecureMrOperatorPICO xrCreateSecureMrOperatorPICO = nullptr;
        PFN_xrCreateSecureMrTensorPICO xrCreateSecureMrTensorPICO = nullptr;
        PFN_xrCreateSecureMrPipelineTensorPICO xrCreateSecureMrPipelineTensorPICO = nullptr;
        PFN_xrResetSecureMrTensorPICO xrResetSecureMrTensorPICO = nullptr;
        PFN_xrResetSecureMrPipelineTensorPICO xrResetSecureMrPipelineTensorPICO = nullptr;
        PFN_xrSetSecureMrOperatorOperandByNamePICO xrSetSecureMrOperatorOperandByNamePICO = nullptr;
        PFN_xrSetSecureMrOperatorOperandByIndexPICO xrSetSecureMrOperatorOperandByIndexPICO = nullptr;
        PFN_xrExecuteSecureMrPipelinePICO xrExecuteSecureMrPipelinePICO = nullptr;
        PFN_xrSetSecureMrOperatorResultByNamePICO xrSetSecureMrOperatorResultByNamePICO = nullptr;
        PFN_xrSetSecureMrOperatorResultByIndexPICO xrSetSecureMrOperatorResultByIndexPICO = nullptr;

        XrSecureMrFrameworkPICO framework_{XR_NULL_HANDLE};
        XrSecureMrPipelinePICO pipeline_1_{XR_NULL_HANDLE};
        XrSecureMrPipelinePICO pipeline_3_{XR_NULL_HANDLE};

        int image_width_ = 3248;   // Same as VST_IMAGE_WIDTH
        int image_height_ = 2464;  // Same as VST_IMAGE_HEIGHT
        int crop_x1_ = 1444;
        int crop_y1_ = 1332;
        int crop_x2_ = 2045;
        int crop_y2_ = 1933;
        int crop_width_ = MODEL_INPUT;
        int crop_height_ = MODEL_INPUT;

        const char* debug_gltf_path_ = "tv.gltf";
        XrSecureMrTensorPICO debug_gltf_tensor_;
        XrSecureMrTensorPICO debug_gltf_tensor_2_;
        XrSecureMrTensorPICO debug_gltf_tensor_3_;
        XrSecureMrPipelineTensorPICO debug_gltf_placeholder_;
        XrSecureMrPipelineTensorPICO debug_gltf_placeholder_2_;
        XrSecureMrPipelineTensorPICO debug_gltf_placeholder_3_;

        XrSecureMrTensorPICO pred_class_global_, pred_score_global_, crop_rgb_global_;
        XrSecureMrPipelineTensorPICO pred_class_write_, pred_score_write_, crop_rgb_write_;
        XrSecureMrPipelineTensorPICO pred_class_read_, pred_score_read_, crop_rgb_read_;
    };

}  // namespace
