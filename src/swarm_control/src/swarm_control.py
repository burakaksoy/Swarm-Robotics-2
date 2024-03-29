#!/usr/bin/env python3
import rospy

# import State2D.msg
from swarm_msgs.msg import State2D
from geometry_msgs.msg import Twist, PoseStamped
import geometry_msgs.msg
from arm_msgs.msg import RobotEnableStatus

import tf2_ros
import tf2_msgs.msg
from std_msgs.msg import Bool, Int32
import tf_conversions # quaternion stuff

from std_srvs.srv import Trigger, TriggerRequest, TriggerResponse

from utilities.safe_swarm_controller import *

import numpy as np

import time

import shapely
import shapely.geometry
import shapely.affinity

'''
swarm_control.py
Alex Elias, Burak Aksoy

Commands robot states (position and velocity) to achieve rigid-body motion

Parameters:

    desired_swarm_vel_topic_name:
    just_swarm_frame_vel_input_topic_name:

    N_robots: Numer of robots to control

    theta_scale: How much we weigh theta vs x,y in choosing safe swarm velocity
        Higher theta_scale ==> Swarm velocity follows angle commands closer
        (But doesn't follow XY commands as closely)

    Repeat for all N robots:
        just_robot_vel_input_topic_name_0:
        state_publish_topic_name_0: State2D command for robot 0
        tf_frame_name_0: Frame name (for rviz) for robot 0
        vel_lim_x_0: Velocity limit in X for robot 0
        vel_lim_y_0: Velocity limit in Y for robot 0
        vel_lim_theta_0: Velocity limit in theta for robot 0
        acc_lim_x_0: Acceleration limit in X for robot 0
        acc_lim_y_0: Acceleration limit in Y for robot 0
        acc_lim_theta_0: Acceleration limit in theta for robot 0

'''

# Velocity commands will only be considered if they are spaced closer than MAX_TIMESTEP
MAX_TIMESTEP = 0.1
log_tag = "Swarm Controller"

