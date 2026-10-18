/*
 * Copyright 2024 - 2024 PICO. All rights reserved.
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
#include "SkyBox.h"
#include "imgui_internal.h"
#include "GuiWindow.h"
#include "GuiPlane.h"
#include "CartesianBranch.h"
#include "RandomGenerator.h"
#include "Mesh.h"
#include "GltfModel.h"

using namespace PVRSampleFW;

class BasicDemo : public AndroidOpenXrProgram {
public:
    explicit BasicDemo(const std::shared_ptr<PVRSampleFW::Configurations> &appConfigParam)
        : AndroidOpenXrProgram(appConfigParam) {
    }

    BasicDemo() : AndroidOpenXrProgram() {
    }

    virtual ~BasicDemo() = default;

    bool CustomizedAppPostInit() override {
        AndroidOpenXrProgram::CustomizedAppPostInit();
        AddCubes();
        AddCartesianBranch();
        AddGuiPlane();
        AddSkybox();
        AddSimpleMesh();
        SetupGltfModel();
        return true;
    }

    bool CustomizedXrInputHandlerSetup() override {
        AndroidOpenXrProgram::CustomizedXrInputHandlerSetup();

        // register the input callback to the openxr wrapper.
        auto handleInputFunc = [](class BasicOpenXrWrapper *openxr, const PVRSampleFW::XrFrameIn &frameIn) {
            auto pOpenXrAppWrapper = dynamic_cast<BasicDemo *>(openxr);
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
                        auto xrSession = pOpenXrAppWrapper->GetXrSession();
                        CHECK_XRCMD(xrApplyHapticFeedback(xrSession, &hapticActionInfo,
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

        // for a async build type gltf model check and load to gpu
        if (gltf_model_obj_ != nullptr) {
            if (!gltf_model_obj_->IsValid() && nullptr != check_and_load_to_gpu_) {
                check_and_load_to_gpu_();
            }
        }
        return true;
    }

    /// Override the CustomizedRender function render what you want besides the scene paradigm
    bool CustomizedRender() override {
        /// TODO: implement you own render logic, and return true
        /// if you don't, only scenes will be rendered
        return true;
    }

    /// Override the CustomizedExtensionAndFeaturesInit function to register features those you want to activate
    void CustomizedExtensionAndFeaturesInit() override {
        AndroidOpenXrProgram::CustomizedExtensionAndFeaturesInit();
        // TODO：non-plugin extensions

        // TODO：setup your customized feature plugins here
        // 1. extension features
        // 2. the others (Not yet)
    }

    std::string GetApplicationName() override {
        return "BasicDemo";
    }

private:
    void updateControllers() {
        Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CONTROLLER);
        auto Objects = scene.GetAllObjects();
        for (int i = 0; i < Side::COUNT; i++) {
            auto hand = scene.GetObject(controller_ids_[i]);
            auto ray = scene.GetObject(aim_ids_[i]);
            if (current_frame_in_.controller_actives[i]) {
                XrPosef handPose = current_frame_in_.controller_poses[i];
                hand->SetPose(handPose);

                auto scale = input_.controller_scales[i] * 0.1f;
                XrVector3f handScale = {scale, scale, scale};
                hand->SetScale(handScale);

                auto rayPose = current_frame_in_.controller_aim_poses[i];
                ray->SetPose(rayPose);
            }
        }
    }

    void SetControllerScale(int hand, float scale) {
        input_.controller_scales[hand] = scale;
    }

    void AddCubes() {
        // Add hand cubes at the scene of controller at index 0, 1
        Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CONTROLLER);
        // Add hand cubes
        for (int i = 0; i < Side::COUNT; i++) {
            XrPosef handPose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            float scale = 0.1f;
            XrVector3f handScale = {scale, scale, scale};
            auto cubeHand = std::make_shared<PVRSampleFW::Cube>(handPose, handScale);
            controller_ids_[i] = scene.AddObject(cubeHand);
            PLOGI("Add hand cube at id %d", controller_ids_[i]);
        }

        // Add an initial cube
        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
        XrPosef frontCubeLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, -2.0f}};
        XrVector3f frontCubeScale = {0.4f, 0.2f, 0.2f};
        auto cubeFront = std::make_shared<PVRSampleFW::Cube>(frontCubeLocation, frontCubeScale);
        cubeFront->EnableWireframe(true);
        customScene.AddObject(cubeFront);
    }

    void AddCartesianBranch() {
        // Add hand aim cartesianBranches at the scene of controller at index 2, 3
        Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CONTROLLER);
        // Add hand cartesianBranches
        for (int i = 0; i < Side::COUNT; i++) {
            XrPosef handPose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            float scale = 0.1f;
            XrVector3f handScale = {scale, scale, scale};
            auto cartesianBranch = std::make_shared<PVRSampleFW::CartesianBranch>(handPose, handScale);
            aim_ids_[i] = scene.AddObject(cartesianBranch);
            PLOGI("Add hand cartesianBranch at id %d", aim_ids_[i]);
        }

        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
        XrPosef frontLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.5f, 0.5f, -1.0f}};
        XrVector3f frontScale = {0.2f, 0.2f, 0.2f};
        auto cartesianBranch = std::make_shared<PVRSampleFW::CartesianBranch>(frontLocation, frontScale);
        customScene.AddObject(cartesianBranch);
    }

    void AddGuiPlane() {
        GuiWindow::Builder builder;
        std::shared_ptr<GuiWindow> window =
                builder.SetTitle("Hello Window")
                        .SetSize(800, 600)
                        .SetBgColor(0.3f, 0.2f, 0.4f, 0.6f)
                        .SetText(
                                "Hello, this is an ImGui example, which contains a text description and two buttons."
                                "You can press the [Add] button to add a cube to the current scene,"
                                "or press [Clear] to clear all added cubes in the scene.")
                        .SetFontSize(24)
                        .SetTextColor(0.1f, 0.8f, 0.1f, 1.0f)
                        .Build();
        // buttons and checkbox in the window
        window->AddButton("Add", 30, 260, [&]() {
            PLOGD("Add a Cube");
            Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
            float x = random_generator_.generateNormalInRange(-1.0f, 1.0f);
            float y = random_generator_.generateNormalInRange(-0.5f, 0.5f);
            float z = random_generator_.generateNormalInRange(-3.0f, -8.0f);
            XrPosef frontCubeLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {x, y, z}};
            XrVector3f frontCubeScale = {0.2f, 0.2f, 0.2f};
            auto cubeFront = std::make_shared<PVRSampleFW::Cube>(frontCubeLocation, frontCubeScale);
            /*
            // Sample 1: add a text label to the object
            cubeFront->AddTextLabel("Cube");

            // Sample 2: add a text label to the object with pose, extent and windowPixelSize
            // The pose and extent are used to calculate the pose of the text label.
            XrVector2f extent = {0.6f, 0.2f};
            XrPosef guiPose = {
                    {0.0f, 0.0f, 0.0f, 1.0f},
                    {(frontCubeScale.x + extent.x) / 2.0f,
                     (frontCubeScale.y + extent.y) / 2.0f,
                     frontCubeScale.z / 2.0f + 0.01f}
            };
            XrVector2f windowPixelSize = {150, 50};
            cubeFront->AddTextLabel("Cube", guiPose, extent, windowPixelSize);

            // Sample 3: add a button label to the object.
            // The button label is a vector of ButtonPair, which contains the button name and
            // the button function. The maximum number of buttons is 8.
            std::vector<ButtonPair> buttons;
            buttons.push_back({"select0", [&]() {
                PLOGI("Select0 this Cube");
            }});
            buttons.push_back({"select1", [&]() {
                PLOGI("Select1 this Cube");
            }});
            buttons.push_back({"select2", [&]() {
                PLOGI("Select2 this Cube");
            }});
            buttons.push_back({"select3", [&]() {
                PLOGI("Select3 this Cube");
            }});
            cubeFront->AddButtonLabel(buttons);
            */

            scene.AddObject(cubeFront);
        });
        window->AddButton("Clear", 150, 260, [&]() {
            PLOGD("Clear the custom scene");
            Scene &scene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
            scene.Clear();
        });
        static bool bCheckBox = false;
        window->AddCheckBox("check box", 30, 200, &bCheckBox);

        // An example of a new text component
        static std::shared_ptr<GuiWindow> staticWindow = window;
        static int componentId = window->AddText("This is a text description.");
        window->SetComponentBgColor(componentId, 1.0f, 0.8f, 1.0f, 1.0f);
        window->SetComponentSize(componentId, 600, 80);
        window->SetComponentTextColor(componentId, 0.7f, 0.9f, 0.2f, 1.0f);
        window->SetComponentTextSize(componentId, 24);
        window->SetComponentPos(componentId, 30, 420);

        window->AddButton("Small Font", 300, 520, [&]() {
            PLOGD("Press <Small Font> button");
            static int buttonClickCount = 0;
            staticWindow->SetComponentTextSize(componentId, 16);
            staticWindow->UpdateText(componentId, "This is a text description, changed by <Small Font> button.");
        });
        window->AddButton("Large Font", 30, 520, [&]() {
            PLOGD("Press <Large Font> button");
            static int buttonClickCount = 0;
            staticWindow->SetComponentTextSize(componentId, 40);
            staticWindow->UpdateText(componentId, "This is a text description, changed by <Large Font> button.");
        });

        Scene &guiScene = scenes_.at(SAMPLE_SCENE_TYPE_GUI);
        XrPosef guiPose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, -3.0f}};
        XrVector3f guiScale = {2.0f, 1.5f, 1.0f};
        auto guiPlane = std::make_shared<GuiPlane>(guiPose, guiScale, window);
        guiScene.AddObject(guiPlane);
    }

    void AddSkybox() {
        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_ENVIRONMENT);
        auto skybox = std::make_shared<PVRSampleFW::SkyBox>();

        // load the skybox texture data from the file
        // right
        auto rightBuffer = LoadFileFromAsset("skybox/BrownStudio2k/px.png");
        skybox->SetRightTexture(rightBuffer);
        // left
        auto leftBuffer = LoadFileFromAsset("skybox/BrownStudio2k/nx.png");
        skybox->SetLeftTexture(leftBuffer);
        // top
        auto topBuffer = LoadFileFromAsset("skybox/BrownStudio2k/py.png");
        skybox->SetTopTexture(topBuffer);
        // bottom
        auto bottomBuffer = LoadFileFromAsset("skybox/BrownStudio2k/ny.png");
        skybox->SetBottomTexture(bottomBuffer);
        // front
        auto frontBuffer = LoadFileFromAsset("skybox/BrownStudio2k/pz.png");
        skybox->SetFrontTexture(frontBuffer);
        // back
        auto backBuffer = LoadFileFromAsset("skybox/BrownStudio2k/nz.png");
        skybox->SetBackTexture(backBuffer);

        skybox->GLLoadCubeMapTexture();
        customScene.AddObject(skybox);
    }

    void AddSimpleMesh() {
        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
        // pose and scale
        XrPosef meshLocation = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, -1.0f, -1.0f}};
        XrVector3f meshScale = {0.5f, 0.5f, 0.5f};
        // Add a simple mesh
        auto mesh = std::make_shared<PVRSampleFW::Mesh>(meshLocation, meshScale);
        // describe a corner mesh
        // vertices
        std::vector<XrVector3f> vertices = {
                RTB, RBB, RBF, RTB, RBF, RTF,  // right side
                LBB, LBF, RBF, LBB, RBF, RBB,  // bottom side
                LBB, RBB, RTB, LBB, RTB, LTB,  // back side
        };
        // indices
        std::vector<uint32_t> indices = {
                0,  1,  2,  3,  4,  5,   // right side
                6,  7,  8,  9,  10, 11,  // bottom side
                12, 13, 14, 15, 16, 17,  // back side
        };
        // color
        XrColor4f colorRgb = {XrColorGreen.r, XrColorGreen.g, XrColorGreen.b, 1.0f};
        mesh->BuildObject(vertices, indices, colorRgb);

        /*
        std::vector<MeshVertex> vertices2 = {
                {RTB, DarkRed}, {RBB, DarkRed}, {RBF, DarkRed}, {RTB, DarkRed}, {RBF, DarkRed}, {RTF, DarkRed}, // right side
                {LBB, Blue}, {LBF, Blue}, {RBF, Blue}, {LBB, Blue}, {RBF, Blue}, {RBB, Blue}, // bottom side
                {LBB, DarkGreen}, {RBB, DarkGreen}, {RTB, DarkGreen}, {LBB, DarkGreen}, {RTB, DarkGreen}, {LTB, DarkGreen}, // back side
        };
        mesh->BuildObject(vertices2, indices);
        */

        mesh->SetMovable(true);
        customScene.AddObject(mesh);
    }

    void SetupGltfModel() {
        Scene &customScene = scenes_.at(SAMPLE_SCENE_TYPE_CUSTOM);
        // pose and scale
        XrPosef meshLocation = {{0.0f, -0.259f, 0.0f, 0.966f}, {1.0f, 1.0f, -1.0f}};
        XrVector3f meshScale = {0.1f, 0.1f, 0.1f};
        gltf_model_obj_ = std::make_shared<PVRSampleFW::GltfModel>(meshLocation, meshScale);
        // load gltf model from asset
        GLTFModelHandle gltfModel;
        GLTFModelInstanceHandle gltfModelInstance;

        auto make_model_builder_ = [this]() -> Gltf::ModelBuilder {
            auto modelData = this->LoadFileFromAsset("gltfModel/MetalRoughSpheres.glb");
            return Gltf::ModelBuilder(LoadGLTF(modelData));
        };

        gltf_model_obj_->SetBuilderFunction(make_model_builder_, true);
        check_and_load_to_gpu_ = gltf_model_obj_->BuildObject(graphics_plugin_->GetPbrResources());
        customScene.AddObject(gltf_model_obj_);
    }

private:
    RandomGenerator random_generator_;
    int64_t controller_ids_[Side::COUNT] = {-1, -1};
    int64_t aim_ids_[Side::COUNT] = {-1, -1};
    std::shared_ptr<GltfModel> gltf_model_obj_;
    std::function<void()> check_and_load_to_gpu_;
};

void android_main(struct android_app *app) {
    PLOGI("BasicDemo android_main()");
    auto config = std::make_shared<Configurations>();
    /// you can set customized config here
    /*config->environment_blend_mode = "AlphaBlend";*/
    auto program = std::make_shared<BasicDemo>(config);
    program->Run(app);
}
