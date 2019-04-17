#pragma once

#include <Eigen/Core>

#include "multirotor_sim/state.h"
#include "multirotor_sim/vehicle_base.h"

namespace  multirotor_sim
{

class EmptyVehicle : public VehicleBase
{
public:
  EmptyVehicle() {}

  void step(const double& dt) {}
  void landmarkLocations(std::vector<Vector3d>& pts)
  {
    pts.clear();
  }

  virtual Vector2d getPosition()
  {
    Vector2d pos;
    pos.setZero();
    return pos;
  }
};

}
