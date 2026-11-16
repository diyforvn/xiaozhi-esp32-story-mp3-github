#include "robot_base.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <math.h>
#include <stdexcept>
#include <string>

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "robot_emotion_hook.h"

#define TAG "RobotBase"
#define TICK_MS 20

#if defined(CONFIG_ROBOT_VL53L0X_ENABLE) && CONFIG_ROBOT_VL53L0X_ENABLE
static constexpr bool kVl53l0xEnabled = true;
#else
static constexpr bool kVl53l0xEnabled = false;
#endif

#if defined(CONFIG_ROBOT_BMI160_ENABLE) && CONFIG_ROBOT_BMI160_ENABLE
static constexpr bool kBmi160Enabled = true;
#else
static constexpr bool kBmi160Enabled = false;
#endif

static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }

static const char* MotionStateName(robot::MotionState state) {
    switch (state) {
        case robot::MotionState::kUncalibrated: return "uncalibrated";
        case robot::MotionState::kResting: return "resting";
        case robot::MotionState::kPickedUp: return "picked_up";
        case robot::MotionState::kHeld: return "held";
    }
    return "unknown";
}

static const char* TiltDirectionName(const robot::MotionSnapshot& motion) {
    if (motion.pitch_deg >= 18) return "forward";
    if (motion.pitch_deg <= -18) return "backward";
    if (motion.roll_deg >= 18) return "right";
    if (motion.roll_deg <= -18) return "left";
    return "level";
}

RobotBase& RobotBase::GetInstance() {
    static RobotBase instance;
    return instance;
}

i2c_master_bus_handle_t RobotBase::CreateI2cBus() {
#if defined(CONFIG_ROBOT_I2C_SDA) && defined(CONFIG_ROBOT_I2C_SCL)
    i2c_master_bus_handle_t bus = nullptr;
    i2c_master_bus_config_t cfg = {
        .i2c_port = (i2c_port_t)0,
        .sda_io_num = (gpio_num_t)CONFIG_ROBOT_I2C_SDA,
        .scl_io_num = (gpio_num_t)CONFIG_ROBOT_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "cannot create I2C bus on SDA=%d SCL=%d", CONFIG_ROBOT_I2C_SDA, CONFIG_ROBOT_I2C_SCL);
        return nullptr;
    }
    return bus;
#else
    // OLED build: the board already owns the I2C bus and passes it to Init(); nothing to create here.
    return nullptr;
#endif
}

