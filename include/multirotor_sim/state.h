#pragma once

#include <Eigen/Core>
#include <geometry/xform.h>

using namespace Eigen;
using namespace xform;
using namespace quat;

namespace multirotor_sim
{


struct ErrorState
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  enum
  {
    SIZE = 12
  };


  Matrix<double, SIZE, 1> arr;
  Map<Vector6d> X;
  Map<Vector3d> p;
  Map<Vector3d> q;
  Map<Vector3d> v;
  Map<Vector3d> w;

  ErrorState() :
    X(arr.data()),
    p(arr.data()),
    q(arr.data()+3),
    v(arr.data()+6),
    w(arr.data()+9)
  {}

  ErrorState(const ErrorState& obj) :
    X(arr.data()),
    p(arr.data()),
    q(arr.data()+3),
    v(arr.data()+6),
    w(arr.data()+9)
  {
    arr = obj.arr;
  }

  ErrorState& operator= (const ErrorState& obj)
  {
    arr = obj.arr;
    return *this;
  }

  ErrorState operator* (const double& s)
  {
    ErrorState out;
    out.arr = s * arr;
    return out;
  }

  ErrorState operator+ (const ErrorState& obj)
  {
    ErrorState out;
    out.arr = obj.arr + arr;
    return out;
  }
};

struct State
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  enum
  {
    SIZE = 13
  };


  Matrix<double, SIZE, 1> arr;
  Xformd X;
  Map<Vector3d> p;
  Quatd q;
  Map<Vector3d> v;
  Map<Vector3d> w;

  State() :
    X(arr.data()),
    p(arr.data()),
    q(arr.data()+3),
    v(arr.data()+7),
    w(arr.data()+10)
  {
    arr.setZero();
    q = Quatd::Identity();
  }

  State(const State& x) :
    X(arr.data()),
    p(arr.data()),
    q(arr.data()+3),
    v(arr.data()+7),
    w(arr.data()+10)
  {
    arr = x.arr;
  }

  State& operator= (const State& obj)
  {

    arr = obj.arr;
    return *this;
  }

  State operator+(const ErrorState& dx) const
  {
    State xp;
    xp.p = p + dx.p;
    xp.q = q + dx.q;
    xp.v = v + dx.v;
    xp.w = w + dx.w;
    return xp;
  }

  State& operator+=(const ErrorState& dx)
  {
    p = p + dx.p;
    q = q + dx.q;
    v = v + dx.v;
    w = w + dx.w;
    return *this;
  }

  ErrorState operator-(const State& x) const
  {
    ErrorState dx;
    dx.p = p - x.p;
    dx.q = q - x.q;
    dx.v = v - x.v;
    dx.w = w - x.w;
    return dx;
  }
};

struct Input
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  enum
  {
    SIZE = 4
  };
  Matrix<double, SIZE, 1> arr;
  double& T;
  Map<Vector3d> tau;

  Input() :
    T(*arr.data()),
    tau(arr.data()+3)
  {}
};

struct IMU
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  enum
  {
    SIZE = 6
  };
  Matrix<double, SIZE, 1> arr;
  Map<Vector3d> acc;
  Map<Vector3d> gyro;

  IMU() :
    acc(arr.data()),
    gyro(arr.data()+3)
  {}
};

class ImageFeat
{
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  int id; // image label
  double t; // time stamp of this image
  std::vector<Vector2d, aligned_allocator<Vector2d>> pixs; // pixel measurements in this image
  //std::vector<double> depths; // feature distances corresponding to pixel measurements
  std::vector<int> feat_ids; // feature ids corresonding to pixel measurements

  void reserve(const int& N)
  {
    pixs.reserve(N);
    //depths.reserve(N);
    feat_ids.reserve(N);
  }

  void clear()
  {
    pixs.clear();
    //depths.clear();
    feat_ids.clear();
  }
};


} // namespace multirotor_sim