class Swarm_Control:
    def __init__(self):
        rospy.init_node('swarm_controller', anonymous=False)

        topic_prefix = rospy.get_param('~topic_prefix', "")

        # Read in all the parameters
        desired_swarm_vel_topic_name = topic_prefix + rospy.get_param('~desired_swarm_vel_topic_name')
        just_swarm_frame_vel_input_topic_name = topic_prefix + rospy.get_param('~just_swarm_frame_vel_input_topic_name')
        sync_frame_topic_name = topic_prefix + rospy.get_param('~frame_sync_topic_name')
        robot_enable_status_topic_name = topic_prefix + rospy.get_param('~robot_enable_status_topic_name')

        self.tf_swarm_frame_name = rospy.get_param('~tf_swarm_frame_name', 'swarm_frame')

        self.N_robots = rospy.get_param('~N_robots')
        self.theta_scale = rospy.get_param('~theta_scale')

        just_robot_vel_input_topic_names = ['']*self.N_robots
        state_publish_topic_names = ['']*self.N_robots
        self.tf_frame_names = ['']*self.N_robots
        self.footprints = [None]*self.N_robots
        self.vel_pubs = [0]*self.N_robots
        self.enabled_robots=[]
        self.v_max = np.zeros((3, self.N_robots))
        self.a_max = np.zeros((3, self.N_robots))

        for i in range(self.N_robots):
            self.enabled_robots.append(False)
            # just_robot_vel_input_topic_names[i] = topic_prefix + rospy.get_param('~just_robot_vel_input_topic_name_' + str(i))
            just_robot_vel_input_topic_names[i] = rospy.get_param('~just_robot_vel_input_topic_name_' + str(i))
            state_publish_topic_names[i] = rospy.get_param('~state_publish_topic_name_' + str(i))
            self.tf_frame_names[i] = rospy.get_param('~tf_frame_name_' + str(i))
            self.footprints[i] = np.array(rospy.get_param('~footprint_' + str(i))) # each element in the footprints list has an Nx2 numpy array that represents the footprint coordinates in the robot center where N is the number points in the footprint of the robot.

            self.v_max[0, i] = rospy.get_param('~vel_lim_x_' 	 + str(i))
            self.v_max[1, i] = rospy.get_param('~vel_lim_y_' 	 + str(i))
            self.v_max[2, i] = rospy.get_param('~vel_lim_theta_' + str(i))
            self.a_max[0, i] = rospy.get_param('~acc_lim_x_' 	 + str(i))
            self.a_max[1, i] = rospy.get_param('~acc_lim_y_'	 + str(i))
            self.a_max[2, i] = rospy.get_param('~acc_lim_theta_' + str(i))

        # Subscribe
        
        rospy.Subscriber(desired_swarm_vel_topic_name, Twist, self.desired_swarm_velocity_callback, queue_size=1)
        rospy.Subscriber(just_swarm_frame_vel_input_topic_name, Twist, self.just_swarm_frame_velocity_callback, queue_size=1)
        rospy.Subscriber(sync_frame_topic_name, PoseStamped, self.frame_changer_callback, queue_size=20)
        # rospy.Subscriber(robot_enable_status_topic_name, Int32, self.robot_enable_changer, queue_size=5)
        rospy.Subscriber(robot_enable_status_topic_name, RobotEnableStatus, self.robot_enable_changer_callback, queue_size=5)

        for i in range(self.N_robots):
            rospy.Subscriber(just_robot_vel_input_topic_names[i], Twist, self.just_robot_velocity_callback, i, queue_size=1)

        # Publish
        self.tf_broadcaster = tf2_ros.TransformBroadcaster()
        for i in range(self.N_robots):
            self.vel_pubs[i] = rospy.Publisher(state_publish_topic_names[i], State2D, queue_size=1)

        # Initialize local variables to keep track of swarm/robot frames
        self.swarm_xyt = np.zeros((3,1))
        self.robots_xyt = np.zeros((3, self.N_robots))
        self.robots_last_velocities = np.zeros((3, self.N_robots))
        self.last_timestep_requests = {}
        self.v_robots_prev = np.zeros((3,self.N_robots))

        # Swarm footprint polygon publisher
        self.footprint_publish_topic_name = rospy.get_param('~footprint_publish_topic_name', "swarm_footprint")
        self.footprint_pub = rospy.Publisher(self.footprint_publish_topic_name, geometry_msgs.msg.PolygonStamped, queue_size=1)

        # Service Names
        self.send_swarm_frame_to_centroid_service_name = rospy.get_param("~send_swarm_frame_to_centroid_service_name", "send_swarm_frame_to_centroid")
        # Service to send the swarm frame to the centroid of virtual frames of the enabled robots
        self.srv_send_swarm_frame_to_centroid = rospy.Service(self.send_swarm_frame_to_centroid_service_name, 
                                                    Trigger, 
                                                    self.srv_send_swarm_frame_to_centroid_cb)


        # TF publish timer
        tf_update_rate = 30.0
        rospy.Timer(rospy.Duration(1.0 / tf_update_rate), self.publish_tf_frames)

        # Footprint publish timer
        footprint_update_rate = 3.0
        rospy.Timer(rospy.Duration(1.0 / footprint_update_rate), self.publish_formation_footprint_polygon)


    # def robot_enable_changer(self,data):
    #     number=data.data
    #     rospy.logwarn(str(len(self.enabled_robots)))
    #     rospy.logwarn(str(self.N_robots))
    #     for i in range(self.N_robots-1,-1,-1):
    #         if(number>>i==1):
    #             number-=pow(2,i)
                
    #             self.enabled_robots[i]=False
    #             rospy.logwarn("disabled robot: "+str(i+1)+"status: "+str(self.enabled_robots[i]))
    #             self.robots_last_velocities[:,i] = 0
    #         else:
                
    #             self.enabled_robots[i]=True
    #             rospy.logwarn("enabled robot: "+str(i+1)+"status: "+str(self.enabled_robots[i]))

    def robot_enable_changer_callback(self, msg: RobotEnableStatus) -> None:
        '''Changes the enable status of the robots based on the message.

        Args:
            msg: RobotEnableStatus containing enabled_ids and disabled_ids.
                    Each message is team-specific and contains the true
                    robot IDs.
        '''
        enabled_ids = msg.enabled_ids
        disabled_ids = msg.disabled_ids

        # Set the enabled IDs to True.
        for robot_id in enabled_ids:
            # Indices start at 0, but ID's start at 1.
            robot_idx = robot_id - 1
            self.enabled_robots[robot_idx] = True

        # Set the disabled IDs to False.
        for robot_id in disabled_ids:
            # Indices start at 0, but ID's start at 1.
            robot_idx = robot_id - 1
            self.enabled_robots[robot_idx] = False
        
        rospy.loginfo(f"{log_tag}: Robot enable status: "
                      f"{self.enabled_robots}"
        )

    def frame_changer_callback(self,data):
        """
        Synchronizes the virtual robot frame locations to the current real robot locations.
        """
        if(data.header.frame_id in self.tf_frame_names):
            array_position=self.tf_frame_names.index(data.header.frame_id)
            orientations= [data.pose.orientation.x,data.pose.orientation.y,data.pose.orientation.z,data.pose.orientation.w]
            (roll,pitch,yaw)=tf_conversions.transformations.euler_from_quaternion(orientations)
            rospy.logwarn(np.array2string(self.robots_xyt))
            self.robots_xyt[0,array_position]=data.pose.position.x
            self.robots_xyt[1,array_position]=data.pose.position.y
            self.robots_xyt[2,array_position]=yaw

    def srv_send_swarm_frame_to_centroid_cb(self, req):
        assert isinstance(req, TriggerRequest)
        rospy.loginfo("Sending the swarm frame to the centroid of the robot frames")
        
        """
        Sends the virtual swarm frame to the centroid of 
        current locations of virtual frames of the enabled robots in the swarm.
        (Keeps the previous orientation of the swarm frame the same)
        """
        # Check if any robot is enabled
        if any(self.enabled_robots):
            # Use numpy's boolean indexing to get coordinates of enabled robots
            enabled_indices = np.where(self.enabled_robots)[0]
            coords = self.robots_xyt[:2, enabled_indices].T  # Transposed to get Nx2 array

            # Calculate the centroid of the robots
            centroid = np.mean(coords, axis=0)
            centroid_xyt = np.array([centroid[0], centroid[1], 0.0]).reshape((3, 1))  # in swarm frame

            # Send the swarm frame to the centroid in world frame
            self.swarm_xyt = self.swarm_xyt + rot_mat_3d(self.swarm_xyt[2, 0]).dot(centroid_xyt)

            # Keep the virtual robot frame locations in the world the same, so subtract the centroid
            self.robots_xyt = self.robots_xyt - centroid_xyt

        return TriggerResponse(success=True, message="The swarm frame is sent to the centroid of the robot frames!")

    def desired_swarm_velocity_callback(self, data):
        if sum(self.enabled_robots) == 0:
            return

        dt = self.get_timestep("desired_swarm_velocity")
        if dt == 0: # Exceeded MAX_TIMESTEP
            self.v_robots_prev *= 0.
            return

        v_desired = np.zeros((3,1))
        v_desired[0, 0] = data.linear.x
        v_desired[1, 0] = data.linear.y
        v_desired[2, 0] = data.angular.z


        enabled_index = np.where(self.enabled_robots)[0]

        p_i_mat = self.robots_xyt[0:1+1,enabled_index]
        theta_vec = self.robots_xyt[[2],[enabled_index]]
        
        v_i_world, v_i_rob, xyt_i, v, xyt_swarm_next = safe_motion_controller(
            v_desired, self.theta_scale, p_i_mat, theta_vec,
            self.v_max[:, enabled_index], self.a_max[:, enabled_index], dt, sum(self.enabled_robots),
            self.v_robots_prev[:, enabled_index], self.swarm_xyt)

        # Don't update self.robots_xyt, since that's in the swarm frame
        self.v_robots_prev[:, enabled_index] = v_i_rob;
        self.swarm_xyt = xyt_swarm_next

        # Send desired state to each robot
        for i in range(sum(self.enabled_robots)):
            i_total = enabled_index[i]
            
            s = State2D();
            s.pose.x = xyt_i[0,i]
            s.pose.y = xyt_i[1,i]
            s.pose.theta = xyt_i[2,i]
            s.twist.linear.x = v_i_world[0,i]
            s.twist.linear.y = v_i_world[1,i]
            s.twist.angular.z = v_i_world[2,i]
            self.vel_pubs[i_total].publish(s)

    def just_swarm_frame_velocity_callback(self, data):
        '''
        Move the position of the swarm frame without moving the robots
        i.e. move the swarm frame, and move the robots
            w.r.t. the swarm frame in the opposite direction
        '''
        dt = self.get_timestep("desired_swarm_frame_velocity")

        qd_world = np.zeros((3,1))
        qd_world[0, 0] = data.linear.x
        qd_world[1, 0] = data.linear.y
        qd_world[2, 0] = data.angular.z

        qd_swarm = rot_mat_3d(-self.swarm_xyt[2, 0]).dot(qd_world)

        q_world_delta = dt * qd_world
        q_swarm_delta = dt * qd_swarm

        self.swarm_xyt = self.swarm_xyt + q_world_delta

        #  np.diag([1., 1., 0.]).
        self.robots_xyt = rot_mat_3d(-q_swarm_delta[2,0]).dot(self.robots_xyt  - q_swarm_delta )

    def just_robot_velocity_callback(self, data, i_robot):
        if(self.enabled_robots[i_robot]):
            # Move the position of robot i_robot in the world frame
            dt = self.get_timestep("just_robot_velocty_"+str(i_robot))

            qd_world = np.zeros((3,1))
            qd_world[0, 0] = data.linear.x
            qd_world[1, 0] = data.linear.y
            qd_world[2, 0] = data.angular.z

            # robots_xyt is in the swarm frame
            qd_swarm = rot_mat_3d(-self.swarm_xyt[2]).dot(qd_world)
            self.robots_xyt[:,i_robot] = self.robots_xyt[:,i_robot] + dt * qd_swarm.flatten()
            #print(self.robots_xyt)


    def get_timestep(self, integrator_name):
        current_time = time.time()
        if integrator_name in self.last_timestep_requests:
            dt = current_time - self.last_timestep_requests[integrator_name]
            self.last_timestep_requests[integrator_name] = current_time
            if dt > MAX_TIMESTEP:
                dt = 0.0
            return dt
        else:
            self.last_timestep_requests[integrator_name] = current_time
            return 0.0

    
    def publish_tf_frames(self,event):
        tf_swarm_frame = xyt2TF(self.swarm_xyt, "map", self.tf_swarm_frame_name)
        self.tf_broadcaster.sendTransform(tf_swarm_frame)

        for i in range(self.N_robots):
            if(self.enabled_robots[i]):
                tf_robot_i = xyt2TF(self.robots_xyt[:,i], self.tf_swarm_frame_name, self.tf_frame_names[i])
                self.tf_broadcaster.sendTransform(tf_robot_i)


    def publish_formation_footprint_polygon(self,event):
        if sum(self.enabled_robots) == 0:
            return
        
        # Find the points in the active formation
        coords = []
        coords.append([0.0,0.0]) # Include the location of the swarm frame to the footprint
        default_footprint = [[0.345, -0.260], [0.345, 0.260], [-0.345, 0.260], [-0.345, -0.260]]
        for point in default_footprint:
            coords.append(point)

        for i in range(self.N_robots):
            if(self.enabled_robots[i]):
                xyt = self.robots_xyt[:,i].flatten() # in swarm frame pose

                robot_pts = self.footprints[i] # Nx2
                # convert to shapely multi point object
                robot_pts_multi_pt = shapely.geometry.MultiPoint(robot_pts)
                # rotate
                robot_pts_multi_pt = shapely.affinity.rotate(robot_pts_multi_pt, xyt[2], origin=(0,0), use_radians=True)
                # translate
                robot_pts_multi_pt = shapely.affinity.translate(robot_pts_multi_pt, xoff=xyt[0], yoff=xyt[1])

                for point in robot_pts_multi_pt.geoms:
                    coords.append(list(point.coords[0]))

        # Find the convex hull of the points with shapely
        multi_pt = shapely.geometry.MultiPoint(coords)
        hull = multi_pt.convex_hull
        
        # NOTE: convex_hull will return a polygon if your hull contains more than two points. If it only contains two points, you'll get a LineString, and if it only contains one point, you'll get a Point.

        # Convert the hull points to ROS Point32 messages
        polygon = []
        if hull.geom_type == 'Polygon':
            for pt in list(hull.exterior.coords):
                point32 = geometry_msgs.msg.Point32()
                point32.x = pt[0]
                point32.y = pt[1]
                point32.z = 0  # assuming 2D coordinates
                polygon.append(point32)
        else:
            rospy.loginfo("Not enough points to form a polygon.")
            return
        
        # Prepare the msg and publish
        msg = geometry_msgs.msg.PolygonStamped()
        msg.header.stamp = rospy.Time.now()
        msg.header.frame_id = self.tf_swarm_frame_name  # replace with appropriate frame
        msg.polygon.points = polygon
        self.footprint_pub.publish(msg)

            
