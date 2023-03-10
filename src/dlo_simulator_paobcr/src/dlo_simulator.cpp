/*
 * Author: Burak Aksoy
 */

#include "dlo_simulator_paobcr/dlo_simulator.h"

// using namespace std;
using namespace dlo_simulator;

DloSimulator::DloSimulator(ros::NodeHandle &nh, ros::NodeHandle &nh_local, boost::recursive_mutex &mtx): 
    nh_(nh), 
    nh_local_(nh_local),
    mtx_(mtx)
{
    p_active_ = false;

    time_frames_ = 0;
    time_sum_ = 0.0;

    is_auto_sim_rate_set_ = false; 

    /*
    is_rob_01_attached_ = false;
    is_rob_02_attached_ = false;
    is_rob_03_attached_ = false;
    is_rob_04_attached_ = false;

    rob_01_attached_id_ = -1; //note: ids start from 0. -1 would be a null id.
    rob_02_attached_id_ = -1;
    rob_03_attached_id_ = -1;
    rob_04_attached_id_ = -1;

    rob_01_attached_force_.setZero();
    rob_02_attached_force_.setZero();
    rob_03_attached_force_.setZero();
    rob_04_attached_force_.setZero();
    */

    // Initialize Timers with deafault period (note: last 2 false mean: oneshot=false, autostart=false)
    timer_render_ = nh_.createTimer(ros::Duration(1.0), &DloSimulator::render, this,false, false); 
    timer_simulate_ = nh_.createTimer(ros::Duration(1.0), &DloSimulator::simulate, this,false, false); 

    /*
    timer_wrench_pub_ = nh_.createTimer(ros::Duration(1.0), &DloSimulator::publishWrenches, this,false, false); 
    */

    // Initilize parameters
    params_srv_ = nh_local_.advertiseService("params", &DloSimulator::updateParams, this);
    initialize();
}

DloSimulator::~DloSimulator() {
    nh_local_.deleteParam("gravity_x");
    nh_local_.deleteParam("gravity_y");
    nh_local_.deleteParam("gravity_z");

    nh_local_.deleteParam("dt");
    nh_local_.deleteParam("set_sim_rate_auto");
    nh_local_.deleteParam("num_substeps");
    nh_local_.deleteParam("num_steps");

    nh_local_.deleteParam("dlo_l");
    nh_local_.deleteParam("dlo_r");
    nh_local_.deleteParam("dlo_density");
    nh_local_.deleteParam("dlo_resolution");

    nh_local_.deleteParam("dlo_stretching_compliance");
    nh_local_.deleteParam("dlo_shearing_compliance_x");
    nh_local_.deleteParam("dlo_shearing_compliance_y");

    nh_local_.deleteParam("dlo_twisting_compliance");
    nh_local_.deleteParam("dlo_bending_compliance_x");
    nh_local_.deleteParam("dlo_bending_compliance_y");

    nh_local_.deleteParam("initial_height");

    nh_local_.deleteParam("num_hang_corners");

    nh_local_.deleteParam("simulation_rate");
    nh_local_.deleteParam("rendering_rate");
    /*
    nh_local_.deleteParam("wrench_pub_rate");
    */
    nh_local_.deleteParam("dlo_points_topic_name");
    nh_local_.deleteParam("dlo_points_frame_id");
    
    /*
    nh_local_.deleteParam("odom_01_topic_name");
    nh_local_.deleteParam("odom_02_topic_name");
    nh_local_.deleteParam("odom_03_topic_name");
    nh_local_.deleteParam("odom_04_topic_name");

    nh_local_.deleteParam("wrench_01_topic_name");
    nh_local_.deleteParam("wrench_02_topic_name");
    nh_local_.deleteParam("wrench_03_topic_name");
    nh_local_.deleteParam("wrench_04_topic_name");

    nh_local_.deleteParam("wrench_01_frame_id");
    nh_local_.deleteParam("wrench_02_frame_id");
    nh_local_.deleteParam("wrench_03_frame_id");
    nh_local_.deleteParam("wrench_04_frame_id");
    */

    nh_local_.deleteParam("dlo_rob_z_offset");
}

