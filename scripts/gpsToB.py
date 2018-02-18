#!/usr/bin/env python

import rospy
import numpy as np
from std_msgs.msg import Float32
from sensor_msgs.msg import NavSatFix


class gpsToB:
    def __init__(self, destLat, destLong):
        rospy.init_node('gpsToB')
        self.course_pub = rospy.Publisher('course', Float32, queue_size=10)
        self.loc_sub = rospy.Subscriber('/gps/fix', NavSatFix, self.callback)
        self.destLat = destLat
        self.destLong = destLong
        try:
            rospy.spin()
        except KeyboardInterrupt:
            print('Shutting down')

    def angular_distance(self, latDep, longDep, latDest, longDest):
        yDep = np.radians(latDep)
        xDep = np.radians(longDep)
        yDest = np.radians(latDest)
        xDest = np.radians(longDest)

        angular_x = 2 * np.arcsin(np.cos(yDep) * np.sin((xDep - xDest) / 2))
        angular_y = yDep - yDest

        return angular_x, angular_y

    def calcTriangle(self, x, y):
        # vectorize positions of a triangle, with the departure oriented along the z-axis
        dep = np.array([0, 0, 1])
        orth = np.array([-np.sin(x), 0, np.cos(x)])
        dest = np.array(
            [-np.cos(x) * np.sin(y), np.sin(y), np.cos(x) * np.cos(y)])

        vecToOrth = np.cross(np.cross(orth, dep), dep)
        vecToDest = np.cross(np.cross(dest, dep), dep)
        angle = np.arccos(np.dot(vecToDest, vecToOrth) /
                          np.linalg.norm(vecToOrth / np.linalg.norm(vecToDest)))

        return angle

    def callback(self, data):
        latitude = data.latitude
        longitude = data.longitude

        # Find long/lat diffs
        dLatitude = self.destLat - latitude
        dLongitude = self.destLong - longitude

        angular_x, angular_y = self.angular_distance(
            latitude, longitude, self.destLat, self.destLong)

        angle = self.calcTriangle(angular_x, angular_y)

        self.course_pub.publish(np.degrees(angle))


if __name__ == '__main__':
    gpsToB(42.29,-71.26)
