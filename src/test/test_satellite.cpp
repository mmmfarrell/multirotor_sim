#include <gtest/gtest.h>

#include "multirotor_sim/satellite.h"
#include "multirotor_sim/test_common.h"
#include "multirotor_sim/wsg84.h"
#include "multirotor_sim/datetime.h"

class TestSatellite : public ::testing::Test
{
protected:
  TestSatellite() :
    sat(1, 0)
  {}
  void SetUp() override
    {
      time.week = 86400.00 / DateTime::SECONDS_IN_WEEK;
      time.tow_sec = 86400.00 - (time.week * DateTime::SECONDS_IN_WEEK);

      eph.sat = 1;
      eph.A = 5153.79589081 * 5153.79589081;
      eph.toe.week = 93600.0 / DateTime::SECONDS_IN_WEEK;
      eph.toe.tow_sec = 93600.0 - (eph.toe.week * DateTime::SECONDS_IN_WEEK);
      eph.toes = 93600.0;
      eph.deln =  0.465376527657e-08;
      eph.M0 =  1.05827953357;
      eph.e =  0.00223578442819;
      eph.omg =  2.06374037770;
      eph.cus =  0.177137553692e-05;
      eph.cuc =  0.457651913166e-05;
      eph.crs =  88.6875000000;
      eph.crc =  344.96875;
      eph.cis = -0.856816768646e-07;
      eph.cic =  0.651925802231e-07;
      eph.idot =  0.342514267094e-09;
      eph.i0 =  0.961685061380;
      eph.OMG0 =  1.64046615454;
      eph.OMGd = -0.856928551657e-08;
      sat.addEphemeris(eph);

  }
  eph_t eph;
  GTime time;
  Satellite sat;
};

TEST_F (TestSatellite, CheckSatPositionVelocityClock)
{
    Vector3d oracle_pos, oracle_vel, oracle_pos2;
    Vector3d new_pos, new_vel;
    Vector3d truth_pos, truth_vel;
    double oracle_clock, oracle_clock2, oracle_clk_rate;
    double dt = 1e-3;
    GTime t2 = time + dt;
    truth_pos << -12611434.19782218519,
            -13413103.97797041226,
            19062913.07357876760;
    truth_vel <<  266.280379332602,
            -2424.768347293139,
            -1529.762077704072;
    eph2pos(time, &eph, oracle_pos, &oracle_clock);
    eph2pos(t2, &eph, oracle_pos2, &oracle_clock2);
    oracle_vel = (oracle_pos2 - oracle_pos) / dt;
    oracle_clk_rate = (oracle_clock2 - oracle_clock) / dt;

    Vector2d clock;
    sat.computePositionVelocityClock(time, new_pos, new_vel, clock);

    EXPECT_MAT_NEAR(oracle_pos, new_pos, 1e-5);
    EXPECT_MAT_NEAR(oracle_vel, new_vel, 1e-3);
    EXPECT_MAT_NEAR(new_pos, truth_pos, 1e-5);
    EXPECT_MAT_NEAR(new_vel, truth_vel, 1e-5);

    EXPECT_NEAR(clock(0), oracle_clock, 1e-8);
    EXPECT_NEAR(clock(1), oracle_clk_rate, 1e-8);
}

TEST_F (TestSatellite, AzimuthElevationStraightUp)
{
    Vector2d az_el, clock;
    Vector3d sat_pos, sat_vel;
    sat.computePositionVelocityClock(time, sat_pos, sat_vel, clock);
    Vector3d sat_lla = WSG84::ecef2lla(sat_pos);
    Vector3d surface_lla = sat_lla;
    surface_lla(2) = 0;
    Vector3d surface_ecef = WSG84::lla2ecef(surface_lla);

    Vector3d los_ecef = sat_pos - surface_ecef;

    sat.los2azimuthElevation(surface_ecef, los_ecef, az_el);

    ASSERT_NEAR(az_el(1), M_PI/2.0, 1e-7);
}

