/*  This file is part of assfonts.
 *
 *  assfonts is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation,
 *  either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  assfonts is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with assfonts. If not, see <https://www.gnu.org/licenses/>.
 *
 *  written by wyzdwdz (https://github.com/wyzdwdz)
 */

#include <clocale>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <utility>

#include <ImGuiFileDialog.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#include "assfonts.h"
#include "circular_buffer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "NotoSansCJK_Regular.hxx"
#include "icon.hxx"

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

using ScaleLambda = std::function<float(float)>;

enum HdrComboState { NO_HDR = 0, HDR_LOW, HDR_HIGH };

static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

static std::string input_text_buffer;
static std::string output_text_buffer;
static std::string font_text_buffer;
static std::string database_text_buffer = ".";
static CircularBuffer<std::pair<unsigned int, std::string>> log_text_buffer(
    8 * 1024);

static std::vector<std::string> drop_buffer;
static const std::regex ass_regex(".+\\.(ass|ssa)$",
                                  std::regex_constants::icase);

static bool is_font_text_locked = false;

static int hdr_state = NO_HDR;
static bool is_subset_only = false;
static bool is_embed_only = false;

static bool is_running = false;

static bool is_show_log = false;
static bool is_show_space = false;
static int show_log_level = ASSFONTS_INFO;

void AppInit();

void AppRender(GLFWwindow* window, ImGuiIO& io, const ScaleLambda& Scale);

void TabBarRender(GLFWwindow* window, const ScaleLambda& Scale);

void InputGroupRender(const ScaleLambda& Scale);
void OutputGroupRender(const ScaleLambda& Scale);
void FontGroupRender(const ScaleLambda& Scale);
void DatabaseGroupRender(const ScaleLambda& Scale);
void SettingGroupRender(const ScaleLambda& Scale);
void RunGroupRender(GLFWwindow* window, const ScaleLambda& Scale);
void LogGroupRender(GLFWwindow* window, const ScaleLambda& Scale);

void LogCallback(const char* msg, const unsigned int log_level);
void LogToClipBoard(GLFWwindow* window);

void DropCallback(GLFWwindow* window, int count, const char** paths);

void OnDropInput();
void OnDropOutput();
void OnDropFont();
void OnDropDatabase();

void OnBuildDatabase();
void OnStart();

void BuildDatabase();
void Start();

std::string Trim(const std::string& str);

int main() {
  std::setlocale(LC_ALL, ".UTF8");

  if (!glfwInit()) {
    return -1;
  }

#if defined(IMGUI_IMPL_OPENGL_ES2)
  const char* glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  float xscale, yscale;
  GLFWmonitor* primary = glfwGetPrimaryMonitor();
  glfwGetMonitorContentScale(primary, &xscale, &yscale);
  float dpi_scale = yscale;

  auto Scale = [=](const float res) {
    return std::floor(res * dpi_scale);
  };

  GLFWwindow* window =
      glfwCreateWindow(Scale(640), Scale(410), "assfonts", nullptr, nullptr);
  if (window == nullptr) {
    return -1;
  }
  glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);

  GLFWimage icon[1];
  icon[0].pixels = stbi_load_from_memory(icon_data, icon_size, &icon[0].width,
                                         &icon[0].height, 0, 4);
  glfwSetWindowIcon(window, 1, icon);
  stbi_image_free(icon[0].pixels);

  glfwSetDropCallback(window, DropCallback);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigViewportsNoAutoMerge = true;
  io.ConfigViewportsNoTaskBarIcon = true;

  ImGui::StyleColorsLight();

  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }
  style.ScaleAllSizes(dpi_scale);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImFontGlyphRangesBuilder glyph_range_builder;
  ImVector<ImWchar> combined_glyph_ranges;
  glyph_range_builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
  glyph_range_builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
  glyph_range_builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
  glyph_range_builder.BuildRanges(&combined_glyph_ranges);
  io.Fonts->AddFontFromMemoryCompressedTTF(
      NotoSansCJK_Regular_compressed_data, NotoSansCJK_Regular_compressed_size,
      Scale(18.0f), nullptr, combined_glyph_ranges.Data);

  io.IniFilename = nullptr;

  AppInit();

  while (!glfwWindowShouldClose(window)) {
    AppRender(window, io, Scale);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}