def rot_mat_3d(theta):
    c, s = np.cos(theta), np.sin(theta)
    return np.array([[c, -s, 0.], [s, c, 0.], [0., 0., 1.]])
    
def wrapToPi(a):
    '''
    Wraps angle to [-pi,pi)
    '''
    return ((a+np.pi) % (2*np.pi))-np.pi

def xyt2TF(xyt, header_frame_id, child_frame_id):
    '''
    Converts a numpy vector [x; y; theta]
    into a tf2_msgs.msg.TFMessage message
    '''
    xyt = xyt.flatten()
    t = geometry_msgs.msg.TransformStamped()

    t.header.frame_id = header_frame_id
    #t.header.stamp = ros_time #rospy.Time.now()
    t.header.stamp = rospy.Time.now()
    t.child_frame_id = child_frame_id
    t.transform.translation.x = xyt[0]
    t.transform.translation.y = xyt[1]
    t.transform.translation.z = 0

    q = tf_conversions.transformations.quaternion_from_euler(0, 0,xyt[2])
    t.transform.rotation.x = q[0]
    t.transform.rotation.y = q[1]
    t.transform.rotation.z = q[2]
    t.transform.rotation.w = q[3]

    return t

if __name__ == '__main__':
    swarm_control = Swarm_Control()
    rospy.spin()
