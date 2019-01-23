#include "dynamics.h"
#include "simulator.h"

namespace multirotor_sim
{
Dynamics::Dynamics() {}


void Dynamics::load(std::string filename)
{
  Vector4d q_b_u;
  get_yaml_node("mass", filename, mass_);
  get_yaml_node("drag_constant", filename, drag_constant_);
  get_yaml_node("max_thrust", filename, max_thrust_);
  get_yaml_node("angular_drag_constant", filename, angular_drag_);
  get_yaml_node("RK4", filename, RK4_);
  get_yaml_eigen("p_b_u", filename, p_b_u_);
  get_yaml_eigen("q_b_u", filename, q_b_u);
  q_b_u_ = Quatd(q_b_u);

  // Initialize wind and its random walk/noise parameters
  double vw_init_var, vw_walk_stdev;
  get_yaml_node("wind_init_stdev", filename, vw_init_var);
  get_yaml_node("wind_walk_stdev", filename, vw_walk_stdev_);
  get_yaml_node("enable_wind", filename, wind_enabled_);
  vw_ = vw_init_var * Eigen::Vector3d::Random();
  if (!wind_enabled_)
    vw_.setZero();

  int seed;
  get_yaml_node("seed", filename, seed);
  if (seed < 0)
    seed = std::chrono::system_clock::now().time_since_epoch().count();
  rng_ = std::default_random_engine(seed);

  Vector3d inertia_diag;
  get_yaml_eigen("x0", filename, x_.arr);
  if (get_yaml_eigen<Vector3d>("inertia", filename, inertia_diag))
  {
    inertia_matrix_ = inertia_diag.asDiagonal();
    inertia_inv_ = inertia_matrix_.inverse();
  }
}

void Dynamics::f(const State &x, const Vector4d &u, ErrorState &dx) const
{
  Eigen::Vector3d v_rel_ = x.v - x.q.rotp(vw_); // Vehicle air velocity
  dx.p = x.q.rota(x.v);
  dx.v = -1.0 * e_z * u(THRUST)*max_thrust_ / mass_ - drag_constant_ * v_rel_ + x.q.rotp(gravity_) - x.w.cross(x.v);
  dx.q = x.w;
  dx.w = inertia_inv_ * (u.segment<3>(TAUX) - x.w.cross(inertia_matrix_ * x.w) - angular_drag_ * x.w.cwiseProduct(x.w));
}

void Dynamics::f(const State &x, const Vector4d &u, ErrorState &dx, Vector6d& imu) const
{
    f(x, u, dx);
    imu.segment<3>(ACC) = q_b_u_.rotp(dx.v + x.w.cross(x.v) + x.w.cross(x.w.cross(p_b_u_)) + dx.w.cross(p_b_u_) - x.q.rotp(gravity_));
    imu.segment<3>(GYRO) = q_b_u_.rotp(x.w);
}

void Dynamics::run(const double dt, const Vector4d &u)
{
  if (RK4_)
  {
    // 4th order Runge-Kutta integration
    f(x_, u, k1_, imu_);

    x2_ = x_;
    x2_ += k1_ * (dt/2.0);
    f(x2_, u, k2_);

    x3_ = x_;
    x3_ += k2_ * (dt/2.0);
    f(x3_, u, k3_);

    x4_ = x_;
    x4_ += k3_ * dt;
    f(x4_, u, k4_);

    dx_ = (k1_ + k2_*2.0 + k3_*2.0 + k4_) * (dt / 6.0);
  }
  else
  {
    // Euler integration
    f(x_, u, dx_, imu_);
    dx_.arr *= dt;
  }

  // Copy output
  x_ += dx_;

  // Update wind velocity for next iteration
  if (wind_enabled_)
  {
    Eigen::Vector3d vw_walk;
    random_normal_vec(vw_walk, vw_walk_stdev_, standard_normal_dist_, rng_);
    vw_ += vw_walk * dt;
  }
}

Vector3d Dynamics::get_imu_accel() const
{
  return imu_.segment<3>(ACC);
}

Vector3d Dynamics::get_imu_gyro() const
{
  return imu_.segment<3>(GYRO);
}

}
