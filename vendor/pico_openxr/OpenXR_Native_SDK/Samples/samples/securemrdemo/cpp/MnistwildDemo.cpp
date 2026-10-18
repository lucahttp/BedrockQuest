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

#include "AndroidOpenXrProgram.h"
#include "CheckUtils.h"
#include "Cube.h"
#include "SecureMrProgram.h"

using namespace PVRSampleFW;

class Mnist : public AndroidOpenXrProgram {
public:
    explicit Mnist(const std::shared_ptr<PVRSampleFW::Configurations> &appConfigParam)
        : AndroidOpenXrProgram(appConfigParam) {
    }

    Mnist() : AndroidOpenXrProgram() {
    }

    virtual ~Mnist() = default;

    bool CustomizedAppPostInit() override {
        // TODO：Three cubes are initially added for you, two of which are used to represent controllers,
        // and the other one is placed in the front.
        AddCubes();
        return true;
    }

    bool CustomizedSessionInit() override {
        auto xr_instance = this->GetXrInstance();
        auto xr_session = this->GetXrSession();
        securemr_program_ = std::make_shared<SecureMrProgram>(xr_instance, xr_session, this);
        securemr_program_->CreateFramework();
        securemr_program_->CreatePipeline();
        securemr_program_->CreateRenderer();
        return true;
    }

    bool CustomizedXrInputHandlerSetup() override {
        AndroidOpenXrProgram::CustomizedXrInputHandlerSetup();

        // register the input callback to the openxr wrapper.
        auto handleInputFunc = [](class BasicOpenXrWrapper *openxr, const PVRSampleFW::XrFrameIn &frameIn) {
            auto pOpenXrAppWrapper = dynamic_cast<Mnist *>(openxr);
            for (int hand = 0; hand < Side::COUNT; hand++) {
                if (frameIn.controller_actives[hand]) {
                    auto triggerValue = frameIn.controller_trigger_value[hand];
                    // Scale the rendered hand by 1.0f (open) to 0.5f (fully squeezed).
                    auto input = pOpenXrAppWrapper->GetInputState();
                    auto scale = 1.0f - 0.5f * triggerValue;
                    pOpenXrAppWrapper->SetControllerScale(hand, scale);

                    // Apply a vibration feedback to the controller
                    if (triggerValue > 0.9f) {
                        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
                        vibration.amplitude = 0.5;
                        vibration.duration = XR_MIN_HAPTIC_DURATION;
                        vibration.frequency = XR_FREQUENCY_UNSPECIFIED;

                        XrHapticActionInfo hapticActionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
                        hapticActionInfo.action = input.vibrate_action;
                        hapticActionInfo.subactionPath = input.controller_subaction_paths[hand];
                        auto xr_session = pOpenXrAppWrapper->GetXrSession();
                        CHECK_XRCMD(xrApplyHapticFeedback(xr_session, &hapticActionInfo,
                                                          reinterpret_cast<XrHapticBaseHeader *>(&vibration)));
                    }
                }
            }
        };
        RegisterHandleInputFunc(handleInputFunc);
        return true;
    }

    bool CustomizedPreRenderFrame() override {
        updateControllers();
        return true;
    }

    /// Override the CustomizedRender function render what you want besides the scene paradigm
    bool CustomizedRender() override {
        /// TODO: implement you own render logic, and return true
        /// if you don't, only scenes will be rendered

        auto now = std::chrono::steady_clock::now();
        if (now - last_pipeline_run_ >= pipeline_interval_) {
            securemr_program_->RunPipeline();
            last_pipeline_run_ = now;
        }

        securemr_program_->RenderFrame();
        return true;
    }

    /// Override the CustomizedExtensionAndFeaturesInit function to register features those you want to activate
    void CustomizedExtensionAndFeaturesInit() override {
        AndroidOpenXrProgram::CustomizedExtensionAndFeaturesInit();
        // TODO：non-plugin extensions

        // TODO：setup your customized feature plugins here
        // 1. extension features
        // 2. the others (Not yet)
        non_plugin_extensions_.push_back(XR_PICO_SECURE_MIXED_REALITY_EXTENSION_NAME);
    }

private:
    std::string GetApplicationName() override {
        return "Mnist";
    }

    void updateControllers() {
        Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CONTROLLER);
        auto Objects = scene.GetAllObjects();
        for (int i = 0; i < Side::COUNT; i++) {
            auto hand = scene.GetObject(controller_ids_[i]);
            if (current_frame_in_.controller_actives[i]) {
                XrPosef handPose = current_frame_in_.controller_poses[i];
                hand->SetPose(handPose);

                auto scale = input_.controller_scales[i] * 0.1f;
                XrVector3f handScale = {scale, scale, scale};
                hand->SetScale(handScale);
            }
        }
    }

    void SetControllerScale(int hand, float scale) {
        input_.controller_scales[hand] = scale;
    }

    void AddCubes() {
        Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CONTROLLER);
        // Add hand cubes
        for (int i = 0; i < Side::COUNT; i++) {
            XrPosef handPose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            float scale = 0.1f;
            XrVector3f handScale = {scale, scale, scale};
            auto cubeHand = std::make_shared<PVRSampleFW::Cube>(handPose, handScale);
            controller_ids_[i] = scene.AddObject(cubeHand);
        }

        // Add an initial cube
        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);

        XrPosef leftLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.7f, 0.3f, -2.0f}};
        XrVector3f cubeScale = {0.002f, 0.002f, 0.002f};
        auto left_cube = std::make_shared<PVRSampleFW::Cube>(leftLocation, cubeScale);
        left_cube->AddTextLabel("number");
        customScene.AddObject(left_cube);

        XrPosef rightLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.3f, 0.3f, -2.0f}};
        auto right_cube = std::make_shared<PVRSampleFW::Cube>(rightLocation, cubeScale);
        right_cube->AddTextLabel("score");
        customScene.AddObject(right_cube);

        XrPosef topLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.3f, 1.3f, -2.0f}};
        auto top_cube = std::make_shared<PVRSampleFW::Cube>(topLocation, cubeScale);
        top_cube->AddTextLabel("input_image");
        customScene.AddObject(top_cube);
    }

private:
    int64_t controller_ids_[Side::COUNT] = {-1, -1};

    std::shared_ptr<SecureMrProgram> securemr_program_ = nullptr;
    std::chrono::time_point<std::chrono::steady_clock> last_pipeline_run_ = std::chrono::steady_clock::now();
    const std::chrono::milliseconds pipeline_interval_ = std::chrono::milliseconds(33);  // 30 FPS
};

/// TODO: you can also implement your own android_main function as entry
void android_main(struct android_app *app) {
    PLOGI("Mnist android_main()");
    auto config = std::make_shared<Configurations>();
    /// you can set customized config here
    config->environment_blend_mode = "AlphaBlend";
    auto program = std::make_shared<Mnist>(config);
    program->Run(app);
}