bool DloSimulator::updateParams(std_srvs::Empty::Request& req, std_srvs::Empty::Response& res)
{
    bool prev_active = p_active_;

    // Get parameters from the parameter server
    nh_local_.param<bool>("active", p_active_, true);
    nh_local_.param<bool>("reset", p_reset_, false);

    nh_local_.param<Real>("gravity_x", gravity_x_, 0.0);
    nh_local_.param<Real>("gravity_y", gravity_y_, 0.0);
    nh_local_.param<Real>("gravity_z", gravity_z_, -9.81);
    
    nh_local_.param<Real>("dt", dt_, 1.0 / 100.0); //200
    nh_local_.param<bool>("set_sim_rate_auto", set_sim_rate_auto_, false); // to set the simulation rate and dt automatically

    nh_local_.param<int>("num_substeps", num_substeps_, 30); //3
    nh_local_.param<int>("num_steps", num_steps_, 1);
    
    nh_local_.param<Real>("dlo_l", dlo_l_, 3.); //2
    nh_local_.param<Real>("dlo_r", dlo_r_, 0.005); //2
    nh_local_.param<Real>("dlo_density", dlo_density_, 1000.0);
    nh_local_.param<Real>("dlo_resolution", dlo_resolution_, 10); //10

    nh_local_.param<Real>("dlo_stretching_compliance", dlo_stretching_compliance_, 0.01);
    nh_local_.param<Real>("dlo_shearing_compliance_x", dlo_shearing_compliance_x_, 0.01);
    nh_local_.param<Real>("dlo_shearing_compliance_y", dlo_shearing_compliance_y_, 0.01);

    nh_local_.param<Real>("dlo_twisting_compliance", dlo_twisting_compliance_, 0.01);
    nh_local_.param<Real>("dlo_bending_compliance_x", dlo_bending_compliance_x_, 0.01);
    nh_local_.param<Real>("dlo_bending_compliance_y", dlo_bending_compliance_y_, 0.01);

    nh_local_.param<Real>("initial_height", initial_height_, 1.0);

    nh_local_.param<int>("num_hang_corners", num_hang_corners_, 1);
    
    nh_local_.param<Real>("simulation_rate", simulation_rate_, 90.0); //90
    nh_local_.param<Real>("rendering_rate", rendering_rate_, 30.0); //30
    /*
    nh_local_.param<Real>("wrench_pub_rate", wrench_pub_rate_, 60.0); //60
    */
    nh_local_.param<std::string>("dlo_points_topic_name", dlo_points_topic_name_, std::string("dlo_points"));
    nh_local_.param<std::string>("dlo_points_frame_id", dlo_points_frame_id_, std::string("map"));

    /*
    nh_local_.param<std::string>("odom_01_topic_name", odom_01_topic_name_, std::string("d1/ground_truth/dlo_mount/odom"));
    nh_local_.param<std::string>("odom_02_topic_name", odom_02_topic_name_, std::string("d2/ground_truth/dlo_mount/odom"));
    nh_local_.param<std::string>("odom_03_topic_name", odom_03_topic_name_, std::string("d3/ground_truth/dlo_mount/odom"));
    nh_local_.param<std::string>("odom_04_topic_name", odom_04_topic_name_, std::string("d4/ground_truth/dlo_mount/odom"));

    nh_local_.param<std::string>("wrench_01_topic_name", wrench_01_topic_name_, std::string("d1/dlo_wrench_stamped"));
    nh_local_.param<std::string>("wrench_02_topic_name", wrench_02_topic_name_, std::string("d2/dlo_wrench_stamped"));
    nh_local_.param<std::string>("wrench_03_topic_name", wrench_03_topic_name_, std::string("d3/dlo_wrench_stamped"));
    nh_local_.param<std::string>("wrench_04_topic_name", wrench_04_topic_name_, std::string("d4/dlo_wrench_stamped"));

    nh_local_.param<std::string>("wrench_01_frame_id", wrench_01_frame_id_, std::string("d1_tf_dlo_mount_link"));
    nh_local_.param<std::string>("wrench_02_frame_id", wrench_02_frame_id_, std::string("d2_tf_dlo_mount_link"));
    nh_local_.param<std::string>("wrench_03_frame_id", wrench_03_frame_id_, std::string("d3_tf_dlo_mount_link"));
    nh_local_.param<std::string>("wrench_04_frame_id", wrench_04_frame_id_, std::string("d4_tf_dlo_mount_link"));
    */

    nh_local_.param<Real>("dlo_rob_z_offset_", dlo_rob_z_offset_, 0.0); // 0.785145); // makes it 80cm above ground

    // Set timer periods based on the parameters
    timer_render_.setPeriod(ros::Duration(1.0/rendering_rate_));
    timer_simulate_.setPeriod(ros::Duration(1.0/simulation_rate_));

    /*
    timer_wrench_pub_.setPeriod(ros::Duration(1.0/wrench_pub_rate_));
    */

    // Initilize gravity vector
    gravity_ << gravity_x_, gravity_y_, gravity_z_;    

    //Create mesh
    std::string dlo_name = "dlo";
    pbd_object::MeshDLO dlo_mesh = DloSimulator::createMeshDLO(dlo_name, dlo_l_, initial_height_, dlo_resolution_);

    // std::cout << "dlo_mesh.name: " << dlo_mesh.name << std::endl;

    // Create dlo
    dlo_ = pbd_object::Dlo(dlo_mesh, 
                           dlo_stretching_compliance_, 
                           dlo_shearing_compliance_x_, 
                           dlo_shearing_compliance_y_, 
                           dlo_twisting_compliance_, 
                           dlo_bending_compliance_x_, 
                           dlo_bending_compliance_y_, 
                           dlo_density_,dlo_r_);

    // Hang dlo from corners
    dlo_.hangFromCorners(num_hang_corners_);

    if (p_active_ != prev_active) {
        if (p_active_) {
            // Create visualization marker publisher
            pub_dlo_points_ = nh_.advertise<visualization_msgs::Marker>(dlo_points_topic_name_, 1);

            /*
            // Create subscribers
            sub_odom_01_ = nh_.subscribe(odom_01_topic_name_, 1, &DloSimulator::odometryCb_01, this);
            sub_odom_02_ = nh_.subscribe(odom_02_topic_name_, 1, &DloSimulator::odometryCb_02, this);
            sub_odom_03_ = nh_.subscribe(odom_03_topic_name_, 1, &DloSimulator::odometryCb_03, this);
            sub_odom_04_ = nh_.subscribe(odom_04_topic_name_, 1, &DloSimulator::odometryCb_04, this);

            // Create publishers
            pub_wrench_stamped_01_ = nh_.advertise<geometry_msgs::WrenchStamped>(wrench_01_topic_name_, 1);
            pub_wrench_stamped_02_ = nh_.advertise<geometry_msgs::WrenchStamped>(wrench_02_topic_name_, 1);
            pub_wrench_stamped_03_ = nh_.advertise<geometry_msgs::WrenchStamped>(wrench_03_topic_name_, 1);
            pub_wrench_stamped_04_ = nh_.advertise<geometry_msgs::WrenchStamped>(wrench_04_topic_name_, 1);
            */

            // Start timers
            timer_simulate_.start();
            timer_render_.start();

            /*
            timer_wrench_pub_.start();
            */
        }
        else {
            // Send empty message?

            // Stop publishers
            pub_dlo_points_.shutdown();

            /*
            // Stop subscribers
            sub_odom_01_.shutdown();
            sub_odom_02_.shutdown();
            sub_odom_03_.shutdown();
            sub_odom_04_.shutdown();

            // Stop publishers
            pub_wrench_stamped_01_.shutdown();
            pub_wrench_stamped_02_.shutdown();
            pub_wrench_stamped_03_.shutdown();
            pub_wrench_stamped_04_.shutdown();
            */

            // Stop timers
            timer_render_.stop();
            timer_simulate_.stop();

            /*
            timer_wrench_pub_.stop();
            */
        }
    }

    if (p_reset_)
        reset();

    return true;
}

