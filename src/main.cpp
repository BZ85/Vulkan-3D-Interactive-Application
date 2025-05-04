/* Xinjie Zhu's final graphics application for chapter 1-4*/

#include "shared/VulkanApp.h"

#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "shared/LineCanvas.h"


// global variables
const vec3 kInitialCameraPos    = vec3(0.0f, 1.0f, -1.5f);
const vec3 kInitialCameraTarget = vec3(0.0f, 0.5f, 0.0f);
const vec3 kInitialCameraAngles = vec3(-18.5f, 180.0f, 0.0f);

CameraPositioner_MoveTo positionerMoveTo(kInitialCameraPos, kInitialCameraAngles);
vec3 cameraPos = kInitialCameraPos;
vec3 cameraAngles = kInitialCameraAngles;

//vec3 scaleVector = vec3(0.1f, 0.1f, 0.1f);
vec3 scaleVector    = vec3(1.0f, 1.0f, 1.0f);
vec3 rotationVector = vec3(-90.0f, 0.0f, 0.0f);
vec3 rotationVectorLastFrame = vec3(0.0f, 0.0f, 0.0f);
bool keepRotating   = false;

glm::quat objectOrientation = glm::quat(1, 0, 0, 0);

// ImGUI stuff
const char* cameraType          = "FirstPerson";
const char* comboBoxItems[]     = { "FirstPerson", "MoveTo" };
const char* currentComboBoxItem = cameraType;

LinearGraph fpsGraph("##fpsGraph", 2048);
LinearGraph sinGraph("##sinGraph", 2048);

void reinitCamera(VulkanApp& app)
{
  if (!strcmp(cameraType, "FirstPerson")) {
    app.camera_ = Camera(app.positioner_);
  } else {
    if (!strcmp(cameraType, "MoveTo")) {
      cameraPos    = kInitialCameraPos;
      cameraAngles = kInitialCameraAngles;
      positionerMoveTo.setDesiredPosition(kInitialCameraPos);
      positionerMoveTo.setDesiredAngles(kInitialCameraAngles);
      app.camera_ = Camera(positionerMoveTo);
    }
  }
}

