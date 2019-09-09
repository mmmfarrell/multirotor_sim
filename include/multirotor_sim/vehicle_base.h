#pragma once

#include <Eigen/Core>
#include "geometry/quat.h"

#include "multirotor_sim/state.h"

namespace multirotor_sim
{
class VehicleBase
{
public:
  virtual void step(const double& dt) = 0;
  virtual void arucoLocation(Vector3d& pt) = 0;
  virtual void arucoOrientation(quat::Quatd& q_I_a) = 0;
  virtual void landmarkLocations(std::vector<int>& ids,
                                 std::vector<Vector3d>& pts) = 0;
  virtual Vector2d getPosition() = 0;
};

}  // namespace multirotor_sim