void DloSimulator::reset(){
    time_frames_ = 0;
    time_sum_ = 0.0;

    is_auto_sim_rate_set_ = false; 

    /*
    is_rob_01_attached_ = false;
    is_rob_02_attached_ = false;
    is_rob_03_attached_ = false;
    is_rob_04_attached_ = false;

    rob_01_attached_id_ = -1; //note: ids start from 0. -1 would be a null id.
    rob_02_attached_id_ = -1;
    rob_03_attached_id_ = -1;
    rob_04_attached_id_ = -1;

    rob_01_attached_force_.setZero();
    rob_02_attached_force_.setZero();
    rob_03_attached_force_.setZero();
    rob_04_attached_force_.setZero();
    */

    p_reset_ = false;
    nh_local_.setParam("reset",false);
}

pbd_object::MeshDLO DloSimulator::createMeshDLO(const std::string &name, const Real &dlo_l, const Real &dlo_z, const Real &dlo_res){
    // Assume: a dlo centered at the origin, and parallel with the y-axis, 
    // Assume: particle ids start from 0, and each consecutive id pair is an edge (e.g., 0-1, 1-2, ...)
    // Each edge has an orientation specified with a quaternion

    int num_particles = dlo_l * dlo_res + 1;
    int num_quaternions = num_particles - 1;

    // create a linear spaced coordinate vectors for the coordinates

    // Create vector of 3D Eigen vectors to hold the "list of vertices" 
    std::vector<Eigen::Matrix<Real,3,1>> vertices(num_particles);
    // Create vector of queternions to hold the "list of orientations" 
    std::vector<Eigen::Quaternion<Real>> quaternions(num_quaternions);

    
    // Generate the y_coords
    Eigen::Matrix<Real,1,Eigen::Dynamic> y_coords(num_particles);
    Real start = dlo_l/2.0;
    Real end = -dlo_l/2.0;
    Real step = (end - start) / (num_particles - 1);

    for (int i = 0; i < num_particles; i++) {
        y_coords(i) = start + i * step;
    }

    // Create vertices with x,y,z coordinates (x=0 based on the assumptions above)
    for (int j = 0; j < y_coords.size(); j++) {
        Eigen::Matrix<Real,3,1> v(0.0, y_coords(j), dlo_z);
        vertices[j] = v;
    }

    // Create quaterninons (orientations) 
    Eigen::Matrix<Real, 1, 3> from(0,0,1); // dlo is expanded following z-axes
    for (int j = 0; j < num_quaternions; j++) {
        Eigen::Matrix<Real, 1, 3> to = (vertices[j+1] - vertices[j]).normalized();
        Eigen::Quaternion<Real> dq = Eigen::Quaternion<Real>::FromTwoVectors(from,to);

        if (j == 0){
            quaternions[j] = dq;
        } else {
            quaternions[j] = (dq * quaternions[j-1]); // notice the reverse order!
        }

        from = to;
    }

    pbd_object::MeshDLO mesh;
    mesh.name = name;
    mesh.vertices = vertices;
    mesh.quaternions = quaternions;

    return mesh;
}