int main()
{
	// construct the VulkanApp instance (GLFW callback functions are also set up in the constructor)
  VulkanApp app({ .initialCameraPos = kInitialCameraPos, .initialCameraTarget = kInitialCameraTarget });

  LineCanvas2D canvas2d;
  LineCanvas3D canvas3d;

  app.fpsCounter_.avgInterval_ = 0.002f;
  app.fpsCounter_.printFPS_    = false;

  std::unique_ptr<lvk::IContext> ctx(app.ctx_.get());

  {
    struct VertexData {
      vec3 pos;
      vec3 n;
      vec2 tc;
    };

    lvk::Holder<lvk::ShaderModuleHandle> vert       = loadShaderModule(ctx, "Chapter04/04_CubeMap/src/main.vert");
    lvk::Holder<lvk::ShaderModuleHandle> frag       = loadShaderModule(ctx, "Chapter04/04_CubeMap/src/main.frag");
    lvk::Holder<lvk::ShaderModuleHandle> vertSkybox = loadShaderModule(ctx, "Chapter04/04_CubeMap/src/skybox.vert");
    lvk::Holder<lvk::ShaderModuleHandle> fragSkybox = loadShaderModule(ctx, "Chapter04/04_CubeMap/src/skybox.frag");

    const lvk::VertexInput vdesc = {
      .attributes    = {{ .location = 0, .format = lvk::VertexFormat::Float3, .offset = offsetof(VertexData, pos) },
                        { .location = 1, .format = lvk::VertexFormat::Float3, .offset = offsetof(VertexData, n) },
                        { .location = 2, .format = lvk::VertexFormat::Float2, .offset = offsetof(VertexData, tc) }, },
      .inputBindings = { { .stride = sizeof(VertexData) } },
    };

	 // set up pipeline for dusk mesh (object)
    lvk::Holder<lvk::RenderPipelineHandle> pipeline = ctx->createRenderPipeline({
        .vertexInput = vdesc,
        .smVert      = vert,
        .smFrag      = frag,
        .color       = { { .format = ctx->getSwapchainFormat() } },
        .depthFormat = app.getDepthFormat(),
        .cullMode    = lvk::CullMode_Back,
    });

	 // set up pipeline for skybox (no vertex input state since we use programmable-vertex pulling here)
    lvk::Holder<lvk::RenderPipelineHandle> pipelineSkybox = ctx->createRenderPipeline({
        .smVert      = vertSkybox,
        .smFrag      = fragSkybox,
        .color       = { { .format = ctx->getSwapchainFormat() } },
        .depthFormat = app.getDepthFormat(),
    });

	 // load the rubber duck from a gltf file
    const aiScene* scene = aiImportFile("data/rubber_duck/scene.gltf", aiProcess_Triangulate);
   // const aiScene* scene = aiImportFile("data/aircraft/E 45 Aircraft_obj.obj", aiProcess_Triangulate);
   // const aiScene* scene = aiImportFile("data/dragon/DragonDispersion.gltf", aiProcess_Triangulate);
    if (!scene || !scene->HasMeshes()) {
      printf("Unable to load data/rubber_duck/scene.gltf\n");
      exit(255);
    }

	 // pack the scene into vertices and indices
    const aiMesh* mesh = scene->mMeshes[0];
    std::vector<VertexData> vertices;
    for (uint32_t i = 0; i != mesh->mNumVertices; i++) {
      const aiVector3D v = mesh->mVertices[i];
      const aiVector3D n = mesh->mNormals[i];
      const aiVector3D t = mesh->mTextureCoords[0][i];
      vertices.push_back({ .pos = vec3(v.x, v.y, v.z), .n = vec3(n.x, n.y, n.z), .tc = vec2(t.x, t.y) });
    }
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i != mesh->mNumFaces; i++) {
      for (uint32_t j = 0; j != 3; j++)
        indices.push_back(mesh->mFaces[i].mIndices[j]);
    }
    aiReleaseImport(scene);

    const size_t kSizeIndices  = sizeof(uint32_t) * indices.size();
    const size_t kSizeVertices = sizeof(VertexData) * vertices.size();

	 // create two GPU buffers to hold indices and vertices
    // indices
    lvk::Holder<lvk::BufferHandle> bufferIndices = ctx->createBuffer(
        { .usage     = lvk::BufferUsageBits_Index,
          .storage   = lvk::StorageType_Device,
          .size      = kSizeIndices,
          .data      = indices.data(),
          .debugName = "Buffer: indices" },
        nullptr);

    // vertices
    lvk::Holder<lvk::BufferHandle> bufferVertices = ctx->createBuffer(
        { .usage     = lvk::BufferUsageBits_Vertex,
          .storage   = lvk::StorageType_Device,
          .size      = kSizeVertices,
          .data      = vertices.data(),
          .debugName = "Buffer: vertices" },
        nullptr);

    struct PerFrameData {
      mat4 model;
      mat4 view;
      mat4 proj;
      vec4 cameraPos;
      uint32_t tex     = 0;
      uint32_t texCube = 0;
    };

    lvk::Holder<lvk::BufferHandle> bufferPerFrame = ctx->createBuffer(
        { .usage     = lvk::BufferUsageBits_Uniform,
          .storage   = lvk::StorageType_Device,
          .size      = sizeof(PerFrameData),
          .debugName = "Buffer: per-frame" },
        nullptr);

    // 2D texture for rubber duck model object
    lvk::Holder<lvk::TextureHandle> texture = loadTexture(ctx, "data/rubber_duck/textures/Duck_baseColor.png");
    //lvk::Holder<lvk::TextureHandle> texture = loadTexture(ctx, "data/dragon/textures/Dragon_ThicknessMap.jpg");
	 // cube map for skybox
    lvk::Holder<lvk::TextureHandle> cubemapTex;
    {
      int w, h;
     // const float* img = stbi_loadf("data/kloofendal_48d_partly_cloudy_puresky_4k.hdr", &w, &h, nullptr, 4);
    //  const float* img = stbi_loadf("data/piazza_bologni_1k.hdr", &w, &h, nullptr, 4);
       const float* img = stbi_loadf("data/golden_gate_hills_4k.hdr", &w, &h, nullptr, 4);
      Bitmap in(w, h, 4, eBitmapFormat_Float, img);
      Bitmap out = convertEquirectangularMapToVerticalCross(in);
      stbi_image_free((void*)img);

		// save the vertical cross
      stbi_write_hdr(".cache/screenshot.hdr", out.w_, out.h_, out.comp_, (const float*)out.data_.data());

		// convert the vertical cross to cube maps
      Bitmap cubemap = convertVerticalCrossToCubeMapFaces(out);

      cubemapTex = ctx->createTexture({
          .type       = lvk::TextureType_Cube,
          .format     = lvk::Format_RGBA_F32,
          .dimensions = {(uint32_t)cubemap.w_, (uint32_t)cubemap.h_},
          .usage      = lvk::TextureUsageBits_Sampled,
          .data       = cubemap.data_.data(),
          .debugName  = "data/piazza_bologni_1k.hdr",
      });
    }

	 // main loop (wrapped by VulkanApp class)
	 // first-person camera positoner is updated automatically in run function (before the call back function below)
    app.run([&](uint32_t width, uint32_t height, float aspectRatio, float deltaSeconds) {
      positionerMoveTo.update(deltaSeconds, app.mouseState_.pos, ImGui::GetIO().WantCaptureMouse ? false : app.mouseState_.pressedLeft);

      const mat4 p  = glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
      const mat4 m0 = glm::scale(mat4(1.0f), scaleVector);
      const mat4 r1 = glm::rotate(mat4(1.0f), glm::radians(90.0f), vec3(1, 0, 0));
     // const mat4 r2 = glm::rotate(mat4(1.0f), glm::radians(180.0f), vec3(0, 0, 1));
      const mat4 m10 = glm::rotate(mat4(1.0f), glm::radians(rotationVector.x), vec3(1, 0, 0)); // pitch X
      const mat4 m11 = glm::rotate(mat4(1.0f), glm::radians(rotationVector.y), vec3(0, 1, 0)); // yaw Y
      const mat4 m12 = glm::rotate(mat4(1.0f), glm::radians(rotationVector.z), vec3(0, 0, 1)); // roll Z
      const mat4 m2 = glm::rotate(mat4(1.0f), (float)glfwGetTime(), vec3(0.0f, 1.0f, 0.0f));
      const mat4 v  = glm::translate(mat4(1.0f), app.camera_.getPosition());

	  // compute the delta of Euler angle
	  const vec3 delta = rotationVector - rotationVectorLastFrame;

	 // convert the delta of Euler angle to quaternion in pitch, yaw and roll
     const glm::quat q0 = glm::quat(vec3(glm::radians(delta.x), 0.0f, 0.0f)); // pitch
     const glm::quat q1 = glm::quat(vec3(0.0f, glm::radians(delta.y), 0.0f)); // yaw
     const glm::quat q2 = glm::quat(vec3(0.0f, 0.0f, glm::radians(delta.z))); // roll

	  // rotate in ZXY order (pitch yaw roll)
	  // using accumulate orientation way with quaternions to avoid gimbal lock
	  // if we reconstruct the quaternions every frame but not in accumulative way, then it doesn't solve the gimbal lock
	  objectOrientation = glm::normalize(q1 * q0 * q2 * objectOrientation);
     const glm::mat4 m = glm::mat4_cast(objectOrientation);

      const PerFrameData pc = {
    //  .model     = keepRotating ? m2 * r1 * m0 : m11 * m10 * m12 * m0, // rotate in ZXY order (pitch yaw roll)
        .model     = keepRotating ? m2 * r1 * m0 : m * m0, 
        .view      = app.camera_.getViewMatrix(), // get the view matrix from camera class
        .proj      = p,
        .cameraPos = vec4(app.camera_.getPosition(), 1.0f),
        .tex       = texture.index(),
        .texCube   = cubemapTex.index(),
      };

    //  ctx->upload(bufferPerFrame, &pc, sizeof(pc));

      const lvk::RenderPass renderPass = {
        .color = { { .loadOp = lvk::LoadOp_Clear, .clearColor = { 1.0f, 1.0f, 1.0f, 1.0f } } },
        .depth = { .loadOp = lvk::LoadOp_Clear, .clearDepth = 1.0f }
      };

      const lvk::Framebuffer framebuffer = {
        .color        = { { .texture = ctx->getCurrentSwapchainTexture() } },
        .depthStencil = { .texture = app.getDepthTexture() },
      };

      lvk::ICommandBuffer& buf = ctx->acquireCommandBuffer();
		// vkCmdUpdateBuffer is wrapped in cmdUpdateBuffer
		// because the data size is small, we can use this function to avoid synchronization problem
		// we can also use multiple round robin buffers as another way
      buf.cmdUpdateBuffer(bufferPerFrame, pc);
      {
        buf.cmdBeginRendering(renderPass, framebuffer);
        {
          {
            buf.cmdPushDebugGroupLabel("Skybox", 0xff0000ff);
            buf.cmdBindRenderPipeline(pipelineSkybox);
            buf.cmdPushConstants(ctx->gpuAddress(bufferPerFrame));
            buf.cmdDraw(36);
            buf.cmdPopDebugGroupLabel();
          }
          {
            buf.cmdPushDebugGroupLabel("Mesh", 0xff0000ff);
            buf.cmdBindVertexBuffer(0, bufferVertices);
            buf.cmdBindRenderPipeline(pipeline);
            buf.cmdBindDepthState({ .compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true });
            buf.cmdBindIndexBuffer(bufferIndices, lvk::IndexFormat_UI32);
            buf.cmdDrawIndexed(indices.size());
            buf.cmdPopDebugGroupLabel();
          }

			 // ImGui Renderer helper class for feeding the GUI data into graphics pipeline and rendering
          app.imgui_->beginFrame(framebuffer);

          // memo (keyboard hints)
          ImGui::SetNextWindowPos(ImVec2(10, 10));
          ImGui::Begin(
              "Keyboard hints:", nullptr,
              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                  ImGuiWindowFlags_NoCollapse);
          ImGui::Text("W/S/A/D - camera movement");
          ImGui::Text("1/2 - camera up/down");
          ImGui::Text("Shift - fast movement");
          ImGui::Text("Space - reset view");
          ImGui::End();

          // FPS
          if (const ImGuiViewport* v = ImGui::GetMainViewport()) {
            ImGui::SetNextWindowPos({ v->WorkPos.x + v->WorkSize.x - 15.0f, v->WorkPos.y + 15.0f }, ImGuiCond_Always, { 1.0f, 0.0f });
          }
          ImGui::SetNextWindowBgAlpha(0.30f);
          ImGui::SetNextWindowSize(ImVec2(ImGui::CalcTextSize("FPS : _______").x, 0));
          if (ImGui::Begin(
                  "##FPS", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("FPS : %i", (int)app.fpsCounter_.getFPS());
            ImGui::Text("Ms  : %.1f", 1000.0 / app.fpsCounter_.getFPS());
          }
          ImGui::End();

			  ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
          // camera controls
          ImGui::Begin("Camera Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
          {
				 // BeginCombo function will return true if the user has clicked on a label
            if (ImGui::BeginCombo("##combo", currentComboBoxItem)) // the second parameter is the label previewed before opening the combo. Set it to be the current selected item
            {
              for (int n = 0; n < IM_ARRAYSIZE(comboBoxItems); n++) {
                // check if the item[n] is selected in combo box currently
                const bool isSelected = (currentComboBoxItem == comboBoxItems[n]);

					 // draw the selectable UI, and highlight (focus) it in dropdown, if it is selected now (isSelected == true)
					 // if the function returns true, then it means the user latestly interacts with/ selects this item in this frame
                if (ImGui::Selectable(comboBoxItems[n], isSelected))
                  currentComboBoxItem = comboBoxItems[n];

                 if (isSelected)
                  ImGui::SetItemDefaultFocus(); // initial focus when opening the combo (scrolling + for keyboard navigation support)
              }
              ImGui::EndCombo();
            }

				// if MoveTo camera type is selected, render vec3 input sliders
            if (!strcmp(cameraType, "MoveTo")) {
					// last two parameters are min and max values
					// glm::value_ptr is used for getting a pointer of the vec3, so that the vec3 can be updated based on the slide bar
              if (ImGui::SliderFloat3("Position", glm::value_ptr(cameraPos), -10.0f, +10.0f))
                positionerMoveTo.setDesiredPosition(cameraPos);
              if (ImGui::SliderFloat3("Pitch/Pan/Roll", glm::value_ptr(cameraAngles), -180.0f, +180.0f))
                positionerMoveTo.setDesiredAngles(cameraAngles);
            }

				// print a debug message if the camera type is switched (camera type is different from the currentComboBoxItem)
            if (currentComboBoxItem && strcmp(currentComboBoxItem, cameraType)) {
              printf("Selected new camera type: %s\n", currentComboBoxItem);
              cameraType = currentComboBoxItem; // set the new value to the camera type
              reinitCamera(app);
            }
          }
          ImGui::End();

			 // ImGui window for model transformation controls
			 ImGui::SetNextWindowPos(ImVec2(1000, 10), ImGuiCond_FirstUseEver);
			 ImGui::Begin("Model Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
			 {
           rotationVectorLastFrame.x = rotationVector.x;
           rotationVectorLastFrame.y = rotationVector.y;
           rotationVectorLastFrame.z = rotationVector.z;
           ImGui::SliderFloat3("Rotation", glm::value_ptr(rotationVector), -180.0f, 180.0f);
           ImGui::SliderFloat3("Scale", glm::value_ptr(scaleVector), 0.1f, +2.0f);
           ImGui::Checkbox("Keep rotating", &keepRotating);
			 }
          ImGui::End();

          // graphs
          sinGraph.renderGraph(0, height * 0.7f, width, height * 0.2f, vec4(0.0f, 1.0f, 0.0f, 1.0f));
          fpsGraph.renderGraph(0, height * 0.8f, width, height * 0.2f);

          canvas2d.clear();
       //   canvas2d.line({ 100, 300 }, { 100, 400 }, vec4(1, 0, 0, 1));
      //    canvas2d.line({ 100, 400 }, { 200, 400 }, vec4(0, 1, 0, 1));
      //    canvas2d.line({ 200, 400 }, { 200, 300 }, vec4(0, 0, 1, 1));
     //     canvas2d.line({ 200, 300 }, { 100, 300 }, vec4(1, 1, 0, 1));

			 // using canvas2d to draw my own name (XINJIE) on the screen 
			 float x       = 50.0f;  // starting x position
          float y       = 220.0f; // baseline y position
          float w       = 50.0f;  // letter width
          float h       = 100.0f; // letter height
          float spacing = 60.0f;
          vec4 color    = vec4(1, 0, 0, 1); // white

          // Letter: X
          canvas2d.line({ x, y }, { x + w, y + h }, vec4(1, 0, 0, 1));
          canvas2d.line({ x + w, y }, { x, y + h }, vec4(1, 0, 0, 1));
          x += spacing;

          // Letter: I
          canvas2d.line({ x + w * 0.5f, y }, { x + w * 0.5f, y + h }, vec4(0, 1, 0, 1));
          x += spacing;

          // Letter: N
          canvas2d.line({ x, y }, { x, y + h }, vec4(1, 0.8, 0, 1));
          canvas2d.line({ x, y }, { x + w, y + h }, vec4(1, 0.8, 0, 1));
          canvas2d.line({ x + w, y }, { x + w, y + h }, vec4(1, 0.8, 0, 1));
          x += spacing;

          // Letter: J
          canvas2d.line({ x + w, y }, { x + w, y + h * 0.8f }, vec4(1, 1, 0, 1));
          canvas2d.line({ x + w, y + h * 0.8f }, { x + w * 0.5f, y + h }, vec4(1, 1, 0, 1));
          canvas2d.line({ x + w * 0.5f, y + h }, { x, y + h * 0.8f }, vec4(1, 1, 0, 1));
          x += spacing;

          // Letter: I
          canvas2d.line({ x + w * 0.5f, y }, { x + w * 0.5f, y + h }, vec4(1, 0, 1, 1));
          x += spacing;

          // Letter: E
          canvas2d.line({ x, y }, { x, y + h }, vec4(0, 1, 1, 1));
          canvas2d.line({ x, y }, { x + w, y }, vec4(0, 1, 1, 1));
          canvas2d.line({ x, y + h * 0.5f }, { x + w * 0.7f, y + h * 0.5f }, vec4(0, 1, 1, 1));
          canvas2d.line({ x, y + h }, { x + w, y + h }, vec4(0, 1, 1, 1));

			 canvas2d.render("##plane");

          canvas3d.clear();
          canvas3d.setMatrix(pc.proj * pc.view);
          canvas3d.plane(vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 0, 1), 40, 40, 10.0f, 10.0f, vec4(1, 0, 0, 1), vec4(0, 1, 0, 1));
          // set the matrix of bounding box to be identity matrix
          // we don't need the bounding box to be rotated by model matrix
			 canvas3d.box(mat4(1.0f), BoundingBox(vec3(-2), vec3(+2)), vec4(1, 1, 0, 1));

			 // render a view frustum based on a view-projection matrix (different from the app's camera's view frustum)
			 canvas3d.frustum( // using sin/cos and glfwGetTime() to make the frustum rotating (keep changing the camera position)
              glm::lookAt(vec3(cos(glfwGetTime()), kInitialCameraPos.y, sin(glfwGetTime())), kInitialCameraTarget, vec3(0.0f, 1.0f, 0.0f)),
              glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 30.0f), vec4(1, 1, 1, 1));
          canvas3d.render(*ctx.get(), framebuffer, buf);

			 // the function from ImGui renderer helper class will do UI rendering 
          app.imgui_->endFrame(buf);

          buf.cmdEndRendering();
        }
      }
		// submit the command buffer to GPU
      ctx->submit(buf, ctx->getCurrentSwapchainTexture());

		// add points to both graphs (update the graphs)
      fpsGraph.addPoint(app.fpsCounter_.getFPS());
      sinGraph.addPoint(sinf(glfwGetTime() * 20.0f));
    });

    ctx.release();
  }

  return 0;
}