bool RobotBase::Init(i2c_master_bus_handle_t i2c_bus) {
    if (initialized_) return true;
    mu_ = xSemaphoreCreateMutex();
    if (!mu_) {
        ESP_LOGE(TAG, "cannot create robot mutex");
        return false;
    }

    // ---- configuration (menuconfig: Xiaozhi Assistant -> Robot Base) ----------------------
    robot::ControllerConfig cc;
#ifdef CONFIG_ROBOT_WANDER_ENABLE
    cc.wander = true;
#else
    cc.wander = false;
#endif
#ifdef CONFIG_ROBOT_STOP_WHEN_LISTENING
    cc.stop_when_listening = true;
#else
    cc.stop_when_listening = false;
#endif
    stop_when_listening_ = cc.stop_when_listening;
#ifdef CONFIG_ROBOT_BLIND_REVERSE
    cc.blind_reverse = true;
#else
    cc.blind_reverse = false;
#endif
#ifdef CONFIG_ROBOT_FACE_REACTION
    cc.face_reaction = true;
#else
    cc.face_reaction = false;
#endif
    cc.reverse_max_ms = CONFIG_ROBOT_REVERSE_MAX_MS;
    cc.emotion_max_ms = (uint32_t)CONFIG_ROBOT_EMOTION_MAX_SEC * 1000u;
    ctl_ = std::make_unique<robot::RobotController>(cc, (uint32_t)esp_timer_get_time());

    // ---- head servo ---------------------------------------------------------------------
    HeadServo::Config hc;
    hc.pin = (gpio_num_t)CONFIG_ROBOT_PIN_SERVO;
    hc.center_deg = CONFIG_ROBOT_HEAD_CENTER_DEG;
    hc.range_deg = CONFIG_ROBOT_HEAD_RANGE_DEG;
#ifdef CONFIG_ROBOT_HEAD_INVERT
    hc.invert = true;
#endif
    hc.min_us = CONFIG_ROBOT_SERVO_MIN_US;
    hc.max_us = CONFIG_ROBOT_SERVO_MAX_US;
    bool head_ok = head_.Init(hc);

    // ---- wheels -------------------------------------------------------------------------
    MotorDriver::Config mc;
    mc.left_in1 = (gpio_num_t)CONFIG_ROBOT_PIN_MOTOR_L_IN1;
    mc.left_in2 = (gpio_num_t)CONFIG_ROBOT_PIN_MOTOR_L_IN2;
    mc.right_in1 = (gpio_num_t)CONFIG_ROBOT_PIN_MOTOR_R_IN1;
    mc.right_in2 = (gpio_num_t)CONFIG_ROBOT_PIN_MOTOR_R_IN2;
#ifdef CONFIG_ROBOT_MOTOR_L_INVERT
    mc.left_invert = true;
#endif
#ifdef CONFIG_ROBOT_MOTOR_R_INVERT
    mc.right_invert = true;
#endif
    mc.min_start_pct = CONFIG_ROBOT_MOTOR_MIN_START_PCT;
    mc.max_pct = CONFIG_ROBOT_MOTOR_MAX_PCT;
    bool motors_ok = motors_.Init(mc);

    // ---- laser guard --------------------------------------------------------------------
    RangeGuard::Config gc;
    gc.int_pin = (gpio_num_t)CONFIG_ROBOT_PIN_TOF_INT;
    gc.cliff_mm = CONFIG_ROBOT_CLIFF_MM;
    gc.obstacle_margin_mm = CONFIG_ROBOT_OBSTACLE_MARGIN_MM;
    bool guard_ok = true;
    if (kVl53l0xEnabled) {
        motors_.SetForwardInhibit(true);
        guard_.SetHazardCallback([this](robot::Hazard h) {
            motors_.SetForwardInhibit(h != robot::Hazard::kNone);
            if (h == robot::Hazard::kSensorFault) motors_.SetMotionInhibit(true);
        });
        guard_ok = guard_.Init(i2c_bus, gc);
        if (!guard_ok) {
            motors_.SetForwardInhibit(true);
            motors_.SetMotionInhibit(true);
            ESP_LOGE(TAG, "VL53L0X enabled but unavailable -> movement is locked");
        }
    } else {
        motors_.SetForwardInhibit(false);
        ESP_LOGI(TAG, "VL53L0X disabled in menuconfig; cliff and obstacle checks are off");
    }

#if defined(CONFIG_ROBOT_BMI160_ENABLE) && CONFIG_ROBOT_BMI160_ENABLE
    Bmi160Motion::Config ic;
#if defined(CONFIG_ROBOT_BMI160_ADDRESS_69) && CONFIG_ROBOT_BMI160_ADDRESS_69
    ic.address = 0x69;
#endif
#if defined(CONFIG_ROBOT_BMI160_FORWARD_Y) && CONFIG_ROBOT_BMI160_FORWARD_Y
    ic.forward_axis = 1;
#endif
#if defined(CONFIG_ROBOT_BMI160_FORWARD_INVERT) && CONFIG_ROBOT_BMI160_FORWARD_INVERT
    ic.forward_sign = -1;
#endif
    ic.lift_tilt_deg = CONFIG_ROBOT_BMI160_LIFT_TILT_DEG;
    ic.release_impact_g = CONFIG_ROBOT_BMI160_RELEASE_IMPACT_MG / 1000.0f;
    ic.release_delta_g = CONFIG_ROBOT_BMI160_RELEASE_DELTA_MG / 1000.0f;
    if (!motion_.Init(i2c_bus, ic, [this](robot::MotionEvent event) { OnMotionEvent(event); })) {
        ESP_LOGW(TAG, "BMI160 enabled but unavailable; movement is locked until sensor is fixed or disabled in menuconfig");
    }
#endif

    BaseType_t task_ok = xTaskCreate(TaskEntry, "robot_engine", 4096, this, 5, nullptr);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create robot engine task");
        motors_.Brake();
        return false;
    }
    initialized_ = true;
    RegisterTools();
    RobotEmotionHook::Set([this](const char* e) { OnEmotion(e); });
    RobotEmotionHook::SetEmotionResolver([this](const char* e) {
        if (MotionHeld()) return std::string("shocked");
        return e ? std::string(e) : std::string("neutral");
    });
    RobotEmotionHook::SetStateHandler([this](DeviceState state) { OnDeviceState(state); });
    ESP_LOGI(TAG, "robot base up: head=%d motors=%d VL53L0X enabled=%d ready=%d BMI160 enabled=%d ready=%d",
             head_ok, motors_ok, kVl53l0xEnabled, guard_ok, kBmi160Enabled, !kBmi160Enabled || motion_.present());
    return head_ok && motors_ok && guard_ok;
}

