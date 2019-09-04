#pragma once

#include <Eigen/Core>

#include "multirotor_sim/state.h"

namespace  multirotor_sim
{

class VehicleBase
{
public:
  virtual void step(const double& dt) = 0;
  virtual void arucoLocation(Vector3d& pt) = 0;
  virtual void landmarkLocations(std::vector<Vector3d>& pts) = 0;
  virtual Vector2d getPosition() = 0;
};

}
