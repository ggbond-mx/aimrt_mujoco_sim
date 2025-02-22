// Copyright (c) 2023, AgiBot Inc.
// All rights reserved.

#include "mujoco_sim_module/publisher/joint_sensor_publisher.h"
#include "aimrt_module_protobuf_interface/channel/protobuf_channel.h"
#include "aimrt_module_protobuf_interface/util/protobuf_tools.h"
#include "mujoco_sim_module/global.h"

namespace YAML {
template <>
struct convert<aimrt_mujoco_sim::mujoco_sim_module::JointSensorPublisher::Options> {
  using Options = aimrt_mujoco_sim::mujoco_sim_module::JointSensorPublisher::Options;

  static Node encode(const Options& rhs) {
    Node node;

    node["joints"] = YAML::Node();
    for (const auto& joint : rhs.joints) {
      Node joint_node;
      joint_node["name"] = joint.name;
      joint_node["bind_joint"] = joint.bind_joint;
      joint_node["bind_jointpos_sensor"] = joint.bind_jointpos_sensor;
      joint_node["bind_jointvel_sensor"] = joint.bind_jointvel_sensor;
      node["joints"].push_back(joint_node);
    }

    return node;
  }

  static bool decode(const Node& node, Options& rhs) {
    if (node["joints"] && node["joints"].IsSequence()) {
      for (const auto& joint_node : node["joints"]) {
        auto joint_node_options = Options::Joint{
            .name = joint_node["name"].as<std::string>(),
            .bind_joint = joint_node["bind_joint"].as<std::string>(),
            .bind_jointpos_sensor = joint_node["bind_jointpos_sensor"].as<std::string>(),
            .bind_jointvel_sensor = joint_node["bind_jointvel_sensor"].as<std::string>()};

        rhs.joints.emplace_back(std::move(joint_node_options));
      }
    }
    return true;
  }
};
}  // namespace YAML

namespace aimrt_mujoco_sim::mujoco_sim_module {

void JointSensorPublisher::Initialize(YAML::Node options_node) {
  if (options_node && !options_node.IsNull())
    options_ = options_node.as<Options>();

  CheckFrequency();
  RegisterSensorAddr();

  options_node = options_;

  bool ret = aimrt::channel::RegisterPublishType<aimrt::protocols::sensor::JointState>(publisher_);

  AIMRT_CHECK_ERROR_THROW(ret, "Register publish type failed.");
}

void JointSensorPublisher::Start() {
}

void JointSensorPublisher::Shutdown() {
}

void JointSensorPublisher::PublishSensorData() {
  static constexpr uint32_t ONE_MB = 1024 * 1024;

  if (conter_++ < avg_interval_) return;

  std::unique_ptr<SensorStateGroup[]> state_array(new SensorStateGroup[joint_num_]);

  for (size_t i = 0; i < joint_num_; i++) {
    const auto& addr = sensor_addr_vec_[i];
    state_array[i].jointpos_state = addr.jointpos_addr >= 0 ? d_->sensordata[addr.jointpos_addr] : 0.0;
    state_array[i].jointvel_state = addr.jointvel_addr >= 0 ? d_->sensordata[addr.jointvel_addr] : 0.0;
  }

  executor_.Execute([this, state_array = std::move(state_array)]() {
    aimrt::protocols::sensor::JointState state;
    for (int i = 0; i < joint_num_; ++i) {
      auto* data = state.add_data();
      data->set_name(name_vec_[i]);
      data->set_position(state_array[i].jointpos_state);
      data->set_velocity(state_array[i].jointvel_state);
    }

    aimrt::channel::Publish(publisher_, state);
  });

  avg_interval_ += avg_interval_base_;

  if (conter_ > ONE_MB) {
    avg_interval_ -= ONE_MB;
    conter_ -= ONE_MB;
  }
}

void JointSensorPublisher::RegisterSensorAddr() {
  for (const auto& joint : options_.joints) {
    const uint32_t pos_idx = !joint.bind_jointpos_sensor.empty()
                                 ? mj_name2id(m_, mjOBJ_SENSOR, joint.bind_jointpos_sensor.c_str())
                                 : -1;

    const uint32_t vel_idx = !joint.bind_jointvel_sensor.empty()
                                 ? mj_name2id(m_, mjOBJ_SENSOR, joint.bind_jointvel_sensor.c_str())
                                 : -1;

    if (!joint.bind_jointpos_sensor.empty() && pos_idx < 0) {
      AIMRT_CHECK_ERROR_THROW(false, "Invalid position sensor name '{}'.",
                              joint.bind_jointpos_sensor);
    }
    if (!joint.bind_jointvel_sensor.empty() && vel_idx < 0) {
      AIMRT_CHECK_ERROR_THROW(false, "Invalid velocity sensor name '{}'.",
                              joint.bind_jointvel_sensor);
    }

    sensor_addr_vec_.emplace_back(SensorAddrGroup{
        .jointpos_addr = pos_idx,
        .jointvel_addr = vel_idx});

    name_vec_.emplace_back(joint.name);
  }

  joint_num_ = sensor_addr_vec_.size();
}

void JointSensorPublisher::CheckFrequency() {
  constexpr static uint32_t MAX_SIM_FRQ = 1000;
  constexpr static double kError = 0.05;

  AIMRT_CHECK_ERROR_THROW(channel_frq_ <= MAX_SIM_FRQ,
                          "Invalid channel frequency {}, exceeds maximum frequency (1000 Hz)",
                          channel_frq_);
  avg_interval_base_ = static_cast<double>(MAX_SIM_FRQ) / static_cast<double>(channel_frq_);

  if (MAX_SIM_FRQ % channel_frq_ == 0) return;

  const uint32_t lower_interval = MAX_SIM_FRQ / channel_frq_;
  const uint32_t upper_interval = lower_interval + 1;

  const double lower_error = std::abs(lower_interval - avg_interval_base_) / avg_interval_base_;
  const double upper_error = std::abs(upper_interval - avg_interval_base_) / avg_interval_base_;

  AIMRT_CHECK_ERROR_THROW((lower_error <= kError && upper_error <= kError),
                          "Invalid channel frequency {}, which cauess the frequency error is more than {} ",
                          channel_frq_, kError);
}

}  // namespace aimrt_mujoco_sim::mujoco_sim_module