bool RobotBase::MotionHeld() const {
    if (!kBmi160Enabled) return false;
    const robot::MotionState state = motion_.snapshot().state;
    return state == robot::MotionState::kPickedUp || state == robot::MotionState::kHeld;
}

bool RobotBase::SensorsReady() const {
    if (kVl53l0xEnabled && (!guard_.healthy() || guard_.calibration_active())) return false;
    if (kBmi160Enabled && (!motion_.healthy() || motion_.calibrating() || MotionHeld() ||
                           motion_.snapshot().state == robot::MotionState::kUncalibrated)) return false;
    return true;
}

bool RobotBase::LocomotionAllowed() const {
    return !calibrating_floor_.load() && SensorsReady();
}

void RobotBase::OnEmotion(const char* emotion) {
    if (!initialized_ || !emotion || calibrating_floor_.load()) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    if (!calibrating_floor_.load()) {
        perform_end_ms_ = 0;                 // a real emotion from the server replaces a tool-driven one
        ctl_->SetEmotion(emotion, robot::DefaultEmotionIntensity(emotion));
    }
    xSemaphoreGive(mu_);
}

void RobotBase::OnDeviceState(DeviceState state) {
    if (!initialized_) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    if (state == kDeviceStateIdle) {
        if (last_device_state_ == kDeviceStateListening || last_device_state_ == kDeviceStateSpeaking) {
            ctl_->SetInteraction(false);
        }
        ctl_->SetEmotion("neutral");
        perform_end_ms_ = 0;
        motors_.SetMotionInhibit(ctl_->motion_held() || calibrating_floor_.load() || !SensorsReady());
    } else if (state == kDeviceStateListening) {
        if (last_device_state_ == kDeviceStateIdle || last_device_state_ == kDeviceStateConnecting) {
            ctl_->SetInteraction(true);
        }
        ctl_->SetEmotion("neutral");
        perform_end_ms_ = 0;
        motors_.SetMotionInhibit(ctl_->motion_held() || calibrating_floor_.load() || !SensorsReady() ||
                                 stop_when_listening_);
    } else if (state == kDeviceStateConnecting) {
        ctl_->Stop();
        ctl_->SetEmotion("neutral");
        perform_end_ms_ = 0;
        motors_.SetMotionInhibit(true);
    } else if (state != kDeviceStateSpeaking) {
        ctl_->Stop();
        ctl_->SetEmotion("neutral");
        perform_end_ms_ = 0;
        motors_.SetMotionInhibit(true);
    } else {
        motors_.SetMotionInhibit(true);
    }
    if (MotionHeld()) SetFace("shocked");
    last_device_state_ = state;
    xSemaphoreGive(mu_);
}

void RobotBase::SetFace(const char* emotion) {
    auto* display = Board::GetInstance().GetDisplay();
    if (!display || !emotion) return;
    const std::string face = MotionHeld() ? "shocked" : emotion;
    // Display calls belong to the main task, like application.cc does it.
    Application::GetInstance().Schedule([display, face]() { display->SetEmotion(face.c_str()); });
}

