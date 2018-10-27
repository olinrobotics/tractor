/**
  hind_brain.ino
  Purpose: Provides firmware interface from software to hardware, runs
  realtime control/safety loop

  @author Connor Novak
  @email connor.novak@students.olin.edu
  @version 1.4 10/24/2018
*/

// Header file
#include "hind_brain.h"

// Declare switch & estop
Estop *e;
OAKSoftSwitch *l;

// RoboClaws
auto rc1_serial = &Serial1;
RoboClaw rc1(rc1_serial, RC_TIMEOUT);
auto rc2_serial = &Serial2;
RoboClaw rc2(rc2_serial, RC_TIMEOUT);

// States
boolean isEStopped = false;
boolean isAuto = false;

// Global Variables
unsigned int velMsg = VEL_CMD_MIN;                  // Initialize velocity to 0
signed int steerMsg = (STEER_CMD_MAX + STEER_CMD_MIN) / 2; // Initialize steering to straight
unsigned long timer = millis();
char buf[7];

// ROS nodes, publishers, subscribers
ros::NodeHandle nh;
ackermann_msgs::AckermannDrive curr_drive_pose;
ros::Subscriber<ackermann_msgs::AckermannDrive> sub("/cmd_ack", &ackermannCB);
ros::Publisher pub_drive("/curr_drive", &curr_drive_pose);

void setup() {

  // Initialize estop and auto-switch
  e = new Estop(&nh, ESTOP_PIN, 1);
  pinMode(ESTOP_PIN, OUTPUT);
  e->onStop(eStop);
  e->offStop(eStart);
  l = new OAKSoftSwitch(&nh, "/auto", AUTO_LED_PIN);

  //Stop engine for safety
  stopEngine();

  //Open serial communication with roboclaw
  rc1.begin(38400);

  // Set up ROS node, subscribers, publishers
  nh.getHardware()->setBaud(115200);
  nh.initNode();
  nh.subscribe(sub);
  nh.advertise(pub_drive);

  // Wait for connection
  while(true){
    if(nh.connected()) {break;}
    nh.spinOnce();
    delay(1);
  }

  // Send message of connectivity
  // TODO: make wait until motors reach default positions
  delay(500);
  nh.loginfo("Hindbrain connected; Setting motor positions to neutral.");
  delay(500);

  // TODO Operator verify that all pins are removed

  // Set actuators to default positions
  rc1.SpeedAccelDeccelPositionM1(RC1_ADDRESS, 0, 300, 0, velMsg, 1);
  rc1.SpeedAccelDeccelPositionM2(RC1_ADDRESS, 0, 500, 0, steerMsg, 1);

} // setup()

void loop() {

  // Checks for connectivity with mid-brain
  checkSerial(&nh);

  // Send updated motor commands to roboclaws
  if (!isEStopped) {
    updateRoboClaw(velMsg, steerMsg);
  } else {
    stopRoboClaw(&rc1);
  }

  // Update current published pose
  updateCurrDrive();

  // Updates node
  nh.spinOnce();
  delay(1);

} // loop()

void ackermannCB(const ackermann_msgs::AckermannDrive &drive) {
  // Callback for ackermann messages; saves data to global vars
  steerMsg = steerAckToCmd(drive.steering_angle);
  velMsg = velAckToCmd(drive.speed);

}  //ackermannCB()

void checkSerial(ros::NodeHandle *nh) {
  // Given node, estops if node is not connected
  // https://answers.ros.org/question/124481/rosserial-arduino-how-to-check-on-device-if-in-sync-with-host/
  if(!nh->connected()) {
    if(!isEStopped) {
      nh->logerror("Lost connectivity . . .");
      eStop();
    }
  }
}  //checkSerial()