TEST_F (TestSatellite, AzimuthElevationProvo)
{
    Vector2d az_el, clock;
    Vector3d sat_pos, sat_vel;
    sat.computePositionVelocityClock(time, sat_pos, sat_vel, clock);
    Vector3d sat_lla = WSG84::ecef2lla(sat_pos);

    Vector3d provo_lla{40.246184 * DEG2RAD , -111.647769 * DEG2RAD, 1387.997511};
    Vector3d provo_ecef = WSG84::lla2ecef(provo_lla);

    Vector3d los_ecef = sat_pos - provo_ecef;

    sat.los2azimuthElevation(provo_ecef, los_ecef, az_el);

    ASSERT_NEAR(az_el(0), -1.09260980, 1e-8);
    ASSERT_NEAR(az_el(1), 1.18916781, 1e-8);
}

TEST_F (TestSatellite, IonoshereCalculation)
{
    Vector2d az_el, clock;
    Vector3d sat_pos, sat_vel;
    sat.computePositionVelocityClock(time, sat_pos, sat_vel, clock);
    Vector3d provo_lla{40.246184 * DEG2RAD , -111.647769 * DEG2RAD, 1387.997511};
    Vector3d provo_ecef = WSG84::lla2ecef(provo_lla);
    Vector3d los_ecef = sat_pos - provo_ecef;
    sat.los2azimuthElevation(provo_ecef, los_ecef, az_el);

    double oracle_ion_delay = ionmodel(time, provo_lla.data(), az_el.data());
    double new_ion_delay = sat.ionosphericDelay(time, provo_lla, az_el);

    ionoutc_t ion = {true, true,
                     0.1118E-07,-0.7451E-08,-0.5961E-07, 0.1192E-06,
                     0.1167E+06,-0.2294E+06,-0.1311E+06, 0.1049E+07};
    double sdr_lib_delay = ionosphericDelay(&ion, time, provo_lla.data(), az_el.data());

    ASSERT_NEAR(oracle_ion_delay, new_ion_delay, 1e-8);
    ASSERT_NEAR(sdr_lib_delay, new_ion_delay, 1e-8);
}

TEST_F (TestSatellite, PsuedorangeSim)
{
    Vector3d provo_lla{40.246184 * DEG2RAD , -111.647769 * DEG2RAD, 1387.997511};
    Vector3d provo_ecef = WSG84::lla2ecef(provo_lla);
    Vector3d rec_vel = Vector3d::Constant(0);

    Vector3d z;

    sat.computeMeasurement(time, provo_ecef, rec_vel, Vector2d::Zero(), z);

    ionoutc_t ion = {true, true,
                     0.1118E-07,-0.7451E-08,-0.5961E-07, 0.1192E-06,
                     0.1167E+06,-0.2294E+06,-0.1311E+06, 0.1049E+07};
    range_t rho;
    computeRange(&rho, sat, &ion, time, provo_ecef);

    EXPECT_NEAR(rho.range, z(0), 1e-5);
    EXPECT_NEAR(rho.rate, z(1), 1e-5);
}

TEST (Satellite, ReadFromFile)
{
  std::vector<int> sat_ids = {3, 8, 10, 11, 14, 18, 22, 31, 32, 61, 62, 64, 67, 83, 84};
  std::vector<int> eph_counts = {4, 4, 14, 9, 14, 14, 5, 14, 14, 21, 39, 39, 24, 18, 39};
  std::vector<Satellite> satellites;
  for (int i = 0; i < sat_ids.size(); i++)
  {
    Satellite sat(sat_ids[i], i);
    sat.readFromRawFile(MULTIROTOR_SIM_DIR"/sample/eph.dat");

//    EXPECT_GT(sat.eph_.size(), 0);
  }
}