void DloSimulator::simulate(const ros::TimerEvent& e){
    // With some kind of self lock to prevent collision with rendering
    boost::recursive_mutex::scoped_lock lock(mtx_);

    Real sdt = dt_ / num_substeps_;

    ros::Time start_time = ros::Time::now();

    // Small steps implementation
    // -------------------------------
    for (int i = 0; i< num_steps_; i++){
        for (int j = 0; j < num_substeps_; j++){
            
            dlo_.preSolve(sdt,gravity_);
            dlo_.solve(sdt);
            dlo_.postSolve(sdt);
        }
    }
    // -------------------------------

    // // To debug force readings from hanged corners (use inly when robots are not attached)
    // std::vector<int> *attached_ids_ptr = dlo_.getAttachedIdsPtr();
    // Eigen::Matrix<Real,Eigen::Dynamic,3> *for_ptr = dlo_.getForPtr();
    // int id = (*attached_ids_ptr)[0]; // First attached id
    // // force at that attached id
    // std::cout << "id: " << id << ". Force = " << for_ptr->col(id)/num_substeps_ << " N." << std::endl;

    // +++++++++++++++++++++++++++++++++++++++++++++++++++==
    /*
    readAttachedRobotForces();
    dlo_.resetForces();
    */
    // +++++++++++++++++++++++++++++++++++++++++++++++++++==

    ros::Time finish_time = ros::Time::now();
    ros::Duration elapsed_time = finish_time - start_time;
    time_sum_ += elapsed_time.toSec();
    time_frames_ += 1;
    if (time_frames_ > 100) {
        time_sum_ /= time_frames_;
        ROS_INFO("[Dlo Simulator]: %-4.2lf ms per simulation iteration", time_sum_*1000);
        // Smart dt and simulation rate selection (debug)
        if (!is_auto_sim_rate_set_ && set_sim_rate_auto_) {
            dt_ = time_sum_;
            timer_simulate_.setPeriod(ros::Duration(time_sum_));
            is_auto_sim_rate_set_ = true; 
            std::cout << "Automatic setup. dt = " << dt_ << " seconds." << std::endl;
        }
        time_frames_ = 0;
        time_sum_ = 0;
    }
}

