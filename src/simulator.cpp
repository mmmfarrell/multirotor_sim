﻿#include "simulator.h"
#include <Eigen/StdVector>
#include <chrono>

#include "multirotor_sim/estimator_base.h"
#include "multirotor_sim/controller.h"

using namespace std;

namespace  multirotor_sim
{


Simulator::Simulator(bool prog_indicator, uint64_t seed) :
  seed_(seed < 0 ? std::chrono::system_clock::now().time_since_epoch().count() : seed),
  env_(seed_),
  rng_(seed_),
  uniform_(0.0, 1.0),
  prog_indicator_(prog_indicator)
{
  ReferenceController* ref_con = new ReferenceController();
  cont_ = ref_con;
  traj_ = ref_con;
  srand(seed_);
}


Simulator::Simulator(ControllerBase *_cont, TrajectoryBase* _traj, bool prog_indicator, uint64_t seed):
  seed_(seed < 0 ? std::chrono::system_clock::now().time_since_epoch().count() : seed),
  cont_(_cont),
  traj_(_traj),
  env_(seed_),
  rng_(seed_),
  uniform_(0.0, 1.0),
  prog_indicator_(prog_indicator)
{
  srand(seed_);
}


Simulator::~Simulator()
{
  if (prog_indicator_)
    cout << endl;

  if (log_.is_open())
    log_.close();
}


void Simulator::load(string filename)
{
  param_filename_ = filename;
  t_ = 0;
  get_yaml_node("tmax", filename, tmax_);
  get_yaml_node("dt", filename, dt_);
  get_yaml_node("seed", filename, seed_);
  seed_ < 0 ? std::chrono::system_clock::now().time_since_epoch().count() : seed_;
  rng_ = default_random_engine(seed_);
  srand(seed_);

  // Log
  get_yaml_node("log_filename", filename, log_filename_);
  if (!log_filename_.empty())
  {
    log_.open(log_filename_);
  }

  // Initialize Desired sensors
  get_yaml_node("imu_enabled", filename, imu_enabled_);
  get_yaml_node("alt_enabled", filename, alt_enabled_);
  get_yaml_node("mocap_enabled", filename, mocap_enabled_);
  get_yaml_node("vo_enabled", filename, vo_enabled_);
  get_yaml_node("camera_enabled", filename, camera_enabled_);
  get_yaml_node("gnss_enabled", filename, gnss_enabled_);
  get_yaml_node("raw_gnss_enabled", filename, raw_gnss_enabled_);

  if (imu_enabled_)
    init_imu();
  if (camera_enabled_)
    init_camera();
  if (alt_enabled_)
    init_altimeter();
  if (vo_enabled_)
    init_vo();
  if (mocap_enabled_)
    init_mocap();
  if (gnss_enabled_)
    init_gnss();
  if (raw_gnss_enabled_)
    init_raw_gnss();

  // Load sub-class parameters
  if (camera_enabled_)
    env_.load(filename);
  dyn_.load(filename);
  cont_->load(filename);
  traj_->load(filename);

  // Start Progress Bar
  if (prog_indicator_)
    prog_.init(std::round(tmax_/dt_), 40);

  // start at hover throttle
  u_(multirotor_sim::THRUST) = dyn_.mass_ / dyn_.max_thrust_ * multirotor_sim::G;
}


bool Simulator::run()
{
  if (t_ < tmax_ - dt_ / 2.0) // Subtract half time step to prevent occasional extra iteration
  {
    // Propagate forward in time and get new control input and true acceleration
    t_ += dt_;
    cont_->computeControl(t_, dyn_.get_state(), traj_->getCommandedState(t_), u_);
    dyn_.run(dt_, u_);
    if (prog_indicator_)
      prog_.print(t_/dt_);
    update_measurements();

    log_state();
    return true;
  }
  else
  {
    if (prog_indicator_)
      prog_.finished();
    return false;
  }
}


void Simulator::init_imu()
{
  // Load IMU parameters
  Vector4d q_b_u;
  get_yaml_node("imu_update_rate", param_filename_, imu_update_rate_);
  get_yaml_eigen("p_b_u", param_filename_, p_b2u_);
  get_yaml_eigen("q_b_u", param_filename_, q_b_u);
  q_b2u_ = Quatd(q_b_u);

  // Accelerometer
  bool use_accel_truth;
  double accel_noise, accel_walk, accel_init;
  get_yaml_node("use_accel_truth", param_filename_, use_accel_truth);
  get_yaml_node("accel_init_stdev", param_filename_, accel_init);
  get_yaml_node("accel_noise_stdev", param_filename_, accel_noise);
  get_yaml_node("accel_bias_walk", param_filename_, accel_walk);
  accel_bias_ =  !use_accel_truth * accel_init * Vector3d::Random(); // Uniformly random init within +-accel_walk
  accel_noise_stdev_ = !use_accel_truth * accel_noise;
  accel_walk_stdev_ = !use_accel_truth * accel_walk;

  // Gyro
  bool use_gyro_truth;
  double gyro_noise, gyro_walk, gyro_init;
  get_yaml_node("use_gyro_truth", param_filename_, use_gyro_truth);
  get_yaml_node("gyro_noise_stdev", param_filename_, gyro_noise);
  get_yaml_node("gyro_init_stdev", param_filename_, gyro_init);
  get_yaml_node("gyro_bias_walk", param_filename_, gyro_walk);
  gyro_bias_ = gyro_init * Vector3d::Random()  * !use_gyro_truth; // Uniformly random init within +-gyro_walk
  gyro_noise_stdev_ = gyro_noise * !use_gyro_truth;
  gyro_walk_stdev_ = gyro_walk * !use_gyro_truth;

  imu_R_.topLeftCorner<3,3>() = accel_noise * accel_noise * I_3x3;
  imu_R_.bottomRightCorner<3,3>() = gyro_noise * gyro_noise * I_3x3;
  imu_R_.bottomLeftCorner<3,3>().setZero();
  imu_R_.topRightCorner<3,3>().setZero();
  last_imu_update_ = 0.0;
}


void Simulator::init_camera()
{
  // Camera
  bool use_camera_truth;
  double pixel_noise;
  Vector2d focal_len;
  get_yaml_node("camera_time_delay", param_filename_, camera_time_delay_);
  get_yaml_node("use_camera_truth", param_filename_, use_camera_truth);
  get_yaml_node("camera_update_rate", param_filename_, camera_update_rate_);
  get_yaml_eigen("cam_center", param_filename_, cam_center_);
  get_yaml_eigen("image_size", param_filename_, image_size_);
  get_yaml_eigen("q_b_c", param_filename_, q_b2c_.arr_);
  get_yaml_eigen("p_b_c", param_filename_, p_b2c_);
  get_yaml_eigen("focal_len", param_filename_, focal_len);
  get_yaml_node("pixel_noise_stdev", param_filename_, pixel_noise);
  get_yaml_node("loop_closure", param_filename_, loop_closure_);
  pixel_noise_stdev_ = !use_camera_truth * pixel_noise;
  cam_F_ << focal_len(0,0), 0, 0, 0, focal_len(1,0), 0; // Copy focal length into 2x3 matrix for future use

  // Depth
  double depth_noise;
  bool use_depth_truth;
  get_yaml_node("use_depth_truth", param_filename_, use_depth_truth);
  get_yaml_node("depth_update_rate", param_filename_, depth_update_rate_);
  get_yaml_node("depth_noise_stdev", param_filename_, depth_noise);
  depth_noise_stdev_ = depth_noise * !use_depth_truth;

  image_id_ = 0;
  next_feature_id_ = 0;
  last_camera_update_ = 0.0;
  feat_R_ = pixel_noise * pixel_noise * I_2x2;
  depth_R_ << depth_noise * depth_noise;

  tracked_points_.reserve(NUM_FEATURES);
  img_.reserve(NUM_FEATURES);
}


void Simulator::init_altimeter()
{
  bool use_altimeter_truth;
  double altimeter_noise;
  get_yaml_node("use_altimeter_truth", param_filename_, use_altimeter_truth);
  get_yaml_node("altimeter_update_rate", param_filename_, altimeter_update_rate_);
  get_yaml_node("altimeter_noise_stdev", param_filename_, altimeter_noise);
  altimeter_noise_stdev_ = altimeter_noise * !use_altimeter_truth;
  alt_R_ << altimeter_noise * altimeter_noise;
  last_altimeter_update_ = 0.0;
}


void Simulator::init_vo()
{
  double vo_translation_noise, vo_rotation_noise;
  X_I2bk_ = dyn_.get_global_pose();
  bool use_vo_truth;
  get_yaml_node("use_vo_truth", param_filename_, use_vo_truth);
  get_yaml_node("vo_delta_position", param_filename_, vo_delta_position_);
  get_yaml_node("vo_delta_attitude", param_filename_, vo_delta_attitude_);
  get_yaml_node("vo_translation_noise_stdev", param_filename_, vo_translation_noise);
  get_yaml_node("vo_rotation_noise_stdev", param_filename_, vo_rotation_noise);
  vo_translation_noise_stdev_ = vo_translation_noise * !use_vo_truth;
  vo_rotation_noise_stdev_ = vo_rotation_noise * !use_vo_truth;

  vo_R_.setIdentity();
  vo_R_.block<3,3>(0,0) *= vo_translation_noise * vo_translation_noise;
  vo_R_.block<3,3>(3,3) *= vo_rotation_noise * vo_rotation_noise;
}


void Simulator::init_mocap()
{
  // Truth
  double att_noise, pos_noise;
  bool use_mocap_truth;
  get_yaml_node("mocap_update_rate", param_filename_, mocap_update_rate_);
  get_yaml_node("use_mocap_truth", param_filename_, use_mocap_truth);
  get_yaml_node("attitude_noise_stdev", param_filename_, att_noise);
  get_yaml_node("position_noise_stdev", param_filename_, pos_noise);
  get_yaml_node("mocap_time_offset", param_filename_, mocap_time_offset_);
  get_yaml_node("mocap_transmission_noise", param_filename_, mocap_transmission_noise_);
  get_yaml_node("mocap_transmission_time", param_filename_, mocap_transmission_time_);
  get_yaml_eigen("p_b_m", param_filename_, p_b2m_);
  get_yaml_eigen("q_b_m", param_filename_, q_b2m_.arr_);
  attitude_noise_stdev_ = att_noise * !use_mocap_truth;
  position_noise_stdev_ = pos_noise * !use_mocap_truth;

  mocap_R_ << pos_noise * pos_noise * I_3x3,
              Matrix3d::Zero(),
              Matrix3d::Zero(),
              att_noise * att_noise * I_3x3;

  last_mocap_update_ = 0.0;
  next_mocap_measurement_ = 0.0;
}


void Simulator::init_gnss()
{
  // gnss
  Vector3d refLla;
  bool use_gnss_truth;
  double gnss_pos_noise_h, gnss_pos_noise_v, gnss_vel_noise;
  get_yaml_eigen("ref_LLA", param_filename_, refLla);
  X_e2n_ = WSG84::x_ecef2ned(WSG84::lla2ecef(refLla));
  get_yaml_node("gnss_update_rate", param_filename_, gnss_update_rate_);
  get_yaml_node("use_gnss_truth", param_filename_, use_gnss_truth);
  get_yaml_node("gnss_horizontal_position_stdev", param_filename_, gnss_pos_noise_h);
  get_yaml_node("gnss_vertical_position_stdev", param_filename_, gnss_pos_noise_v);
  get_yaml_node("gnss_velocity_stdev", param_filename_, gnss_vel_noise);
  gnss_horizontal_position_stdev_ = gnss_pos_noise_h * !use_gnss_truth;
  gnss_vertical_position_stdev_ = gnss_pos_noise_v * !use_gnss_truth;
  gnss_velocity_stdev_ = gnss_vel_noise * !use_gnss_truth;

  gnss_R_.setIdentity();
  gnss_R_.block<2,2>(0,0) *= gnss_pos_noise_h;
  gnss_R_(2,2) *= gnss_pos_noise_v;
  gnss_R_.block<3,3>(3,3) *= gnss_vel_noise;
  auto gnss_pos_block = gnss_R_.block<3,3>(0,0);
  auto gnss_vel_block = gnss_R_.block<3,3>(3,3);
  gnss_pos_block = X_e2n_.q().R().transpose() * gnss_pos_block * X_e2n_.q().R();
  gnss_vel_block = X_e2n_.q().R().transpose() * gnss_vel_block * X_e2n_.q().R();

  last_gnss_update_ = 0.0;
}

void Simulator::init_raw_gnss()
{
  Vector3d refLla;
  bool use_raw_gnss_truth;
  get_yaml_eigen("ref_LLA", param_filename_, refLla);
  X_e2n_ = WSG84::x_ecef2ned(WSG84::lla2ecef(refLla));
  double pseudorange_noise, p_rate_noise, cp_noise, clock_walk;
  get_yaml_node("gnss_update_rate", param_filename_, gnss_update_rate_);
  get_yaml_node("use_raw_gnss_truth", param_filename_, use_raw_gnss_truth);
  get_yaml_node("use_raw_gnss_truth", param_filename_, use_raw_gnss_truth);
  get_yaml_node("pseudorange_stdev", param_filename_, pseudorange_noise);
  get_yaml_node("pseudorange_rate_stdev", param_filename_, p_rate_noise);
  get_yaml_node("carrier_phase_stdev", param_filename_, cp_noise);
  get_yaml_node("ephemeris_filename", param_filename_, ephemeris_filename_);
  get_yaml_node("clock_init_stdev", param_filename_, clock_init_stdev_);
  get_yaml_node("clock_walk_stdev", param_filename_, clock_walk);
  get_yaml_node("start_time_week", param_filename_, start_time_.week);
  get_yaml_node("start_time_tow_sec", param_filename_, start_time_.tow_sec);
  pseudorange_stdev_ = pseudorange_noise * !use_raw_gnss_truth;
  pseudorange_rate_stdev_ = p_rate_noise * !use_raw_gnss_truth;
  carrier_phase_stdev_ = cp_noise * !use_raw_gnss_truth;
  clock_walk_stdev_ = clock_walk * !use_raw_gnss_truth;

  for (int i = 0; i < 100; i++)
  {
    Satellite sat(i, satellites_.size());
    sat.readFromRawFile(ephemeris_filename_);
    if (sat.eph_.A > 0)
    {
      satellites_.push_back(sat);
      carrier_phase_integer_offsets_.push_back(use_raw_gnss_truth ? 0 : round(uniform_(rng_) * 100) - 50);
    }
  }

  raw_gnss_R_ = Vector3d{pseudorange_stdev_*pseudorange_stdev_,
      pseudorange_rate_stdev_*pseudorange_rate_stdev_,
      carrier_phase_stdev_*carrier_phase_stdev_}.asDiagonal();

  clock_bias_ = uniform_(rng_) * clock_init_stdev_;
  last_raw_gnss_update_ = 0.0;
}

void Simulator::register_estimator(EstimatorBase *est)
{
  est_.push_back(est);
}


void Simulator::log_state()
{
  if (log_.is_open())
  {
    log_.write((char*)&t_, sizeof(double));
    log_.write((char*)dyn_.get_state().arr.data(), sizeof(double)*State::SIZE);
  }
}


void Simulator::update_camera_pose()
{
  p_I2c_ = dyn_.get_state().p + dyn_.get_state().q.rota(p_b2c_);
  q_I2c_ = dyn_.get_state().q * q_b2c_;
}


void Simulator::update_imu_meas()
{
  double dt = t_ - last_imu_update_;
  if (std::round(dt * 1e4) / 1e4 >= 1.0/imu_update_rate_)
  {
    last_imu_update_ = t_;

    // Bias random walks and IMU noise
    accel_bias_ += randomNormal<Vector3d>(accel_walk_stdev_, normal_, rng_) * dt;
    gyro_bias_ += randomNormal<Vector3d>(gyro_walk_stdev_, normal_, rng_) * dt;

    // Populate accelerometer and gyro measurements
    Vector6d imu;
    imu.segment<3>(0) = dyn_.get_imu_accel() + accel_bias_ + randomNormal<Vector3d>(accel_noise_stdev_, normal_,  rng_);
    imu.segment<3>(3) = dyn_.get_imu_gyro() + gyro_bias_ + randomNormal<Vector3d>(gyro_noise_stdev_, normal_,  rng_);;

    for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
      (*it)->imuCallback(t_, imu, imu_R_);
  }
}


void Simulator::update_camera_meas()
{
  // If it's time to capture new measurements, then do it
  if (std::round((t_ - last_camera_update_) * 1e4) / 1e4 >= 1.0/camera_update_rate_)
  {
    last_camera_update_ = t_;
    update_camera_pose();

    // Update feature measurements for currently tracked features
    for(auto it = tracked_points_.begin(); it != tracked_points_.end();)
    {
      if (update_feature(*it))
      {
        measurement_t meas;
        meas.t = t_;
        meas.z = it->pixel + randomNormal<Vector2d>(pixel_noise_stdev_, normal_, rng_);
        meas.R = feat_R_;
        meas.feature_id = (*it).id;
        meas.depth = it->depth + depth_noise_stdev_ * normal_(rng_);
        camera_measurements_buffer_.push_back(meas);
        DBG("update feature - ID = %d\n", it->id);
        it++;
      }
      else
      {
        if (it->zeta(2,0) < 0)
        {
          DBG("clearing feature - ID = %d because went negative [%f, %f, %f]\n",
              it->id, it->zeta(0,0), it->zeta(1,0), it->zeta(2,0));
        }
        else if ((it->pixel.array() < 0).any() || (it->pixel.array() > image_size_.array()).any())
        {
          DBG("clearing feature - ID = %d because went out of frame [%f, %f]\n",
              it->id, it->pixel(0,0), it->pixel(1,0));
        }
        tracked_points_.erase(it);
      }
    }

    while (tracked_points_.size() < NUM_FEATURES)
    {
      // Add the new feature to our "tracker"
      feature_t new_feature;
      if (!get_feature_in_frame(new_feature, loop_closure_))
        break;
      tracked_points_.push_back(new_feature);
      DBG("new feature - ID = %d [%f, %f, %f], [%f, %f]\n",
          new_feature.id, new_feature.zeta(0,0), new_feature.zeta(1,0),
          new_feature.zeta(2,0), new_feature.pixel(0,0), new_feature.pixel(1,0));

      // Create a measurement for this new feature
      measurement_t meas;
      meas.t = t_;
      meas.z = new_feature.pixel + randomNormal<Vector2d>(pixel_noise_stdev_, normal_, rng_);
      meas.R = feat_R_;
      meas.feature_id = new_feature.id;
      meas.depth = new_feature.depth + depth_noise_stdev_ * normal_(rng_);
      camera_measurements_buffer_.push_back(meas);
    }
  }

  // Push out the measurement if it is time to send it
  if ((t_ > last_camera_update_ + camera_time_delay_) && (camera_measurements_buffer_.size() > 0))
  {
    // Populate the Image class with all feature measurements
    img_.clear();
    img_.t = t_;
    img_.id = image_id_;
    for (auto zit = camera_measurements_buffer_.begin(); zit != camera_measurements_buffer_.end(); zit++)
    {
      img_.pixs.push_back(zit->z);
      img_.feat_ids.push_back(zit->feature_id);
      img_.depths.push_back(zit->depth);
    }

    for (std::vector<EstimatorBase*>::iterator eit = est_.begin(); eit != est_.end(); eit++)
        (*eit)->imageCallback(t_, img_, feat_R_, depth_R_);
    camera_measurements_buffer_.clear();
    ++image_id_;
  }
}


void Simulator::update_alt_meas()
{
  if (std::round((t_ - last_altimeter_update_) * 1e4) / 1e4 >= 1.0/altimeter_update_rate_)
  {
    Vector1d z_alt;
    z_alt << -1.0 * state().p.z() + altimeter_noise_stdev_ * normal_(rng_);

    last_altimeter_update_ = t_;
    for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
      (*it)->altCallback(t_, z_alt, alt_R_);
  }
}


void Simulator::update_mocap_meas()
{
  if (std::round((t_ - last_mocap_update_) * 1e4) / 1e4 >= 1.0/mocap_update_rate_)
  {
    measurement_t mocap_meas;
    mocap_meas.t = t_ - mocap_time_offset_;
    mocap_meas.z.resize(7,1);

    // Add noise to mocap measurements and transform into mocap coordinate frame
    Vector3d noise = randomNormal<Vector3d>(position_noise_stdev_, normal_, rng_);
    Vector3d I_p_b_I = state().p; // p_{b/I}^I
    Vector3d I_p_m_I = I_p_b_I + state().q.rota(p_b2m_); // p_{m/I}^I = p_{b/I}^I + R(q_I^b)^T (p_{m/b}^b)
    mocap_meas.z.topRows<3>() = I_p_m_I + noise;

    noise = randomNormal<Vector3d>(attitude_noise_stdev_, normal_, rng_);
    Quatd q_I_m = state().q * q_b2m_; //  q_I^m = q_I^b * q_b^m
    mocap_meas.z.bottomRows<4>() = (q_I_m + noise).elements();

    mocap_meas.R = mocap_R_;

    double pub_time = std::max(mocap_transmission_time_ + normal_(rng_) * mocap_transmission_noise_, 0.0) + t_;

    mocap_measurement_buffer_.push_back(std::pair<double, measurement_t>{pub_time, mocap_meas});
    last_mocap_update_ = t_;
  }

  while (mocap_measurement_buffer_.size() > 0 && mocap_measurement_buffer_[0].first >= t_)
  {
    measurement_t* m = &(mocap_measurement_buffer_[0].second);
    if (mocap_enabled_)
    {
      for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
        (*it)->mocapCallback(t_, Xformd(m->z), m->R);
    }
    mocap_measurement_buffer_.erase(mocap_measurement_buffer_.begin());
  }
}


void Simulator::update_vo_meas()
{
  Xformd T_i2b = dyn_.get_global_pose();
  Vector6d delta = T_i2b - X_I2bk_;
  if (delta.segment<3>(0).norm() >= vo_delta_position_ || delta.segment<3>(3).norm() >= vo_delta_attitude_)
  {
    // Compute position and attitude relative to the keyframe
    Xformd T_c2ck;
    T_c2ck.t_ = q_b2c_.rotp(T_i2b.q().rotp(X_I2bk_.t() + X_I2bk_.q().inverse().rotp(p_b2c_) -
                                           (T_i2b.t() + T_i2b.q().inverse().rotp(p_b2c_))));
    T_c2ck.q_ = q_b2c_.inverse() * T_i2b.q().inverse() * X_I2bk_.q().inverse() * q_b2c_;

    for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
      (*it)->voCallback(t_, T_c2ck, vo_R_);

    // Set new keyframe to current pose
    X_I2bk_ = dyn_.get_global_pose();
  }
}

void Simulator::update_gnss_meas()
{
  /// TODO: Simulate gnss sensor delay
  if (std::round((t_ - last_gnss_update_) * 1e4) / 1e4 >= 1.0/gnss_update_rate_)
  {
    last_gnss_update_ = t_;
    /// TODO: Simulate the random walk associated with gnss position
    Vector3d p_NED = dyn_.get_global_pose().t();
    p_NED.segment<2>(0) += gnss_horizontal_position_stdev_ * randomNormal<double, 2, 1>(normal_, rng_);
    p_NED(2) += gnss_vertical_position_stdev_ * normal_(rng_);
    Vector3d p_ECEF = WSG84::ned2ecef(X_e2n_, p_NED);

    Vector3d v_NED = dyn_.get_global_pose().q().rota(dyn_.get_state().v);
    Vector3d v_ECEF = X_e2n_.q().rota(v_NED);
    v_ECEF += gnss_velocity_stdev_ * randomNormal<double, 3, 1>(normal_, rng_);

    Vector6d z;
    z << p_ECEF, v_ECEF;

    for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
      (*it)->gnssCallback(t_, z, gnss_R_);
  }
}

void Simulator::update_raw_gnss_meas()
{
  /// TODO: Simulator gnss sensor delay
  double dt = t_ - last_raw_gnss_update_;
  if (std::round(dt * 1e4) / 1e4 >= 1.0/gnss_update_rate_)
  {
    last_raw_gnss_update_ = t_;
    clock_bias_rate_ += normal_(rng_) * clock_walk_stdev_ * dt;
    clock_bias_ += clock_bias_rate_ * dt;

    GTime t_now = t_ + start_time_;
    Vector3d p_ECEF = get_position_ecef();
    Vector3d v_ECEF = get_velocity_ecef();

    Vector3d z;
    int i;
    vector<Satellite>::iterator sat;
    for (i = 0, sat = satellites_.begin(); sat != satellites_.end(); sat++, i++)
    {
      sat->computeMeasurement(t_now, p_ECEF, v_ECEF, Vector2d{clock_bias_, clock_bias_rate_}, z);
      z(0) += normal_(rng_) * pseudorange_stdev_;
      z(1) += normal_(rng_) * pseudorange_rate_stdev_;
      z(2) += normal_(rng_) * carrier_phase_stdev_ + carrier_phase_integer_offsets_[i];
      for (std::vector<EstimatorBase*>::iterator it = est_.begin(); it != est_.end(); it++)
        (*it)->rawGnssCallback(t_now, z, raw_gnss_R_, *sat);
    }
  }
}


void Simulator::update_measurements()
{
  if (imu_enabled_)
    update_imu_meas();
  if (camera_enabled_)
    update_camera_meas();
  if (alt_enabled_)
    update_alt_meas();
  if (mocap_enabled_)
    update_mocap_meas();
  if (vo_enabled_)
    update_vo_meas();
  if (gnss_enabled_)
    update_gnss_meas();
  if (raw_gnss_enabled_)
    update_raw_gnss_meas();
}


bool Simulator::update_feature(feature_t &feature) const
{
  if (feature.id > env_.get_points().size() || feature.id < 0)
    return false;
  
  // Calculate the bearing vector to the feature
  Vector3d pt = env_.get_points()[feature.id];
  feature.zeta = q_I2c_.rotp(pt - p_I2c_);
  feature.zeta /= feature.zeta.norm();
  feature.depth = (env_.get_points()[feature.id] - p_I2c_).norm();
  
  // we can reject anything behind the camera
  if (feature.zeta(2) < 0.0)
    return false;
  
  // See if the pixel is in the camera frame
  proj(feature.zeta, feature.pixel);
  if ((feature.pixel.array() < 0).any() || (feature.pixel.array() > image_size_.array()).any())
    return false;
  else
    return true;
}

bool Simulator::get_previously_tracked_feature_in_frame(feature_t &feature)
{

  Vector3d ground_pt;
  env_.get_center_img_center_on_ground_plane(p_I2c_, q_I2c_, ground_pt);
  vector<Vector3d, aligned_allocator<Vector3d>> pts;
  vector<size_t> ids;
  if (env_.get_closest_points(ground_pt, NUM_FEATURES, 2.0, pts, ids))
  {
    for (int i = 0; i < pts.size(); i++)
    {
      if (is_feature_tracked(ids[i]))
        continue;
      // Calculate the bearing vector to the feature
      Vector3d pt = env_.get_points()[ids[i]];
      feature.zeta = q_I2c_.rotp(pt - p_I2c_);
      if (feature.zeta(2) < 0.0)
        continue;

      feature.zeta /= feature.zeta.norm();
      feature.depth = (pts[i] - p_I2c_).norm();
      proj(feature.zeta, feature.pixel);
      if ((feature.pixel.array() < 0).any() || (feature.pixel.array() > image_size_.array()).any())
        continue;
      else
      {
        feature.id = ids[i];
        return true;
      }
    }
  }
  return false;
}

bool Simulator::get_feature_in_frame(feature_t &feature, bool retrack)
{
  if (retrack && get_previously_tracked_feature_in_frame(feature))
  {
    return true;
  }
  else
  {
    return create_new_feature_in_frame(feature);
  }
}

bool Simulator::create_new_feature_in_frame(feature_t &feature)
{
  // First, look for features in frame that are not currently being tracked
  feature.id = env_.add_point(p_I2c_, q_I2c_, feature.zeta, feature.pixel, feature.depth);
  if (feature.id != -1)
  {
    feature.id = next_feature_id_++;
    return true;
  }
  else
  {
    //    cout << "\nGround Plane is not in camera frame " << endl;
    return false;
  }
}

bool Simulator::is_feature_tracked(int id) const
{
  auto it = tracked_points_.begin();
  while (it < tracked_points_.end() && it->id != id)
  {
    it++;
  }
  return it != tracked_points_.end();
}

void Simulator::proj(const Vector3d &zeta, Vector2d& pix) const
{
  double ezT_zeta = e_z.transpose() * zeta;
  pix = cam_F_ * zeta / ezT_zeta + cam_center_;
}


Vector3d Simulator::get_position_ecef() const
{
  return WSG84::ned2ecef(X_e2n_, dyn_.get_state().p);
}

Vector3d Simulator::get_velocity_ecef() const
{
  Vector3d v_NED = dyn_.get_global_pose().q().rota(dyn_.get_state().v);
  return X_e2n_.q().rota(v_NED);
}

}