void AppInit() {
  std::string version_info = "assfonts -- version " +
                             std::to_string(ASSFONTS_VERSION_MAJOR) + "." +
                             std::to_string(ASSFONTS_VERSION_MINOR) + "." +
                             std::to_string(ASSFONTS_VERSION_PATCH) + "\n";
  LogCallback(version_info.c_str(), ASSFONTS_TEXT);
  LogCallback("\n", ASSFONTS_TEXT);
}

void AppRender(GLFWwindow* window, ImGuiIO& io, const ScaleLambda& Scale) {
  glfwPollEvents();

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  int width, height, xpos, ypos;
  glfwGetWindowSize(window, &width, &height);
  glfwGetWindowPos(window, &xpos, &ypos);
  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::SetNextWindowPos(ImVec2(xpos, ypos));

  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);

  if (ImGui::Begin(
          "assfonts", nullptr,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse)) {
    TabBarRender(window, Scale);
    ImGui::End();
  }

  ImGui::Render();
  int display_w, display_h;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
               clear_color.z * clear_color.w, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    GLFWwindow* backup_current_context = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(backup_current_context);
  }

  glfwSwapBuffers(window);
}

void TabBarRender(GLFWwindow* window, const ScaleLambda& Scale) {
  if (ImGui::BeginTabBar("tabBar")) {
    if (ImGui::BeginTabItem(" Main ")) {
      InputGroupRender(Scale);
      OutputGroupRender(Scale);
      FontGroupRender(Scale);
      DatabaseGroupRender(Scale);
      SettingGroupRender(Scale);
      RunGroupRender(window, Scale);
      ImGui::EndTabItem();
    }

    ImGuiTabItemFlags log_tab_item_flags = ImGuiTabItemFlags_None;
    if (is_show_log) {
      log_tab_item_flags |= ImGuiTabItemFlags_SetSelected;
      is_show_log = false;
    }

    if (ImGui::BeginTabItem(" Log ", nullptr, log_tab_item_flags)) {
      LogGroupRender(window, Scale);
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
}

void InputGroupRender(const ScaleLambda& Scale) {
  ImGui::Spacing();
  ImGui::Indent(1 * ImGui::GetStyle().IndentSpacing / 2);

  ImGui::BeginGroup();
  ImGui::PushID("Input");

  ImGui::TextUnformatted("Input ASS files");

  ImGui::Spacing();
  ImGui::SetNextItemWidth(Scale(560));
  ImGui::InputText("##input_text", &input_text_buffer);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    OnDropInput();
  }

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button(u8"\u2026", ImVec2(Scale(30.0f), Scale(0.0f)))) {
    ImGuiFileDialog::Instance()->OpenDialog(
        "ChooseInputDlgKey", "Choose File - Input ASS files", ".ass,.ssa", ".",
        128, nullptr, ImGuiFileDialogFlags_CaseInsensitiveExtention);
  }

  if (ImGuiFileDialog::Instance()->Display(
          "ChooseInputDlgKey", ImGuiWindowFlags_NoCollapse,
          ImVec2(Scale(500.0f), Scale(300.0f)))) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      input_text_buffer.clear();

      auto selections = ImGuiFileDialog::Instance()->GetSelection();
      output_text_buffer = ImGuiFileDialog::Instance()->GetCurrentPath();

      for (auto it = selections.rbegin(); it != selections.rend(); ++it) {
        input_text_buffer.append((*it).second);

        if (std::distance(it, selections.rend()) > 1) {
          input_text_buffer.append("; ");
        }
      }
    }

    ImGuiFileDialog::Instance()->Close();
  }

  ImGui::PopID();
  ImGui::EndGroup();
}