void DloSimulator::render(const ros::TimerEvent& e){
    // // With some kind of self lock to prevent collision with simulation
    boost::recursive_mutex::scoped_lock lock(mtx_);

    const std::vector<Eigen::Matrix<Real,3,1>> *pos_ptr = dlo_.getPosPtr();
    const Eigen::Matrix2Xi *stretching_ids_ptr = dlo_.getStretchShearIdsPtr();
    drawRviz(pos_ptr);
    drawRvizWireframe(pos_ptr,stretching_ids_ptr);
}

void DloSimulator::drawRviz(const std::vector<Eigen::Matrix<Real,3,1>> *poses){
    std::vector<geometry_msgs::Point> dloRVIZPoints;

    for (int i = 0; i < poses->size(); i++) {
        geometry_msgs::Point p;
        p.x = (*poses)[i](0);
        p.y = (*poses)[i](1);
        p.z = (*poses)[i](2);

        dloRVIZPoints.push_back(p);
    }

    publishRvizPoints(dloRVIZPoints);
}

void DloSimulator::drawRvizWireframe(const std::vector<Eigen::Matrix<Real,3,1>> *poses, const Eigen::Matrix2Xi *ids){
    // objects: *poses, *ids
    std::vector<geometry_msgs::Point> dloRVIZEdges;

    for (int i = 0; i < ids->cols(); i++) {
        int id0 = (*ids)(0,i);
        int id1 = (*ids)(1,i);

        geometry_msgs::Point p1;
        p1.x = (*poses)[id0](0);
        p1.y = (*poses)[id0](1);
        p1.z = (*poses)[id0](2);
        dloRVIZEdges.push_back(p1);

        geometry_msgs::Point p2;
        p2.x = (*poses)[id1](0);
        p2.y = (*poses)[id1](1);
        p2.z = (*poses)[id1](2);
        dloRVIZEdges.push_back(p2);
    }

    publishRvizLines(dloRVIZEdges);
}

void DloSimulator::publishRvizPoints(const std::vector<geometry_msgs::Point> &points){
    visualization_msgs::Marker m;

    m.header.frame_id = dlo_points_frame_id_;
    m.header.stamp = ros::Time::now();

    m.type = visualization_msgs::Marker::POINTS;
    m.id = 0;
    m.action = visualization_msgs::Marker::ADD;

    m.pose.orientation.w = 1.0;

    m.points = points;

    m.scale.x = 0.015;
    m.scale.y = 0.015;
    m.scale.z = 0.015;

    m.color.a = 1.;
    m.color.r = 1.0;
    m.color.g = 0.0;
    m.color.b = 1.0;

    pub_dlo_points_.publish(m);
}