TEST (Satellite, ReadFromFileCheckTime)
{
  std::vector<int> sat_ids = {3, 8, 10, 11, 14, 18, 22, 31, 32, 61, 62, 64, 67, 83, 84};
  std::vector<Satellite> satellites;

  DateTime log_time;
  log_time.year = 2018;
  log_time.month = 11;
  log_time.day = 5;
  log_time.hour = 14 + 7; // convert to UTC from MST
  log_time.minute = 50;
  log_time.second = 28;
  GTime log_start = log_time;

  for (int i = 0; i < sat_ids.size(); i++)
  {
    Satellite sat(sat_ids[i], i);
    sat.readFromRawFile(MULTIROTOR_SIM_DIR"/sample/eph.dat");
    EXPECT_LE(std::abs((log_start - sat.eph_.toe).toSec()), Satellite::MAXDTOE);
  }
}

TEST (Satellite, ReadFromFileCheckPositions)
{
  std::vector<int> sat_ids = {3, 8, 10, 11, 14, 18, 22, 31, 32};
  std::vector<Satellite> satellites;

  MatrixXd truth(9,3);
  truth <<
           -1.979905544756119,   0.839505069743874,   1.550338475517639,
           -2.550826868235846,  -0.608941404478547,  -0.468480492854142,
            0.903365875083797,  -2.301372567736093,   0.949931884053757,
           -2.153305827938237,  -0.469592151583447,   1.412499785966901,
           -0.716190834612790,  -1.596467415879906,   2.033341931482639,
           -1.776768089932820,  -1.220720590953266,   1.513509991901378,
           -1.823263124705158,   0.093328649658817,   1.951093711022132,
           -0.647831455760295,  -2.514495751363997,   0.437098017226730,
            0.136331156293721,  -1.525421674314513,   2.174087630568290;
  truth *= 1e7;
  truth.transposeInPlace();

  GTime log_start = GTime::fromUTC(1541454646,  0.993);
  log_start += 200;

  for (int i = 0; i < sat_ids.size(); i++)
  {
    Satellite sat(sat_ids[i], i);
    sat.readFromRawFile(MULTIROTOR_SIM_DIR"/sample/eph.dat");

    Vector3d pos, vel;
    Vector2d clock;
    sat.computePositionVelocityClock(log_start, pos, vel, clock);

    /// TODO: Figure out why this is so far off
    EXPECT_MAT_NEAR(truth.col(i), pos, 4e5);
  }
}

TEST (Satellite, ReadFromFileCheckAzEl)
{
  std::vector<int> sat_ids = {3, 8, 10, 11, 14, 18, 22, 31, 32};
  std::vector<Satellite> satellites;

  GTime log_start = GTime::fromUTC(1541454646,  0.993);

  Vector3d rec_pos {-1798904.13, -4532227.1 ,  4099781.95};

  for (int i = 0; i < sat_ids.size(); i++)
  {
    Satellite sat(sat_ids[i], i);
    sat.readFromRawFile(MULTIROTOR_SIM_DIR"/sample/eph.dat");

    Vector3d pos, vel;
    Vector2d clock, az_el;
    sat.computePositionVelocityClock(log_start, pos, vel, clock);
    Vector3d los_ecef = pos - rec_pos;
    sat.los2azimuthElevation(rec_pos, los_ecef, az_el);
    EXPECT_GE(az_el(1), -0.2);
  }
}

TEST (Satellite, CheckMagnitudeOfCarrierPhase)
{
  Satellite sat(3, 0);
  sat.readFromRawFile(MULTIROTOR_SIM_DIR"/sample/eph.dat");
  GTime log_start = GTime::fromUTC(1541454646,  0.993);
  Vector3d rec_pos {-1798904.13, -4532227.1 ,  4099781.95};
  Vector3d z;
  sat.computeMeasurement(log_start, rec_pos, Vector3d::Zero(), Vector2d::Zero(), z);

  EXPECT_NEAR(z(2), 1.3e8, 1e7);
}
