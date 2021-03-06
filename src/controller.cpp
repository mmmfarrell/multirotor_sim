#include <stdio.h>

#include "multirotor_sim/controller.h"
#include "multirotor_sim/simulator.h"


namespace multirotor_sim
{

ReferenceController::ReferenceController() :
  prev_time_(0),
  initialized_(false)
{
  vhat_.setZero();
  s_prev_ = 0;
}

void ReferenceController::computeControl(const double& t, const State &x, const State& x_c, const Vector4d &ur, Vector4d& u)
{
  // Function constants
  static const Vector3d e3(0,0,1); // general unit vector in z-direction

  // Copy the current state
  xhat_ = x;


  // Time data
  t_c_ = t;
  double dt = t - prev_time_;
  prev_time_ = t;
  if (dt < 1e-7)
  {
    u.setZero();
    return;
  }
  xc_ = x_c;
  
  // Compute control
  double throttle;
  if (control_type_ == 0)
    nlc_.computeControl(xhat_, xc_, dt, sh_, throttle);
  else if (control_type_ == 1)
    lqr_.computeControl(xhat_, xc_, sh_, throttle);
  else
    throw std::runtime_error("Undefined control type in controller.cpp");

  // Calculate the Final Output Torques using PID
  u(multirotor_sim::THRUST) = throttle;
  u(multirotor_sim::TAUX) = roll_.run(dt, xhat_.q.roll(), xc_.q.roll(), false, xhat_.w(0));
  u(multirotor_sim::TAUY) = pitch_.run(dt, xhat_.q.pitch(), xc_.q.pitch(), false, xhat_.w(1));
  u(multirotor_sim::TAUZ) = yaw_rate_.run(dt, xhat_.w(2), xc_.w(2), false);

  // Hover throttle observer
  Matrix3d R_v1_to_b = frame_helper::R_v_to_b(xhat_.q.roll(), xhat_.q.pitch(), 0);
  Vector3d vhat_dot = multirotor_sim::G * (I_3x3 - sh_inv_hat_ * s_prev_ * R_v1_to_b.transpose()) * e3 -
      xhat_.w.cross(vhat_) + sh_kv_ * (xhat_.v - vhat_);
  double sh_inv_hat_dot = -sh_ks_ * multirotor_sim::G * s_prev_ * (xhat_.v - vhat_).transpose() * R_v1_to_b.transpose() * e3;
  vhat_ += vhat_dot * dt;
  sh_inv_hat_ += sh_inv_hat_dot * dt;
  sh_ = 1.0 / sh_inv_hat_;
  s_prev_ = throttle;
}

void ReferenceController::load(const std::string filename)
{
  if(file_exists(filename))
  {
    // Random number generation
    int seed;
    get_yaml_node("seed", filename, seed);
    if (seed == 0)
      seed = std::chrono::system_clock::now().time_since_epoch().count();
    rng_ = std::default_random_engine(seed);
    srand(seed);
    udist_ = std::uniform_real_distribution<double>(-1.0, 1.0);

    get_yaml_node("path_type", filename, path_type_);
    int num_waypoints;
    if (path_type_ == 0)
    {
      std::vector<double> loaded_wps;
      if (get_yaml_node("waypoints", filename, loaded_wps))
      {
        num_waypoints = std::floor(loaded_wps.size()/4.0);
        waypoints_ = Map<MatrixXd>(loaded_wps.data(), 4, num_waypoints);
      }
    }
    else if (path_type_ == 1)
    {
      // Load random waypoint parameters
      double random_heading_bound, altitude, alt_var, wp_sep, wp_var;
      State x0;
      get_yaml_node("heading_walk", filename, random_heading_bound);
      get_yaml_node("altitude", filename, altitude);
      get_yaml_node("altitude_variance", filename, alt_var);
      get_yaml_node("waypoint_separation", filename, wp_sep);
      get_yaml_node("waypoint_sep_variance", filename, wp_var);
      get_yaml_eigen("x0", filename, x0.arr); // need initial horizontal position

      // Get number of waypoints to create and initialize array and heading
      get_yaml_node("num_random_waypoints", filename, num_waypoints);
      waypoints_.setZero(4, num_waypoints);

      // For each waypoint, compute horizontal position components based on previous heading
      for (int i = 0; i < waypoints_.cols(); ++i)
      {
        // Get heading and position of previous waypoint
        double pn, pe, psi;
        if (i == 0)
        {
          pn = x0.p.x();
          pe = x0.p.y();
          psi = 0;
        }
        else
        {
          pn = waypoints_(0,i-1);
          pe = waypoints_(1,i-1);
          psi = waypoints_(3,i-1);
        }

        // Step position forward from previous position along heading direction of previous waypoint
        double step_size = wp_sep + wp_var * (udist_(rng_) + 1.0) / 2.0;
        waypoints_(0,i) = pn + step_size * cos(psi);
        waypoints_(1,i) = pe + step_size * sin(psi);
        waypoints_(2,i) = altitude + alt_var * udist_(rng_);
        waypoints_(3,i) = psi + random_heading_bound * udist_(rng_);
      }
    }
    else if (path_type_ == 2)
    {
      double traj_north_period, traj_east_period, traj_alt_period, traj_yaw_period;
      get_yaml_node("traj_delta_north", filename, traj_delta_north_);
      get_yaml_node("traj_delta_east", filename, traj_delta_east_);
      get_yaml_node("traj_delta_alt", filename, traj_delta_alt_);
      get_yaml_node("traj_delta_yaw", filename, traj_delta_yaw_);
      get_yaml_node("traj_nom_north", filename, traj_nom_north_);
      get_yaml_node("traj_nom_east", filename, traj_nom_east_);
      get_yaml_node("traj_nom_alt", filename, traj_nom_alt_);
      get_yaml_node("traj_nom_yaw", filename, traj_nom_yaw_);
      get_yaml_node("traj_north_period", filename, traj_north_period);
      get_yaml_node("traj_east_period", filename, traj_east_period);
      get_yaml_node("traj_alt_period", filename, traj_alt_period);
      get_yaml_node("traj_yaw_period", filename, traj_yaw_period);
      traj_north_freq_ = 2.0 * M_PI / traj_north_period;
      traj_east_freq_ = 2.0 * M_PI / traj_east_period;
      traj_alt_freq_ = 2.0 * M_PI / traj_alt_period;
      traj_yaw_freq_ = 2.0 * M_PI / traj_yaw_period;
    }
    else if (path_type_ == 3)
    {
      // Load constant velocity magnitude and yaw rate walk parameters
      get_yaml_node("traj_altitude", filename, xc_.p(2));
      get_yaml_node("velocity_magnitude", filename, vmag_);
      get_yaml_node("traj_heading_walk", filename, traj_heading_walk_);
      get_yaml_node("traj_heading_straight_gain", filename, traj_heading_straight_gain_);
      xc_.q = quat::Quatd::Identity();
    }
    else
    {
      std::stringstream err;
      err << "\n\tFile: " << __FILE__ << "\n\tLine: " << __LINE__;
      err << "\n\tMessage: You specified an invalid path type";
      throw std::runtime_error(err.str());
    }
    
    // Initialize controller
    get_yaml_node("control_type", filename, control_type_);
    if (control_type_ == 0)
    {
      Vector3d Kp_diag, Kd_diag, Kv_diag;
      get_yaml_eigen("Kp", filename, Kp_diag);
      get_yaml_eigen("Kd", filename, Kd_diag);
      get_yaml_eigen("Kv", filename, Kv_diag);
      K_p_ = Kp_diag.asDiagonal();
      K_d_ = Kd_diag.asDiagonal();
      K_v_ = Kv_diag.asDiagonal();

      get_yaml_node("throttle_eq", filename, sh_);
      sh_inv_hat_ = 1.0 / sh_;
      get_yaml_node("mass", filename, mass_);
      get_yaml_node("max_thrust", filename, max_thrust_);
      get_yaml_node("waypoint_threshold", filename, waypoint_threshold_);
      get_yaml_node("waypoint_velocity_threshold", filename, waypoint_velocity_threshold_);
      get_yaml_node("drag_constant", filename, drag_constant_);

      get_yaml_node("sh_kv", filename, sh_kv_);
      get_yaml_node("sh_ks", filename, sh_ks_);
      get_yaml_node("roll_kp", filename, roll_.kp_);
      get_yaml_node("roll_ki", filename, roll_.ki_);
      get_yaml_node("roll_kd", filename, roll_.kd_);
      get_yaml_node("pitch_kp", filename, pitch_.kp_);
      get_yaml_node("pitch_ki", filename, pitch_.ki_);
      get_yaml_node("pitch_kd", filename, pitch_.kd_);
      get_yaml_node("yaw_rate_kp", filename, yaw_rate_.kp_);
      get_yaml_node("yaw_rate_ki", filename, yaw_rate_.ki_);
      get_yaml_node("yaw_rate_kd", filename, yaw_rate_.kd_);
      get_yaml_node("max_tau_x", filename, roll_.max_);
      get_yaml_node("max_tau_y", filename, pitch_.max_);
      get_yaml_node("max_tau_z", filename, yaw_rate_.max_);
      get_yaml_node("max_roll", filename, max_.roll);
      get_yaml_node("max_pitch", filename, max_.pitch);
      get_yaml_node("max_yaw_rate", filename, max_.yaw_rate);
      get_yaml_node("max_throttle", filename, max_.throttle);
      get_yaml_node("max_vel", filename, max_.vel);
      nlc_.init(K_p_, K_v_, K_d_, path_type_, max_, traj_heading_walk_, traj_heading_straight_gain_, rng_, udist_);
    }
    else if (control_type_ == 1)
    {
      // Load LQR parameters
      Eigen::Matrix<double,6,1> lqr_Q_diag;
      Eigen::Vector4d lqr_R_diag;
      get_yaml_node("lqr_max_pos_error", filename, lqr_p_err_max_);
      get_yaml_node("lqr_max_vel_error", filename, lqr_v_err_max_);
      get_yaml_node("lqr_max_yaw_error", filename, lqr_yaw_err_max_);
      get_yaml_eigen("lqr_Q", filename, lqr_Q_diag);
      get_yaml_eigen("lqr_R", filename, lqr_R_diag);
      lqr_Q_ = lqr_Q_diag.asDiagonal();
      lqr_R_ = lqr_R_diag.asDiagonal();
      lqr_.init(path_type_, max_, lqr_p_err_max_, lqr_v_err_max_, lqr_yaw_err_max_, lqr_Q_, lqr_R_);
    }
    else
      throw std::runtime_error("Undefined control type in controller.cpp");
  }
  else
    printf("Unable to find file %s\n", (current_working_dir() + filename).c_str());
}

void ReferenceController::getCommandedState(const double &t, State &x_c, Vector4d &u_r)
{
  // Refresh the waypoint
  if (path_type_ < 2)
    updateWaypointManager();
  if (path_type_ == 2)
    updateTrajectoryManager();
  x_c = xc_;
  u_r.setZero();
}

void ReferenceController::updateWaypointManager()
{
  if (!initialized_)
  {
    initialized_ = true;
    Map<Vector4d> new_waypoint(waypoints_.block<4,1>(0, 0).data());
    xc_.p(0) = new_waypoint(PX);
    xc_.p(1) = new_waypoint(PY);
    xc_.p(2) = new_waypoint(PZ);
    xc_.q = quat::Quatd::from_euler(xc_.q.roll(), xc_.q.pitch(), new_waypoint(PSI));
    current_waypoint_id_ = 0;
  }

  // Find the distance to the desired waypoint
  Vector4d current_waypoint = waypoints_.block<4,1>(0, current_waypoint_id_);
  Vector4d error;
  error(PX) = current_waypoint(PX) - xhat_.p(0);
  error(PY) = current_waypoint(PY) - xhat_.p(1);
  error(PZ) = current_waypoint(PZ) - xhat_.p(2);
  error(PSI) = current_waypoint(PSI) - xhat_.q.yaw();
  
  // Angle wrapping on heading
  if (error(PSI) > M_PI)
    error(PSI) -= 2.0 * M_PI;
  else if (error(PSI) < -M_PI)
    error(PSI) += 2.0 * M_PI;
  
  if (error.norm() < waypoint_threshold_ && xhat_.v.norm() < waypoint_velocity_threshold_)
  {
    // increment waypoint
    current_waypoint_id_ = (current_waypoint_id_ + 1) % waypoints_.cols();
    
    // Update The commanded State
    Map<Vector4d> new_waypoint(waypoints_.block<4,1>(0, current_waypoint_id_).data());
    xc_.p(0) = new_waypoint(PX);
    xc_.p(1) = new_waypoint(PY);
    xc_.p(2) = new_waypoint(PZ);
    xc_.q = quat::Quatd::from_euler(xc_.q.roll(), xc_.q.pitch(), new_waypoint(PSI));
  }
}

void ReferenceController::updateTrajectoryManager()
{
  xc_.p(0) = traj_nom_north_ + traj_delta_north_ / 2.0 * cos(traj_north_freq_ * t_c_);
  xc_.p(1) = traj_nom_east_ + traj_delta_east_ / 2.0 * sin(traj_east_freq_ * t_c_);
  xc_.p(2) = -(traj_nom_alt_ + traj_delta_alt_ / 2.0 * sin(traj_alt_freq_ * t_c_));
  double psi = traj_nom_yaw_ + traj_delta_yaw_ / 2.0 * sin(traj_yaw_freq_ * t_c_);
  xc_.q = quat::Quatd::from_euler(xc_.q.roll(), xc_.q.pitch(), psi);
}

}