void DloSimulator::publishRvizLines(const std::vector<geometry_msgs::Point> &points){
    visualization_msgs::Marker m;

    m.header.frame_id = dlo_points_frame_id_;
    m.header.stamp = ros::Time::now();

    m.type = visualization_msgs::Marker::LINE_LIST;
    m.id = 1;
    m.action = visualization_msgs::Marker::ADD;

    m.pose.orientation.w = 1.0;

    m.points = points;

    // LINE_STRIP/LINE_LIST markers use only the x component of scale, for the line width
    m.scale.x = dlo_r_;

    m.color.a = 1.;
    m.color.r = 0.;
    m.color.g = 1.0;
    m.color.b = 1.0;

    pub_dlo_points_.publish(m);
}

/*
void DloSimulator::odometryCb_01(const nav_msgs::Odometry::ConstPtr odom_msg){
    Real x = odom_msg->pose.pose.position.x;
    Real y = odom_msg->pose.pose.position.y;
    Real z = odom_msg->pose.pose.position.z + dlo_rob_z_offset_;
    
    Eigen::Matrix<Real,3,1> pos(x, y, z);

    if (!is_rob_01_attached_)
    {
        // tell sim objects (dlo) to attach robot to the nearest particles
        rob_01_attached_id_ = dlo_.attachNearest(pos);
        // std::cout << "self.rob_01_attached_id, " << rob_01_attached_id_ << std::endl;

        if (rob_01_attached_id_ != -1)
        {
            is_rob_01_attached_ = true;
        }
    }
    else
    {
        // tell sim object to update its position
            dlo_.updateAttachedPose(rob_01_attached_id_, pos);
    }
}

void DloSimulator::odometryCb_02(const nav_msgs::Odometry::ConstPtr odom_msg){
    Real x = odom_msg->pose.pose.position.x;
    Real y = odom_msg->pose.pose.position.y;
    Real z = odom_msg->pose.pose.position.z + dlo_rob_z_offset_;
    
    Eigen::Matrix<Real,3,1> pos(x, y, z);

    if (!is_rob_02_attached_)
    {
        // tell sim objects (dlo) to attach robot to the nearest particles
        rob_02_attached_id_ = dlo_.attachNearest(pos);
        // std::cout << "self.rob_02_attached_id, " << rob_02_attached_id_ << std::endl;

        if (rob_02_attached_id_ != -1)
        {
            is_rob_02_attached_ = true;
        }
    }
    else
    {
        // tell sim object to update its position
            dlo_.updateAttachedPose(rob_02_attached_id_, pos);
    }
}

void DloSimulator::odometryCb_03(const nav_msgs::Odometry::ConstPtr odom_msg){
    Real x = odom_msg->pose.pose.position.x;
    Real y = odom_msg->pose.pose.position.y;
    Real z = odom_msg->pose.pose.position.z + dlo_rob_z_offset_;
    
    Eigen::Matrix<Real,3,1> pos(x, y, z);

    if (!is_rob_03_attached_)
    {
        // tell sim objects (dlo) to attach robot to the nearest particles
        rob_03_attached_id_ = dlo_.attachNearest(pos);
        // std::cout << "self.rob_03_attached_id, " << rob_03_attached_id_ << std::endl;

        if (rob_03_attached_id_ != -1)
        {
            is_rob_03_attached_ = true;
        }
    }
    else
    {
        // tell sim object to update its position
            dlo_.updateAttachedPose(rob_03_attached_id_, pos);
    }
}

void DloSimulator::odometryCb_04(const nav_msgs::Odometry::ConstPtr odom_msg){
    Real x = odom_msg->pose.pose.position.x;
    Real y = odom_msg->pose.pose.position.y;
    Real z = odom_msg->pose.pose.position.z + dlo_rob_z_offset_;
    
    Eigen::Matrix<Real,3,1> pos(x, y, z);

    if (!is_rob_04_attached_)
    {
        // tell sim objects (dlo) to attach robot to the nearest particles
        rob_04_attached_id_ = dlo_.attachNearest(pos);
        // std::cout << "self.rob_04_attached_id, " << rob_04_attached_id_ << std::endl;

        if (rob_04_attached_id_ != -1)
        {
            is_rob_04_attached_ = true;
        }
    }
    else
    {
        // tell sim object to update its position
            dlo_.updateAttachedPose(rob_04_attached_id_, pos);
    }
}

void DloSimulator::readAttachedRobotForces(){
    Eigen::Matrix<Real,Eigen::Dynamic,3> *for_ptr = dlo_.getForPtr();

    // std::cout << "Here" << std::endl;

    if (is_rob_01_attached_){
        rob_01_attached_force_ = for_ptr->col(rob_01_attached_id_)/num_substeps_;
        // std::cout << "id: " << rob_01_attached_id_ << ". Force = " << ": " << for_ptr->col(rob_01_attached_id_)/num_substeps_ << " N." << std::endl;
    }
    if (is_rob_02_attached_){
        rob_02_attached_force_ = for_ptr->col(rob_02_attached_id_)/num_substeps_;
    }
    if (is_rob_03_attached_){
        rob_03_attached_force_ = for_ptr->col(rob_03_attached_id_)/num_substeps_;
    }
    if (is_rob_04_attached_){
        rob_04_attached_force_ = for_ptr->col(rob_04_attached_id_)/num_substeps_;
    }
}

// Publish forces on each robot by the dlo
void DloSimulator::publishWrenches(const ros::TimerEvent& e){
    geometry_msgs::WrenchStamped msg;
    msg.header.stamp = ros::Time::now();

    // std::cout << "Here22" << std::endl;

    if (is_rob_01_attached_){
        // std::cout << "id: " << rob_01_attached_id_ << ". Force = " << rob_01_attached_force_ << " N." << std::endl;

        msg.header.frame_id = wrench_01_frame_id_;
        msg.wrench.force.x = rob_01_attached_force_(0);
        msg.wrench.force.y = rob_01_attached_force_(1);
        msg.wrench.force.z = rob_01_attached_force_(2);
        msg.wrench.torque.x = 0.0;
        msg.wrench.torque.y = 0.0;
        msg.wrench.torque.z = 0.0;
        pub_wrench_stamped_01_.publish(msg);
    }
    if (is_rob_02_attached_){
        // std::cout << "id: " << rob_02_attached_id_ << ". Force = " << rob_02_attached_force_ << " N." << std::endl;

        msg.header.frame_id = wrench_02_frame_id_;
        msg.wrench.force.x = rob_02_attached_force_(0);
        msg.wrench.force.y = rob_02_attached_force_(1);
        msg.wrench.force.z = rob_02_attached_force_(2);
        msg.wrench.torque.x = 0.0;
        msg.wrench.torque.y = 0.0;
        msg.wrench.torque.z = 0.0;
        pub_wrench_stamped_02_.publish(msg);
    }
    if (is_rob_03_attached_){
        // std::cout << "id: " << rob_03_attached_id_ << ". Force = " << rob_03_attached_force_ << " N." << std::endl;

        msg.header.frame_id = wrench_03_frame_id_;
        msg.wrench.force.x = rob_03_attached_force_(0);
        msg.wrench.force.y = rob_03_attached_force_(1);
        msg.wrench.force.z = rob_03_attached_force_(2);
        msg.wrench.torque.x = 0.0;
        msg.wrench.torque.y = 0.0;
        msg.wrench.torque.z = 0.0;
        pub_wrench_stamped_03_.publish(msg);
    }
    if (is_rob_04_attached_){
        // std::cout << "id: " << rob_04_attached_id_ << ". Force = " << rob_04_attached_force_ << " N." << std::endl;

        msg.header.frame_id = wrench_04_frame_id_;
        msg.wrench.force.x = rob_04_attached_force_(0);
        msg.wrench.force.y = rob_04_attached_force_(1);
        msg.wrench.force.z = rob_04_attached_force_(2);
        msg.wrench.torque.x = 0.0;
        msg.wrench.torque.y = 0.0;
        msg.wrench.torque.z = 0.0;
        pub_wrench_stamped_04_.publish(msg);
    }    
}
*/