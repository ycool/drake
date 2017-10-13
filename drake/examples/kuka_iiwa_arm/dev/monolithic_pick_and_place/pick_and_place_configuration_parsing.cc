#include "drake/examples/kuka_iiwa_arm/dev/monolithic_pick_and_place/pick_and_place_configuration_parsing.h"

#include <algorithm>

#include "google/protobuf/text_format.h"

#include "drake/common/drake_throw.h"
#include "drake/common/find_resource.h"
#include "drake/common/proto/protobuf.h"
#include "drake/examples/kuka_iiwa_arm/dev/monolithic_pick_and_place/pick_and_place_configuration.pb.h"
#include "drake/math/roll_pitch_yaw.h"

namespace drake {
namespace examples {
namespace kuka_iiwa_arm {
namespace monolithic_pick_and_place {

using math::rpy2rotmat;

namespace {
proto::PickAndPlaceConfiguration ReadProtobufFileOrThrow(
    const std::string& filename) {
  auto istream =
      drake::MakeFileInputStreamOrThrow(FindResourceOrThrow(filename));
  proto::PickAndPlaceConfiguration configuration;
  google::protobuf::TextFormat::Parse(istream.get(), &configuration);
  return configuration;
}

Isometry3<double> ParsePose(const proto::Pose& pose) {
  Isometry3<double> X{Isometry3<double>::Identity()};
  if (!pose.xyz().empty()) {
    DRAKE_THROW_UNLESS(pose.xyz_size() == 3);
    X.translation() = Vector3<double>(pose.xyz(0), pose.xyz(1), pose.xyz(2));
  }
  if (!pose.rpy().empty()) {
    DRAKE_THROW_UNLESS(pose.rpy_size() == 3);
    X.linear() =
        rpy2rotmat(Vector3<double>(pose.rpy(0), pose.rpy(1), pose.rpy(2)));
  }
  return X;
}
}  // namespace

PlannerConfiguration ParsePlannerConfigurationOrThrow(
    std::string filename, RobotBaseIndex robot_base_index,
    TargetIndex target_index) {
  PlannerConfiguration planner_configuration;

  // Read configuration file
  const proto::PickAndPlaceConfiguration configuration{
      ReadProtobufFileOrThrow(filename)};

  // Extract Optitrack Ids for tables
  std::transform(configuration.table().begin(), configuration.table().end(),
                 std::back_inserter(planner_configuration.table_optitrack_ids),
                 [](const proto::Model& model) -> int {
                   return model.optitrack_info().id();
                 });

  // Extract table radii
  std::transform(configuration.table().begin(), configuration.table().end(),
                 std::back_inserter(planner_configuration.table_radii),
                 [&configuration](const proto::Model& model) -> double {
                   DRAKE_THROW_UNLESS(
                       configuration.planning_geometry().find(model.name()) !=
                       configuration.planning_geometry().end());
                   const proto::Geometry& geometry =
                       configuration.planning_geometry().at(model.name());
                   double radius{};
                   switch (geometry.geometry_case()) {
                     case proto::Geometry::kBox: {
                       radius = *std::min_element(geometry.box().size().begin(),
                                                  geometry.box().size().end());
                     } break;
                     case proto::Geometry::kCylinder: {
                       radius = geometry.cylinder().radius();
                     } break;
                     case proto::Geometry::GEOMETRY_NOT_SET: {
                       DRAKE_THROW_UNLESS(geometry.geometry_case() !=
                                          proto::Geometry::GEOMETRY_NOT_SET);
                     }
                   }
                   return radius;
                 });

  // Extract IIWA Optitrack ID
  DRAKE_THROW_UNLESS(robot_base_index < configuration.robot_base_size());
  planner_configuration.iiwa_base_optitrack_id =
      configuration.robot_base(robot_base_index).optitrack_info().id();

  // Extract target Optitrack ID
  DRAKE_THROW_UNLESS(target_index < configuration.object_size());
  const proto::Model& target = configuration.object(target_index);
  planner_configuration.target_optitrack_id = target.optitrack_info().id();

  // Extract target dimensions
  DRAKE_THROW_UNLESS(configuration.planning_geometry().find(target.name()) !=
                     configuration.planning_geometry().end());
  const proto::Geometry& target_geometry =
      configuration.planning_geometry().at(target.name());
  switch (target_geometry.geometry_case()) {
    case proto::Geometry::kBox: {
      const proto::Geometry::Box& box = target_geometry.box();
      planner_configuration.target_dimensions =
          Vector3<double>(box.size(0), box.size(1), box.size(2));
    } break;
    case proto::Geometry::kCylinder: {
      proto::Geometry::Cylinder cylinder = target_geometry.cylinder();
      planner_configuration.target_dimensions = Vector3<double>(
          2 * cylinder.radius(), 2 * cylinder.radius(), cylinder.length());
    } break;
    case proto::Geometry::GEOMETRY_NOT_SET: {
      DRAKE_THROW_UNLESS(target_geometry.geometry_case() !=
                         proto::Geometry::GEOMETRY_NOT_SET);
    }
  }
  planner_configuration.model_path = configuration.planning_model_path();
  planner_configuration.end_effector_name =
      configuration.planning_end_effector_name();
  return planner_configuration;
}

SimulatedPlantConfiguration ParseSimulatedPlantConfigurationOrThrow(
    const std::string& filename) {
  SimulatedPlantConfiguration plant_configuration;

  // Read configuration file
  const proto::PickAndPlaceConfiguration configuration{
      ReadProtobufFileOrThrow(filename)};

  // Extract base positions
  std::transform(
      configuration.robot_base().begin(), configuration.robot_base().end(),
      std::back_inserter(plant_configuration.base_xy_positions),
      [](const proto::RobotBase& robot_base) -> Vector2<double> {
        return Vector2<double>(robot_base.base_x(), robot_base.base_y());
      });

  // Extract table model paths
  std::transform(
      configuration.table().begin(), configuration.table().end(),
      plant_configuration.table_models.begin(),
      [&configuration](const proto::Model& table) -> std::string {
        DRAKE_THROW_UNLESS(configuration.model_path().find(table.name()) !=
                           configuration.model_path().end());
        return FindResourceOrThrow(configuration.model_path().at(table.name()));
      });

  // Extract table poses
  std::transform(
      configuration.table().begin(), configuration.table().end(),
      std::back_inserter(plant_configuration.table_poses),
      [](const proto::Model& table) -> Isometry3<double> {
        Isometry3<double> X_WT;
        X_WT.translation() = Vector3<double>(
            table.pose().xyz(0), table.pose().xyz(1), table.pose().xyz(2));
        X_WT.linear() = rpy2rotmat(Vector3<double>(
            table.pose().rpy(0), table.pose().rpy(1), table.pose().rpy(2)));
        return X_WT;
      });

  // Extract object model paths
  std::transform(
      configuration.object().begin(), configuration.object().end(),
      plant_configuration.object_models.begin(),
      [&configuration](const proto::Model& object) -> std::string {
        DRAKE_THROW_UNLESS(configuration.model_path().find(object.name()) !=
                           configuration.model_path().end());
        return FindResourceOrThrow(
            configuration.model_path().at(object.name()));
      });

  // Extract object poses
  std::transform(
      configuration.object().begin(), configuration.object().end(),
      std::back_inserter(plant_configuration.object_poses),
      [](const proto::Model& object) -> Isometry3<double> {
        Isometry3<double> X_WT;
        X_WT.translation() = Vector3<double>(
            object.pose().xyz(0), object.pose().xyz(1), object.pose().xyz(2));
        X_WT.linear() = rpy2rotmat(Vector3<double>(
            object.pose().rpy(0), object.pose().rpy(1), object.pose().rpy(2)));
        return X_WT;
      });

  return plant_configuration;
}

}  // namespace monolithic_pick_and_place
}  // namespace kuka_iiwa_arm
}  // namespace examples
}  // namespace drake