void RobotBase::OnMotionEvent(robot::MotionEvent event) {
    if (event == robot::MotionEvent::kShaken) {
        SetFace("confused");
        return;
    }
    const bool holding = event == robot::MotionEvent::kPickedUp || event == robot::MotionEvent::kHeld;
    if (holding) {
        motors_.SetMotionInhibit(true);
    }

    const char* emotion = nullptr;
    uint8_t intensity = 60;
    if (event == robot::MotionEvent::kPickedUp) {
        emotion = "shocked";
        intensity = 90;
    } else if (event == robot::MotionEvent::kHeld) {
        emotion = "shocked";
        intensity = 90;
    } else if (event == robot::MotionEvent::kReleased) {
        emotion = "happy";
        intensity = 60;
    }
    if (!emotion) return;

    xSemaphoreTake(mu_, portMAX_DELAY);
    if (event == robot::MotionEvent::kPickedUp || event == robot::MotionEvent::kHeld) {
        ctl_->SetMotionHeld(true);
        perform_end_ms_ = 0;
    } else if (event == robot::MotionEvent::kReleased) {
        ctl_->SetMotionHeld(false);
    }
    ctl_->SetEmotion(emotion, intensity);
    if (event == robot::MotionEvent::kReleased || event == robot::MotionEvent::kShaken) {
        perform_end_ms_ = NowMs() + 2500;
    }
    xSemaphoreGive(mu_);
    SetFace(emotion);
}

void RobotBase::TaskEntry(void* arg) {
    static_cast<RobotBase*>(arg)->Loop();
    vTaskDelete(nullptr);
}

void RobotBase::Loop() {
    const float dt_s = TICK_MS / 1000.0f;
    const float kAccel = 300.0f, kDecel = 800.0f;   // wheel % per second
    const float kHeadRate = 450.0f;                 // head pose units per second
    TickType_t last = xTaskGetTickCount();
    int last_pose = 9999;
    bool motor_test_was_active = false;
    bool manual_was_active = false;
    bool manual_was_blocked = false;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));

        DeviceState st = Application::GetInstance().GetDeviceState();
        robot::ControllerInput in;
        in.dt_ms = TICK_MS;
        in.hazard = !kVl53l0xEnabled ? robot::Hazard::kNone :
                    (guard_.present() ? guard_.hazard() : robot::Hazard::kSensorFault);
        in.sensor_ok = !kVl53l0xEnabled || guard_.healthy();
        in.listening = (st == kDeviceStateListening);
        in.speaking = (st == kDeviceStateSpeaking);
        in.idle = (st == kDeviceStateIdle);
        in.motion_allowed = !calibrating_floor_.load() && SensorsReady() &&
                            (in.idle || in.listening || in.speaking);

        robot::ControllerOutput out;
        xSemaphoreTake(mu_, portMAX_DELAY);
        if (perform_end_ms_ && NowMs() >= perform_end_ms_) {
            perform_end_ms_ = 0;
            ctl_->SetEmotion("neutral");
            SetFace("neutral");
        }
        out = ctl_->Tick(in);
        xSemaphoreGive(mu_);

        if (out.face) SetFace(out.face);

        // Stop immediately during calibration, unavailable sensing, or a quiet conversation state.
        const bool motor_test_requested = NowMs() < motor_test_until_ms_.load();
        const bool motor_test_active = motor_test_requested && SensorsReady();
        if (motor_test_requested && !motor_test_active) {
            motor_test_until_ms_.store(0);
            ESP_LOGE(TAG, "forced motor test cancelled because an enabled sensor is not ready");
        }
        const bool hard_stop = !motor_test_active && (!SensorsReady() || calibrating_floor_.load() ||
                               !in.motion_allowed || in.speaking ||
                               (in.listening && stop_when_listening_ && !out.manual && !out.wander));
        if (out.manual && (!manual_was_active || hard_stop != manual_was_blocked)) {
            ESP_LOGI(TAG, "manual move state=%d output L=%d R=%d hard_stop=%d listening=%d speaking=%d sensors tof_ok=%d imu_ok=%d tof_disabled=%d imu_disabled=%d",
                     (int)st, out.left, out.right, hard_stop, in.listening, in.speaking,
                     kVl53l0xEnabled ? guard_.healthy() : true,
                     kBmi160Enabled ? motion_.healthy() : true, !kVl53l0xEnabled, !kBmi160Enabled);
        }
        manual_was_active = out.manual;
        manual_was_blocked = hard_stop;
        if (motor_test_was_active && !motor_test_active) {
            motors_.Brake();
            motors_.SetForwardInhibit(kVl53l0xEnabled && guard_.hazard() != robot::Hazard::kNone);
            ESP_LOGI(TAG, "forced motor test ended; normal safety interlocks restored");
        }
        motors_.SetMotionInhibit(hard_stop);
        if (motor_test_active) {
            motors_.SetForwardInhibit(false);
            cur_l_ = static_cast<float>(motor_test_left_.load());
            cur_r_ = static_cast<float>(motor_test_right_.load());
            motors_.DriveTest((int)cur_l_, (int)cur_r_);
            if (!motor_test_was_active) {
                ESP_LOGW(TAG, "FORCED MOTOR TEST active: L=%d%% R=%d%%; sensor health was checked, hazard and conversation locks bypassed",
                         motor_test_left_.load(), motor_test_right_.load());
            }
        } else if (hard_stop) {
            cur_l_ = cur_r_ = 0;
            motors_.Brake();
        } else {
            cur_l_ = robot::RampToward(cur_l_, (float)out.left, dt_s, kAccel, kDecel);
            cur_r_ = robot::RampToward(cur_r_, (float)out.right, dt_s, kAccel, kDecel);
            motors_.Drive((int)lroundf(cur_l_), (int)lroundf(cur_r_));
        }
        motor_test_was_active = motor_test_active;
        motion_.SetRobotStationary(hard_stop || (fabsf(cur_l_) < 1.0f && fabsf(cur_r_) < 1.0f));
        const int floor_error = (int)guard_.distance_mm() - (int)guard_.floor_mm();
        const int absolute_floor_error = floor_error < 0 ? -floor_error : floor_error;
        motion_.SetRobotOnCalibratedFloor(!kVl53l0xEnabled ||
                                          (!calibrating_floor_.load() && !guard_.calibration_active() &&
                                          guard_.healthy() && guard_.hazard() == robot::Hazard::kNone &&
                                          guard_.floor_mm() > 0 && absolute_floor_error <= 20));

        // head
        cur_head_ = robot::RampToward(cur_head_, out.head, dt_s, kHeadRate, kHeadRate);
        int pose = (int)lroundf(cur_head_);
        if (pose != last_pose) {
            head_.SetPose(pose);
            last_pose = pose;
        }
    }
}