void OutputGroupRender(const ScaleLambda& Scale) {
  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::BeginGroup();
  ImGui::PushID("Output");

  ImGui::TextUnformatted("Output directory");

  ImGui::Spacing();
  ImGui::SetNextItemWidth(Scale(560));
  ImGui::InputText("##output_text", &output_text_buffer);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    OnDropOutput();
  }

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button(u8"\u2026", ImVec2(Scale(30.0f), Scale(0.0f)))) {
    ImGuiFileDialog::Instance()->OpenDialog(
        "ChooseOutputDlgKey", "Choose Directory - Output directory", nullptr,
        ".", 1, nullptr);
  }

  if (ImGuiFileDialog::Instance()->Display(
          "ChooseOutputDlgKey", ImGuiWindowFlags_NoCollapse,
          ImVec2(Scale(500.0f), Scale(300.0f)))) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      output_text_buffer = ImGuiFileDialog::Instance()->GetCurrentPath();
    }

    ImGuiFileDialog::Instance()->Close();
  }

  ImGui::PopID();
  ImGui::EndGroup();
}

void FontGroupRender(const ScaleLambda& Scale) {
  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::BeginGroup();
  ImGui::PushID("Font");

  ImGui::TextUnformatted("Font directory");

  ImGui::Spacing();
  ImGui::SetNextItemWidth(Scale(560));
  if (is_font_text_locked) {
    ImGui::InputText("##font_text", &font_text_buffer,
                     ImGuiInputTextFlags_ReadOnly);
  } else {
    ImGui::InputText("##font_text", &font_text_buffer);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    OnDropFont();
  }

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button(u8"\u2026", ImVec2(Scale(30.0f), Scale(0.0f)))) {
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFontDlgKey",
                                            "Choose Directory - Font directory",
                                            nullptr, ".", 1, nullptr);
  }

  if (ImGuiFileDialog::Instance()->Display(
          "ChooseFontDlgKey", ImGuiWindowFlags_NoCollapse,
          ImVec2(Scale(500.0f), Scale(300.0f)))) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      font_text_buffer = ImGuiFileDialog::Instance()->GetCurrentPath();
    }

    ImGuiFileDialog::Instance()->Close();
  }

  ImGui::PopID();
  ImGui::EndGroup();
}

void DatabaseGroupRender(const ScaleLambda& Scale) {
  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::BeginGroup();
  ImGui::PushID("Database");

  ImGui::TextUnformatted("Database directory");

  ImGui::Spacing();
  ImGui::SetNextItemWidth(Scale(560));
  ImGui::InputText("##database_text", &database_text_buffer);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    OnDropDatabase();
  }

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button(u8"\u2026", ImVec2(Scale(30.0f), Scale(0.0f)))) {
    ImGuiFileDialog::Instance()->OpenDialog(
        "ChooseDatabaseDlgKey", "Choose Directory - Database directory",
        nullptr, ".", 1, nullptr);
  }

  if (ImGuiFileDialog::Instance()->Display(
          "ChooseDatabaseDlgKey", ImGuiWindowFlags_NoCollapse,
          ImVec2(Scale(500.0f), Scale(300.0f)))) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      database_text_buffer = ImGuiFileDialog::Instance()->GetCurrentPath();
    }

    ImGuiFileDialog::Instance()->Close();
  }

  ImGui::PopID();
  ImGui::EndGroup();
}

void SettingGroupRender(const ScaleLambda& Scale) {
  for (int i = 0; i < 5; ++i) {
    ImGui::Spacing();
  }

  ImGui::BeginGroup();

  ImGui::SetNextItemWidth(Scale(120));
  ImGui::Combo("##HDR", &hdr_state, "No HDR\0HDR Low\0HDR High\0");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip(
        "Change HDR subtitle brightness\n"
        " HDR Low  targets 100 nit\n"
        " HDR High  targets 203 nit");
  }

  ImGui::SameLine(0, 2 * ImGui::GetStyle().ItemSpacing.x);
  ImGui::Checkbox("Subset only", &is_subset_only);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("Subset fonts but not embed them into subtitle");
  }

  ImGui::SameLine(0, 2 * ImGui::GetStyle().ItemSpacing.x);
  ImGui::Checkbox("Embed only", &is_embed_only);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("Embed fonts into subtitle but not subset them");
  }

  ImGui::EndGroup();
}

