#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_mousewheel.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "stackchan/avatar/skins/default/default.h"

namespace {

using stackchan::avatar::DefaultAvatar;
using stackchan::avatar::Emotion;

constexpr int kAvatarWidth = 320;
constexpr int kAvatarHeight = 240;
constexpr int kWindowWidth = 860;
constexpr int kWindowHeight = 620;

struct FeatureState {
    int x = 0;
    int y = 0;
    int rotation = 0;
    int weight = 0;
    int size = 0;
};

struct ExpressionState {
    FeatureState leftEye{0, 0, 0, 100, 0};
    FeatureState rightEye{0, 0, 0, 100, 0};
    FeatureState mouth{0, 0, 0, 0, 0};
};

struct ControlBinding {
    std::string label;
    int* value = nullptr;
    int min = 0;
    int max = 0;
    lv_obj_t* title = nullptr;
    lv_obj_t* slider = nullptr;
    lv_obj_t* value_label = nullptr;
};

class AvatarSimulator {
public:
    bool init()
    {
        lv_init();

        display_ = lv_sdl_window_create(kWindowWidth, kWindowHeight);
        if (display_ == nullptr) {
            std::fprintf(stderr, "failed to create LVGL SDL window\n");
            return false;
        }
        lv_sdl_window_set_title(display_, "StackChan Avatar SDL Simulator");

        auto* group = lv_group_create();
        lv_group_set_default(group);

        auto* mouse = lv_sdl_mouse_create();
        lv_indev_set_group(mouse, group);
        lv_indev_set_display(mouse, display_);

        auto* wheel = lv_sdl_mousewheel_create();
        lv_indev_set_group(wheel, group);
        lv_indev_set_display(wheel, display_);

        create_ui();
        apply_state();
        select_control(0);

        SDL_AddEventWatch(&AvatarSimulator::sdl_event_watch, this);
        return true;
    }

    int run()
    {
        while (!quit_) {
            handle_pending_keys();
            avatar_.update();
            lv_timer_handler();
            SDL_Delay(8);
        }

        SDL_DelEventWatch(&AvatarSimulator::sdl_event_watch, this);
        lv_sdl_quit();
        return 0;
    }

private:
    static int sdl_event_watch(void* userdata, SDL_Event* event)
    {
        auto* self = static_cast<AvatarSimulator*>(userdata);
        if (event->type == SDL_KEYDOWN) {
            self->pending_keys_.push_back(event->key.keysym.sym);
        } else if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE) {
            self->quit_ = true;
        } else if (event->type == SDL_QUIT) {
            self->quit_ = true;
        }
        return 1;
    }

    static void slider_event_cb(lv_event_t* event)
    {
        auto* binding = static_cast<ControlBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->value == nullptr) {
            return;
        }

        *binding->value = static_cast<int>(lv_slider_get_value(binding->slider));
        update_value_label(*binding);