// ---------------------------------------------------------------------------------------
// MCP tools (what the LLM can call)
// ---------------------------------------------------------------------------------------
void RobotBase::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.robot.get_status",
                "Get robot status: current mode/behavior, laser distance to the floor ahead in mm, hazard "
                "(none/obstacle/cliff/sensor_fault/unknown), calibrated floor distance, head pose, emotion "
                "intensity, wander on/off, enabled sensor configuration and health, and BMI160 lift/tilt state and orientation.",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    cJSON* j = cJSON_CreateObject();
                    cJSON_AddStringToObject(j, "mode", ctl_->ModeName());
                    cJSON_AddStringToObject(j, "behavior", ctl_->BehaviorName());
                    cJSON_AddStringToObject(j, "emotion", ctl_->last_emotion());
                    cJSON_AddNumberToObject(j, "intensity", ctl_->emotion_intensity());
                    cJSON_AddBoolToObject(j, "wander", ctl_->wander_enabled());
                    xSemaphoreGive(mu_);
                    const robot::MotionSnapshot imu = motion_.snapshot();
                    cJSON_AddStringToObject(j, "hazard", robot::HazardName(
                        kVl53l0xEnabled ? guard_.hazard() : robot::Hazard::kNone));
                    cJSON_AddNumberToObject(j, "distance_mm", guard_.distance_mm());
                    cJSON_AddNumberToObject(j, "floor_mm", guard_.floor_mm());
                    cJSON_AddBoolToObject(j, "vl53l0x_enabled", kVl53l0xEnabled);
                    cJSON_AddBoolToObject(j, "vl53l0x_ok", !kVl53l0xEnabled || guard_.healthy());
                    cJSON_AddBoolToObject(j, "sensor_ok", SensorsReady());
                    cJSON_AddBoolToObject(j, "imu_enabled", kBmi160Enabled);
                    cJSON_AddBoolToObject(j, "imu_present", motion_.present());
                    cJSON_AddBoolToObject(j, "imu_ok", !kBmi160Enabled ||
                        (motion_.healthy() && !motion_.calibrating() &&
                         motion_.snapshot().state != robot::MotionState::kUncalibrated));
                    cJSON_AddBoolToObject(j, "imu_calibrating", motion_.calibrating());
                    cJSON_AddBoolToObject(j, "sensors_ready", SensorsReady());
                    cJSON_AddStringToObject(j, "imu_state", MotionStateName(imu.state));
                    cJSON_AddStringToObject(j, "tilt_direction", TiltDirectionName(imu));
                    cJSON_AddNumberToObject(j, "pitch_deg", imu.pitch_deg);
                    cJSON_AddNumberToObject(j, "roll_deg", imu.roll_deg);
                    cJSON_AddNumberToObject(j, "head_pose", head_.pose());
                    char* s = cJSON_PrintUnformatted(j);
                    std::string r(s);
                    cJSON_free(s);
                    cJSON_Delete(j);
                    return r;
                });

    mcp.AddTool("self.robot.motor_test",
                "DANGEROUS diagnostic: directly apply raw PWM for at most 1 second. It still requires every "
                "menuconfig-enabled sensor to be healthy, but bypasses hazard, conversation, motion safety locks "
                "and the configured PWM cap. Use only with wheels lifted or the robot secured. "
                "direction: forward, backward, spin_left, spin_right; duty_percent 10-100.",
                PropertyList({Property("direction", kPropertyTypeString, "forward"),
                              Property("duty_percent", kPropertyTypeInteger, 50, 10, 100),
                              Property("duration_ms", kPropertyTypeInteger, 300, 100, 1000)}),
                [this](const PropertyList& p) -> ReturnValue {
                    if (!motors_.ready()) throw std::runtime_error("Motor driver is not initialized");
                    if (!SensorsReady()) {
                        throw std::runtime_error("An enabled sensor is not healthy; motor test is locked");
                    }
                    const std::string direction = p["direction"].value<std::string>();
                    const int duty = p["duty_percent"].value<int>();
                    const int duration_ms = p["duration_ms"].value<int>();
                    int left = 0;
                    int right = 0;
                    if (direction == "forward") { left = duty; right = duty; }
                    else if (direction == "backward") { left = -duty; right = -duty; }
                    else if (direction == "spin_left") { left = -duty; right = duty; }
                    else if (direction == "spin_right") { left = duty; right = -duty; }
                    else throw std::runtime_error("direction must be forward, backward, spin_left, or spin_right");

                    motor_test_left_.store(left);
                    motor_test_right_.store(right);
                    motor_test_until_ms_.store(NowMs() + duration_ms);
                    ESP_LOGW(TAG, "forced raw PWM test requested: dir=%s duty=%d%% duration=%dms (bypasses configured max=%d%%)",
                             direction.c_str(), duty, duration_ms, CONFIG_ROBOT_MOTOR_MAX_PCT);
                    cJSON* j = cJSON_CreateObject();
                    cJSON_AddBoolToObject(j, "started", true);
                    cJSON_AddStringToObject(j, "direction", direction.c_str());
                    cJSON_AddNumberToObject(j, "raw_pwm_duty_percent", duty);
                    cJSON_AddNumberToObject(j, "duration_ms", duration_ms);
                    char* s = cJSON_PrintUnformatted(j);
                    std::string r(s);
                    cJSON_free(s);
                    cJSON_Delete(j);
                    return r;
                });

    mcp.AddTool("self.robot.move",
                "Drive the robot wheels for a short time. direction: forward, backward, left (curve left), "
                "right (curve right), spin_left, spin_right. speed 10-100 percent, duration_ms 100-5000. "
                "The laser guard stops forward motion automatically at obstacles and table edges.",
                PropertyList({Property("direction", kPropertyTypeString, "forward"),
                              Property("speed", kPropertyTypeInteger, 75, 10, 100),
                              Property("duration_ms", kPropertyTypeInteger, 1000, 100, 5000)}),
                [this](const PropertyList& p) -> ReturnValue {
                    if (!LocomotionAllowed()) {
                        throw std::runtime_error("Robot movement is disabled by a sensor, calibration, or hold safety lock");
                    }
                    if (calibrating_floor_.load() || motion_.calibrating()) {
                        throw std::runtime_error("Sensor calibration is in progress");
                    }
                    std::string d = p["direction"].value<std::string>();
                    int s = p["speed"].value<int>();
                    uint32_t ms = (uint32_t)p["duration_ms"].value<int>();
                    int l, r;
                    if (d == "forward") { l = s; r = s; }
                    else if (d == "backward") { l = -s; r = -s; }
                    else if (d == "left") { l = s * 3 / 10; r = s; }
                    else if (d == "right") { l = s; r = s * 3 / 10; }
                    else if (d == "spin_left") { l = -s; r = s; }
                    else if (d == "spin_right") { l = s; r = -s; }
                    else throw std::runtime_error("Unknown direction: " + d);
                    ESP_LOGI(TAG, "manual move requested: dir=%s speed=%d%% duration=%lu ms",
                             d.c_str(), s, (unsigned long)ms);
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    if (calibrating_floor_.load() || !SensorsReady()) {
                        xSemaphoreGive(mu_);
                        throw std::runtime_error("Robot movement is disabled by calibration or hold safety lock");
                    }
                    ctl_->ManualMove(l, r, ms);
                    xSemaphoreGive(mu_);
                    return true;
                });

    mcp.AddTool("self.robot.stop", "Stop the wheels and cancel the current emotion/behavior.", PropertyList(),
                [this](const PropertyList&) -> ReturnValue {
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    perform_end_ms_ = 0;
                    ctl_->Stop();
                    xSemaphoreGive(mu_);
                    SetFace("neutral");
                    return true;
                });

    mcp.AddTool("self.robot.head",
                "Move the head (screen). pose -100 (head down) .. 0 (centre) .. +100 (head up). "
                "It holds for hold_ms then returns to normal behaviour.",
                PropertyList({Property("pose", kPropertyTypeInteger, 0, -100, 100),
                              Property("hold_ms", kPropertyTypeInteger, 3000, 300, 10000)}),
                [this](const PropertyList& p) -> ReturnValue {
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    ctl_->ManualHead(p["pose"].value<int>(), (uint32_t)p["hold_ms"].value<int>());
                    xSemaphoreGive(mu_);
                    return true;
                });

    mcp.AddTool("self.robot.perform",
                "Act out an emotion with wheels, head and face. emotion: happy, laughing, silly, sad, crying, "
                "angry, sleepy, surprised, confused, embarrassed, loving, thinking, cool, relaxed, winking, "
                "delicious, neutral. intensity 25-100 controls motion size. duration_s 1-60, then it returns to neutral.",
                PropertyList({Property("emotion", kPropertyTypeString, "happy"),
                              Property("duration_s", kPropertyTypeInteger, 8, 1, 60),
                              Property("intensity", kPropertyTypeInteger, 70, 25, 100)}),
                [this](const PropertyList& p) -> ReturnValue {
                    std::string e = p["emotion"].value<std::string>();
                    bool known = false;
                    robot::FindEmotionBehavior(e.c_str(), &known);
                    if (!known) throw std::runtime_error("Unknown emotion: " + e);
                    if (calibrating_floor_.load()) throw std::runtime_error("Floor calibration is in progress");
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    if (calibrating_floor_.load()) {
                        xSemaphoreGive(mu_);
                        throw std::runtime_error("Floor calibration is in progress");
                    }
                    ctl_->SetEmotion(e.c_str(), (uint8_t)p["intensity"].value<int>());
                    perform_end_ms_ = (e == "neutral") ? 0 : NowMs() + (int64_t)p["duration_s"].value<int>() * 1000;
                    xSemaphoreGive(mu_);
                    SetFace(e.c_str());
                    return true;
                });

    mcp.AddTool("self.robot.set_wander",
                "Turn free wandering (random driving and spinning while waiting in idle or listening state) on or off. "
                "Wandering pauses while Xiaozhi is speaking and resumes when listening resumes.",
                PropertyList({Property("enabled", kPropertyTypeBoolean)}),
                [this](const PropertyList& p) -> ReturnValue {
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    ctl_->SetWander(p["enabled"].value<bool>());
                    xSemaphoreGive(mu_);
                    return true;
                });

    if (kVl53l0xEnabled) mcp.AddTool("self.robot.calibrate_floor",
                "Re-measure the laser distance to the floor. Only call it when the robot stands on a flat floor "
                "with nothing in front. Takes about one second.",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    if (!kVl53l0xEnabled) throw std::runtime_error("VL53L0X is disabled in menuconfig");
                    if (!guard_.healthy()) throw std::runtime_error("Laser guard is not healthy");
                    if (motion_.calibrating()) throw std::runtime_error("BMI160 calibration is in progress");
                    const robot::MotionState motion_state = motion_.snapshot().state;
                    if (motion_state == robot::MotionState::kPickedUp || motion_state == robot::MotionState::kHeld) {
                        throw std::runtime_error("Set the robot down before calibrating the floor");
                    }
                    bool expected = false;
                    if (!calibrating_floor_.compare_exchange_strong(expected, true)) {
                        throw std::runtime_error("Floor calibration is already in progress");
                    }
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    perform_end_ms_ = 0;
                    ctl_->Stop();
                    xSemaphoreGive(mu_);
                    motors_.SetMotionInhibit(true);
                    motors_.Brake();
                    SetFace("neutral");
                    bool ok = guard_.Calibrate();
                    calibrating_floor_.store(false);
                    DeviceState state = Application::GetInstance().GetDeviceState();
                    bool quiet = state == kDeviceStateSpeaking ||
                                 (state == kDeviceStateListening && stop_when_listening_);
                    bool state_blocked = state != kDeviceStateIdle && state != kDeviceStateListening &&
                                         state != kDeviceStateSpeaking;
                    motors_.SetMotionInhibit(!ok || quiet || state_blocked || !SensorsReady());
                    if (!ok) throw std::runtime_error("Floor calibration failed; previous calibration was preserved");
                    cJSON* j = cJSON_CreateObject();
                    cJSON_AddBoolToObject(j, "ok", true);
                    cJSON_AddNumberToObject(j, "floor_mm", guard_.floor_mm());
                    char* s = cJSON_PrintUnformatted(j);
                    std::string r(s);
                    cJSON_free(s);
                    cJSON_Delete(j);
                    return r;
                });

    if (kBmi160Enabled) mcp.AddTool("self.robot.calibrate_imu",
                "Calibrate the BMI160 orientation. Place the robot upright on a level, stationary surface and call "
                "this while the device is idle. Takes about one second.",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    if (!kBmi160Enabled) throw std::runtime_error("BMI160 is disabled in menuconfig");
                    if (!motion_.present()) throw std::runtime_error("BMI160 is not available");
                    if (calibrating_floor_.load() || guard_.calibration_active()) {
                        throw std::runtime_error("Floor calibration is in progress");
                    }
                    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
                        throw std::runtime_error("BMI160 calibration is only allowed while the device is idle");
                    }
                    const bool was_held = motion_.snapshot().state == robot::MotionState::kPickedUp ||
                                          motion_.snapshot().state == robot::MotionState::kHeld;
                    xSemaphoreTake(mu_, portMAX_DELAY);
                    perform_end_ms_ = 0;
                    ctl_->Stop();
                    xSemaphoreGive(mu_);
                    motors_.SetMotionInhibit(true);
                    motors_.Brake();
                    SetFace("neutral");
                    bool ok = motion_.Calibrate();
                    if (ok && was_held) {
                        xSemaphoreTake(mu_, portMAX_DELAY);
                        ctl_->SetMotionHeld(false);
                        ctl_->SetEmotion("happy", 60);
                        perform_end_ms_ = NowMs() + 2500;
                        xSemaphoreGive(mu_);
                        SetFace("happy");
                    }
                    DeviceState state = Application::GetInstance().GetDeviceState();
                    bool state_blocked = state != kDeviceStateIdle && state != kDeviceStateListening &&
                                         state != kDeviceStateSpeaking;
                    const bool quiet = state == kDeviceStateSpeaking ||
                                       (state == kDeviceStateListening && stop_when_listening_);
                    motors_.SetMotionInhibit(!ok || quiet || state_blocked || !SensorsReady() ||
                                             calibrating_floor_.load());
                    if (!ok) {
                        throw std::runtime_error("BMI160 calibration failed; keep the robot still and level, then retry");
                    }
                    cJSON* j = cJSON_CreateObject();
                    cJSON_AddBoolToObject(j, "ok", true);
                    cJSON_AddStringToObject(j, "imu_state", MotionStateName(motion_.snapshot().state));
                    char* s = cJSON_PrintUnformatted(j);
                    std::string r(s);
                    cJSON_free(s);
                    cJSON_Delete(j);
                    return r;
                });
}