void RunGroupRender(GLFWwindow* window, const ScaleLambda& Scale) {
  for (int i = 0; i < 5; ++i) {
    ImGui::Spacing();
  }

  ImGui::BeginGroup();

  if (ImGui::Button("Build Databse", ImVec2(Scale(125.0f), Scale(45.0f)))) {
    is_show_log = true;
    OnBuildDatabase();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("Build fonts database");
  }

  ImGui::SameLine(0, 2 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button("Start", ImVec2(Scale(70.0f), Scale(45.0f)))) {
    is_show_log = true;
    OnStart();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("Start program");
  }

  ImGui::SameLine(0, 42 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button("Exit", ImVec2(Scale(50.0f), Scale(45.0f)))) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }

  ImGui::EndGroup();
}

void LogGroupRender(GLFWwindow* window, const ScaleLambda& Scale) {
  ImGui::Spacing();
  ImGui::BeginGroup();

  if (ImGui::Button("Clear", ImVec2(Scale(55.0f), Scale(0.0f)))) {
    log_text_buffer.clear();
  }

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button("Copy", ImVec2(Scale(55.0f), Scale(0.0f)))) {
    LogToClipBoard(window);
  }

  ImGui::SameLine(0, 2 * ImGui::GetStyle().ItemSpacing.x);
  ImGui::Checkbox("Show space", &is_show_space);

  ImGui::SameLine(0, 25 * ImGui::GetStyle().ItemSpacing.x);
  ImGui::TextUnformatted("Show log level >=");

  ImGui::SameLine(0, 1 * ImGui::GetStyle().ItemSpacing.x);
  ImGui::SetNextItemWidth(Scale(80));
  ImGui::Combo("##log_level", &show_log_level, "INFO\0WARN\0ERROR\0");

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  if (ImGui::BeginChild("scrolling", ImVec2(Scale(0.0f), Scale(335.0f)),
                        false)) {
    ImGui::SameLine(0, 0.5 * ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginGroup();

    for (const auto& log : log_text_buffer) {
      if (!is_show_space && log.second == "\n") {
        continue;
      }

      switch (log.first) {
        case ASSFONTS_INFO:
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
          break;

        case ASSFONTS_WARN:
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
          break;

        case ASSFONTS_ERROR:
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
          break;

        default:
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
          break;
      }

      if (log.first >= static_cast<unsigned int>(show_log_level)) {
        ImGui::TextWrappedUnformatted(log.second.c_str());
      }

      ImGui::PopStyleColor();
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndGroup();

    ImGui::PopStyleColor();
    ImGui::EndChild();
  }

  ImGui::EndGroup();
}

void LogCallback(const char* msg, const unsigned int log_level) {
  log_text_buffer.push_back(make_pair(log_level, std::string(msg)));
}

void LogToClipBoard(GLFWwindow* window) {
  std::string paste_text;

  for (const auto& log : log_text_buffer) {
    if (!is_show_space && log.second == "\n") {
      continue;
    }

    if (log.first >= static_cast<unsigned int>(show_log_level)) {
      paste_text.append(log.second);
    }
  }

  glfwSetClipboardString(window, Trim(paste_text).c_str());
}

void DropCallback(GLFWwindow* window, int count, const char** paths) {
  if (drop_buffer.empty()) {
    for (int i = 0; i < count; ++i) {
      drop_buffer.emplace_back(std::string(paths[i]));
    }
  }
}

void OnDropInput() {
  if (!drop_buffer.empty()) {
    bool is_already_clear = false;
    std::filesystem::path first_item;

    for (auto it = drop_buffer.begin(); it != drop_buffer.end(); ++it) {
      if (!std::regex_match(*it, ass_regex)) {
        continue;
      }

      if (!is_already_clear) {
        input_text_buffer.clear();
        is_already_clear = true;

        first_item = std::filesystem::path(*it);
      }

      input_text_buffer.append(*it);
      if (it != drop_buffer.end() - 1) {
        input_text_buffer.append("; ");
      }
    }

    if (!first_item.empty()) {
      output_text_buffer = first_item.parent_path().string();
    }

    drop_buffer.clear();
  }
}

void OnDropOutput() {
  if (!drop_buffer.empty()) {
    std::filesystem::path first_item(drop_buffer[0]);

    if (std::filesystem::is_directory(first_item)) {
      output_text_buffer = drop_buffer[0];
    }

    drop_buffer.clear();
  }
}

void OnDropFont() {
  if (!drop_buffer.empty()) {
    std::filesystem::path first_item(drop_buffer[0]);

    if (std::filesystem::is_directory(first_item)) {
      font_text_buffer = drop_buffer[0];
    }

    drop_buffer.clear();
  }
}

void OnDropDatabase() {
  if (!drop_buffer.empty()) {
    std::filesystem::path first_item(drop_buffer[0]);

    if (std::filesystem::is_directory(first_item)) {
      database_text_buffer = drop_buffer[0];
    }

    drop_buffer.clear();
  }
}

void OnBuildDatabase() {
  if (!is_running) {
    auto thrd = std::thread([]() { BuildDatabase(); });
    thrd.detach();
  }
}

void OnStart() {
  if (!is_running) {
    auto thrd = std::thread([]() { Start(); });
    thrd.detach();
  }
}

void BuildDatabase() {
  is_running = true;

  std::string fonts_path = font_text_buffer;
  std::string database_path = database_text_buffer;

  is_font_text_locked = true;
  font_text_buffer.clear();
  is_font_text_locked = false;

  AssfontsBuildDB(fonts_path.c_str(), database_path.c_str(), LogCallback,
                  ASSFONTS_INFO);

  LogCallback("\n", ASSFONTS_TEXT);

  is_running = false;
}

void Start() {
  is_running = true;

  std::string input_text = input_text_buffer;
  std::vector<std::string> input_vec;

  size_t pos = 0;
  size_t pos_next = 0;
  while (pos != std::string::npos) {
    pos_next = input_text.find(';', pos);
    std::string input_trim = Trim(input_text.substr(pos, pos_next - pos));

    if (!input_trim.empty()) {
      input_vec.emplace_back(input_trim);
    }

    pos = (pos_next != std::string::npos) ? pos_next + 1 : pos_next;
  }

  auto input_paths = std::make_unique<char*[]>(input_vec.size());
  for (size_t idx = 0; idx < input_vec.size(); ++idx) {
    input_paths[idx] = const_cast<char*>(input_vec[idx].c_str());
  }

  unsigned int brightness = 0;
  switch (hdr_state) {
    case NO_HDR:
      brightness = 0;
      break;
    case HDR_LOW:
      brightness = 100;
      break;
    case HDR_HIGH:
      brightness = 203;
      break;
    default:
      break;
  }

  AssfontsRun(const_cast<const char**>(input_paths.get()), input_vec.size(),
              output_text_buffer.c_str(), font_text_buffer.c_str(),
              database_text_buffer.c_str(), brightness, is_subset_only,
              is_embed_only, LogCallback, ASSFONTS_INFO);

  LogCallback("\n", ASSFONTS_TEXT);

  is_running = false;
}

std::string Trim(const std::string& str) {
  std::string res = str;

  res.erase(res.begin(),
            std::find_if(res.begin(), res.end(),
                         [](unsigned char ch) { return !std::isspace(ch); }));

  res.erase(std::find_if(res.rbegin(), res.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            res.end());

  return res;
}