        auto* self = static_cast<AvatarSimulator*>(lv_obj_get_user_data(binding->slider));
        if (self != nullptr) {
            self->apply_state();
        }
    }

    static void button_event_cb(lv_event_t* event)
    {
        auto* action = static_cast<ButtonAction*>(lv_event_get_user_data(event));
        if (action == nullptr || action->owner == nullptr) {
            return;
        }
        action->owner->handle_button_action(action->kind);
    }

    enum class ButtonKind {
        Reset,
        Speech,
        Neutral,
        Happy,
        Angry,
        Sad,
        Doubt,
        Sleepy,
    };

    struct ButtonAction {
        AvatarSimulator* owner = nullptr;
        ButtonKind kind = ButtonKind::Reset;
    };

    void create_ui()
    {
        auto* screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x202225), LV_PART_MAIN);
        lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

        auto* avatar_title = lv_label_create(screen);
        lv_label_set_text(avatar_title, "Avatar viewport");
        lv_obj_set_style_text_color(avatar_title, lv_color_hex(0xF4F4F4), LV_PART_MAIN);
        lv_obj_set_pos(avatar_title, 24, 18);

        avatar_viewport_ = lv_obj_create(screen);
        lv_obj_set_size(avatar_viewport_, kAvatarWidth, kAvatarHeight);
        lv_obj_set_pos(avatar_viewport_, 24, 46);
        lv_obj_set_style_radius(avatar_viewport_, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(avatar_viewport_, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(avatar_viewport_, lv_color_hex(0x4A4D52), LV_PART_MAIN);
        lv_obj_set_style_pad_all(avatar_viewport_, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(avatar_viewport_, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_remove_flag(avatar_viewport_, LV_OBJ_FLAG_SCROLLABLE);

        avatar_.init(avatar_viewport_);

        create_help(screen);
        create_control_panel(screen);
    }

    void create_help(lv_obj_t* parent)
    {
        auto* help = lv_label_create(parent);
        lv_label_set_text(help,
                          "Keyboard: tab focus  arrows edit  1-6 presets  r reset  s speech  b blink  esc quit");
        lv_obj_set_style_text_color(help, lv_color_hex(0xC7CBD1), LV_PART_MAIN);
        lv_obj_set_width(help, 320);
        lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(help, 24, 304);
    }

    void create_control_panel(lv_obj_t* parent)
    {
        auto* title = lv_label_create(parent);
        lv_label_set_text(title, "Avatar controls");
        lv_obj_set_style_text_color(title, lv_color_hex(0xF4F4F4), LV_PART_MAIN);
        lv_obj_set_pos(title, 374, 18);

        auto* panel = lv_obj_create(parent);
        lv_obj_set_pos(panel, 374, 46);
        lv_obj_set_size(panel, 460, 542);
        lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xF7F8FA), LV_PART_MAIN);
        lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(panel, 14, LV_PART_MAIN);
        lv_obj_set_scroll_dir(panel, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);

        int y = 0;
        add_button_row(panel, y);
        y += 58;

        add_section_label(panel, "Left Eye", y);
        y += 30;
        add_slider(panel, "x", &state_.leftEye.x, -100, 100, y);
        add_slider(panel, "y", &state_.leftEye.y, -100, 100, y);
        add_slider(panel, "rotation", &state_.leftEye.rotation, -1800, 1800, y);
        add_slider(panel, "weight", &state_.leftEye.weight, 0, 100, y);
        add_slider(panel, "size", &state_.leftEye.size, -100, 100, y);

        add_section_label(panel, "Right Eye", y);
        y += 30;
        add_slider(panel, "x", &state_.rightEye.x, -100, 100, y);
        add_slider(panel, "y", &state_.rightEye.y, -100, 100, y);
        add_slider(panel, "rotation", &state_.rightEye.rotation, -1800, 1800, y);
        add_slider(panel, "weight", &state_.rightEye.weight, 0, 100, y);
        add_slider(panel, "size", &state_.rightEye.size, -100, 100, y);

        add_section_label(panel, "Mouth", y);
        y += 30;
        add_slider(panel, "x", &state_.mouth.x, -100, 100, y);
        add_slider(panel, "y", &state_.mouth.y, -100, 100, y);
        add_slider(panel, "rotation", &state_.mouth.rotation, -1800, 1800, y);
        add_slider(panel, "weight", &state_.mouth.weight, 0, 100, y);

        lv_obj_update_layout(panel);
    }

    void add_section_label(lv_obj_t* parent, const char* text, int y)
    {
        auto* label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(0x24272B), LV_PART_MAIN);
        lv_obj_set_pos(label, 0, y);
    }

    void add_slider(lv_obj_t* parent, const std::string& label, int* value, int min, int max, int& y)
    {
        auto binding = std::make_unique<ControlBinding>();
        binding->label = label;
        binding->value = value;
        binding->min = min;
        binding->max = max;

        binding->title = lv_label_create(parent);
        lv_label_set_text(binding->title, label.c_str());
        lv_obj_set_style_text_color(binding->title, lv_color_hex(0x4B5563), LV_PART_MAIN);
        lv_obj_set_pos(binding->title, 0, y + 8);

        binding->slider = lv_slider_create(parent);
        lv_slider_set_range(binding->slider, min, max);
        lv_slider_set_value(binding->slider, *value, LV_ANIM_OFF);
        lv_obj_set_size(binding->slider, 250, 14);
        lv_obj_set_pos(binding->slider, 92, y + 10);
        lv_obj_set_user_data(binding->slider, this);
        lv_obj_add_event_cb(binding->slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, binding.get());

        binding->value_label = lv_label_create(parent);
        lv_obj_set_width(binding->value_label, 68);
        lv_obj_set_style_text_align(binding->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_color(binding->value_label, lv_color_hex(0x111827), LV_PART_MAIN);
        lv_obj_set_pos(binding->value_label, 356, y + 4);
        update_value_label(*binding);

        controls_.push_back(std::move(binding));
        y += 34;
    }

    void add_button_row(lv_obj_t* parent, int y)
    {
        int x = 0;
        add_button(parent, "Reset", ButtonKind::Reset, x, y, 72);
        x += 82;
        add_button(parent, "Speech", ButtonKind::Speech, x, y, 78);
        x += 88;
        add_button(parent, "Neutral", ButtonKind::Neutral, x, y, 82);
        x += 92;
        add_button(parent, "Happy", ButtonKind::Happy, x, y, 72);
        x = 0;
        y += 32;
        add_button(parent, "Angry", ButtonKind::Angry, x, y, 72);
        x += 82;
        add_button(parent, "Sad", ButtonKind::Sad, x, y, 62);
        x += 72;
        add_button(parent, "Doubt", ButtonKind::Doubt, x, y, 72);
        x += 82;
        add_button(parent, "Sleepy", ButtonKind::Sleepy, x, y, 78);
    }

    void add_button(lv_obj_t* parent, const char* text, ButtonKind kind, int x, int y, int width)
    {
        actions_.push_back(std::make_unique<ButtonAction>(ButtonAction{this, kind}));

        auto* button = lv_button_create(parent);
        lv_obj_set_size(button, width, 26);
        lv_obj_set_pos(button, x, y);
        lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, actions_.back().get());

        auto* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }

    static void update_value_label(ControlBinding& binding)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%d", binding.value != nullptr ? *binding.value : 0);
        lv_label_set_text(binding.value_label, text);
    }

    void handle_pending_keys()
    {
        if (pending_keys_.empty()) {
            return;
        }

        auto keys = std::move(pending_keys_);
        pending_keys_.clear();

        for (auto key : keys) {
            switch (key) {
                case SDLK_ESCAPE:
                    quit_ = true;
                    break;
                case SDLK_TAB:
                    select_control(selected_control_ + 1);
                    break;
                case SDLK_LEFT:
                    nudge_selected(-1);
                    break;
                case SDLK_RIGHT:
                    nudge_selected(1);
                    break;
                case SDLK_UP:
                    nudge_selected(10);
                    break;
                case SDLK_DOWN:
                    nudge_selected(-10);
                    break;
                case SDLK_r:
                    reset_avatar();
                    break;
                case SDLK_s:
                    toggle_speech();
                    break;
                case SDLK_b:
                    blink_once();
                    break;
                case SDLK_1:
                    apply_emotion_preset(ButtonKind::Neutral);
                    break;
                case SDLK_2:
                    apply_emotion_preset(ButtonKind::Happy);
                    break;
                case SDLK_3:
                    apply_emotion_preset(ButtonKind::Angry);
                    break;
                case SDLK_4:
                    apply_emotion_preset(ButtonKind::Sad);
                    break;
                case SDLK_5:
                    apply_emotion_preset(ButtonKind::Doubt);
                    break;
                case SDLK_6:
                    apply_emotion_preset(ButtonKind::Sleepy);
                    break;
                default:
                    break;
            }
        }
    }

    void handle_button_action(ButtonKind kind)
    {
        switch (kind) {
            case ButtonKind::Reset:
                reset_avatar();
                break;
            case ButtonKind::Speech:
                toggle_speech();
                break;
            default:
                apply_emotion_preset(kind);
                break;
        }
    }

    void select_control(int index)
    {
        if (controls_.empty()) {
            selected_control_ = 0;
            return;
        }

        if (index < 0) {
            index = static_cast<int>(controls_.size()) - 1;
        }
        selected_control_ = index % static_cast<int>(controls_.size());

        for (std::size_t i = 0; i < controls_.size(); ++i) {
            const bool selected = static_cast<int>(i) == selected_control_;
            lv_obj_set_style_text_color(controls_[i]->title,
                                        selected ? lv_color_hex(0x0B7A75) : lv_color_hex(0x4B5563),
                                        LV_PART_MAIN);
            lv_obj_set_style_outline_width(controls_[i]->slider, selected ? 2 : 0, LV_PART_MAIN);
            lv_obj_set_style_outline_color(controls_[i]->slider, lv_color_hex(0x0B7A75), LV_PART_MAIN);
            lv_obj_set_style_outline_pad(controls_[i]->slider, 3, LV_PART_MAIN);
        }

        lv_group_focus_obj(controls_[selected_control_]->slider);
    }

    void nudge_selected(int delta)
    {
        if (controls_.empty()) {
            return;
        }

        auto& control = *controls_[selected_control_];
        int value = std::clamp(*control.value + delta, control.min, control.max);
        *control.value = value;
        lv_slider_set_value(control.slider, value, LV_ANIM_OFF);
        update_value_label(control);
        apply_state();
    }

    void reset_avatar()
    {
        state_ = ExpressionState{};
        sync_sliders();
        apply_state();
    }

    void toggle_speech()
    {
        speech_visible_ = !speech_visible_;
        if (speech_visible_) {
            avatar_.setSpeech("Hello from macOS SDL");
        } else {
            avatar_.clearSpeech();
        }
    }

    void blink_once()
    {
        blink_closed_ = !blink_closed_;
        if (blink_closed_) {
            avatar_.leftEye().setWeight(25);
            avatar_.rightEye().setWeight(25);
        } else {
            avatar_.leftEye().setWeight(state_.leftEye.weight);
            avatar_.rightEye().setWeight(state_.rightEye.weight);
        }
    }

    void apply_emotion_preset(ButtonKind kind)
    {
        blink_closed_ = false;

        switch (kind) {
            case ButtonKind::Neutral:
                state_.leftEye.weight = 100;
                state_.rightEye.weight = 100;
                state_.leftEye.rotation = 0;
                state_.rightEye.rotation = 0;
                avatar_.setEmotion(Emotion::Neutral);
                break;
            case ButtonKind::Happy:
                state_.leftEye.weight = 72;
                state_.rightEye.weight = 72;
                state_.leftEye.rotation = 1550;
                state_.rightEye.rotation = -1550;
                avatar_.setEmotion(Emotion::Happy);
                break;
            case ButtonKind::Angry:
                state_.leftEye.weight = 70;
                state_.rightEye.weight = 70;
                state_.leftEye.rotation = 450;
                state_.rightEye.rotation = -450;
                avatar_.setEmotion(Emotion::Angry);
                break;
            case ButtonKind::Sad:
                state_.leftEye.weight = 70;
                state_.rightEye.weight = 70;
                state_.leftEye.rotation = -400;
                state_.rightEye.rotation = 400;
                avatar_.setEmotion(Emotion::Sad);
                break;
            case ButtonKind::Doubt:
                state_.leftEye.weight = 75;
                state_.rightEye.weight = 75;
                state_.leftEye.rotation = 0;
                state_.rightEye.rotation = 0;
                avatar_.setEmotion(Emotion::Doubt);
                break;
            case ButtonKind::Sleepy:
                state_.leftEye.weight = 35;
                state_.rightEye.weight = 35;
                state_.leftEye.rotation = -50;
                state_.rightEye.rotation = 50;
                avatar_.setEmotion(Emotion::Sleepy);
                break;
            default:
                break;
        }

        sync_sliders();
        apply_state();
    }

    void sync_sliders()
    {
        for (auto& control : controls_) {
            lv_slider_set_value(control->slider, *control->value, LV_ANIM_OFF);
            update_value_label(*control);
        }
    }

    void apply_state()
    {
        apply_feature(state_.leftEye, avatar_.leftEye(), true);
        apply_feature(state_.rightEye, avatar_.rightEye(), true);
        apply_feature(state_.mouth, avatar_.mouth(), false);
    }

    template <typename Feature>
    void apply_feature(const FeatureState& state, Feature& feature, bool apply_size)
    {
        feature.setPosition({state.x, state.y});
        feature.setRotation(state.rotation);
        feature.setWeight(state.weight);
        if (apply_size) {
            feature.setSize(state.size);
        }
    }

    lv_display_t* display_ = nullptr;
    lv_obj_t* avatar_viewport_ = nullptr;
    DefaultAvatar avatar_;
    ExpressionState state_;
    std::vector<std::unique_ptr<ControlBinding>> controls_;
    std::vector<std::unique_ptr<ButtonAction>> actions_;
    std::vector<SDL_Keycode> pending_keys_;
    int selected_control_ = 0;
    bool speech_visible_ = false;
    bool blink_closed_ = false;
    bool quit_ = false;
};

}  // namespace

int main()
{
    AvatarSimulator simulator;
    if (!simulator.init()) {
        return EXIT_FAILURE;
    }
    return simulator.run();
}