void updateRoboClaw(int velMsg, int steerMsg) {
  // Given velocity and steering message, sends vals to RoboClaw
  // TODO: update to take roboclaw as arg

  // Write velocity to RoboClaw
  rc1.SpeedAccelDeccelPositionM1(RC1_ADDRESS, 100000, 1000, 0, velMsg, 0);

  // Write steering to RoboClaw if tractor is moving, else returns debug msg
  // if (velAckToCmd(curr_drive_pose.speed) < VEL_CMD_MAX/2) {rc1.SpeedAccelDeccelPositionM2(RC1_ADDRESS, 0, 1000, 0, steerMsg, 0);}
  // else {nh.logwarn("Tractor not moving, steering message rejected");}

  timer = millis();  // Reset timer

  // roslog msgs if debugging
  #ifdef DEBUG
    char j[36];
    snprintf(j, sizeof(j), "DBG: steerMsg = %d, velMsg = %d", steerMsg, velMsg);
    nh.loginfo(j);
  #endif //DEBUG

} // updateRoboClaw()

void stopRoboClaw(RoboClaw *rc) {
  // Given roboclaw to stop, publishes messages such that Roboclaw is safe

  // Send velocity pedal to stop position
  rc->SpeedAccelDeccelPositionM1(RC1_ADDRESS, 100000, 1000, 0, VEL_CMD_MIN, 0);

  // Stop steering motor
  rc->SpeedM2(RC1_ADDRESS, 0);

} // stopRoboClaw

void updateCurrDrive() {
  // Read encoder values, convert to ackermann drive, publish

  uint32_t encoder1, encoder2;
  rc1.ReadEncoders(RC1_ADDRESS, encoder1, encoder2);
  curr_drive_pose.speed = mapPrecise(encoder1, VEL_CMD_MIN, VEL_CMD_MAX, VEL_MSG_MIN, VEL_MSG_MAX);
  curr_drive_pose.steering_angle = mapPrecise(encoder2, STEER_CMD_MIN, STEER_CMD_MAX, STEER_MSG_MIN, STEER_MSG_MAX);
  pub_drive.publish(&curr_drive_pose);

} // updateCurrDrive()

int steerAckToCmd(float ack_steer){
  //  Given ackermann steering message, returns corresponding RoboClaw command

  // Convert from range of input signal to range of output signal, then shift signal
  ack_steer = map(ack_steer, STEER_MSG_MIN, STEER_MSG_MAX, STEER_CMD_MIN, STEER_CMD_MAX);

  // Safety limits for signal
  if (ack_steer > STEER_CMD_MAX) {ack_steer = STEER_CMD_MAX;}
  else if (ack_steer < STEER_CMD_MIN) {ack_steer = STEER_CMD_MIN;}

  return ack_steer;
} //steerMsgToCmd

int velAckToCmd(float ack_vel){
  // given ackermann velocity, returns corresponding RoboClaw command

  // filter to remove tractor reversal commands (platform wont back up)
  if (ack_vel < 0) {ack_vel = 0;}

  // Convert from range of input signal to range of output signal
  ack_vel = map(ack_vel, VEL_MSG_MIN, VEL_MSG_MAX, VEL_CMD_MIN, VEL_CMD_MAX);

  // Safety limits for signal; feels switched bc high vals = low speed
  if (ack_vel < VEL_CMD_MAX) {ack_vel = VEL_CMD_MAX;}
  else if(ack_vel > VEL_CMD_MIN) {ack_vel = VEL_CMD_MIN;}

  return ack_vel;
} //velMsgToCmd()

void stopEngine() {
  // Toggles engine stop relay

  digitalWrite(ESTOP_PIN, HIGH);
  delay(2000);
  digitalWrite(ESTOP_PIN, LOW);

} // stopEngine()

void eStop() {
  // Estops tractor

  isEStopped = true;
  nh.logerror("Tractor has E-Stopped");
  stopRoboClaw(&rc1);
  stopEngine();
} // eStop()

void eStart() {
  // Disactivates isEStopped state

  isEStopped = false;

  // Logs verification msg
  char i[32];
  snprintf(i, sizeof(i), "MSG: EStop Disactivated");
  nh.loginfo(i);

} // eStart()

float mapPrecise(float x, float inMin, float inMax, float outMin, float outMax) {
  // Emulates Arduino map() function, but uses floats for precision
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;

} // mapPrecise()
