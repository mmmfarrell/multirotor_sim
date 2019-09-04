#pragma once

#include <Eigen/Core>

#include "multirotor_sim/state.h"
#include "multirotor_sim/vehicle_base.h"

namespace multirotor_sim
{
class EmptyVehicle : public VehicleBase
{
public:
  EmptyVehicle()
  {
  }

  void step(const double& dt)
  {
  }
  void arucoLocation(Vector3d& pt)
  {
  }

  void landmarkLocations(std::vector<int>& ids, std::vector<Vector3d>& pts)
  {
    ids.clear();
    pts.clear();
  }

  virtual Vector2d getPosition()
  {
    Vector2d pos;
    pos.setZero();
    return pos;
  }
};

}  // namespace multirotor_sim
